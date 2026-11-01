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
#include "changepipepropertiescommand.h"

#include "../qetgraphicsitem/pipeline.h"

ChangePipePropertiesCommand::ChangePipePropertiesCommand(QUndoCommand *parent) :
	QUndoCommand(parent)
{
	setText(QObject::tr("Change the properties of a pipe line"));
}

void ChangePipePropertiesCommand::addChange(PipeLine *pipe, const PipeProperties &properties)
{
	if (!pipe || pipe->properties() == properties) {
		return;
	}
	m_changes.append({pipe, pipe->properties(), properties});
}

void ChangePipePropertiesCommand::undo()
{
	for (const Change &change : std::as_const(m_changes)) {
		if (change.pipe) {
			change.pipe->setProperties(change.old_properties);
		}
	}
}

void ChangePipePropertiesCommand::redo()
{
	for (const Change &change : std::as_const(m_changes)) {
		if (change.pipe) {
			change.pipe->setProperties(change.new_properties);
		}
	}
}
