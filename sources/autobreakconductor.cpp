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
#include "autobreakconductor.h"

#include "diagram.h"
#include "qetproject.h"
#include "conductorautonumerotation.h"
#include "pidfreeend.h"
#include "pidnozzlemagnet.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetgraphicsitem/conductor.h"
#include "undocommand/addgraphicsobjectcommand.h"
#include "undocommand/deleteqgraphicsitemcommand.h"
#include "QPropertyUndoCommand/qpropertyundocommand.h"

#include <QLineF>
#include <QPainterPath>
#include <QMap>
#include <QRectF>
#include <limits>

namespace {

/**
	@brief distanceToSegment
	@param point : point to measure from
	@param segment : the finite segment (not the infinite line through it)
	@return the distance from @a point to the closest point actually on
	@a segment.  Unlike QET::orthogonalProjection(), which reports a hit
	for any point on the segment's infinite extension, this clamps the
	projection to the segment itself: a point collinear with a wire but
	past its actual drawn end is correctly reported as far away, not "on"
	the wire.
*/
qreal distanceToSegment(const QPointF &point, const QLineF &segment)
{
	const QPointF a = segment.p1();
	const QPointF b = segment.p2();
	const QPointF ab = b - a;
	const qreal len2 = QPointF::dotProduct(ab, ab);

	if (len2 <= 0.0)
		return QLineF(point, a).length();

	qreal t = QPointF::dotProduct(point - a, ab) / len2;
	t = qBound(0.0, t, 1.0);
	return QLineF(point, a + t * ab).length();
}

	/// Two nozzles against each other, facing: they touch, no pipe between
bool inContact(const Terminal *a, const Terminal *b)
{
	return a && b && PidNozzleMagnet::faces(a, b)
			&& QLineF(a->dockConductor(), b->dockConductor()).length() < PidNozzleMagnet::kSamePoint;
}

	/// @p nozzle is the free end which ends @p pipe
bool isFreeEndOf(const Terminal *nozzle, const Conductor *pipe)
{
	return pipe && PidFreeEnd::pipeOf(nozzle) == pipe;
}

	/// The free end of a line on @p nozzle of @p element, facing the same
	/// way: the symbol takes the line on there (P&ID, 5l-1c)
Terminal *freeEndOn(Diagram *diagram, const Element *element, Terminal *nozzle,
		    const QList<Conductor *> &conductors_handled)
{
	const QPointF dock = nozzle->dockConductor();
	const qreal near = PidNozzleMagnet::kSamePoint;
	const QRectF area(dock.x() - near, dock.y() - near, 2 * near, 2 * near);
	for (QGraphicsItem *item : diagram->items(area))
	{
		Terminal *end = qgraphicsitem_cast<Terminal *>(item);
		if (!end || end->parentElement() == element) {
			continue;
		}
		const Conductor *held = PidFreeEnd::pipeOf(end);
		if (held && !conductors_handled.contains(held)
			&& end->orientation() == nozzle->orientation()
			&& QLineF(end->dockConductor(), dock).length() < near
			&& PidFreeEnd::mayJoin(end, nozzle)) {
			return end;
		}
	}
	return nullptr;
}

