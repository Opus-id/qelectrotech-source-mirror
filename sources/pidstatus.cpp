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
#include "pidstatus.h"

#include "QPropertyUndoCommand/qpropertyundocommand.h"
#include "conductorproperties.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "undocommand/changeelementoptionscommand.h"

#include <QCoreApplication>
#include <QUndoCommand>

QString PidStatus::option()
{
	return QStringLiteral("pid_status");
}

QList<QPair<QString, QString>> PidStatus::statuses()
{
	return {
		{QString(), QCoreApplication::translate("PidStatus", "To be built")},
		{QStringLiteral("existing"), QCoreApplication::translate("PidStatus", "Existing, kept")},
		{QStringLiteral("demolished"), QCoreApplication::translate("PidStatus", "To be demolished")}
	};
}

bool PidStatus::applies(const QGraphicsItem *item)
{
	if (const auto *conductor = qgraphicsitem_cast<const Conductor *>(item)) {
		return conductor->medium() == TerminalData::Pipe;
	}
	if (const auto *element = qgraphicsitem_cast<const Element *>(item)) {
		return !element->elementData().m_pid_family.isEmpty();
	}
	return false;
}

QUndoCommand *PidStatus::change(const QList<QGraphicsItem *> &items, const QString &status)
{
	auto *undo = new QUndoCommand(QCoreApplication::translate("PidStatus", "Change the status of works"));
	for (QGraphicsItem *item : items)
	{
		if (!applies(item)) {
			continue;
		}
		if (auto *conductor = qgraphicsitem_cast<Conductor *>(item))
		{
			ConductorProperties properties = conductor->properties();
			properties.m_pipe.status = status;
			if (properties == conductor->properties()) {
				continue;
			}
			QVariant old_value, new_value;
			old_value.setValue(conductor->properties());
			new_value.setValue(properties);
			new QPropertyUndoCommand(conductor, "properties", old_value, new_value, undo);
		}
		else if (auto *element = qgraphicsitem_cast<Element *>(item))
		{
			ElementOptions::Choices choices = element->optionChoices();
			if (status.isEmpty()) {
				choices.remove(option());
			} else {
				choices.insert(option(), status);
			}
			if (choices != element->optionChoices()) {
				new ChangeElementOptionsCommand(element, choices, undo);
			}
		}
	}
	if (!undo->childCount()) {
		delete undo;
		return nullptr;
	}
	return undo;
}
