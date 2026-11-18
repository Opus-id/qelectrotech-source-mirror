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
#include "signalpropertieswidget.h"

#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../conductorproperties.h"
#include "../diagram.h"
#include "../pidsection.h"
#include "../pidstatus.h"
#include "../qetgraphicsitem/conductor.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

/**
	@brief SignalPropertiesWidget::SignalPropertiesWidget
	@param lines : the signal lines to edit
	@param parent
*/
SignalPropertiesWidget::SignalPropertiesWidget(const QList<Conductor *> &lines, QWidget *parent) :
	PropertiesEditorWidget(parent)
{
	m_live_edit = true;
	buildUi();
	setLines(lines);
}

/**
	@brief SignalPropertiesWidget::setLines
	Edit @p lines instead
	@param lines
*/
void SignalPropertiesWidget::setLines(const QList<Conductor *> &lines)
{
	for (const QPointer<Conductor> &line : std::as_const(m_lines)) {
		if (line) {
			disconnect(line, &Conductor::propertiesChange, this, nullptr);
		}
	}
	m_lines.clear();
	for (Conductor *line : lines) {
		if (line) {
			m_lines << line;
				//Undone, redone or changed elsewhere: the field follows
			connect(line, &Conductor::propertiesChange, this, [this]() { updateUi(); });
		}
	}
	updateUi();
}

/**
	@brief SignalPropertiesWidget::lines
	@return the signal lines edited
*/
QList<Conductor *> SignalPropertiesWidget::lines() const
{
	QList<Conductor *> list;
	for (const QPointer<Conductor> &line : m_lines) {
		if (line) list << line;
	}
	return list;
}

/**
	@brief SignalPropertiesWidget::buildUi
	One section, with a title never folded, as the page of a pipe
*/
void SignalPropertiesWidget::buildUi()
{
	auto *layout = new QFormLayout(this);
	m_count = new QLabel(this);
	m_count->setObjectName(QStringLiteral("count"));
	layout->addRow(m_count);

	layout->addRow(PidSection::title(tr("Signal line", "section of the page of a signal line"), this));
	m_type = new QComboBox(this);
	m_type->setObjectName(QStringLiteral("signal_type"));
	const QList<SignalProperties::SignalType> types = SignalProperties::types();
	for (SignalProperties::SignalType type : types) {
		m_type->addItem(SignalProperties::typeName(type), int(type));
	}
	layout->addRow(tr("Type of signal:"), m_type);
	connect(m_type, &QComboBox::activated, this, [this](int index) {
		const auto type = SignalProperties::SignalType(m_type->itemData(index).toInt());
		applyChange([type](SignalProperties &s) { s.signal_type = type; });
	});

		//The status of works, as on the page of a pipe
	m_status = new QComboBox(this);
	m_status->setObjectName(QStringLiteral("status"));
	for (const auto &status : PidStatus::statuses()) {
		m_status->addItem(status.second, status.first);
	}
	layout->addRow(tr("Status of works:"), m_status);
	connect(m_status, &QComboBox::activated, this, [this](int index) {
		const QString status = m_status->itemData(index).toString();
		applyChange([status](SignalProperties &s) { s.status = status; });
	});
}

/**
	@brief SignalPropertiesWidget::applyChange
	Do @p change to the data of every signal line edited, in one undo step,
	if it changes anything
	@param change
*/
void SignalPropertiesWidget::applyChange(const std::function<void(SignalProperties &)> &change)
{
	auto *undo = new QUndoCommand(tr("Change the properties of a signal line"));
	Diagram *diagram = nullptr;
	const QList<Conductor *> list = lines();
	for (Conductor *line : list)
	{
		ConductorProperties properties = line->properties();
		change(properties.m_signal);
		if (properties == line->properties()) {
			continue;
		}
		QVariant old_value, new_value;
		old_value.setValue(line->properties());
		new_value.setValue(properties);
		new QPropertyUndoCommand(line, "properties", old_value, new_value, undo);
		if (!diagram) {
			diagram = line->diagram();
		}
	}
	if (!undo->childCount() || !diagram) {
		delete undo;
		return;
	}
	diagram->undoStack().push(undo);
}

/**
	@brief SignalPropertiesWidget::updateUi
	Show the data of the signal lines edited
*/
void SignalPropertiesWidget::updateUi()
{
	const QList<Conductor *> list = lines();
	m_count->setText(tr("%n signal line(s) selected", "", int(list.size())));
	m_count->setVisible(list.size() > 1);
	setEnabled(!list.isEmpty());
	if (list.isEmpty()) {
		return;
	}

		//The type they all have; none shown when they differ
	const QSignalBlocker blocker(m_type);
	const SignalProperties::SignalType type = list.first()->properties().m_signal.signal_type;
	bool same_type = true;
	for (Conductor *line : list) {
		if (line->properties().m_signal.signal_type != type) {
			same_type = false;
		}
	}
	m_type->setCurrentIndex(same_type ? m_type->findData(int(type)) : -1);

		//The same for the status of works; one this version does not know
		//shows none
	const QSignalBlocker status_blocker(m_status);
	const QString status = list.first()->properties().m_signal.status;
	bool same_status = true;
	for (Conductor *line : list) {
		if (line->properties().m_signal.status != status) {
			same_status = false;
		}
	}
	m_status->setCurrentIndex(-1);
	for (int i = 0 ; same_status && i < m_status->count() ; ++i) {
		if (m_status->itemData(i).toString() == status) {
			m_status->setCurrentIndex(i);
		}
	}
}

/**
	@brief SignalPropertiesWidget::setLiveEdit
	The data of a signal line are always applied at once, in the dock as in
	the dialog of editInDialog()
	@return true
*/
bool SignalPropertiesWidget::setLiveEdit(bool live_edit)
{
	Q_UNUSED(live_edit)
	m_live_edit = true;
	return true;
}

/**
	@brief SignalPropertiesWidget::title
	@return the title of this editor
*/
QString SignalPropertiesWidget::title() const
{
	return tr("Signal line");
}

/**
	@brief SignalPropertiesWidget::editInDialog
	Edit @p line in a dialog of its own: what a double click on a signal
	line opens. Each change is applied at once, so there is only a button to
	close it, and undo to go back.
	@param line
	@param parent
*/
void SignalPropertiesWidget::editInDialog(Conductor *line, QWidget *parent)
{
	QDialog dialog(parent);
	auto *widget = new SignalPropertiesWidget({line}, &dialog);
	dialog.setWindowTitle(widget->title());
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto *layout = new QVBoxLayout(&dialog);
	layout->addWidget(widget);
	layout->addWidget(buttons);
	dialog.exec();
}