	/// An end of a line (a symbol of the pipe family with a single nozzle,
	/// free: a free end, an arrow, a vent, a closure...) dropped with its
	/// nozzle on another nozzle turns the way it must (P&ID, 6b-3c2): on a
	/// free end, the same way, to take its line on (freeEndOn()); on a free
	/// nozzle of another symbol, facing it, the two touching. Turned about
	/// its nozzle, in the undo step of @p parent.
void turnLineEnd(Diagram *diagram, Element *element, QUndoCommand *parent)
{
	if (element->pidFamily() != QLatin1String("pipe")) {
		return;
	}
	Terminal *nozzle = nullptr;
	for (Terminal *t : element->terminals()) {
		if (t->medium() != TerminalData::Pipe) continue;
		if (nozzle) return;
		nozzle = t;
	}
	if (!nozzle || !nozzle->hasRoomForWire()) {
		return;
	}
	const QPointF dock = nozzle->dockConductor();
	const qreal near = PidNozzleMagnet::kSamePoint;
	const QRectF area(dock.x() - near, dock.y() - near, 2 * near, 2 * near);
	Terminal *target = nullptr;
	bool on_free_end = false;
	for (QGraphicsItem *item : diagram->items(area))
	{
		Terminal *other = qgraphicsitem_cast<Terminal *>(item);
		if (!other || !other->parentElement() || other->parentElement() == element
			|| other->medium() != TerminalData::Pipe
			|| QLineF(other->dockConductor(), dock).length() >= near) {
			continue;
		}
		if (PidFreeEnd::pipeOf(other)) {
			target = other;
			on_free_end = true;
			break;
		}
		if (!target && other->hasRoomForWire() && other->conductors().isEmpty()) {
			target = other;
		}
	}
	if (!target) {
		return;
	}
	const Qet::Orientation wanted = on_free_end
			? target->orientation()
			: Qet::Orientation((int(target->orientation()) + 2) % 4);
	if (nozzle->orientation() == wanted) {
		return;
	}
	const int turns = (int(wanted) - int(nozzle->orientation()) + 4) % 4;
	const qreal old_rotation = element->rotation();
	const QPointF old_pos = element->pos();
	element->setRotation(old_rotation + 90.0 * turns);
	element->setPos(old_pos + dock - nozzle->dockConductor());
	new QPropertyUndoCommand(element, "rotation", old_rotation, element->rotation(), parent);
	new QPropertyUndoCommand(element, "pos", old_pos, element->pos(), parent);
}

} // anonymous namespace

/**
	@brief liesOnConductor
	See the declaration
*/
bool liesOnConductor(const Conductor *conductor, const QPointF &scene_point)
{
	if (!conductor) {
		return false;
	}
		//A wire as before; a pipe up to half the grid away as well: a
		//symbol placed with the mouse stands on the grid, a pipe from a
		//nozzle brought off it by the magnet may run half a grid away
	const qreal tolerance = conductor->medium() == TerminalData::Pipe
			? qMax(5.0, qMax(Diagram::xGrid, Diagram::yGrid) / 2.0) + 0.01
			: 5.0;
	const QPointF local = conductor->mapFromScene(scene_point);
	const QPainterPath path = conductor->path();
	for (int i = 0; i < path.elementCount() - 1; ++i)
	{
		const QPainterPath::Element &e1 = path.elementAt(i);
		const QPainterPath::Element &e2 = path.elementAt(i + 1);
		if (distanceToSegment(local, QLineF(QPointF(e1.x, e1.y), QPointF(e2.x, e2.y))) < tolerance) {
			return true;
		}
	}
	return false;
}

