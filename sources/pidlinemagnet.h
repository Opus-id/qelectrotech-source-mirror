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
#ifndef PIDLINEMAGNET_H
#define PIDLINEMAGNET_H

#include <QLineF>
#include <QList>
#include <QPointer>
#include <QPointF>

class Conductor;
class Element;
class QGraphicsLineItem;
class QGraphicsScene;
class Terminal;

/**
	@brief The PidLineMagnet class
	The magnet of the lines (P&ID, step 8r-4 a, b): a symbol of a line (a
	valve, a check valve, a reducer, a mark on the line, a tee or a cross by
	their straight run: Element::pipeRun(), or a reducer) placed or dragged
	with the mouse near a straight segment of a pipe comes onto its axis,
	its two nozzles of the run on the pipe, so that the drop breaks it
	there (autoBreakConductors()). It turns a quarter by itself when its run
	lies across the segment, horizontal or vertical. Only when the two
	nozzles of the run are free, and only a pipe of no symbol moved. The
	segment caught is shown while the mouse moves. The magnet of the
	nozzles comes first; Ctrl held places without either.
*/
class PidLineMagnet
{
	public:
		struct Snap
		{
				/// What brings the run of the symbol onto the axis of the
				/// segment, once turned by turn
			QPointF step;
				/// 90 when the symbol must turn a quarter first, else 0
			qreal turn = 0;
			QPointer<Conductor> pipe;
				/// The segment caught, on the folio
			QLineF segment;
			bool isNull() const { return pipe.isNull(); }
		};

			/// The two nozzles of the straight run of @p element facing away
			/// from each other, both free (no pipe yet), whose axis is
			/// horizontal when @p horizontal, vertical otherwise; false when
			/// it has none such
		static bool run(const Element *element, bool horizontal, Terminal **first, Terminal **second);
			/// The nearest straight segment within @p radius (in units of the
			/// folio) of the middle of a run of @p element, which the run
			/// fits on; @p moved are the symbols placed or dragged, whose
			/// pipes are never caught. Null when none, or @p radius is not
			/// positive
		static Snap find(Element *element, qreal radius, const QList<Element *> &moved);
			/// find(), and the quarter turn it asks for given to @p element
			/// (Element rotation, about its point of insertion): the step
			/// then left to do. A symbol turned which then finds nothing is
			/// turned back. Null when nothing is caught.
		static Snap catchOn(Element *element, qreal radius, const QList<Element *> &moved);

		static void mark(const Snap &snap);
		static void clearMark();
			/// The segment shown as caught; null when none
		static QLineF marked();

	private:
		static QGraphicsLineItem *s_line;
		static QPointer<QGraphicsScene> s_line_folio;
};

#endif // PIDLINEMAGNET_H
