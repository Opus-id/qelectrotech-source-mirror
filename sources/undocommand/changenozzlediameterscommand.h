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
#ifndef CHANGENOZZLEDIAMETERSCOMMAND_H
#define CHANGENOZZLEDIAMETERSCOMMAND_H

#include <QMap>
#include <QPointer>
#include <QString>
#include <QUndoCommand>

class Element;

/**
	@brief The ChangeNozzleDiametersCommand class
	Give the nozzles of a placed element other nominal diameters
	(Element::setNozzleDiameters()), and give them back on undo.
*/
class ChangeNozzleDiametersCommand : public QUndoCommand
{
	public:
		ChangeNozzleDiametersCommand(Element *element,
									 const QMap<QString, int> &new_diameters,
									 QUndoCommand *parent = nullptr);
		void undo() override;
		void redo() override;

	private:
		QPointer<Element> m_element;
		QMap<QString, int> m_old_diameters, m_new_diameters;
};

#endif // CHANGENOZZLEDIAMETERSCOMMAND_H
