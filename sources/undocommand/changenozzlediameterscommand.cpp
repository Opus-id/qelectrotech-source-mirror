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
		const QMap<QString, int> &new_diameters,
		QUndoCommand *parent) :
	QUndoCommand(parent),
	m_element(element),
	m_old_diameters(element->nozzleDiameters()),
	m_new_diameters(new_diameters)
{
	setText(QObject::tr("Change the nominal diameter of a nozzle"));
}

void ChangeNozzleDiametersCommand::undo()
{
	if (m_element) {
		m_element->setNozzleDiameters(m_old_diameters);
	}
}

void ChangeNozzleDiametersCommand::redo()
{
	if (m_element) {
		m_element->setNozzleDiameters(m_new_diameters);
	}
}
