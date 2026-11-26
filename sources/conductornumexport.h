/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech. If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef ConductorNumExport_H
#define ConductorNumExport_H

#include <QHash>

class QETProject;
class QWidget;

/**
	@brief The ConductorNumExport class
	A class to export the num of conductors into a csv file.
*/
class ConductorNumExport
{
	public:
	ConductorNumExport(QETProject *project, QWidget *parent = nullptr);
	bool toCsv();
	QString wiresNum() const;
		/// How many lines of another medium than electrical (pipes, signal
		/// lines) were left out: they carry no wire number
	int otherMediaCount() const {return m_other_media;}

	private:
	void fillHash();

	private:
	QETProject *m_project = nullptr;
	QHash <QString, int> m_hash;
	int m_other_media = 0;
	QWidget *m_parent_widget = nullptr;
};

#endif // ConductorNumExport_H
