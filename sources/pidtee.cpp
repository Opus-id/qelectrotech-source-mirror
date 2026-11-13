/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "pidtee.h"

#include "conductorautonumerotation.h"
#include "diagram.h"
#include "diagramcontent.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "undocommand/addgraphicsobjectcommand.h"
#include "undocommand/deleteqgraphicsitemcommand.h"

#include <QLineF>
#include <QPainterPath>
#include <QUndoCommand>
#include <QUndoStack>

#include <algorithm>
#include <limits>

namespace {

	/// The points of the line of @p conductor on the folio, from its
	/// terminal1 to its terminal2, without repeated points nor points
	/// where it goes straight on
QList<QPointF> linePoints(const Conductor *conductor)
{
	QList<QPointF> points;
	const QPainterPath path = conductor->path();
	for (int i = 0 ; i < path.elementCount() ; ++i)
	{
		const QPainterPath::Element &e = path.elementAt(i);
		const QPointF p = conductor->mapToScene(QPointF(e.x, e.y));
		if (!points.isEmpty() && QLineF(points.constLast(), p).length() < 0.5) {
			continue;
		}
		points << p;
	}
	for (int i = int(points.size()) - 2 ; i >= 1 ; --i)
	{
		const QPointF a = points.at(i - 1), b = points.at(i), c = points.at(i + 1);
		const bool vertical = qAbs(a.x() - b.x()) < 0.01 && qAbs(b.x() - c.x()) < 0.01;
		const bool horizontal = qAbs(a.y() - b.y()) < 0.01 && qAbs(b.y() - c.y()) < 0.01;
		if (vertical || horizontal) {
			points.removeAt(i);
		}
	}
	if (points.size() >= 2 && conductor->terminal1
		&& QLineF(points.constFirst(), conductor->terminal1->dockConductor()).length()
		   > QLineF(points.constLast(), conductor->terminal1->dockConductor()).length()) {
		std::reverse(points.begin(), points.end());
	}
	return points;
}

qreal distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b)
{
	const QPointF ab = b - a;
	const qreal length2 = QPointF::dotProduct(ab, ab);
	if (length2 <= 0.0) {
		return QLineF(point, a).length();
	}
	const qreal t = qBound(0.0, QPointF::dotProduct(point - a, ab) / length2, 1.0);
	return QLineF(point, a + t * ab).length();
}

	/// The way of @p d, horizontal or vertical, whichever is longer
Qet::Orientation facingOf(const QPointF &d)
{
	if (qAbs(d.x()) >= qAbs(d.y())) {
		return d.x() >= 0 ? Qet::East : Qet::West;
	}
	return d.y() > 0 ? Qet::South : Qet::North;
}

QPointF unitOf(Qet::Orientation o)
{
	switch (o) {
		case Qet::North: return QPointF(0, -1);
		case Qet::East:  return QPointF(1, 0);
		case Qet::South: return QPointF(0, 1);
		case Qet::West:  return QPointF(-1, 0);
	}
	return QPointF();
}

Qet::Orientation opposite(Qet::Orientation o)
{
	return Qet::Orientation((int(o) + 2) % 4);
}

	/// The nozzle of @p tee facing @p facing, nullptr for none
Terminal *nozzleFacing(const Element *tee, Qet::Orientation facing)
{
	for (Terminal *t : tee->terminals()) {
		if (t->medium() == TerminalData::Pipe && t->orientation() == facing) {
			return t;
		}
	}
	return nullptr;
}

} // anonymous namespace

/**
	@brief PidTee::notATee
	See the declaration
*/
QString PidTee::notATee()
{
	return tr("The symbol of tee of the project is not a tee (two nozzles facing each other "
			  "and a third aside): choose another one (properties of the project, P&ID, "
			  "Connections).");
}

/**
	@brief PidTee::symbol
	See the declaration
*/
ElementsLocation PidTee::symbol(const Diagram *diagram)
{
	QETProject *project = diagram ? diagram->project() : nullptr;
	if (!project) return ElementsLocation();
	const QString path = project->pidCodification().toolSymbol(PidCodification::teeRole());
	if (path.isEmpty()) return ElementsLocation();
	const ElementsLocation location = path.startsWith(QLatin1String("embed://"))
			? ElementsLocation(path, project) : ElementsLocation(path);
	return location.isElement() && location.exist() ? location : ElementsLocation();
}

/**
	@brief PidTee::isSet
	See the declaration
*/
bool PidTee::isSet(const Diagram *diagram)
{
	return diagram && diagram->project()
			&& !diagram->project()->pidCodification().toolSymbol(PidCodification::teeRole()).isEmpty();
}

