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
#ifndef FILLUNSIZEDPIPESCOMMAND_H
#define FILLUNSIZEDPIPESCOMMAND_H

#include "../conductorproperties.h"
#include "../pipediameter.h"

#include <QList>
#include <QPair>
#include <QPointer>
#include <QStringList>
#include <QUndoCommand>

class Conductor;
class Element;
class Terminal;

/**
	@brief The FillUnsizedPipesCommand class
	A size which comes to a nozzle with a size of its own (equipment) once
	its pipes are drawn, typed or brought by another symbol ("Change type"),
	is given to the pipes without a size of its line: through valves and
	placed symbols (Conductor::unsizedRun()), up to a reducer, equipment or
	a pipe which has a size. A size is never replaced. Each pipe takes it as
	a new pipe takes the size of its nozzle (Conductor::giveNozzleSize()):
	never a question (step 5l-1aa). Undone with the step it is a child of.
*/
class FillUnsizedPipesCommand : public QUndoCommand
{
	public:
			/// A size given to a nozzle
		using NozzleSize = QPair<const Terminal *, PipeDiameter>;
			/// A pipe and what it becomes
		struct Fill {
			QPointer<Conductor> pipe;
			ConductorProperties before, after;
		};
			/// What @p sizes give the pipes of the lines of their nozzles,
			/// as they are now; the first nozzle which reaches a pipe gives
			/// it its size. @p notes, if given, receives what is to be said:
			/// how many pipes took a size, a DN or an outside diameter
			/// changed by their material
		static QList<Fill> plan(const QList<NozzleSize> &sizes, QStringList *notes = nullptr);
			/// Give the pipes of @p fills what plan() found
		explicit FillUnsizedPipesCommand(const QList<Fill> &fills,
										 QUndoCommand *parent = nullptr);
			/// Give the pipes the sizes the nozzles of @p element have once
			/// the steps before this one are done, for those whose size
			/// changed since this command was made: made before them, as a
			/// child of their step, after them
		explicit FillUnsizedPipesCommand(Element *element, QUndoCommand *parent = nullptr);
		void undo() override;
		void redo() override;

	private:
		QList<Fill> m_fills;
			/// For the second constructor: the sizes before, found again on
			/// the first redo()
		QList<QPair<QPointer<Terminal>, PipeDiameter>> m_sizes_before;
		bool m_planned = true;
};

#endif // FILLUNSIZEDPIPESCOMMAND_H
