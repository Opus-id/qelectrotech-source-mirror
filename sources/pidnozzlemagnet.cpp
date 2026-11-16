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
#include "pidnozzlemagnet.h"

#include "autobreakconductor.h"
#include "diagram.h"
#include "pidfreeend.h"
#include "properties/terminaldata.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"

#include <QBrush>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QLineF>
#include <QPen>
#include <QRectF>

QPointer<Terminal> PidNozzleMagnet::s_marked;
QGraphicsEllipseItem *PidNozzleMagnet::s_spot = nullptr;
QPointer<QGraphicsScene> PidNozzleMagnet::s_spot_folio;

namespace {
/// A pipe nozzle on a symbol shown on the folio
bool shownNozzle(const Terminal *terminal)
{
	return terminal && terminal->isVisible()
			&& terminal->parentElement()
			&& terminal->parentElement()->isVisible()
			&& terminal->medium() == TerminalData::Pipe;
}

/// A pipe nozzle a pipe may still reach, on a symbol shown on the folio
bool freeNozzle(const Terminal *terminal)
{
	return shownNozzle(terminal) && terminal->hasRoomForWire();
}

/// The other end of @p pipe than @p end
const Terminal *farEnd(const Conductor *pipe, const Terminal *end)
{
	return pipe->terminal1 == end ? pipe->terminal2 : pipe->terminal1;
}

/// @p own, of a symbol moved, brought by @p step against @p other, which
/// holds the pipe @p pipe, lies on it: the pipe goes on from the nozzle of
/// the symbol facing back, ahead of it, never back to it
bool goesOnFromTheSymbol(const Terminal *own, const Terminal *other, const Conductor *pipe,
			 const QList<Element *> &moved)
{
	const Terminal *far = farEnd(pipe, other);
	if (!far || moved.contains(far->parentElement())
		|| !liesOnConductor(pipe, own->dockConductor())) {
		return false;
	}
	const QPointF step = other->dockConductor() - own->dockConductor();
	for (const Terminal *nozzle : own->parentElement()->terminals())
	{
		if (nozzle != own && freeNozzle(nozzle)
			&& Qet::isOpposed(nozzle->orientation(), own->orientation())
			&& PidNozzleMagnet::ahead(nozzle->orientation(), nozzle->dockConductor() + step,
						  far->dockConductor()) >= -PidNozzleMagnet::kSamePoint) {
			return true;
		}
	}
	return false;
}

/// Whether @p own, a free nozzle of a symbol moved, is drawn to @p other,
/// a nozzle of a symbol left in place (see the class)
bool catches(const Terminal *own, const Terminal *other, const QList<Element *> &moved)
{
	if (!shownNozzle(other) || !own->sameMediumAs(other)) {
		return false;
	}
		//The free end of a line: the symbol takes the end
	if (const Conductor *held = PidFreeEnd::pipeOf(other)) {
		const Terminal *far = farEnd(held, other);
		return far && !moved.contains(far->parentElement())
				&& (own->orientation() == other->orientation()
				    || PidNozzleMagnet::faces(own, other));
	}
	if (!PidNozzleMagnet::faces(own, other)) {
		return false;
	}
	if (other->hasRoomForWire()) {
			//Against a nozzle of a symbol left in place, it is taken:
			//two symbols against each other are joined, with no pipe
		const Terminal *contact = other->contactNozzle();
		return !contact || moved.contains(contact->parentElement());
	}
		//With its pipe, the symbol dropped on it
	const QList<Conductor *> pipes = other->conductors();
	if (pipes.size() != 1) {
		return false;
	}
	const Conductor *pipe = pipes.constFirst();
	return pipe->medium() == TerminalData::Pipe && !pipe->endsTouch()
			&& goesOnFromTheSymbol(own, other, pipe, moved);
}
}

