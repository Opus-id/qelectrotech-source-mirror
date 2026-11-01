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
#ifndef CHANGEELEMENTOPTIONSCOMMAND_H
#define CHANGEELEMENTOPTIONSCOMMAND_H

#include "../properties/elementoptions.h"

#include <QPointer>
#include <QUndoCommand>

class Element;

/**
	@brief The ChangeElementOptionsCommand class
	Give the options of the symbol of a placed element other values, such as
	another motor for a pump (see ElementOptions, Element::setOptionChoices()),
	and give them back on undo.
*/
class ChangeElementOptionsCommand : public QUndoCommand
{
	public:
		ChangeElementOptionsCommand(Element *element,
									const ElementOptions::Choices &new_choices,
									QUndoCommand *parent = nullptr);
		void undo() override;
		void redo() override;

	private:
		QPointer<Element> m_element;
		ElementOptions::Choices m_old_choices, m_new_choices;
};

#endif // CHANGEELEMENTOPTIONSCOMMAND_H
