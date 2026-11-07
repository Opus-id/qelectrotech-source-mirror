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
#include "elementtagwidget.h"

#include <autoNum/elementautonumschemecommand.h>
#include <diagram.h>
#include <pidcodification.h>
#include <pidtag.h>
#include <qetgraphicsitem/element.h>
#include <qetproject.h>
#include <undocommand/changeelementinformationcommand.h>

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>

/**
	@brief ElementTagWidget::ElementTagWidget
	@param element : the element whose tag is made
	@param parent
*/
ElementTagWidget::ElementTagWidget(Element *element, QWidget *parent) :
	AbstractElementPropertiesEditorWidget(parent)
{
	auto *layout = new QFormLayout(this);

	m_unit = new QComboBox(this);
	m_unit->setObjectName(QStringLiteral("unit"));
	m_unit->setEditable(true);
	m_unit->setInsertPolicy(QComboBox::NoInsert);
	m_unit->setToolTip(tr("The functional unit the element belongs to, picked in the list of the project or typed"));
	layout->addRow(tr("Functional unit:"), m_unit);

	m_identifier = new QLabel(this);
	m_identifier->setToolTip(tr("The identifier of the type of the element, given by its symbol"));
	layout->addRow(tr("Identifier:"), m_identifier);

	const auto *digits = new QRegularExpressionValidator(
				QRegularExpression(QStringLiteral("[0-9]*")), this);
	m_train = new QLineEdit(this);
	m_train->setObjectName(QStringLiteral("train"));
	m_train->setValidator(digits);
	m_train->setToolTip(tr("The number of the train, 0 when there is none"));
	layout->addRow(tr("Train:"), m_train);

	m_order = new QLineEdit(this);
	m_order->setObjectName(QStringLiteral("order"));
	m_order->setValidator(digits);
	layout->addRow(tr("Order:"), m_order);

	m_label = new QLabel(this);
	m_label->setObjectName(QStringLiteral("label"));
	m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addRow(tr("Label:"), m_label);

	m_duplicate = new QLabel(this);
	m_duplicate->setObjectName(QStringLiteral("duplicate"));
	m_duplicate->setWordWrap(true);
	m_duplicate->setStyleSheet(QStringLiteral("color: #c05000"));
	layout->addRow(m_duplicate);

	connect(m_unit, &QComboBox::activated, this, &ElementTagWidget::applyIfLive);
	connect(m_unit->lineEdit(), &QLineEdit::editingFinished, this, &ElementTagWidget::applyIfLive);
	connect(m_train, &QLineEdit::editingFinished, this, &ElementTagWidget::applyIfLive);
	connect(m_order, &QLineEdit::editingFinished, this, &ElementTagWidget::applyIfLive);
		//The label follows what is typed, before it is applied
	for (QLineEdit *field : {m_unit->lineEdit(), m_train, m_order}) {
		connect(field, &QLineEdit::textEdited, this, [this]() { showLabel(edited()); });
	}

	setElement(element);
}

/**
	@brief ElementTagWidget::isWanted
	See the declaration
*/
bool ElementTagWidget::isWanted(const Element *element)
{
	if (!element) {
		return false;
	}
	const QString family = element->elementData().m_pid_family;
	return family == QLatin1String("equipment")
			|| family == QLatin1String("valve")
			|| family == QLatin1String("instrument");
}

/**
	@brief ElementTagWidget::setElement
	@param element
*/
void ElementTagWidget::setElement(Element *element)
{
	if (m_element) {
		disconnect(m_element, nullptr, this, nullptr);
	}
	if (m_project) {
		disconnect(m_project, nullptr, this, nullptr);
	}
	m_element = element;
	m_project = (element && element->diagram()) ? element->diagram()->project() : nullptr;
	if (m_element) {
			//Undone, redone, changed in the Informations tab, or a new type
		connect(m_element, &Element::elementInfoChange, this, &ElementTagWidget::updateUi);
		connect(m_element, &Element::symbolChanged, this, &ElementTagWidget::updateUi);
	}
	if (m_project) {
		connect(m_project, &QETProject::pidCodificationChanged, this, [this]() {
			fillUnits();
			updateUi();
		});
	}
	fillUnits();
	updateUi();
}

/**
	@brief ElementTagWidget::fillUnits
	The functional units of the project, and for a valve its fluids too,
	whose code may stand first in its tag (an injection of a reagent)
*/
void ElementTagWidget::fillUnits()
{
	const QSignalBlocker blocker(m_unit);
	const QString typed = m_unit->currentText();
	m_unit->clear();
	m_unit->addItem(QString(), QString());
	if (!m_project || !m_element) {
		return;
	}
	const PidCodification &codification = m_project->pidCodification();
	for (const PidCodification::Entry &unit : codification.functional_units) {
		m_unit->addItem(unit.name.isEmpty() ? unit.code
											: QStringLiteral("%1 – %2").arg(unit.code, unit.name),
						unit.code);
	}
	if (m_element->elementData().m_pid_family == QLatin1String("valve"))
	{
		for (const PidCodification::Entry &fluid : codification.fluids) {
			m_unit->addItem(fluid.name.isEmpty() ? fluid.code
												 : QStringLiteral("%1 – %2").arg(fluid.code, fluid.name),
							fluid.code);
		}
	}
	m_unit->setEditText(typed);
}

