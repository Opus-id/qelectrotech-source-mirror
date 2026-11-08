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
#include "../pidcodification.h"
#include "../properties/terminaldata.h"
#include "../qetproject.h"
#include "../qetgraphicsitem/element.h"
#include "../qetgraphicsitem/terminal.h"
#include "../undocommand/changenozzlediameterscommand.h"
#include "diameterlist.h"

#include <algorithm>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
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

	const PidCodification codification = projectCodification();
	for (const Terminal *terminal : m_element->terminals())
	{
		const QString name = terminal->baseName();
		if (terminal->medium() != TerminalData::Pipe || name.isEmpty()) {
			continue;
		}
		auto *connection = new QComboBox(this);
		connection->setObjectName(QStringLiteral("connection_list"));
		for (PipeConnection::Type type : PipeConnection::all()) {
			connection->addItem(PipeConnection::name(type), int(type));
		}
		auto *list = new QComboBox(this);
			//A nozzle has no material: the DN, then the outside diameters
		DiameterList::setUp(list, codification.nozzleSizes(), codification.locale());
		QLineEdit *edit = list->lineEdit();
		const Field field{name, terminal->symbolDiameter(), terminal->symbolConnection(),
						  connection, list, edit, terminal};
		const auto applied = [this, field]() {
			if (checkField(field) && m_live_edit) {
				apply();
			}
		};
		connect(edit, &QLineEdit::editingFinished, this, applied);
		connect(list, &QComboBox::activated, this, applied);
		connect(connection, &QComboBox::activated, this, [this, field]() {
			connectionPicked(field);
			if (m_live_edit) {
				apply();
			}
		});
		auto *row = new QHBoxLayout();
		row->addWidget(connection);
		row->addWidget(list, 1);
		m_layout->addRow(tr("%1:", "label of the DN of a nozzle").arg(name), row);
		m_fields.append(field);
	}
	m_message = new QLabel(this);
	m_message->setObjectName(QStringLiteral("nozzle_message"));
	m_message->setWordWrap(true);
	m_layout->addRow(m_message);
}

/**
	@brief ElementNozzlesWidget::projectCodification
	@return the codification of the project of the element, which has the
	lists of diameters; an empty one without a project
*/
PidCodification ElementNozzlesWidget::projectCodification() const
{
	if (m_element && m_element->diagram() && m_element->diagram()->project()) {
		return m_element->diagram()->project()->pidCodification();
	}
	return PidCodification();
}

/**
	@brief ElementNozzlesWidget::checkField
	See the declaration
*/
bool ElementNozzlesWidget::checkField(const Field &field)
{
	bool ok = false;
	const QString text = field.edit->text().trimmed();
	const PipeConnection::Type connection = pickedConnection(field);
	const PipeDiameter diameter = PipeDiameter::fromText(
				text, PipeConnection::kinds(connection).constFirst(), &ok);
	const PidCodification codification = projectCodification();
	if (ok && !PipeConnection::allows(connection, diameter)) {
		m_message->setText(tr("%1 does not name a nozzle of this connection (%2).")
						   .arg(text, PipeConnection::name(connection)));
		showField(field);
		return false;
	}
	if (ok && codification.acceptsForNozzle(diameter)) {
		m_message->clear();
			//Written as the lists write it, d110 as Ø 110: in the dialog
			//too, where nothing is applied before Apply
		const QSignalBlocker blocker(field.edit);
		field.edit->setText(diameter.text(codification.locale()));
		return true;
	}
		//A size the lists have not: the nearest one of its kind
	const PipeDiameter near = ok ? PidCodification::nearest(diameter, sizesFor(field))
								 : PipeDiameter();
	if (near.isSet()) {
		const QSignalBlocker blocker(field.edit);
		field.edit->setText(near.text(codification.locale()));
		m_message->setText(tr("%1 → %2: the nearest size of the lists of the project.")
						   .arg(text, near.text(codification.locale())));
		return true;
	}
	m_message->setText(tr("%1 is not in the lists of diameters of the project (P&ID properties "
						  "of the project, Codification).").arg(text));
	showField(field);
	return false;
}

/**
	@brief ElementNozzlesWidget::message
	See the declaration
*/
QString ElementNozzlesWidget::message() const
{
	return m_message ? m_message->text() : QString();
}

/**
	@brief ElementNozzlesWidget::showField
	Show in @p field the diameter the element gives its nozzle
*/
void ElementNozzlesWidget::showField(const Field &field)
{
	const NozzleDiameters diameters = m_element->nozzleDiameters();
	const QLocale locale = projectCodification().locale();
	const PipeConnection::Type connection = field.terminal->connection();
	{
		const QSignalBlocker connection_blocker(field.connection);
		field.connection->setCurrentIndex(field.connection->findData(int(connection)));
	}
	DiameterList::fill(field.list, sizesFor(field), locale);
	const QSignalBlocker blocker(field.edit);
		//A DN given to a thread before the threads existed: in inches
	const PipeDiameter own = PipeConnection::fitted(connection, diameters.value(field.name));
	field.edit->setText(own.text(locale));
		//In grey, what the nozzle has without a value of its own: the
		//diameter of its symbol, else that of its pipe, which it follows
	field.edit->setPlaceholderText(field.terminal->diameter().text(locale));
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
	for (const Field &field : std::as_const(m_fields)) {
		showField(field);
	}
}

