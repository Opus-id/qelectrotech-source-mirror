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
#include "resetconductortextscommand.h"

#include <qetgraphicsitem/conductor.h>
#include <qetgraphicsitem/conductortextitem.h>

#include <QObject>

namespace {
	bool movedByHand(const Conductor *conductor)
	{
		const ConductorTextItem *text = conductor ? conductor->textItem() : nullptr;
		return text && (text->wasMovedByUser() || text->wasRotatedByUser());
	}
}

ResetConductorTextsCommand::ResetConductorTextsCommand(const QList<Conductor *> &conductors,
													   QUndoCommand *parent) :
	QUndoCommand(parent)
{
	setText(QObject::tr("Put the texts back in their place", "undo caption"));
	for (Conductor *conductor : conductors) {
		if (!movedByHand(conductor)) {
			continue;
		}
		const ConductorTextItem *item = conductor->textItem();
		Text text;
		text.conductor = conductor;
		text.pos = item->pos();
		text.rotation = item->rotation();
		text.moved = item->wasMovedByUser();
		text.rotated = item->wasRotatedByUser();
		m_texts << text;
	}
}

bool ResetConductorTextsCommand::canReset(const QList<Conductor *> &conductors)
{
	for (const Conductor *conductor : conductors) {
		if (movedByHand(conductor)) {
			return true;
		}
	}
	return false;
}

void ResetConductorTextsCommand::redo()
{
	for (const Text &text : std::as_const(m_texts)) {
		if (!text.conductor) {
			continue;
		}
		ConductorTextItem *item = text.conductor->textItem();
		item->forceRotateByUser(false);
		item->forceMovedByUser(false);
			//Placed by its conductor, whichever came first
		text.conductor->calculateTextItemPosition();
	}
}

void ResetConductorTextsCommand::undo()
{
	for (const Text &text : std::as_const(m_texts)) {
		if (!text.conductor) {
			continue;
		}
		ConductorTextItem *item = text.conductor->textItem();
		item->setRotation(text.rotation);
		item->setPos(text.pos);
		item->forceRotateByUser(text.rotated);
		item->forceMovedByUser(text.moved);
		text.conductor->updateLeader();
	}
}
