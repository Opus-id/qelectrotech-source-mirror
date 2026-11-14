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
#include "pidfreeend.h"

#include "diagram.h"
#include "diagramcontent.h"
#include "pipeconnection.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "undocommand/addgraphicsobjectcommand.h"
#include "undocommand/deleteqgraphicsitemcommand.h"

#include <QLineF>
#include <QPainterPath>
#include <QUndoCommand>
#include <QUndoStack>

#include <algorithm>

namespace {

		/// The other end of @p pipe than @p end
	Terminal *farEnd(const Conductor *pipe, const Terminal *end)
	{
		if (!pipe) return nullptr;
		return pipe->terminal1 == end ? pipe->terminal2 : pipe->terminal1;
	}

		/// The points of @p pipe on the folio, from its other end to @p end
	QList<QPointF> pathTo(const Conductor *pipe, const Terminal *end)
	{
		QList<QPointF> points;
		const QPainterPath path = pipe->path();
		for (int i = 0 ; i < path.elementCount() ; ++i) {
			const QPainterPath::Element &e = path.elementAt(i);
			points << pipe->mapToScene(QPointF(e.x, e.y));
		}
		if (pipe->terminal1 == end) {
			std::reverse(points.begin(), points.end());
		}
		return points;
	}

		/// @p points without repeated points nor points where the line goes
		/// straight on
	QList<QPointF> cleaned(const QList<QPointF> &points)
	{
		QList<QPointF> kept;
		for (const QPointF &p : points)
		{
			if (!kept.isEmpty() && QLineF(kept.constLast(), p).length() < 0.01) {
				continue;
			}
			if (kept.size() >= 2)
			{
				const QPointF &a = kept.at(kept.size() - 2), &b = kept.constLast();
				const bool along_x = qAbs(a.y() - b.y()) < 0.01 && qAbs(b.y() - p.y()) < 0.01;
				const bool along_y = qAbs(a.x() - b.x()) < 0.01 && qAbs(b.x() - p.x()) < 0.01;
				if (along_x || along_y) {
					kept.last() = p;
					continue;
				}
			}
			kept << p;
		}
		return kept;
	}

		/// A nozzle which may take a new pipe
	bool takesAPipe(const Terminal *nozzle)
	{
		return nozzle && nozzle->parentElement() && nozzle->medium() == TerminalData::Pipe
				&& nozzle->hasRoomForWire();
	}
}

/**
	@brief PidFreeEnd::pipeOf
	See the declaration
*/
Conductor *PidFreeEnd::pipeOf(const Terminal *nozzle)
{
	const Element *element = nozzle ? nozzle->parentElement() : nullptr;
	if (!element || !element->isFreeEnd() || nozzle->medium() != TerminalData::Pipe) {
		return nullptr;
	}
	const QList<Conductor *> pipes = nozzle->conductors();
	if (pipes.size() != 1) {
		return nullptr;
	}
	Conductor *pipe = pipes.constFirst();
	if (pipe->medium() != TerminalData::Pipe || !farEnd(pipe, nozzle)) {
		return nullptr;
	}
		//Its other nozzles, if any, are free: a free end ends one line
	for (const Terminal *t : element->terminals()) {
		if (t != nozzle && !t->conductors().isEmpty()) {
			return nullptr;
		}
	}
	return pipe;
}

/**
	@brief PidFreeEnd::wayOut
	See the declaration
*/
Qet::Orientation PidFreeEnd::wayOut(const Terminal *nozzle)
{
	if (!nozzle) return Qet::East;
	if (pipeOf(nozzle)) {
		return Qet::Orientation((int(nozzle->orientation()) + 2) % 4);
	}
	return nozzle->orientation();
}

/**
	@brief PidFreeEnd::mayJoin
	See the declaration
*/
bool PidFreeEnd::mayJoin(const Terminal *from, const Terminal *to)
{
	if (!from || !to || from == to || from->parentElement() == to->parentElement()) {
		return false;
	}
	const Conductor *first = pipeOf(from), *second = pipeOf(to);
	if (!first && !second) {
		return false;
	}
		//Both ends of one pipe: no line to join
	if (first && first == second) {
		return false;
	}
	if ((!first && !takesAPipe(from)) || (!second && !takesAPipe(to))) {
		return false;
	}
		//A tee or a free end about to be placed is on no folio yet
	if (from->diagram() && to->diagram() && from->diagram() != to->diagram()) {
		return false;
	}
		//The line would run from a symbol back to the same nozzle
	const Terminal *a = first ? farEnd(first, from) : from;
	const Terminal *b = second ? farEnd(second, to) : to;
	return a && b && a != b;
}

/**
	@brief PidFreeEnd::nozzleNear
	See the declaration
*/
Terminal *PidFreeEnd::nozzleNear(Diagram *diagram, const QPointF &scene_pos, qreal radius,
								 const Terminal *from)
{
	if (!diagram || radius <= 0) return nullptr;
	const QRectF area(scene_pos.x() - radius, scene_pos.y() - radius, 2 * radius, 2 * radius);
	Terminal *nearest = nullptr;
	qreal nearest_distance = 0;
	for (QGraphicsItem *item : diagram->items(area))
	{
		auto *nozzle = qgraphicsitem_cast<Terminal *>(item);
		if (!nozzle || !pipeOf(nozzle) || (from && !mayJoin(from, nozzle))) {
			continue;
		}
		const qreal distance = QLineF(scene_pos, nozzle->dockConductor()).length();
		if (distance > radius) continue;
		if (!nearest || distance < nearest_distance) {
			nearest = nozzle;
			nearest_distance = distance;
		}
	}
	return nearest;
}