/**
	@brief autoBreakConductors
	For each terminal of @a element, check if its dock point lies on an
	existing conductor.  If so, break the conductor and reconnect through
	the element's terminal.  Broken conductors from the same circuit
	(sharing the same far-end endpoint) are broken together; independent
	crossing conductors are left untouched.
	A pipe is broken whatever the project says (a pipe line never goes
	through a symbol unbroken); a wire or a signal line only when the
	project asks for it (QETProject::autoBreakConductor()).
*/
QSet<Terminal *> autoBreakConductors(
	Diagram *diagram,
	Element *element,
	QUndoCommand *parent,
	QList<Conductor *> &conductors_handled,
	QSet<Terminal *> &used_terminals)
{
	QSet<Terminal *> broken_endpoints;

		//A pipe is always broken by a symbol dropped or moved on it: the
		//project's switch only rules the other media (a wire, a signal)
	const bool all_media = diagram->project()->autoBreakConductor();

		//An end of a line dropped on a nozzle turns the way it must first
	turnLineEnd(diagram, element, parent);

	foreach (Terminal *t, element->terminals())
	{
		if (used_terminals.contains(t))
			continue;
		if (!all_media && t->medium() != TerminalData::Pipe)
			continue;

		QPointF t_dock = t->dockConductor();

			//P&ID: a nozzle on the free end of a line, facing the same way,
			//takes the line on: the free end goes, the pipe comes to the
			//nozzle with its route and data (PidFreeEnd::join())
		if (t->medium() == TerminalData::Pipe && t->hasRoomForWire()) {
			if (Terminal *end = freeEndOn(diagram, element, t, conductors_handled)) {
				Conductor *held = PidFreeEnd::pipeOf(end);
				if (PidFreeEnd::join(diagram, end, t, parent)) {
					conductors_handled.append(held);
					used_terminals.insert(t);
					continue;
				}
			}
		}

		struct ConductorMatch {
			Conductor *conductor;
			Terminal *connect_to;
			Terminal *other;
		};
		QList<ConductorMatch> all_matches;

		foreach (Conductor *c, diagram->conductors())
		{
			if (conductors_handled.contains(c) ||
			    c->terminal1->parentElement() == element ||
			    c->terminal2->parentElement() == element)
				continue;
			if (!all_media && c->medium() != TerminalData::Pipe)
				continue;

			if (!liesOnConductor(c, t_dock))
				continue;

			Terminal *c1 = c->terminal1;
			Terminal *c2 = c->terminal2;
			QPointF c1_dock = c1->dockConductor();
			QPointF c2_dock = c2->dockConductor();

			Terminal *connect_to = nullptr;
			Terminal *other = nullptr;

			switch (t->orientation()) {
				case Qet::North:
					if (c1_dock.y() < t_dock.y()) { connect_to = c1; other = c2; }
					else if (c2_dock.y() < t_dock.y()) { connect_to = c2; other = c1; }
					break;
				case Qet::South:
					if (c1_dock.y() > t_dock.y()) { connect_to = c1; other = c2; }
					else if (c2_dock.y() > t_dock.y()) { connect_to = c2; other = c1; }
					break;
				case Qet::East:
					if (c1_dock.x() > t_dock.x()) { connect_to = c1; other = c2; }
					else if (c2_dock.x() > t_dock.x()) { connect_to = c2; other = c1; }
					break;
				case Qet::West:
					if (c1_dock.x() < t_dock.x()) { connect_to = c1; other = c2; }
					else if (c2_dock.x() < t_dock.x()) { connect_to = c2; other = c1; }
					break;
			}

			if (!connect_to) {
				qreal d1 = QLineF(t_dock, c1_dock).length();
				qreal d2 = QLineF(t_dock, c2_dock).length();
				if (d1 <= d2) { connect_to = c1; other = c2; }
				else { connect_to = c2; other = c1; }
			}

				//A wire of another medium is left whole: an automatic
				//wire never joins two media
			if (!t->sameMediumAs(connect_to))
				continue;

			all_matches.append({c, connect_to, other});
		}

		if (all_matches.isEmpty())
			continue;

		QMap<Terminal *, QList<ConductorMatch>> groups;
		for (const auto &m : all_matches) {
			groups[m.other].append(m);
		}
		Terminal *best_other = nullptr;
		int best_count = 0;
		for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
			if (it.value().size() > best_count) {
				best_count = it.value().size();
				best_other = it.key();
			}
		}

		const QList<ConductorMatch> &matches = groups[best_other];

			//A nozzle takes one pipe (Terminal::wireLimit()): one already
			//connected, or dropped on several, breaks none
		if (t->medium() == TerminalData::Pipe
			&& (t->conductorsCount() > 0 || matches.size() > 1))
			continue;

		used_terminals.insert(t);

		QPointF other_dock = best_other->dockConductor();
		Terminal *other_terminal = nullptr;
		qreal best_dist = std::numeric_limits<qreal>::max();
		foreach (Terminal *ot, element->terminals())
		{
			if (used_terminals.contains(ot) || !best_other->sameMediumAs(ot)
				|| (ot->medium() == TerminalData::Pipe && !ot->hasRoomForWire()))
				continue;

			QPointF ot_dock = ot->dockConductor();
			if (t->medium() == TerminalData::Pipe) {
					//A pipe goes on through the nozzle facing the one it came
					//in by (N2 after N1): never through a branch (the N3 of a
					//tee), however near it lies to the other end
				if (!Qet::isOpposed(ot->orientation(), t->orientation()))
					continue;
			} else {
				bool orientation_ok = false;
				switch (ot->orientation()) {
					case Qet::North: orientation_ok = other_dock.y() < ot_dock.y(); break;
					case Qet::South: orientation_ok = other_dock.y() > ot_dock.y(); break;
					case Qet::East:  orientation_ok = other_dock.x() > ot_dock.x(); break;
					case Qet::West:  orientation_ok = other_dock.x() < ot_dock.x(); break;
				}
				if (!orientation_ok)
					continue;
			}

			qreal dist = QLineF(ot_dock, other_dock).length();
			if (dist < best_dist) {
				best_dist = dist;
				other_terminal = ot;
			}
		}

			//P&ID (5l-1c): against a nozzle of the line, the symbol needs no
			//pipe there, they touch; a free end it stands on or passes over
			//goes, the symbol ending the line; never a pipe going back from
			//the far end to the symbol
		bool join_near = true;
		bool join_far = other_terminal != nullptr;
		QList<Element *> free_ends_taken;
		if (t->medium() == TerminalData::Pipe)
		{
			const Conductor *line = matches.first().conductor;
			Terminal *near = matches.first().connect_to;
			if (inContact(near, t)) {
				join_near = false;
				if (isFreeEndOf(near, line)) {
					free_ends_taken << near->parentElement();
				}
			}
			const qreal ahead = other_terminal
					? PidNozzleMagnet::ahead(other_terminal->orientation(),
								 other_terminal->dockConductor(), other_dock)
					: 0;
			if (isFreeEndOf(best_other, line)) {
				if (!other_terminal || ahead < PidNozzleMagnet::kSamePoint) {
					join_far = false;
					if (!free_ends_taken.contains(best_other->parentElement())) {
						free_ends_taken << best_other->parentElement();
					}
				}
			} else if (other_terminal && (ahead < -PidNozzleMagnet::kSamePoint
						      || inContact(best_other, other_terminal))) {
				join_far = false;
			}
				//Never the whole line lost: a symbol against one end of it
				//(a closure facing away from the line, dropped on its free
				//end) with no nozzle to carry it on, while its far end stays,
				//is only placed there; the line is left whole (6b-3c2)
			if (!join_near && !other_terminal && !isFreeEndOf(best_other, line)) {
				used_terminals.remove(t);
				continue;
			}
		}

		DiagramContent content;
		for (const auto &m : matches) {
			content.m_other_conductors.append(m.conductor);
			conductors_handled.append(m.conductor);
		}
		content.m_elements << free_ends_taken;
		new DeleteQGraphicsItemCommand(diagram, content, parent);

		Conductor *first_half = nullptr;
		for (const auto &m : matches) {
			if (!join_near) {
				broken_endpoints.insert(m.connect_to);
				continue;
			}
			Conductor *new_c = new Conductor(m.connect_to, t);
				//The two halves of a pipe are the same line: they keep its
				//data, which no potential brings back as for a wire
			if (m.conductor->medium() == TerminalData::Pipe) {
				new_c->setProperties(m.conductor->properties());
				new_c->takeNozzleDiameter();
			}
			if (m.conductor == matches.first().conductor) {
				first_half = new_c;
			}
			new AddGraphicsObjectCommand(new_c, diagram, QPointF(), parent);
			ConductorAutoNumerotation can(new_c, diagram, parent);
			can.numerate();
			if (diagram->freezeNewConductors() || diagram->project()->isFreezeNewConductors())
				new_c->setFreezeLabel(true);

			broken_endpoints.insert(m.connect_to);
		}

		if (other_terminal && join_far) {
			Conductor *new_c2 = new Conductor(best_other, other_terminal);
			if (matches.first().conductor->medium() == TerminalData::Pipe) {
				new_c2->setProperties(matches.first().conductor->properties());
				new_c2->takeNozzleDiameter();
					//Its designation written once (step 7b)
				Conductor::keepOneDesignation({first_half, new_c2}, matches.first().conductor);
			}
			new AddGraphicsObjectCommand(new_c2, diagram, QPointF(), parent);
			ConductorAutoNumerotation can2(new_c2, diagram, parent);
			can2.numerate();
			if (diagram->freezeNewConductors() || diagram->project()->isFreezeNewConductors())
				new_c2->setFreezeLabel(true);

			broken_endpoints.insert(best_other);
			used_terminals.insert(other_terminal);
		}
	}

	return broken_endpoints;
}
