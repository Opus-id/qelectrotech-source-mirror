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

#include "backupdialog.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {
const QString AskKey = QStringLiteral("projects/ask_backup_on_open");
}

/**
	@brief BackupDialog::asksOnOpen
	@return whether opening a project asks for a backup copy; true when
	the setting was never written, as before the setting existed
*/
bool BackupDialog::asksOnOpen()
{
	return QSettings().value(AskKey, true).toBool();
}

/**
	@brief BackupDialog::setAsksOnOpen
	@param ask whether opening a project asks for a backup copy
*/
void BackupDialog::setAsksOnOpen(bool ask)
{
	QSettings().setValue(AskKey, ask);
}

/**
	@brief BackupDialog::BackupDialog
	@param parent parent widget
*/
BackupDialog::BackupDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Create a backup copy?", "window title"));
	setMinimumWidth(450);

	auto main_layout = new QVBoxLayout(this);
	main_layout->setSizeConstraint(QLayout::SetFixedSize);

	auto label = new QLabel(
		tr("Would you like to create a backup copy?",
		   "dialog message"));
	label->setWordWrap(true);
	main_layout->addWidget(label);

	main_layout->addStretch();

	m_dont_ask_cb = new QCheckBox(tr("Do not ask again", "checkbox"));
	m_dont_ask_cb->setToolTip(
		tr("It can be asked again in Settings > General, "
		   "\"Ask for a backup copy when a project is opened\".",
		   "checkbox tooltip"));

	auto button_layout = new QHBoxLayout();
	button_layout->addWidget(m_dont_ask_cb);
	button_layout->addStretch();

	auto yes_button = new QPushButton(tr("Yes", "yes button"));
	auto no_button = new QPushButton(tr("No", "no button"));

	button_layout->addWidget(yes_button);
	button_layout->addWidget(no_button);
	main_layout->addLayout(button_layout);

	connect(yes_button, &QPushButton::clicked, this, &QDialog::accept);
	connect(no_button, &QPushButton::clicked, this, &QDialog::reject);
}

/**
	@brief BackupDialog::~BackupDialog
*/
BackupDialog::~BackupDialog() = default;

/**
	@brief BackupDialog::done
	"Do not ask again" holds whatever the answer: this copy is made or
	not as asked, the next projects are opened without asking.
	@param result
*/
void BackupDialog::done(int result)
{
	if (m_dont_ask_cb->isChecked())
		setAsksOnOpen(false);
	QDialog::done(result);
}
