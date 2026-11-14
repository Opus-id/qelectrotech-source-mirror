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
#include "../factory/elementpicturefactory.h"
#include "../pidstatus.h"
#include "../qetpalette.h"
#include "../qetgraphicsitem/element.h"
#include "../undocommand/changeelementoptionscommand.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QIcon>
#include <QPalette>
#include <QPointer>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>

#include <algorithm>

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
	@return true if the symbol of @p element offers options, or a status of
	works
*/
bool ElementOptionsWidget::hasOptions(const Element *element)
{
	return element && (!element->elementData().m_options.isEmpty()
					   || !element->elementData().m_pid_family.isEmpty());
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
	m_status = nullptr;
	m_type = nullptr;
	m_types.clear();
	if (!m_element) {
		return;
	}

	buildTypeList();

	for (const ElementOptions::Option &option : m_element->elementData().m_options.options())
	{
		auto *list = new QComboBox(this);
			//Each value with the drawing it gives (updateUi())
		showLargeDrawings(list);
		for (const ElementOptions::Value &value : option.values) {
			list->addItem(value.names.name(value.name), value.name);
		}
			//Applied at once, in the dialog of the element as in the dock:
			//the drawing follows the choice, as it follows the type
		connect(list, &QComboBox::currentIndexChanged, this, &ElementOptionsWidget::apply);
		m_layout->addRow(tr("%1:", "label of the list of an option").arg(option.names.name(option.name)), list);
		m_lists.append({option.name, list});
	}

	if (!m_element->elementData().m_pid_family.isEmpty())
	{
		m_status = new QComboBox(this);
		m_status->setObjectName(QStringLiteral("status"));
		for (const auto &status : PidStatus::statuses()) {
			m_status->addItem(status.second, status.first);
		}
		connect(m_status, &QComboBox::currentIndexChanged, this, &ElementOptionsWidget::apply);
		m_layout->addRow(tr("Status of works:"), m_status);
	}
}

/**
	@brief ElementOptionsWidget::buildTypeList
	The first row: the symbol of the element and the others of its folder
	which can replace it (Element::otherTypes(), as the "Change type" menu),
	by name, the one of the element shown. Greyed when there is no other.
*/
void ElementOptionsWidget::buildTypeList()
{
	m_types = Element::otherTypes({m_element.data()});
	const bool others = !m_types.isEmpty();
	const ElementsLocation current = m_element->location();
	m_types << current;
	std::sort(m_types.begin(), m_types.end(),
		  [](const ElementsLocation &a, const ElementsLocation &b) {
		return a.name().localeAwareCompare(b.name()) < 0;
	});

	m_type = new QComboBox(this);
	m_type->setObjectName(QStringLiteral("type_list"));
		//A long name must not widen the panel, and the drawing with it:
		//the list shown is as wide as its names
	m_type->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_type->setMinimumContentsLength(12);
		//Each type with its drawing, with the options of the element
		//(updateUi())
	showLargeDrawings(m_type);
	for (const ElementsLocation &location : std::as_const(m_types)) {
		m_type->addItem(location.name());
	}
	m_type->setCurrentIndex(int(m_types.indexOf(current)));
	m_type->setEnabled(others);
	m_type->setToolTip(others
		? tr("The type of the element: another symbol of its folder, of the same size and "
			 "with the same terminals, can replace it. Its place, its tag and what is connected "
			 "to it stay. Undo: Ctrl+Z.")
		: tr("No other symbol of the folder of this one has its size and its terminals."));
		//The panel applies a type at once: the wheel must not swap the
		//symbol while the panel is scrolled
	m_type->installEventFilter(this);
	connect(m_type, &QComboBox::activated, this, &ElementOptionsWidget::typePicked);
	m_layout->addRow(tr("Type:", "type of a placed element"), m_type);
}

/**
	@brief ElementOptionsWidget::showLargeDrawings
	The drawings of the list shown at listIconSize(), the list closed keeping
	the height of the other fields: the delegate of an item view takes the
	size of the icons of the view, not of the list.
	@param list
*/
void ElementOptionsWidget::showLargeDrawings(QComboBox *list)
{
	list->setItemDelegate(new QStyledItemDelegate(list));
	list->view()->setIconSize(listIconSize());
}

/**
	@brief ElementOptionsWidget::drawingIcon
	The symbol at @p location drawn with @p choices, at its own size (sharp
	once scaled to a list or a menu), from the library as from the project;
	black ink made light on a dark palette, as in the collections.
	@param location
	@param choices : the options chosen; the others at their defaults
	@param palette : the palette of the list or menu which shows it
	@return the icon, null when the symbol cannot be drawn
*/
QIcon ElementOptionsWidget::drawingIcon(const ElementsLocation &location,
										const ElementOptions::Choices &choices,
										const QPalette &palette)
{
		//Only the options of the symbol draw something: the others (the
		//status of works, those of another symbol) would make as many
		//drawings of the same thing
	ElementOptions options;
	options.fromXml(location.xml());
	ElementOptions::Choices own;
	for (const ElementOptions::Option &option : options.options()) {
		if (choices.contains(option.name)) {
			own.insert(option.name, choices.value(option.name));
		}
	}
	const QPixmap pixmap = ElementPictureFactory::instance()->pixmap(location, own);
	if (pixmap.isNull()) {
		return QIcon();
	}
	return QIcon(QET::Palette::forPalette(pixmap, palette));
}