/**
	@brief ElementNozzlesWidget::chosen
	@return the diameters of the element with those of the fields, which
	replace the ones of the same names; a field the project refuses changes
	nothing
*/
NozzleDiameters ElementNozzlesWidget::chosen() const
{
	if (!m_element) {
		return {};
	}
	const PidCodification codification = projectCodification();
	NozzleDiameters diameters = m_element->nozzleDiameters();
	for (const Field &field : m_fields)
	{
		bool ok = false;
		const PipeConnection::Type connection = pickedConnection(field);
		const PipeDiameter diameter = PipeDiameter::fromText(
					field.edit->text(), PipeConnection::kinds(connection).constFirst(), &ok);
		if (!ok || !codification.acceptsForNozzle(diameter)
			|| !PipeConnection::allows(connection, diameter)) {
			continue;
		}
			//The one of the symbol is kept only if its connection takes it
			//not: then the element has none, which is no diameter
		if (!diameter.isSet() || diameter == field.symbol_diameter) {
			diameters.remove(field.name);
		} else {
			diameters.insert(field.name, diameter);
		}
	}
	return diameters;
}

/**
	@brief ElementNozzlesWidget::chosenConnections
	See the declaration
*/
NozzleConnections ElementNozzlesWidget::chosenConnections() const
{
	if (!m_element) {
		return {};
	}
	NozzleConnections connections = m_element->nozzleConnections();
	for (const Field &field : m_fields)
	{
		const PipeConnection::Type connection = pickedConnection(field);
		if (connection == field.symbol_connection) {
			connections.remove(field.name);
		} else {
			connections.insert(field.name, connection);
		}
	}
	return connections;
}

/**
	@brief ElementNozzlesWidget::pickedConnection
	See the declaration
*/
PipeConnection::Type ElementNozzlesWidget::pickedConnection(const Field &field)
{
	return PipeConnection::Type(field.connection->currentData().toInt());
}

/**
	@brief ElementNozzlesWidget::sizesFor
	See the declaration
*/
QList<PipeDiameter> ElementNozzlesWidget::sizesFor(const Field &field) const
{
	const QList<PipeDiameter::Kind> kinds = PipeConnection::kinds(pickedConnection(field));
	QList<PipeDiameter> sizes = projectCodification().nozzleSizes();
	sizes.erase(std::remove_if(sizes.begin(), sizes.end(), [&kinds](const PipeDiameter &size) {
		return !kinds.contains(size.kind());
	}), sizes.end());
	return sizes;
}

/**
	@brief ElementNozzlesWidget::connectionPicked
	See the declaration
*/
void ElementNozzlesWidget::connectionPicked(const Field &field)
{
	const PidCodification codification = projectCodification();
	const QLocale locale = codification.locale();
	const PipeConnection::Type connection = pickedConnection(field);
	DiameterList::fill(field.list, sizesFor(field), locale);
		//The diameter of the field, else the one of the symbol
	PipeDiameter diameter = PipeDiameter::fromText(field.edit->text());
	if (!diameter.isSet()) {
		diameter = field.symbol_diameter;
	}
	m_message->clear();
	if (PipeConnection::allows(connection, diameter)) {
		return;
	}
	QList<PipeDiameter> joined;
	for (const PipeDiameter &size : sizesFor(field)) {
		if (codification.compare(diameter, size) == PidCodification::DiameterMatch::Same) {
			joined << size;
		}
	}
	const QSignalBlocker blocker(field.edit);
	if (joined.size() == 1) {
		field.edit->setText(joined.constFirst().text(locale));
		m_message->setText(tr("%1 → %2: the size the table of the diameters of the project "
							  "joins to it.").arg(diameter.text(locale), joined.constFirst().text(locale)));
	} else {
		field.edit->clear();
		m_message->setText(tr("%1 does not name a nozzle of this connection (%2): the nozzle "
							  "has no size of its own.")
						   .arg(diameter.text(locale), PipeConnection::name(connection)));
	}
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
	@brief ElementNozzlesWidget::connectionFor
	See the declaration
*/
QComboBox *ElementNozzlesWidget::connectionFor(const QString &name) const
{
	for (const Field &field : m_fields) {
		if (field.name == name) return field.connection;
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
	const NozzleDiameters diameters = chosen();
	const NozzleConnections connections = chosenConnections();
	if (diameters == m_element->nozzleDiameters()
		&& connections == m_element->nozzleConnections()) {
		return nullptr;
	}
	return new ChangeNozzleDiametersCommand(m_element, diameters, connections);
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