/**
	@brief PidTee::conductorAt
	See the declaration
*/
Conductor *PidTee::conductorAt(const Diagram *diagram, const QPointF &scene_pos)
{
	if (!diagram) return nullptr;
		//Wider than the widest line and its margin
	constexpr qreal reach = 10.0;
	const QRectF area(scene_pos.x() - reach, scene_pos.y() - reach, 2 * reach, 2 * reach);
	Conductor *nearest = nullptr;
	qreal nearest_distance = std::numeric_limits<qreal>::max();
	for (QGraphicsItem *item : diagram->items(area))
	{
		Conductor *conductor = qgraphicsitem_cast<Conductor *>(item);
		if (!conductor || !conductor->terminal1 || !conductor->terminal2) continue;
		const QList<QPointF> points = linePoints(conductor);
		const qreal margin = conductor->properties().lineWidth() / 2.0 + 3.0;
		for (int i = 0 ; i + 1 < points.size() ; ++i)
		{
			const qreal distance = distanceToSegment(scene_pos, points.at(i), points.at(i + 1));
			if (distance <= margin && distance < nearest_distance) {
				nearest = conductor;
				nearest_distance = distance;
			}
		}
	}
	return nearest;
}

/**
	@brief PidTee::spotOn
	See the declaration
*/
PidTee::Spot PidTee::spotOn(Conductor *pipe, const QPointF &scene_pos, qreal arm)
{
	Spot spot;
	spot.pipe = pipe;
	const QList<QPointF> points = pipe ? linePoints(pipe) : QList<QPointF>();
	if (points.size() < 2) {
		spot.refusal = tr("This pipe has no length to take a tee.");
		return spot;
	}
	const int last = int(points.size()) - 1;

		//The straight part nearest to the click
	int part = 0;
	qreal part_distance = std::numeric_limits<qreal>::max();
	for (int i = 0 ; i < last ; ++i)
	{
		const qreal distance = distanceToSegment(scene_pos, points.at(i), points.at(i + 1));
		if (distance < part_distance) {
			part = i;
			part_distance = distance;
		}
	}
		//How far along the pipe each point of it is
	QList<qreal> along{0};
	for (int i = 1 ; i <= last ; ++i) {
		along << along.constLast() + QLineF(points.at(i - 1), points.at(i)).length();
	}

		//At the bend @p k, between two legs
	auto atBend = [&](int k) {
		Spot bend;
		bend.pipe = pipe;
		bend.corner = true;
		bend.point = points.at(k);
		bend.along = along.at(k);
		bend.back = facingOf(points.at(k - 1) - points.at(k));
		bend.forward = facingOf(points.at(k + 1) - points.at(k));
		const qreal back_room = arm + (k - 1 == 0 ? nozzle_room : 0);
		const qreal forward_room = arm + (k + 1 == last ? nozzle_room : 0);
		if (QLineF(points.at(k - 1), points.at(k)).length() < back_room
			|| QLineF(points.at(k), points.at(k + 1)).length() < forward_room) {
			bend.refusal = tr("No room for a tee at this bend: it would come too near a nozzle "
							  "(2.5 mm at least) or another bend.");
		}
		return bend;
	};

	const QPointF a = points.at(part), b = points.at(part + 1);
	const qreal length = QLineF(a, b).length();
	const QPointF dir = (b - a) / length;
	const bool start_is_nozzle = part == 0;
	const bool end_is_nozzle = part + 1 == last;
	const qreal room_a = arm + (start_is_nozzle ? nozzle_room : 0);
	const qreal room_b = arm + (end_is_nozzle ? nozzle_room : 0);
	const qreal clicked = qBound(0.0, QPointF::dotProduct(scene_pos - a, dir), length);

		//Too near an end of this straight part for the tee to lie on it:
		//the end nearer to the click decides; at a bend, the tee takes
		//its place, at a nozzle it is refused
	const bool short_a = clicked < room_a, short_b = length - clicked < room_b;
	const bool nearer_a = clicked <= length - clicked;
	const bool by_a = short_a && (!short_b || nearer_a);
	if (by_a || short_b)
	{
		const bool nozzle = by_a ? start_is_nozzle : end_is_nozzle;
		if (!nozzle) {
			return atBend(by_a ? part : part + 1);
		}
		spot.refusal = tr("Too near a nozzle: a tee keeps 2.5 mm at least from the nozzles. Click "
						  "further from it.");
		return spot;
	}

		//On the grid along the line, on the line across it
	const QPointF projected = a + dir * clicked;
	const QPointF snapped = Diagram::snapToGrid(projected);
	qreal t = QPointF::dotProduct(snapped - a, dir);
	t = qBound(room_a, t, length - room_b);
	spot.point = a + dir * t;
	spot.along = along.at(part) + t;
	spot.back = facingOf(-dir);
	spot.forward = facingOf(dir);
	return spot;
}

