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

#ifndef BACKUPDIALOG_H
#define BACKUPDIALOG_H

#include <QDialog>

class QCheckBox;

/**
	@brief The BackupDialog class
	Asks, when a project is opened, whether a dated copy of its file should
	be made beside it. Whether it is asked at all is a setting
	(Settings > General > Projects), on by default; the dialog's own
	"Do not ask again" box turns it off.
*/
class BackupDialog : public QDialog
{
	Q_OBJECT
	public:
		explicit BackupDialog(QWidget *parent = nullptr);
		~BackupDialog() override;

			/// Whether opening a project asks for a backup copy
		static bool asksOnOpen();
		static void setAsksOnOpen(bool ask);

		void done(int result) override;

	private:
		QCheckBox *m_dont_ask_cb = nullptr;
};

#endif // BACKUPDIALOG_H
