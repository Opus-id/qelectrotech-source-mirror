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
#include "elementoptionswidget.h"

#include "../diagram.h"
#include "../qetgraphicsitem/element.h"
#include "../undocommand/changeelementoptionscommand.h"

#include <QComboBox>
#include <QFormLayout>
#include <QSignalBlocker>

/**
	@brief ElementOptionsWidget::ElementOptionsWidget
	@param element : the element whose options are chosen
	@param parent
*/
ElementOptionsWidget::ElementOptionsWidget(Element *element, QWidget *parent) :
	AbstractElementPropertiesEditorWidget(parent)
{
	m_layout = new QFormLayout(this);
	setElement(element);
}

/**
	@brief ElementOptionsWidget::hasOptions
	@param element
	@return true if the symbol of @p element offers options
*/
bool ElementOptionsWidget::hasOptions(const Element *element)
{
	return element && !element->elementData().m_options.isEmpty();
}

/**
	@brief ElementOptionsWidget::setElement
	Edit the options of @p element: one list per option of its symbol
	@param element
*/
void ElementOptionsWidget::setElement(Element *element)
{
	if (m_element) {
		disconnect(m_element, &Element::optionChoicesChanged, this, &ElementOptionsWidget::updateUi);
	}
	m_element = element;
	buildLists();
	updateUi();
		//Undone, redone or changed elsewhere: the lists follow
	if (m_element) {
		connect(m_element, &Element::optionChoicesChanged, this, &ElementOptionsWidget::updateUi);
	}
}

/**
	@brief ElementOptionsWidget::buildLists
	Make one list per option of the symbol, its values in their order
*/
void ElementOptionsWidget::buildLists()
{
	while (m_layout->rowCount()) {
		m_layout->removeRow(0);
	}
	m_lists.clear();
	if (!m_element) {
		return;
	}

	for (const ElementOptions::Option &option : m_element->elementData().m_options.options())
	{
		auto *list = new QComboBox(this);
		for (const ElementOptions::Value &value : option.values) {
			list->addItem(value.names.name(value.name), value.name);
		}
		connect(list, &QComboBox::currentIndexChanged, this, [this]() {
			if (m_live_edit) {
				apply();
			}
		});
		m_layout->addRow(tr("%1:", "label of the list of an option").arg(option.names.name(option.name)), list);
		m_lists.append({option.name, list});
	}
}

/**
	@brief ElementOptionsWidget::updateUi
	Show in each list the value of its option for the element
*/
void ElementOptionsWidget::updateUi()
{
	if (!m_element) {
		return;
	}
	const ElementOptions::Choices resolved = m_element->resolvedOptions();
	for (const auto &pair : std::as_const(m_lists))
	{
		const QSignalBlocker blocker(pair.second);
		pair.second->setCurrentIndex(pair.second->findData(resolved.value(pair.first)));
	}
}

/**
	@brief ElementOptionsWidget::chosen
	@return the choices of the element with those of the lists: a value
	that is the default of its option is no choice
*/
ElementOptions::Choices ElementOptionsWidget::chosen() const
{
	if (!m_element) {
		return {};
	}
	ElementOptions::Choices choices = m_element->optionChoices();
	const ElementOptions &options = m_element->elementData().m_options;
	for (const auto &pair : m_lists)
	{
		const QString value = pair.second->currentData().toString();
		const ElementOptions::Option *option = options.option(pair.first);
		if (!option || value.isEmpty() || value == option->default_value) {
			choices.remove(pair.first);
		} else {
			choices.insert(pair.first, value);
		}
	}
	return choices;
}

/**
	@brief ElementOptionsWidget::comboFor
	@param name : the name of an option
	@return its list, or nullptr
*/
QComboBox *ElementOptionsWidget::comboFor(const QString &name) const
{
	for (const auto &pair : m_lists) {
		if (pair.first == name) return pair.second;
	}
	return nullptr;
}

/**
	@brief ElementOptionsWidget::associatedUndo
	@return the undo command giving the element the choices of the lists, or
	nullptr when they are those it has
*/
QUndoCommand *ElementOptionsWidget::associatedUndo() const
{
	if (!m_element) {
		return nullptr;
	}
	const ElementOptions::Choices choices = chosen();
	if (choices == m_element->optionChoices()) {
		return nullptr;
	}
	return new ChangeElementOptionsCommand(m_element, choices);
}

/**
	@brief ElementOptionsWidget::apply
	Give the element the choices of the lists, with undo
*/
void ElementOptionsWidget::apply()
{
	if (!m_element || !m_element->diagram()) {
		return;
	}
	if (QUndoCommand *undo = associatedUndo()) {
		m_element->diagram()->undoStack().push(undo);
	}
}

/**
	@brief ElementOptionsWidget::setLiveEdit
	Live, a value picked in a list is applied at once (apply())
	@param live_edit
	@return true: this tab can be live
*/
bool ElementOptionsWidget::setLiveEdit(bool live_edit)
{
	m_live_edit = live_edit;
	return true;
}

/**
	@brief ElementOptionsWidget::title
	@return the title of the tab
*/
QString ElementOptionsWidget::title() const
{
	return tr("Options");
}