/**
	@brief PidFreeEnd::route
	See the declaration
*/
QList<QPointF> PidFreeEnd::route(const Terminal *from, const Terminal *to)
{
	if (!from || !to) return {};
	return Conductor::defaultRoute(from->dockConductor(), wayOut(from),
								   to->dockConductor(), wayOut(to), PipeConnection::pipeExit);
}

/**
	@brief PidFreeEnd::mergedData
	See the declaration
*/
PipeProperties PidFreeEnd::mergedData(const PipeProperties &first, const PipeProperties &second,
									  QStringList *dropped)
{
	PipeProperties data = first;
	QStringList aside;
	auto text = [&](QString &kept, const QString &other, const QString &name) {
		if (kept.isEmpty()) {
			kept = other;
		} else if (!other.isEmpty() && other != kept) {
			aside << QStringLiteral("%1 %2").arg(name, other);
		}
	};
		//The size: DN and outside diameter go together
	if (!data.hasSize()) {
		data.nominal = second.nominal;
		data.outside = second.outside;
	} else if (second.hasSize() && second.lineSize() != data.lineSize()) {
		aside << tr("size %1").arg(second.lineSize().text());
	}
	text(data.line_number, second.line_number, tr("line number"));
	text(data.fluid, second.fluid, tr("fluid"));
	text(data.material, second.material, tr("material"));
	text(data.piping_class, second.piping_class, tr("piping class"));
	text(data.nominal_pressure, second.nominal_pressure, tr("nominal pressure"));
	text(data.flow, second.flow, tr("flow"));
	text(data.spool, second.spool, tr("spool"));
	text(data.external_finish, second.external_finish, tr("external finish"));
	if (!data.color.isValid()) {
		data.color = second.color;
	}
	if (data.status.isEmpty()) {
		data.status = second.status;
	}
	if (dropped) {
		*dropped = aside;
	}
	return data;
}

/**
	@brief PidFreeEnd::join
	See the declaration
*/
Conductor *PidFreeEnd::join(Diagram *diagram, Terminal *from, Terminal *to, QUndoCommand *parent,
							QString *message)
{
	if (!diagram || !parent || !mayJoin(from, to)) return nullptr;
	Conductor *first = pipeOf(from), *second = pipeOf(to);
	Terminal *a = first ? farEnd(first, from) : from;
	Terminal *b = second ? farEnd(second, to) : to;

		//The route kept, the new piece where the free end was
	QList<QPointF> points;
	if (first) points << pathTo(first, from);
	points << route(from, to);
	if (second) {
		QList<QPointF> back = pathTo(second, to);
		std::reverse(back.begin(), back.end());
		points << back;
	}
	points = cleaned(points);

		//The data of the line drawn from
	ConductorProperties properties = (first ? first : second)->properties();
	QStringList dropped;
	if (first && second) {
		properties.m_pipe = mergedData(first->properties().m_pipe, second->properties().m_pipe,
									   &dropped);
	}

	auto *pipe = new Conductor(a, b);
	if (!pipe->isValid()) {
		delete pipe;
		return nullptr;
	}
	pipe->setProperties(properties);
	pipe->takeNozzleDiameter();
		//Straight, nothing to keep: the route of a new pipe
	pipe->setPathPoints(points);

		//The lines taken up, as found before the new pipe: a free end just
		//placed holds it now, and must stay
	DiagramContent content;
	if (first) {
		content.m_elements << from->parentElement();
		content.m_other_conductors << first;
	}
	if (second) {
		content.m_elements << to->parentElement();
		content.m_other_conductors << second;
	}
	new DeleteQGraphicsItemCommand(diagram, content, parent);
	auto *add = new AddGraphicsObjectCommand(pipe, diagram, QPointF(), parent);
	if (parent->text().isEmpty()) {
		parent->setText(add->text());
	}
	if (message) {
		*message = dropped.isEmpty()
				? QString()
				: tr("Lines joined: the data of the line drawn from are kept; set aside from "
					 "the other one: %1.").arg(dropped.join(QStringLiteral(", ")));
	}
	return pipe;
}

/**
	@brief PidFreeEnd::carryOn
	See the declaration
*/
Conductor *PidFreeEnd::carryOn(Terminal *from, Terminal *to, QString *message)
{
	Diagram *diagram = from ? from->diagram() : nullptr;
	if (!diagram || diagram->isReadOnly()) return nullptr;
	auto *undo = new QUndoCommand();
	Conductor *pipe = join(diagram, from, to, undo, message);
	if (!pipe) {
		delete undo;
		return nullptr;
	}
	diagram->undoStack().push(undo);
	return pipe;
}

/**
	@brief PidFreeEnd::conceal
	See the declaration
*/
void PidFreeEnd::conceal(Terminal *nozzle, bool hide)
{
	Element *element = nozzle ? nozzle->parentElement() : nullptr;
	if (!element) return;
	static const char *kept = "pid_concealed_opacity";
	const QVariant before = element->property(kept);
	if (hide)
	{
		if (!before.isValid()) {
			element->setProperty(kept, element->opacity());
		}
		element->setOpacity(0.0);
		nozzle->setFlag(QGraphicsItem::ItemIgnoresParentOpacity, true);
	}
	else if (before.isValid())
	{
		element->setOpacity(before.toReal());
		element->setProperty(kept, QVariant());
		nozzle->setFlag(QGraphicsItem::ItemIgnoresParentOpacity, false);
	}
}
