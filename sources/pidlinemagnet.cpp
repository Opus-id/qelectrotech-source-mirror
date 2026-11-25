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
#include "pidlinemagnet.h"

#include "diagram.h"
#include "properties/terminaldata.h"
#include "qet.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"

#include <QColor>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

QGraphicsLineItem *PidLineMagnet::s_line = nullptr;
QPointer<QGraphicsScene> PidLineMagnet::s_line_folio;

namespace {
	/// Two values nearer than this, in units of the folio, are one
constexpr qreal kSame = 0.01;

	/// A symbol of a line: its straight run (a tee, a cross, a valve...), or
	/// a reducer, whose two nozzles are its run all the same
QList<Terminal *> runNozzles(const Element *element)
{
	QList<Terminal *> nozzles;
	const QStringList names = element->pipeRun();
	int pipe_nozzles = 0;
	for (Terminal *terminal : element->terminals()) {
		if (terminal->medium() == TerminalData::Pipe) {
			++pipe_nozzles;
		}
	}
	const bool reducer = element->isReducer() && pipe_nozzles == 2;
	for (Terminal *terminal : element->terminals())
	{
		if (terminal->medium() != TerminalData::Pipe) {
			continue;
		}
		if (reducer || names.contains(terminal->baseName())) {
			nozzles << terminal;
		}
	}
	return nozzles;
}

	/// A nozzle with no pipe yet, which may take one
bool isFree(const Terminal *terminal)
{
	return terminal->isVisible() && terminal->hasRoomForWire() && terminal->conductors().isEmpty();
}
}

/**
	@brief PidLineMagnet::run
	See the declaration
*/
bool PidLineMagnet::run(const Element *element, bool horizontal, Terminal **first, Terminal **second)
{
	if (!element || !element->isVisible()) {
		return false;
	}
	const QList<Terminal *> nozzles = runNozzles(element);
	for (int i = 0 ; i < nozzles.size() ; ++i)
	{
		for (int j = i + 1 ; j < nozzles.size() ; ++j)
		{
			Terminal *a = nozzles.at(i);
			Terminal *b = nozzles.at(j);
			if (!Qet::isOpposed(a->orientation(), b->orientation()) || !isFree(a) || !isFree(b)) {
				continue;
			}
			const QPointF da = a->dockConductor();
			const QPointF db = b->dockConductor();
			const bool across_x = qAbs(da.y() - db.y()) < kSame;
			const bool across_y = qAbs(da.x() - db.x()) < kSame;
			if ((horizontal && across_x) || (!horizontal && across_y)) {
				*first = a;
				*second = b;
				return true;
			}
		}
	}
	return false;
}

