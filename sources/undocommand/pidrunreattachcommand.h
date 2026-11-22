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
	The pipes of P&ID symbols turned or mirrored (P&ID, 5l-1h): a pipe the
	turn leaves running through its symbol goes over to the nozzle which
	now faces its other end.
	Made a child of the command which turns or mirrors the symbols
	(RotateSelectionCommand, MirrorSelectionCommand), after the children
	which do it: built before the turn, it notes which pipes already ran
	back through their symbol, left as they are; its first redo, after the
	turn, moves the ends (Conductor::moveEnd()), its undo moves them back
	before the turn is undone, the next redos move them the same way again.
	Only between the two nozzles of a straight run (straightPairs()): a
	valve, a flow arrow, the run of a tee or a cross; the branch of a tee
	keeps its pipe. Pipes only: signal lines and wires stay where they are.
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
			/// face opposite ways, neither hidden by its options
		static QList<QPair<Terminal *, Terminal *>> straightPairs(const Element *element);
			/// The pipe ending on @p nozzle, if there is one
		static Conductor *pipeOn(const Terminal *nozzle);
			/// Whether the other end of @p pipe lies behind @p nozzle, so that
			/// the pipe runs back through its symbol
		static bool runsBack(const Terminal *nozzle, const Conductor *pipe);

	private:
		static bool behind(const Terminal *nozzle, const Terminal *other);
		static Terminal *otherEnd(const Conductor *pipe, const Terminal *nozzle);
		void moveEnds();
		bool movePipe(Conductor *pipe, Terminal *from, Terminal *to);
		bool turnedBack(Terminal *nozzle, Conductor *pipe) const;

			/// A pipe end moved from one nozzle to another
		struct EndMove {
			QPointer<Conductor> conductor;
			QPointer<Terminal> from, to;
		};

		QList<QPointer<Element>> m_elements;
			/// The pipes which ran back through their symbol before the
			/// turn, with their nozzle
		QSet<QPair<const Conductor *, const Terminal *>> m_back_before;
		QList<EndMove> m_moves;
		bool m_moves_known = false;
};

#endif // PIDRUNREATTACHCOMMAND_H
