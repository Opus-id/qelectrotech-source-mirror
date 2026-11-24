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
#ifndef PIDNOZZLEMAGNET_H
#define PIDNOZZLEMAGNET_H

#include "qet.h"

#include <QList>
#include <QPointF>
#include <QPointer>

class Element;
class QGraphicsEllipseItem;
class QGraphicsScene;
class Terminal;

/**
	@brief The PidNozzleMagnet class
	The magnet of the nozzles (P&ID, 5l-1c): a symbol placed or dragged with
	the mouse near a free nozzle of another symbol comes against it exactly,
	nozzle on nozzle, so the two touch (Terminal::touchesNozzle()) and draw
	no connection between them. Free pipe nozzles (one pipe each, none yet)
	that face each other catch; a wire or a terminal of another medium never
	does. Two more catch a free nozzle of the symbol moved:
	- the free end of a line (PidFreeEnd), facing the same way or facing it:
	  dropped, the symbol takes the end (autoBreakConductors());
	- a nozzle with its pipe, facing it, when the nozzle of the symbol lies
	  on that pipe: dropped, the pipe goes on from the other nozzle of the
	  symbol; not when it would go back to it (pipe shorter than the symbol).
	The nozzle caught is shown while the mouse moves.
	Ctrl held (step 8r-4; Shift before), or the keyboard (Ctrl + arrows),
	places without it.
*/
class PidNozzleMagnet
{
	public:
		/// What the magnet does: the step that brings @a own on @a target
		struct Snap
		{
			QPointF step;
			Terminal *own = nullptr;
			Terminal *target = nullptr;
			bool isNull() const { return !own || !target; }
		};

			/// Two points nearer than this, in units of the folio, are one:
			/// two nozzles in contact, a nozzle on the end of a pipe. Half a
			/// unit, below the unit of the fine move (Ctrl + arrows)
		static constexpr qreal kSamePoint = 0.5;

		static Snap find(const QList<Element *> &moved, qreal radius);
		static bool faces(const Terminal *a, const Terminal *b);
			/// How far @p to lies ahead of @p from, along @p way (negative:
			/// behind)
		static qreal ahead(Qet::Orientation way, const QPointF &from, const QPointF &to);
		static void mark(Terminal *target);
		static void clearMark();
		static Terminal *marked();

	private:
		static QPointer<Terminal> s_marked;
			/// The mark shown on top of the folio, and its folio
		static QGraphicsEllipseItem *s_spot;
		static QPointer<QGraphicsScene> s_spot_folio;
};

#endif // PIDNOZZLEMAGNET_H
