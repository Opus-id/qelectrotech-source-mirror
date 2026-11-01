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
#ifndef CHANGEPIPEPROPERTIESCOMMAND_H
#define CHANGEPIPEPROPERTIESCOMMAND_H

#include "../pipeproperties.h"

#include <QList>
#include <QPointer>
#include <QUndoCommand>

class PipeLine;

/**
	@brief The ChangePipePropertiesCommand class
	Give one or several pipe lines new properties (PipeProperties), each
	its own, and give them back on undo. Nothing is passed on to the pipe
	lines they are connected to.
*/
class ChangePipePropertiesCommand : public QUndoCommand
{
	public:
		ChangePipePropertiesCommand(QUndoCommand *parent = nullptr);

			/// Give @p pipe the properties @p properties on redo; its
			/// present ones come back on undo
		void addChange(PipeLine *pipe, const PipeProperties &properties);
			/// Whether a change was added
		bool isEmpty() const { return m_changes.isEmpty(); }

		void undo() override;
		void redo() override;

	private:
		struct Change
		{
			QPointer<PipeLine> pipe;
			PipeProperties old_properties;
			PipeProperties new_properties;
		};
		QList<Change> m_changes;
};

#endif // CHANGEPIPEPROPERTIESCOMMAND_H
