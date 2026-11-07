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
#ifndef CHANGEPIDCODIFICATIONCOMMAND_H
#define CHANGEPIDCODIFICATIONCOMMAND_H

#include "../pidcodification.h"

#include "../qetgraphicsitem/dynamicelementtextitem.h"

#include <QList>
#include <QPointer>
#include <QUndoCommand>

class QETProject;

/**
	@brief The ChangePidCodificationCommand class
	Give a project another P&ID codification, its codes, its colours and the
	sizes of its texts, and give the former one back on undo. Everything that
	changes the codification of an open project goes through it: a change
	kept out of the undo stack would be lost when an earlier one is undone.
	A label placed follows the size of its family as long as it has the
	former one: a size set by hand stays.
	Pushed with the codification the project already has, it adds no step.
*/
class ChangePidCodificationCommand : public QUndoCommand
{
	public:
		ChangePidCodificationCommand(QETProject *project,
									 const PidCodification &new_codification,
									 QUndoCommand *parent = nullptr);
		void undo() override;
		void redo() override;

	private:
		struct LabelSize
		{
			QPointer<DynamicElementTextItem> text;
			int old_size;
			int new_size;
		};

		void findLabels();
		static void setSize(DynamicElementTextItem *text, int size);

		QPointer<QETProject> m_project;
		PidCodification m_old_codification, m_new_codification;
		QList<LabelSize> m_labels;
};

#endif // CHANGEPIDCODIFICATIONCOMMAND_H
