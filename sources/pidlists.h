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
#ifndef PID_LISTS_H
#define PID_LISTS_H

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

/**
	@brief The PidLists namespace
	The lists of a P&ID, as views of the project database beside the public
	ones (projectDataBase::publicViews()): what a spreadsheet, a script or
	another program reads the equipment of a project from. Their names and
	their columns are meant to stay: a column is only ever added, at the end.

	- equipmentView(): the placed symbols of the families equipment, valve
	  and instrument that have a label (their tag, PidTag), one row each.
	  uuid is the placed symbol's and the key of any exchange, never the
	  label; unit, train and number are the fields of the tag as typed,
	  unit_name the name the codification of the project gives the unit,
	  identifier the letters of an instrument, else the prefix of its symbol;
	  family and role are those of the symbol's definition; type is the name
	  of its file ("pump_centrifugal"); description its information of that
	  name; then its folio and its place on it, as qet_devices has them;
	  last, class, the kind of object its definition says it is ("pump",
	  ElementData::m_pid_class), whatever its file is called.
	- equipmentDataView(): what each of these symbols holds besides, one row
	  per value (uuid, name, value): its information whose name starts with
	  "pid_" (a power, a voltage: data the project chose to keep), the
	  fields of the tag left out, and its own choices, named "option_" then
	  the name of the option. Only what is filled.
	- codesTable(): the codes of the project's codification with their
	  names (list, code, name), list being "unit", "fluid", "material" or
	  "letters" as in a file of codes (PidCodification::fromCsv()).

	All three are TEMP, as the potential check's (projectDataBase::
	refreshPotentialMembersOf()): a .qetdb that holds a view outside the
	fixed list is refused by every QElectroTech that does not know it
	(QetDbFile), so nothing here is ever saved in a file. They are made on
	request: a query that names one (namedIn()) has them made first
	(refresh()). Every row comes from a table filled when the project is
	opened, so a folio not built gives the same rows.
*/
namespace PidLists
{
	QString equipmentView();
	QString equipmentDataView();
	QString codesTable();

		/// The columns of equipmentView() and of equipmentDataView(), in order
	QStringList equipmentColumns();
	QStringList equipmentDataColumns();

		/// @p sql names one of the three: refresh() is to run before it
	bool namedIn(const QString &sql);

		/// Make the table and the two views on @p db, a project database
		/// (that of an open project, or a .qetdb read itself), and fill the
		/// table from the codification @p db holds. Made again each time,
		/// so an old definition never outlives a change.
		/// @return false if one of them could not be made
	bool refresh(QSqlDatabase db);
}

#endif
