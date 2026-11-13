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
#ifndef PIDFREEEND_H
#define PIDFREEEND_H

#include "pipeproperties.h"
#include "qet.h"

#include <QCoreApplication>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

class Conductor;
class Diagram;
class QUndoCommand;
class Terminal;

/**
	@brief The PidFreeEnd class
	A line taken up at its free end (P&ID, 5l-1p-quater). The nozzle of a
	free end (Element::isFreeEnd(), pid_role="free_end") holds the pipe it
	ends: a new pipe drawn from it, or to it, does not end there. The free
	end goes, and the line goes on as one pipe, from the far end of the pipe
	it held to the other end of the new one:
	- the route of the pipe already drawn is kept, even moved by hand; the
	  new piece follows on from where the free end was, along the route the
	  tool shows (Edit > Reset conductors routes it again);
	- the data of that pipe are kept (fluid, size, line number...);
	- from a free end to another, two lines become one: the data of the line
	  drawn from win, a field it leaves empty is taken from the other, and
	  what is set aside is said.
	The free ends removed, the pipes they held and the new pipe are one undo
	step. Pipes only: a wire never takes up anything.
*/
class PidFreeEnd
{
	Q_DECLARE_TR_FUNCTIONS(PidFreeEnd)

	public:
			/// The pipe held by @p nozzle when it is the nozzle of a free end
			/// with a single pipe, which may be taken up there; else nullptr
		static Conductor *pipeOf(const Terminal *nozzle);
			/// The way a pipe leaves @p nozzle: past a free end it may take
			/// up, on along the line (back from the way its nozzle faces); the
			/// way of any other nozzle
		static Qet::Orientation wayOut(const Terminal *nozzle);
			/// Whether a pipe from @p from to @p to takes up a free end at one
			/// of them at least: the other end a nozzle of a pipe which may
			/// take a pipe, or a free end of another line, not on the same
			/// symbol
		static bool mayJoin(const Terminal *from, const Terminal *to);
			/// The nozzle of a free end of @p diagram nearest to @p scene_pos,
			/// within @p radius, which a pipe from @p from may take up (any
			/// when @p from is null); nullptr for none
		static Terminal *nozzleNear(Diagram *diagram, const QPointF &scene_pos, qreal radius,
									const Terminal *from = nullptr);
			/// The route of a new piece from @p from to @p to, on the folio:
			/// the route a pipe placed between them takes, a free end taken up
			/// being passed straight on
		static QList<QPointF> route(const Terminal *from, const Terminal *to);
			/// The data of two lines joined: those of @p first, with the
			/// fields it leaves empty taken from @p second; @p dropped, when
			/// given, gets the names of the fields of @p second set aside
		static PipeProperties mergedData(const PipeProperties &first, const PipeProperties &second,
										 QStringList *dropped = nullptr);
			/// The line from @p from to @p to taking up the free ends there
			/// (mayJoin()), as children of @p parent: the free ends and their
			/// pipes removed, the pipe of the whole line added. @p message,
			/// when given, says what was set aside. @return the new pipe,
			/// nullptr when nothing was done
		static Conductor *join(Diagram *diagram, Terminal *from, Terminal *to, QUndoCommand *parent,
							   QString *message = nullptr);
			/// join() in an undo step of its own
		static Conductor *carryOn(Terminal *from, Terminal *to, QString *message = nullptr);
			/// While a preview reaches the free end of @p nozzle: its symbol
			/// hidden (@p hide) but its nozzle drawn, lit as hovered; shown
			/// again when @p hide is false
		static void conceal(Terminal *nozzle, bool hide);
};

#endif // PIDFREEEND_H
