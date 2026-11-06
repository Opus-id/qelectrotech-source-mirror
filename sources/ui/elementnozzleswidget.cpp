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
#include "elementnozzleswidget.h"

#include "../diagram.h"
#include "../properties/terminaldata.h"
#include "../qetgraphicsitem/element.h"
#include "../qetgraphicsitem/terminal.h"
#include "../undocommand/changenozzlediameterscommand.h"
#include "diameterlist.h"

#include <algorithm>

#include <QFormLayout>
#include <QLineEdit>
#include <QSignalBlocker>

/**
	@brief ElementNozzlesWidget::ElementNozzlesWidget
	@param element : the element whose nozzles are edited
	@param parent
*/
ElementNozzlesWidget::ElementNozzlesWidget(Element *element, QWidget *parent) :
	AbstractElementPropertiesEditorWidget(parent)
{
	m_layout = new QFormLayout(this);
	setElement(element);
}

/**
	@brief ElementNozzlesWidget::hasNozzles
	@param element
	@return true if @p element has a named nozzle: a diameter is kept by the
	name of its nozzle
*/
bool ElementNozzlesWidget::hasNozzles(const Element *element)
{
	if (!element) {
		return false;
	}
	const QList<Terminal *> terminals = element->terminals();
	return std::any_of(terminals.cbegin(), terminals.cend(), [](const Terminal *terminal) {
		return terminal->medium() == TerminalData::Pipe && !terminal->baseName().isEmpty();
	});
}

/**
	@brief ElementNozzlesWidget::setElement
	Edit the nozzles of @p element: one field per named nozzle
	@param element
*/
void ElementNozzlesWidget::setElement(Element *element)
{
	if (m_element) {
		disconnect(m_element, &Element::nozzleDiametersChanged, this, &ElementNozzlesWidget::updateUi);
	}
	m_element = element;
	buildFields();
	updateUi();
		//Undone, redone or changed elsewhere: the fields follow
	if (m_element) {
		connect(m_element, &Element::nozzleDiametersChanged, this, &ElementNozzlesWidget::updateUi);
	}
}

/**
	@brief ElementNozzlesWidget::buildFields
	Make one field per named nozzle, in the order of the terminals
*/
void ElementNozzlesWidget::buildFields()
{
	while (m_layout->rowCount()) {
		m_layout->removeRow(0);
	}
	m_fields.clear();
	if (!m_element) {
		return;
	}

	for (const Terminal *terminal : m_element->terminals())
	{
		const QString name = terminal->baseName();
		if (terminal->medium() != TerminalData::Pipe || name.isEmpty()) {
			continue;
		}
		auto *list = new QComboBox(this);
		DiameterList::setUp(list);
		QLineEdit *edit = list->lineEdit();
		const int symbol_dn = terminal->symbolDiameter();
		const auto applied = [this]() {
			if (m_live_edit) {
				apply();
			}
		};
		connect(edit, &QLineEdit::editingFinished, this, applied);
		connect(list, &QComboBox::activated, this, applied);
		m_layout->addRow(tr("%1:", "label of the DN of a nozzle").arg(name), list);
		m_fields.append({name, symbol_dn, edit, terminal});
	}
}

/**
	@brief ElementNozzlesWidget::updateUi
	Show in each field the diameter the element gives its nozzle
*/
void ElementNozzlesWidget::updateUi()
{
	if (!m_element) {
		return;
	}
	const QMap<QString, int> diameters = m_element->nozzleDiameters();
	for (const Field &field : std::as_const(m_fields))
	{
		const QSignalBlocker blocker(field.edit);
		field.edit->setText(diameters.contains(field.name)
								? QString::number(diameters.value(field.name))
								: QString());
			//In grey, what the nozzle has without a value of its own: the DN
			//of its symbol, else that of its pipe, which it follows
		const int shown = field.terminal->nominalDiameter();
		field.edit->setPlaceholderText(shown > 0 ? QString::number(shown) : QString());
	}
}

/**
	@brief ElementNozzlesWidget::chosen
	@return the diameters of the element with those of the fields, which
	replace the ones of the same names
*/
QMap<QString, int> ElementNozzlesWidget::chosen() const
{
	if (!m_element) {
		return {};
	}
	QMap<QString, int> diameters = m_element->nozzleDiameters();
	for (const Field &field : m_fields)
	{
		const int dn = TerminalData::dnFromString(field.edit->text());
		if (dn == 0 || dn == field.symbol_dn) {
			diameters.remove(field.name);
		} else {
			diameters.insert(field.name, dn);
		}
	}
	return diameters;
}

/**
	@brief ElementNozzlesWidget::fieldFor
	@param name : the name of a nozzle
	@return its field, or nullptr
*/
QLineEdit *ElementNozzlesWidget::fieldFor(const QString &name) const
{
	for (const Field &field : m_fields) {
		if (field.name == name) return field.edit;
	}
	return nullptr;
}

/**
	@brief ElementNozzlesWidget::associatedUndo
	@return the undo command giving the element the diameters of the fields,
	or nullptr when they are those it has
*/
QUndoCommand *ElementNozzlesWidget::associatedUndo() const
{
	if (!m_element) {
		return nullptr;
	}
	const QMap<QString, int> diameters = chosen();
	if (diameters == m_element->nozzleDiameters()) {
		return nullptr;
	}
	return new ChangeNozzleDiametersCommand(m_element, diameters);
}

/**
	@brief ElementNozzlesWidget::apply
	Give the element the diameters of the fields, with undo
*/
void ElementNozzlesWidget::apply()
{
	if (!m_element || !m_element->diagram()) {
		return;
	}
	if (QUndoCommand *undo = associatedUndo()) {
		m_element->diagram()->undoStack().push(undo);
	}
}

/**
	@brief ElementNozzlesWidget::setLiveEdit
	Live, a value is applied when its field is left (apply())
	@param live_edit
	@return true: this tab can be live
*/
bool ElementNozzlesWidget::setLiveEdit(bool live_edit)
{
	m_live_edit = live_edit;
	return true;
}

/**
	@brief ElementNozzlesWidget::title
	@return the title of the tab
*/
QString ElementNozzlesWidget::title() const
{
	return tr("Nozzles");
}
