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
#ifndef PIDRUNREATTACHCOMMAND_H
#define PIDRUNREATTACHCOMMAND_H

#include <QList>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QUndoCommand>

class Conductor;
class Element;
class Terminal;

/**
	@brief The PidRunReattachCommand class
	The pipes of P&ID symbols turned or mirrored (P&ID, 5l-1h): in each
	pair of opposite nozzles, the pipes go over to the nozzles which now
	face their other ends best (two swap, or one goes to the free nozzle).
	Made a child of the command which turns or mirrors the symbols
	(RotateSelectionCommand, MirrorSelectionCommand), after the children
	which do it: built before the turn, it notes the pairs the user drew
	the wrong way round, left as they are; its first redo, after the
	turn, moves the ends (Conductor::moveEnd()), its undo moves them back
	before the turn is undone, the next redos move them the same way again.
	Only between the two nozzles of a straight run (straightPairs()): a
	valve, a flow arrow, the run of a tee or a cross, or a symbol with two
	opposite nozzles only (a pump, a blower); the branch of a tee keeps its
	pipe, as does any nozzle of an equipment with more than two. Pipes
	only: signal lines and wires stay where they are.
*/
class PidRunReattachCommand : public QUndoCommand
{
	public:
		PidRunReattachCommand(const QList<Element *> &elements, QUndoCommand *parent = nullptr);
		void undo() override;
		void redo() override;

			/// Whether one of @p elements has a straight run of nozzles,
			/// for which this command is worth adding
		static bool concerns(const QList<Element *> &elements);
			/// The pairs of nozzles of @p element a line runs straight
			/// through: two nozzles of its run (Element::pipeRun()) which
			/// face opposite ways, neither hidden by its options; without a
			/// run, its two nozzles if it has only two, face to face
		static QList<QPair<Terminal *, Terminal *>> straightPairs(const Element *element);
			/// The pipe ending on @p nozzle, if there is one
		static Conductor *pipeOn(const Terminal *nozzle);
			/// Whether the other end of @p pipe lies behind @p nozzle, so that
			/// the pipe runs back through its symbol
		static bool runsBack(const Terminal *nozzle, const Conductor *pipe);

	private:
		static qreal facing(const Terminal *nozzle, const Terminal *other);
		static bool betterSwapped(const Terminal *a, const Terminal *b);
		static Terminal *otherEnd(const Conductor *pipe, const Terminal *nozzle);
		void moveEnds();
		bool movePipe(Conductor *pipe, Terminal *from, Terminal *to);

			/// A pipe end moved from one nozzle to another
		struct EndMove {
			QPointer<Conductor> conductor;
			QPointer<Terminal> from, to;
		};

		QList<QPointer<Element>> m_elements;
			/// The straight pairs the user drew the wrong way round before
			/// the turn (betterSwapped()), left as they are
		QSet<QPair<const Terminal *, const Terminal *>> m_drawn_swapped;
		QList<EndMove> m_moves;
		bool m_moves_known = false;
};

#endif // PIDRUNREATTACHCOMMAND_H