/**
	@brief PidNozzleMagnet::find
	@param moved : the symbols placed or dragged; their nozzles are drawn
	to those of the others only
	@param radius : how near, in units of the folio (Terminal::magnetSceneRadius())
	@return the nearest pair of nozzles which catch (see the class) within
	@p radius, a free one of @p moved, one of another symbol; a null snap
	when there is none or @p radius is not positive
*/
PidNozzleMagnet::Snap PidNozzleMagnet::find(const QList<Element *> &moved, qreal radius)
{
	Snap nearest;
	if (radius <= 0) {
		return nearest;
	}
	qreal nearest_distance = 0;
	for (Element *element : moved)
	{
		Diagram *folio = element ? element->diagram() : nullptr;
		if (!folio) {
			continue;
		}
		for (Terminal *own : element->terminals())
		{
			if (!freeNozzle(own)) {
				continue;
			}
			const QPointF dock = own->dockConductor();
			const QRectF area(dock.x() - radius, dock.y() - radius, 2 * radius, 2 * radius);
			for (QGraphicsItem *item : folio->items(area))
			{
				Terminal *other = qgraphicsitem_cast<Terminal *>(item);
				if (!other || moved.contains(other->parentElement())
					|| !catches(own, other, moved)) {
					continue;
				}
				const qreal distance = QLineF(dock, other->dockConductor()).length();
				if (distance > radius) {
					continue;
				}
				if (nearest.isNull() || distance < nearest_distance) {
					nearest.own = own;
					nearest.target = other;
					nearest.step = other->dockConductor() - dock;
					nearest_distance = distance;
				}
			}
		}
	}
	return nearest;
}

/**
	@brief PidNozzleMagnet::faces
	@return true when @p a and @p b point at each other on the folio: placed
	nozzle on nozzle, their symbols stand on either side, not one on the other
*/
bool PidNozzleMagnet::faces(const Terminal *a, const Terminal *b)
{
	return a && b && (a->orientation() + 2) % 4 == b->orientation();
}

/**
	@brief PidNozzleMagnet::ahead
	See the declaration
*/
qreal PidNozzleMagnet::ahead(Qet::Orientation way, const QPointF &from, const QPointF &to)
{
	const QPointF d = to - from;
	switch (way) {
		case Qet::North: return -d.y();
		case Qet::South: return d.y();
		case Qet::East:  return d.x();
		case Qet::West:  return -d.x();
	}
	return 0;
}

/**
	@brief PidNozzleMagnet::mark
	Show @p target as the nozzle caught, and no other
*/
void PidNozzleMagnet::mark(Terminal *target)
{
	if (s_marked == target) {
		return;
	}
	clearMark();
	s_marked = target;
	QGraphicsScene *folio = target ? target->scene() : nullptr;
	if (!folio) {
		return;
	}
		//The mark of a hovered nozzle (Terminal::showHover()), but on top
		//of everything: a symbol lies under its pipes, a thick pipe would
		//hide the nozzle's own mark. Shown only, never clicked nor saved
	const qreal r = 2.5;
	s_spot = new QGraphicsEllipseItem(-r, -r, 2 * r, 2 * r);
	s_spot->setPen(QPen(Terminal::allowedColor, 1));
	s_spot->setBrush(Terminal::allowedColor);
	s_spot->setAcceptedMouseButtons(Qt::NoButton);
	s_spot->setAcceptHoverEvents(false);
	s_spot->setZValue(1000000);
	s_spot->setPos(target->dockConductor());
	folio->addItem(s_spot);
	s_spot_folio = folio;
}

/**
	@brief PidNozzleMagnet::clearMark
	No nozzle shown as caught any more
*/
void PidNozzleMagnet::clearMark()
{
	s_marked.clear();
		//Gone with its folio, when the folio went first
	if (s_spot && s_spot_folio) {
		s_spot_folio->removeItem(s_spot);
		delete s_spot;
	}
	s_spot = nullptr;
	s_spot_folio.clear();
}

/**
	@brief PidNozzleMagnet::marked
	@return the nozzle shown as caught, if any
*/
Terminal *PidNozzleMagnet::marked()
{
	return s_marked.data();
}
