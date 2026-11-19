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

#include <QList>
#include <QPointer>
#include <QUndoCommand>

class Conductor;
class Element;
class Terminal;

/**
	@brief The ChangeElementOptionsCommand class
	Give the options of the symbol of a placed element other values, such as
	another motor for a pump (see ElementOptions, Element::setOptionChoices()),
	and give them back on undo.
	A wire on a terminal the new values hide goes over to the terminal they
	show in its place (Terminal::replacement()), when that one has room for
	it; otherwise it stays where it is. Undone in the same step.
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
		void moveWires();

			/// A wire moved from one terminal to another
		struct WireMove {
			QPointer<Conductor> conductor;
			QPointer<Terminal> from, to;
		};

		QPointer<Element> m_element;
		ElementOptions::Choices m_old_choices, m_new_choices;
			/// The wires moved by the first redo, moved the same way again
			/// by the next ones
		QList<WireMove> m_moves;
		bool m_moves_known = false;
};

#endif // CHANGEELEMENTOPTIONSCOMMAND_H
