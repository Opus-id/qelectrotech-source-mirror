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
#ifndef PIDPROCESSCONNECTION_H
#define PIDPROCESSCONNECTION_H

#include "ElementsCollection/elementslocation.h"
#include "qet.h"

#include <QCoreApplication>
#include <QPointF>
#include <QString>

class Conductor;
class Diagram;
class Terminal;

/**
	@brief The PidProcessConnection class
	A process connection (P&ID, 6b-3c2): the thin line from the place a
	measurement is taken on the process to an instrument bubble, drawn in one
	gesture with the tool "Draw a pipe" (DiagramEventAddPipe). It is a signal
	line of type SignalProperties::ProcessConnection between a signal terminal
	and the process, whichever end it is drawn from:
	- a free end of a pipe (Element::isFreeEnd(), the line ends on nothing):
	  the tool closes the line with the blind flange of the project
	  (PidCodification::blindFlangeRole()), placed where the free end was and
	  turned the same way, marked as placed by the tool
	  (Element::isToolPlaced(): it goes with the pipe it closes), and the line
	  leaves from the signal terminal of that flange;
	- any other free nozzle (equipment, valve, tee...): the line ends on the
	  nozzle itself, which keeps its size, its connection and its flange, and
	  takes no pipe any more.
	The terminals of different media are joined there whatever the wiring
	rules of the project say (Terminal::refusesMediumOf()), without a word:
	that is what a process connection is. Everything is one undo step.
*/
class PidProcessConnection
{
	Q_DECLARE_TR_FUNCTIONS(PidProcessConnection)

	public:
			/// The blind flange of the project of @p diagram, a null location
			/// when it has none or when it is not found
		static ElementsLocation blindFlangeSymbol(const Diagram *diagram);
			/// Whether a process connection may end on @p terminal on the side
			/// of the process: the nozzle of a free end holding its pipe
			/// (closed by a blind flange) or a free nozzle
		static bool isProcessEnd(const Terminal *terminal);
			/// Whether a process connection may join @p a and @p b, in either
			/// order: a signal terminal with room for a line and a process end
			/// (isProcessEnd()), of two symbols of one folio
		static bool mayJoin(const Terminal *a, const Terminal *b);
			/// The terminal of @p diagram nearest to @p scene_pos within
			/// @p radius which a process connection from @p from may reach
			/// (mayJoin()): a process end from a signal terminal, a signal
			/// terminal from a process end; nullptr for none
		static Terminal *endNear(Diagram *diagram, const QPointF &scene_pos, qreal radius,
								 const Terminal *from);
			/// The way the line leaves @p terminal: out of the blind flange,
			/// past the free end, for the nozzle of a free end; else the way
			/// the terminal faces
		static Qet::Orientation wayOut(const Terminal *terminal);
			/// The process connection between @p a and @p b (mayJoin()), in
			/// one undo step. @p message, when given, gets what the user should
			/// know: why nothing was done, or that the free end closed was not
			/// flanged. @return the line, nullptr when nothing was done
		static Conductor *draw(Terminal *a, Terminal *b, QString *message = nullptr);
};

#endif // PIDPROCESSCONNECTION_H
