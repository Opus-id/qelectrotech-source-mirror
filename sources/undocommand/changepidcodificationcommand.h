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

#include "../conductorproperties.h"
#include "../diagramcontext.h"
#include "../pidcodification.h"

#include "../qetgraphicsitem/dynamicelementtextitem.h"

#include <QList>
#include <QMap>
#include <QPointer>
#include <QUndoCommand>

#include <memory>
#include <vector>

class Conductor;
class Element;
class QETProject;

/**
	@brief The ChangePidCodificationCommand class
	Give a project another P&ID codification, its codes, its colours and the
	sizes of its texts, and give the former one back on undo. Everything that
	changes the codification of an open project goes through it: a change
	kept out of the undo stack would be lost when an earlier one is undone.
	A label placed follows the size of its family as long as it has the
	former one: a size set by hand stays.
	A code given another code (CodeChanges) is given to what bears it in the
	same step: the pipe lines of a fluid or of a material; a functional unit,
	which is a plant of QElectroTech, is renamed as the window of plants and
	locations does (PlaceRename: title blocks, structure boxes, symbols, wire
	counters), the labels of the symbols following their unit. A name
	changed touches nothing: they keep the code. The lists of codes are
	saved in the natural order of their codes (PidCodification::sortCodes()).
	Pushed with the codification the project already has, it adds no step.
*/
class ChangePidCodificationCommand : public QUndoCommand
{
	public:
			/// Former code -> new code, in each list
		struct CodeChanges
		{
			QMap<QString, QString> fluids, materials, units;

			bool isEmpty() const {
				return fluids.isEmpty() && materials.isEmpty() && units.isEmpty();
			}
		};

		ChangePidCodificationCommand(QETProject *project,
									 const PidCodification &new_codification,
									 const CodeChanges &changes = CodeChanges(),
									 QUndoCommand *parent = nullptr);
		~ChangePidCodificationCommand() override;
		void undo() override;
		void redo() override;

			/// How many pipe lines bear the fluid or the material @p code, or
			/// how many symbols hold the functional unit @p code themselves
			/// (their plant). All the folios of the
			/// project are built for that. Letters of instruments: none, a
			/// help to type that no tag bears (PidCodification::instrument_letters).
		static int usersOf(QETProject *project, PidCodification::List list, const QString &code);

	private:
		struct LabelSize
		{
			QPointer<DynamicElementTextItem> text;
			int old_size;
			int new_size;
		};
		struct PipeChange
		{
			QPointer<Conductor> pipe;
			ConductorProperties old_properties, new_properties;
		};
		void findLabels();
		void findCodeUsers(const CodeChanges &changes);
		static void setSize(DynamicElementTextItem *text, int size);

		QPointer<QETProject> m_project;
		PidCodification m_old_codification, m_new_codification;
		QList<LabelSize> m_labels;
		QList<PipeChange> m_pipes;
			/// The renames of the functional units (PlaceRename::command()),
			/// done after the new codification, undone before the former one
		std::vector<std::unique_ptr<QUndoCommand>> m_renames;
};

#endif // CHANGEPIDCODIFICATIONCOMMAND_H
