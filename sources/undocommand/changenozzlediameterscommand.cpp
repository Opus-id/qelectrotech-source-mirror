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
#include "changenozzlediameterscommand.h"

#include "../qetgraphicsitem/element.h"

ChangeNozzleDiametersCommand::ChangeNozzleDiametersCommand(
		Element *element,
		const NozzleDiameters &new_diameters,
		QUndoCommand *parent) :
	ChangeNozzleDiametersCommand(element, new_diameters,
								 element->nozzleConnections(), parent)
{}

ChangeNozzleDiametersCommand::ChangeNozzleDiametersCommand(
		Element *element,
		const NozzleDiameters &new_diameters,
		const NozzleConnections &new_connections,
		QUndoCommand *parent) :
	ChangeNozzleDiametersCommand(element, new_diameters, new_connections,
								 element->nozzleMarks(), parent)
{}

ChangeNozzleDiametersCommand::ChangeNozzleDiametersCommand(
		Element *element,
		const NozzleDiameters &new_diameters,
		const NozzleConnections &new_connections,
		const NozzleMarks &new_marks,
		QUndoCommand *parent) :
	QUndoCommand(parent),
	m_element(element),
	m_old_diameters(element->nozzleDiameters()),
	m_new_diameters(new_diameters),
	m_old_connections(element->nozzleConnections()),
	m_new_connections(new_connections),
	m_old_marks(element->nozzleMarks()),
	m_new_marks(new_marks)
{
	if (m_new_connections != m_old_connections) {
		setText(QObject::tr("Change the connection of a nozzle"));
	} else if (m_new_marks != m_old_marks) {
		setText(QObject::tr("Change the drawing of the connection of a nozzle"));
	} else {
		setText(QObject::tr("Change the diameter of a nozzle"));
	}
}

void ChangeNozzleDiametersCommand::undo()
{
	if (m_element) {
		m_element->setNozzleMarks(m_old_marks);
		m_element->setNozzleConnections(m_old_connections);
		m_element->setNozzleDiameters(m_old_diameters);
	}
}

void ChangeNozzleDiametersCommand::redo()
{
	if (m_element) {
		m_element->setNozzleMarks(m_new_marks);
		m_element->setNozzleConnections(m_new_connections);
		m_element->setNozzleDiameters(m_new_diameters);
	}
}