/**
	@brief PidTee::armOf
	See the declaration
*/
qreal PidTee::armOf(const Diagram *diagram)
{
	Element *tee = build(const_cast<Diagram *>(diagram), nullptr, false);
	if (!tee) return 0;
	Terminal *run1 = nullptr, *run2 = nullptr, *branch = nullptr;
	qreal arm = 0;
	if (waysOf(tee, &run1, &run2, &branch)) {
		const QPointF centre = (run1->dockConductor() + run2->dockConductor()) / 2;
		for (const Terminal *t : {run1, run2, branch}) {
			arm = qMax(arm, QLineF(centre, t->dockConductor()).length());
		}
	}
	delete tee;
	return arm;
}

/**
	@brief PidTee::spotAt
	See the declaration
*/
PidTee::Spot PidTee::spotAt(const Diagram *diagram, const QPointF &scene_pos)
{
	Spot spot;
	Conductor *conductor = conductorAt(diagram, scene_pos);
	if (!conductor) return spot;
	if (conductor->medium() != TerminalData::Pipe) {
		spot.pipe = conductor;
		spot.refusal = tr("A tee goes on a pipe only: not on a signal line nor on a wire.");
		return spot;
	}
	const qreal arm = armOf(diagram);
	if (arm <= 0) {
		spot.pipe = conductor;
		spot.refusal = symbol(diagram).isElement()
				? notATee()
				: tr("The symbol of tee of the project is not found: choose another one "
					 "(properties of the project, P&ID, Connections).");
		return spot;
	}
	return spotOn(conductor, scene_pos, arm);
}

/**
	@brief PidTee::branchFacing
	See the declaration
*/
Qet::Orientation PidTee::branchFacing(const Spot &spot, const QPointF &toward)
{
	const QPointF d = toward - spot.point;
	constexpr qreal tolerance = 0.5;
	if (!spot.corner)
	{
		if (Qet::isHorizontal(spot.forward)) {
			return d.y() < -tolerance ? Qet::North : Qet::South;
		}
		return d.x() < -tolerance ? Qet::West : Qet::East;
	}
		//On along one leg or the other, through the bend
	const Qet::Orientation one = opposite(spot.back), two = opposite(spot.forward);
	return QPointF::dotProduct(d, unitOf(one)) >= QPointF::dotProduct(d, unitOf(two)) ? one : two;
}

/**
	@brief PidTee::clash
	See the declaration
*/
QString PidTee::clash(const Spot &a, const Spot &b, qreal arm)
{
	if (a.pipe != b.pipe) return QString();
	if (qAbs(a.along - b.along) < 2 * arm + nozzle_room) {
		return tr("Too near the other tee on this pipe: two tees keep 2.5 mm at least between them.");
	}
	return QString();
}

/**
	@brief PidTee::waysOf
	See the declaration
*/
bool PidTee::waysOf(const Element *tee, Terminal **run1, Terminal **run2, Terminal **branch)
{
	if (!tee) return false;
	QList<Terminal *> nozzles;
	for (Terminal *t : tee->terminals()) {
		if (t->medium() == TerminalData::Pipe) nozzles << t;
	}
	if (nozzles.size() != 3) return false;
	for (int i = 0 ; i < 3 ; ++i)
	{
		Terminal *one = nozzles.at(i), *two = nozzles.at((i + 1) % 3), *aside = nozzles.at((i + 2) % 3);
		if (Qet::isOpposed(one->orientation(), two->orientation())
			&& !Qet::isOpposed(one->orientation(), aside->orientation())
			&& one->orientation() != aside->orientation()
			&& two->orientation() != aside->orientation())
		{
			if (run1) *run1 = one;
			if (run2) *run2 = two;
			if (branch) *branch = aside;
			return true;
		}
	}
	return false;
}

/**
	@brief PidTee::build
	See the declaration
	@param import : false to measure it only, without taking the symbol
	into the project
*/
Element *PidTee::build(Diagram *diagram, QString *error, bool import)
{
	const ElementsLocation location = symbol(diagram);
	if (!location.isElement()) {
		if (error) *error = tr("The symbol of tee of the project is not found: choose another one "
							   "(properties of the project, P&ID, Connections).");
		return nullptr;
	}
	ElementsLocation used = location;
	if (import && !(location.isProject() && location.project() == diagram->project())) {
		used = diagram->project()->importElement(used);
	}
	if (!used.exist()) {
		if (error) *error = tr("The symbol of tee of the project is not found: choose another one "
							   "(properties of the project, P&ID, Connections).");
		return nullptr;
	}
	int state = 0;
	Element *tee = ElementFactory::Instance()->createElement(used, nullptr, &state);
	if (!tee || state || !waysOf(tee, nullptr, nullptr, nullptr)) {
		delete tee;
		if (error) *error = notATee();
		return nullptr;
	}
	tee->takeLabelSize(diagram->project()->pidCodification());
	return tee;
}

