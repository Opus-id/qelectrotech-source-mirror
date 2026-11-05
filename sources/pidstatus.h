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
#ifndef PIDSTATUS_H
#define PIDSTATUS_H

#include <QList>
#include <QPair>
#include <QString>

class Element;
class QGraphicsItem;
class QUndoCommand;

/**
	The status of works of what is drawn on a P&ID: to be built, the default,
	which has no word; existing and kept; to be demolished. A pipe keeps it in
	PipeProperties::status, a placed symbol of a family of objects among the
	options chosen on it (Element::optionChoices()). The project sets the
	colour of each status (PidCodification::status_colors).
*/
namespace PidStatus
{
		/// The name under which a placed symbol keeps its status
	QString option();
		/// Each status (none for the default) with its name, in order
	QList<QPair<QString, QString>> statuses();
		/// True for a pipe, and for a symbol of a family of objects
	bool applies(const QGraphicsItem *item);
		/// The items among @p items which can have a status take @p status,
		/// in one undo step; nullptr when that changes nothing
	QUndoCommand *change(const QList<QGraphicsItem *> &items, const QString &status);
}

#endif // PIDSTATUS_H
