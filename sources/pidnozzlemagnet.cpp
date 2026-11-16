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

#include "diagram.h"
#include "properties/terminaldata.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"

#include <QLineF>
#include <QRectF>

QPointer<Terminal> PidNozzleMagnet::s_marked;

namespace {
/// A pipe nozzle a pipe may still reach, on a symbol shown on the folio
bool freeNozzle(const Terminal *terminal)
{
	return terminal && terminal->isVisible()
			&& terminal->parentElement()
			&& terminal->parentElement()->isVisible()
			&& terminal->medium() == TerminalData::Pipe
			&& terminal->hasRoomForWire();
}
}

/**
	@brief PidNozzleMagnet::find
	@param moved : the symbols placed or dragged; their nozzles are drawn
	to those of the others only
	@param radius : how near, in units of the folio (Terminal::magnetSceneRadius())
	@return the nearest pair of free nozzles facing each other within
	@p radius, one of @p moved, one of another symbol; a null snap when
	there is none or @p radius is not positive
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
				if (!freeNozzle(other) || moved.contains(other->parentElement())
					|| !own->sameMediumAs(other) || !faces(own, other)) {
					continue;
				}
					//Against a nozzle of a symbol left in place, it is taken:
					//two symbols against each other are joined, with no pipe
				const Terminal *contact = other->contactNozzle();
				if (contact && !moved.contains(contact->parentElement())) {
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
	if (target) {
		target->showHover(true, Terminal::allowedColor);
	}
}

/**
	@brief PidNozzleMagnet::clearMark
	No nozzle shown as caught any more
*/
void PidNozzleMagnet::clearMark()
{
	if (s_marked) {
		s_marked->showHover(false);
	}
	s_marked.clear();
}

/**
	@brief PidNozzleMagnet::marked
	@return the nozzle shown as caught, if any
*/
Terminal *PidNozzleMagnet::marked()
{
	return s_marked.data();
}
