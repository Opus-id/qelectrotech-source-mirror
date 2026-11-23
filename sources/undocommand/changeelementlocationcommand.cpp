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
#include "changeelementlocationcommand.h"

#include "../qetgraphicsitem/dynamicelementtextitem.h"
#include "../qetgraphicsitem/element.h"
#include "addelementtextcommand.h"
#include "fillunsizedpipescommand.h"

ChangeElementLocationCommand::ChangeElementLocationCommand(
		Element *element,
		const ElementsLocation &new_location,
		QUndoCommand *parent) :
	QUndoCommand(parent),
	m_element(element),
	m_old_location(element->location()),
	m_new_location(new_location),
	m_old_prefix(element->getPrefix()),
	m_old_nozzle_diameters(element->nozzleDiameters())
{
	setText(QObject::tr("Change the type of an element"));
		//A size the new symbol gives a nozzle of equipment goes to the
		//pipes without a size of its line, in the same step (step 5l-1aa)
	new FillUnsizedPipesCommand(element, this);
		//The texts the new symbol shows from an information the element
		//shows nowhere yet (the comment of an arrow of inlet, once a free
		//end), added after the symbol and taken away before it (step 8b)
	for (DynamicElementTextItem *text : element->textsOfNewType(new_location)) {
		new AddElementTextCommand(element, text, this);
	}
}

void ChangeElementLocationCommand::undo()
{
		//The pipes filled first, then the symbol
	QUndoCommand::undo();
	if (m_element) {
		m_element->changeLocation(m_old_location);
		m_element->setNozzleDiameters(m_old_nozzle_diameters);
		if (m_element->getPrefix() != m_old_prefix) {
			m_element->setPrefix(m_old_prefix);
			m_element->updatePidLabel();
		}
	}
}

void ChangeElementLocationCommand::redo()
{
	if (m_element) {
		m_element->changeLocation(m_new_location);
	}
		//Then the pipes, from the sizes of the new symbol
	QUndoCommand::redo();
}
