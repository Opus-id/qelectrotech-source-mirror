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
#ifndef CHANGEELEMENTLOCATIONCOMMAND_H
#define CHANGEELEMENTLOCATIONCOMMAND_H

#include "../ElementsCollection/elementslocation.h"

#include <QPointer>
#include <QUndoCommand>

class Element;

/**
	@brief The ChangeElementLocationCommand class
	Swap the symbol of a placed element for another definition of the same
	family (same size and same terminals, see Element::changeLocation()),
	and swap it back on undo. Position, information and conductors stay.
*/
class ChangeElementLocationCommand : public QUndoCommand
{
	public:
		ChangeElementLocationCommand(Element *element,
					     const ElementsLocation &new_location,
					     QUndoCommand *parent = nullptr);
		void undo() override;
		void redo() override;

	private:
		QPointer<Element> m_element;
		ElementsLocation m_old_location, m_new_location;
};

#endif // CHANGEELEMENTLOCATIONCOMMAND_H
