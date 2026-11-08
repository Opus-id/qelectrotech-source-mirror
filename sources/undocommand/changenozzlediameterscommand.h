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

#include "../pipeconnection.h"
#include "../pipediameter.h"

#include <QMap>
#include <QPointer>
#include <QString>
#include <QUndoCommand>

class Element;

/**
	@brief The ChangeNozzleDiametersCommand class
	Give the nozzles of a placed element other diameters
	(Element::setNozzleDiameters()) and other connections
	(Element::setNozzleConnections()), and give them back on undo.
*/
class ChangeNozzleDiametersCommand : public QUndoCommand
{
	public:
			/// Other diameters; the connections stay
		ChangeNozzleDiametersCommand(Element *element,
									 const NozzleDiameters &new_diameters,
									 QUndoCommand *parent = nullptr);
		ChangeNozzleDiametersCommand(Element *element,
									 const NozzleDiameters &new_diameters,
									 const NozzleConnections &new_connections,
									 QUndoCommand *parent = nullptr);
		void undo() override;
		void redo() override;

	private:
		QPointer<Element> m_element;
		NozzleDiameters m_old_diameters, m_new_diameters;
		NozzleConnections m_old_connections, m_new_connections;
};

#endif // CHANGENOZZLEDIAMETERSCOMMAND_H