/**
	@brief PidTee::fit
	See the declaration
*/
Terminal *PidTee::fit(Element *tee, const Spot &spot, Qet::Orientation out)
{
	Terminal *run1 = nullptr, *run2 = nullptr, *branch = nullptr;
	if (!waysOf(tee, &run1, &run2, &branch)) return nullptr;
		//Along a straight part the way aside goes out; at a bend the run
		//goes on along one leg, the way aside takes the other
	const Qet::Orientation aside = !spot.corner ? out
								 : (out == opposite(spot.back) ? spot.forward : spot.back);
	const int turns = (int(aside) - int(branch->orientation()) + 4) % 4;
	tee->setRotation(tee->rotation() + 90.0 * turns);
	tee->setPos(QPointF());
	const QPointF centre = (run1->dockConductor() + run2->dockConductor()) / 2;
	tee->setPos(spot.point - centre);
	if (!nozzleFacing(tee, spot.back) || !nozzleFacing(tee, spot.forward)) return nullptr;
	return nozzleFacing(tee, out);
}

/**
	@brief PidTee::cut
	See the declaration
*/
void PidTee::cut(Diagram *diagram, Conductor *pipe, const QList<Spot> &spots,
				 const QList<Element *> &tees, QUndoCommand *parent)
{
	if (!diagram || !pipe || spots.size() != tees.size()) return;

	QList<int> order;
	for (int i = 0 ; i < spots.size() ; ++i) order << i;
	std::sort(order.begin(), order.end(),
			  [&spots](int a, int b) { return spots.at(a).along < spots.at(b).along; });

		//The pieces, in the way of the pipe
	QList<QPair<Terminal *, Terminal *>> pieces;
	Terminal *from = pipe->terminal1;
	for (int i : std::as_const(order))
	{
		pieces << qMakePair(from, nozzleFacing(tees.at(i), spots.at(i).back));
		from = nozzleFacing(tees.at(i), spots.at(i).forward);
	}
	pieces << qMakePair(from, pipe->terminal2);

	for (Element *tee : tees)
	{
		diagram->addItem(tee);
		new AddGraphicsObjectCommand(tee, diagram, tee->pos(), parent);
	}
	DiagramContent content;
	content.m_other_conductors << pipe;
	new DeleteQGraphicsItemCommand(diagram, content, parent);

		//The pieces are the same line: they keep its data and its way
	const ConductorProperties data = pipe->properties();
	for (const auto &piece : std::as_const(pieces))
	{
		auto *conductor = new Conductor(piece.first, piece.second);
		conductor->setProperties(data);
		conductor->takeNozzleDiameter();
		new AddGraphicsObjectCommand(conductor, diagram, QPointF(), parent);
	}
}

/**
	@brief PidTee::branchFrom
	See the declaration
*/
bool PidTee::branchFrom(Terminal *nozzle, const QPointF &scene_pos, QString *refusal)
{
	Diagram *diagram = nozzle ? nozzle->diagram() : nullptr;
	if (!diagram || diagram->isReadOnly() || !isSet(diagram)
		|| nozzle->medium() != TerminalData::Pipe || !nozzle->parentElement()) {
		return false;
	}
	const Spot spot = spotAt(diagram, scene_pos);
	if (!spot.pipe) return false;

	QString why;
	if (!nozzle->hasRoomForWire()) {
		why = tr("This nozzle is already connected: a nozzle takes one pipe.");
	} else if (!spot.isValid()) {
		why = spot.refusal;
	}
	Element *tee = nullptr;
	Terminal *out = nullptr;
	if (why.isEmpty() && (tee = build(diagram, &why)))
	{
		out = fit(tee, spot, branchFacing(spot, nozzle->dockConductor()));
		if (!out) {
			why = notATee();
		}
	}
	if (!why.isEmpty()) {
		delete tee;
		if (refusal) *refusal = why;
		return true;
	}

	auto *undo = new QUndoCommand();
	cut(diagram, spot.pipe, {spot}, {tee}, undo);
	auto *pipe = new Conductor(nozzle, out);
	auto *add_pipe = new AddGraphicsObjectCommand(pipe, diagram, QPointF(), undo);
	undo->setText(add_pipe->text());
	ConductorAutoNumerotation numbering(pipe, diagram, undo);
	numbering.numerate();
	diagram->undoStack().push(undo);
	return true;
}