/**
	@brief ElementTagWidget::unitCode
	@return the code of the unit picked, or the text typed
*/
QString ElementTagWidget::unitCode() const
{
	const QString text = m_unit->currentText().trimmed();
	const int index = m_unit->findText(text);
	if (index > 0) {
		return m_unit->itemData(index).toString();
	}
	return text;
}

/**
	@brief ElementTagWidget::edited
	See the declaration
*/
DiagramContext ElementTagWidget::edited() const
{
	if (!m_element) {
		return DiagramContext();
	}
	DiagramContext information = m_element->elementInformations();
	information.addValue(PidTag::unitKey(), unitCode());
	information.addValue(PidTag::trainKey(), m_train->text().trimmed());
	information.addValue(PidTag::orderKey(), m_order->text().trimmed());
	return m_element->withPidLabel(information);
}

/**
	@brief ElementTagWidget::showLabel
	Show the label @p information carries, and who else has it
*/
void ElementTagWidget::showLabel(const DiagramContext &information)
{
	const QString label = information.value(QStringLiteral("label")).toString();
	if (!m_element || m_element->pidTagModel().isEmpty()) {
		m_label->setText(tr("The project has no model of tag for this family: "
							"the label is typed in the Informations tab."));
		m_label->setWordWrap(true);
	} else {
		m_label->setWordWrap(false);
		m_label->setText(label.isEmpty() ? tr("(none)") : QString(label).replace(QLatin1Char('\n'), QLatin1String(" / ")));
	}

	m_duplicate->clear();
	if (m_project && m_element && !label.isEmpty()
		&& ElementAutoNumSchemeCommand::labelsHeldBesides(m_project, m_element).contains(label)) {
		m_duplicate->setText(tr("Another element of the project already has this label."));
	}
}

/**
	@brief ElementTagWidget::updateUi
	Show the fields of the element
*/
void ElementTagWidget::updateUi()
{
	if (!m_element) {
		return;
	}
	const DiagramContext information = m_element->elementInformations();
	{
		const QSignalBlocker b1(m_unit), b2(m_train), b3(m_order);
		const QString unit = information.value(PidTag::unitKey()).toString();
		int index = -1;
		for (int i = 1 ; i < m_unit->count() ; ++i) {
			if (m_unit->itemData(i).toString() == unit) index = i;
		}
		if (index > 0) {
			m_unit->setCurrentIndex(index);
		} else {
			m_unit->setEditText(unit);
		}
		m_train->setText(information.value(PidTag::trainKey()).toString());
		m_order->setText(information.value(PidTag::orderKey()).toString());
	}
	const QString identifier = m_element->getPrefix();
	m_identifier->setText(identifier.isEmpty() ? tr("(none: the symbol gives none)") : identifier);
	showLabel(information);
}

/**
	@brief ElementTagWidget::associatedUndo
	@return the change of the information of the element, or nullptr when
	the fields are those it has
*/
QUndoCommand *ElementTagWidget::associatedUndo() const
{
	if (!m_element) {
		return nullptr;
	}
	const DiagramContext old_information = m_element->elementInformations();
	DiagramContext new_information = edited();
		//Fields left empty are not written, as a symbol that has no tag
	for (const QString &key : {PidTag::unitKey(), PidTag::trainKey(), PidTag::orderKey()}) {
		if (new_information.value(key).toString().isEmpty() && !old_information.contains(key)) {
			new_information.remove(key);
		}
	}
	if (new_information == old_information) {
		return nullptr;
	}
	auto *undo = new ChangeElementInformationCommand(m_element, old_information, new_information);
	undo->setText(tr("Change the tag of an element"));
	return undo;
}

/**
	@brief ElementTagWidget::apply
	Give the element the fields of the tab, with undo
*/
void ElementTagWidget::apply()
{
	if (!m_element || !m_element->diagram()) {
		return;
	}
	if (QUndoCommand *undo = associatedUndo()) {
		m_element->diagram()->undoStack().push(undo);
	}
}

void ElementTagWidget::applyIfLive()
{
	if (m_live_edit) {
		apply();
	}
}

bool ElementTagWidget::setLiveEdit(bool live_edit)
{
	m_live_edit = live_edit;
	return true;
}

QString ElementTagWidget::title() const
{
	return tr("Tag");
}
