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
#include "changepidcodificationcommand.h"

#include "../qetproject.h"

ChangePidCodificationCommand::ChangePidCodificationCommand(
		QETProject *project,
		const PidCodification &new_codification,
		QUndoCommand *parent) :
	QUndoCommand(parent),
	m_project(project),
	m_old_codification(project->pidCodification()),
	m_new_codification(new_codification)
{
		//No "&" in this text: the Edit menu would read it as a shortcut
	setText(QObject::tr("Change the colors and the codification of the project"));
	setObsolete(m_old_codification == m_new_codification);
}

void ChangePidCodificationCommand::undo()
{
	if (m_project) {
		m_project->setPidCodification(m_old_codification);
	}
}

void ChangePidCodificationCommand::redo()
{
	if (m_project) {
		m_project->setPidCodification(m_new_codification);
	}
}