/**
	@brief ElementOptionsWidget::choose
	Give each of @p elements whose symbol offers the value @p value of the
	option @p option that value, as the list of the option does: a default
	is no choice. One undo step for them all.
	@param elements
	@param option : the name of an option
	@param value : the name of one of its values
	@return the undo command, nullptr when no element changes
*/
QUndoCommand *ElementOptionsWidget::choose(const QList<Element *> &elements,
										   const QString &option, const QString &value)
{
	auto *undo = new QUndoCommand();
	int changed = 0;
	for (Element *element : elements)
	{
		if (!element) {
			continue;
		}
		const ElementOptions::Option *offered = element->elementData().m_options.option(option);
		if (!offered || !offered->hasValue(value)) {
			continue;
		}
		ElementOptions::Choices choices = element->optionChoices();
		if (value == offered->default_value) {
			choices.remove(option);
		} else {
			choices.insert(option, value);
		}
		if (choices != element->optionChoices()) {
			new ChangeElementOptionsCommand(element, choices, undo);
			++changed;
		}
	}
	if (!changed) {
		delete undo;
		return nullptr;
	}
	undo->setText(tr("Change an option of %n element(s)", "undo text", changed));
	return undo;
}

/**
	@brief ElementOptionsWidget::typePicked
	Swap the symbol of the element for the type picked, with undo. This
	section is rebuilt with the new symbol: the swap waits for the list to
	be left.
	@param index : the line of the list
*/
void ElementOptionsWidget::typePicked(int index)
{
	if (!m_element || index < 0 || index >= m_types.size()) {
		return;
	}
	const ElementsLocation location = m_types.at(index);
	const ElementsLocation current = m_element->location();
		//The list shows the type the element has, until it has another
	{
		const QSignalBlocker blocker(m_type);
		m_type->setCurrentIndex(int(m_types.indexOf(current)));
	}
	if (location == current) {
		return;
	}
	QPointer<Element> element = m_element;
	QTimer::singleShot(0, element.data(), [element, location]() {
		if (element) {
			Element::changeType({element.data()}, location);
		}
	});
}

/**
	@brief ElementOptionsWidget::eventFilter
	The wheel never changes the type: the dock scrolls first when the list
	is not being edited (PropertiesEditorDockWidget), and the wheel stops
	here when it is.
*/
bool ElementOptionsWidget::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == m_type && event->type() == QEvent::Wheel) {
		return true;
	}
	return AbstractElementPropertiesEditorWidget::eventFilter(watched, event);
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
	const ElementsLocation location = m_element->location();
		//The list shown is as wide as its drawings and names
	const auto fitPopup = [](QComboBox *list) {
		list->view()->setMinimumWidth(list->view()->sizeHintForColumn(0)
									  + 2 * list->view()->frameWidth() + 4);
	};
	for (const auto &pair : std::as_const(m_lists))
	{
		const QSignalBlocker blocker(pair.second);
		pair.second->setCurrentIndex(pair.second->findData(resolved.value(pair.first)));
			//Each value drawn with the other options as they are chosen
		for (int i = 0 ; i < pair.second->count() ; ++i) {
			ElementOptions::Choices choices = resolved;
			choices.insert(pair.first, pair.second->itemData(i).toString());
			pair.second->setItemIcon(i, drawingIcon(location, choices, pair.second->palette()));
		}
		fitPopup(pair.second);
	}
		//Each type drawn with the options of the element, as it would be
	if (m_type)
	{
		for (int i = 0 ; i < m_type->count() && i < m_types.size() ; ++i) {
			m_type->setItemIcon(i, drawingIcon(m_types.at(i), resolved, m_type->palette()));
		}
		fitPopup(m_type);
	}
	if (m_status)
	{
		const QSignalBlocker blocker(m_status);
		const QString status = m_element->optionChoices().value(PidStatus::option());
		for (int i = 0 ; i < m_status->count() ; ++i) {
			if (m_status->itemData(i).toString() == status) {
				m_status->setCurrentIndex(i);
			}
		}
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
	if (m_status)
	{
		const QString status = m_status->currentData().toString();
		if (status.isEmpty()) {
			choices.remove(PidStatus::option());
		} else {
			choices.insert(PidStatus::option(), status);
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
	A value picked is applied at once (apply()), live or not
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
	return tr("Type and options");
}