/**
	@brief PidLineMagnet::find
	See the declaration
*/
PidLineMagnet::Snap PidLineMagnet::find(Element *element, qreal radius, const QList<Element *> &moved)
{
	Snap nearest;
	Diagram *folio = element ? element->diagram() : nullptr;
	if (!folio || radius <= 0) {
		return nearest;
	}
		//The runs it has, along x and along y (a cross has both)
	struct Run { QPointF middle; qreal half = 0; bool horizontal = true; };
	QList<Run> runs;
	for (const bool horizontal : {true, false}) {
		Terminal *a = nullptr, *b = nullptr;
		if (run(element, horizontal, &a, &b)) {
			const QPointF da = a->dockConductor(), db = b->dockConductor();
			runs << Run{(da + db) / 2, QLineF(da, db).length() / 2, horizontal};
		}
	}
	if (runs.isEmpty()) {
		return nearest;
	}
	qreal nearest_distance = 0;
	auto consider = [&](Conductor *pipe, const QPointF &p1, const QPointF &p2) {
		const bool horizontal = qAbs(p1.y() - p2.y()) < kSame;
		const bool vertical = qAbs(p1.x() - p2.x()) < kSame;
		if (horizontal == vertical) {
				//Slanted, or of no length
			return;
		}
			//A run along the segment, else one to turn a quarter: the
			//other of a cross, or the only one
		const Run *along = nullptr;
		for (const Run &r : std::as_const(runs)) {
			if (r.horizontal == horizontal) along = &r;
		}
		const Run &r = along ? *along : runs.constFirst();
		const qreal across = horizontal ? r.middle.y() - p1.y() : r.middle.x() - p1.x();
		const qreal at = horizontal ? r.middle.x() : r.middle.y();
		const qreal low = horizontal ? qMin(p1.x(), p2.x()) : qMin(p1.y(), p2.y());
		const qreal high = horizontal ? qMax(p1.x(), p2.x()) : qMax(p1.y(), p2.y());
			//The whole run on the segment, the symbol turned or not
		const qreal distance = qAbs(across);
		if (distance > radius || at - r.half < low - kSame || at + r.half > high + kSame) {
			return;
		}
		if (!nearest.isNull() && distance >= nearest_distance) {
			return;
		}
		nearest.pipe = pipe;
		nearest.segment = QLineF(p1, p2);
		nearest.turn = along ? 0 : 90;
		nearest.step = along ? (horizontal ? QPointF(0, -across) : QPointF(-across, 0)) : QPointF();
		nearest_distance = distance;
	};
	for (const Run &r : std::as_const(runs))
	{
		const QRectF area(r.middle.x() - radius - r.half, r.middle.y() - radius - r.half,
						  2 * (radius + r.half), 2 * (radius + r.half));
		for (QGraphicsItem *item : folio->items(area))
		{
			Conductor *pipe = qgraphicsitem_cast<Conductor *>(item);
			if (!pipe || !pipe->isVisible() || pipe->medium() != TerminalData::Pipe || pipe->endsTouch()
				|| moved.contains(pipe->terminal1->parentElement())
				|| moved.contains(pipe->terminal2->parentElement())) {
				continue;
			}
			const QPainterPath path = pipe->path();
			for (int i = 0 ; i + 1 < path.elementCount() ; ++i)
			{
				const QPainterPath::Element &e1 = path.elementAt(i);
				const QPainterPath::Element &e2 = path.elementAt(i + 1);
				consider(pipe, pipe->mapToScene(QPointF(e1.x, e1.y)),
						 pipe->mapToScene(QPointF(e2.x, e2.y)));
			}
		}
	}
	return nearest;
}

/**
	@brief PidLineMagnet::catchOn
	See the declaration
*/
PidLineMagnet::Snap PidLineMagnet::catchOn(Element *element, qreal radius, const QList<Element *> &moved)
{
	Snap snap = find(element, radius, moved);
	if (snap.isNull() || snap.turn == 0) {
		return snap;
	}
		//Turned a quarter, then the run along the segment brought onto it
	const qreal before = element->rotation();
	element->setRotation(before + 90);
	Snap turned = find(element, radius, moved);
	if (turned.isNull() || turned.turn != 0) {
		element->setRotation(before);
		return Snap();
	}
	turned.turn = 90;
	return turned;
}

/**
	@brief PidLineMagnet::mark
	Show the segment of @p snap as the one caught, and no other: a broad
	line over it, on top of everything, as the nozzle caught by the magnet
	of the nozzles (PidNozzleMagnet::mark())
*/
void PidLineMagnet::mark(const Snap &snap)
{
	if (snap.isNull() || !snap.pipe->scene()) {
		clearMark();
		return;
	}
	if (s_line && s_line_folio == snap.pipe->scene()) {
		s_line->setLine(snap.segment);
		return;
	}
	clearMark();
	QColor ink = Terminal::allowedColor;
	ink.setAlpha(110);
	s_line = new QGraphicsLineItem(snap.segment);
	s_line->setPen(QPen(ink, 6, Qt::SolidLine, Qt::RoundCap));
	s_line->setAcceptedMouseButtons(Qt::NoButton);
	s_line->setAcceptHoverEvents(false);
	s_line->setZValue(1000000);
	snap.pipe->scene()->addItem(s_line);
	s_line_folio = snap.pipe->scene();
}

/**
	@brief PidLineMagnet::clearMark
	No segment shown as caught any more
*/
void PidLineMagnet::clearMark()
{
		//Gone with its folio, when the folio went first
	if (s_line && s_line_folio) {
		s_line_folio->removeItem(s_line);
		delete s_line;
	}
	s_line = nullptr;
	s_line_folio.clear();
}

/**
	@brief PidLineMagnet::marked
	See the declaration
*/
QLineF PidLineMagnet::marked()
{
	return s_line && s_line_folio ? s_line->line() : QLineF();
}
