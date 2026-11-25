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

/*
	BackupDialog -- "Create a backup copy?" when a project is opened.

	An installation that never saw the setting must still be asked, as
	before it existed. "Do not ask again" turns the setting off whatever
	the answer, and only when it is ticked: closing the dialog untouched
	writes nothing.

	This test owns its own QSettings scope, so it cannot read or write the
	real configuration of whoever runs it.
*/

#include "ui/backupdialog.h"

#include <QtTest>
#include <QCheckBox>
#include <QSettings>

class TstBackupDialog : public QObject
{
		Q_OBJECT

	private slots:
		void initTestCase();
		void init();

		void asksWhenNeverSet();
		void settingIsKept();
		void untouchedDialogWritesNothing();
		void dontAskAgainOnYes();
		void dontAskAgainOnNo();
};

namespace {
const QString AskKey = QStringLiteral("projects/ask_backup_on_open");
}

void TstBackupDialog::initTestCase()
{
	QCoreApplication::setOrganizationName(
				QStringLiteral("QElectroTech-tst_backupdialog"));
	QCoreApplication::setApplicationName(
				QStringLiteral("tst_backupdialog"));
	QSettings().clear();
}

void TstBackupDialog::init()
{
	QSettings settings;
	settings.remove(AskKey);
	settings.sync();
}

void TstBackupDialog::asksWhenNeverSet()
{
	QVERIFY2(BackupDialog::asksOnOpen(),
		 "an installation that never saw the setting must still be asked");
}

void TstBackupDialog::settingIsKept()
{
	BackupDialog::setAsksOnOpen(false);
	QVERIFY(!BackupDialog::asksOnOpen());
	QCOMPARE(QSettings().value(AskKey).toBool(), false);

	BackupDialog::setAsksOnOpen(true);
	QVERIFY(BackupDialog::asksOnOpen());
}

void TstBackupDialog::untouchedDialogWritesNothing()
{
	{
		BackupDialog dialog;
		dialog.accept();
	}
	{
		BackupDialog dialog;
		dialog.reject();
	}
	QVERIFY(BackupDialog::asksOnOpen());
	QVERIFY2(!QSettings().contains(AskKey),
		 "closing the dialog untouched must write nothing");
}

void TstBackupDialog::dontAskAgainOnYes()
{
	BackupDialog dialog;
	auto box = dialog.findChild<QCheckBox *>();
	QVERIFY(box);
	box->setChecked(true);
	dialog.accept();
	QCOMPARE(dialog.result(), int(QDialog::Accepted));
	QVERIFY(!BackupDialog::asksOnOpen());
}

void TstBackupDialog::dontAskAgainOnNo()
{
	BackupDialog dialog;
	auto box = dialog.findChild<QCheckBox *>();
	QVERIFY(box);
	box->setChecked(true);
	dialog.reject();
	QCOMPARE(dialog.result(), int(QDialog::Rejected));
	QVERIFY(!BackupDialog::asksOnOpen());
}

QTEST_MAIN(TstBackupDialog)
#include "tst_backupdialog.moc"
