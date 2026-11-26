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
#include "elementdatawidget.h"

#include "elementtagwidget.h"

#include <diagram.h>
#include <qetgraphicsitem/element.h>
#include <qetproject.h>
#include <undocommand/changeelementinformationcommand.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>

namespace
{
	const QString yes = QStringLiteral("true");
}

/**
	@brief ElementDataWidget::ElementDataWidget
	@param element : the element whose data are typed
	@param parent
*/
ElementDataWidget::ElementDataWidget(Element *element, QWidget *parent) :
	AbstractElementPropertiesEditorWidget(parent)
{
	m_form = new QFormLayout(this);
	setElement(element);
}

/**
	@brief ElementDataWidget::fieldsOf
	See the declaration
*/
QList<PidCodification::DataField> ElementDataWidget::fieldsOf(const Element *element)
{
	QList<PidCodification::DataField> fields;
	if (!element || element->elementData().m_pid_family.isEmpty()) {
		return fields;
	}
	const QETProject *project = element->diagram() ? element->diagram()->project() : nullptr;
	if (project) {
		fields = project->pidCodification().dataFieldsOf(element->elementData().m_pid_class);
	}
		//What is filled and has no line anywhere else gets one here, so that
		//hides() never hides a value for good
	QStringList shown = ElementTagWidget::shownKeys(element);
	for (const PidCodification::DataField &field : std::as_const(fields)) {
		shown << field.key();
	}
	const QString prefix = PidCodification::dataPrefix();
	const DiagramContext information = element->elementInformations();
	for (const QString &key : information.keys())
	{
		if (!key.startsWith(prefix) || shown.contains(key)
			|| information.value(key).toString().isEmpty()) {
			continue;
		}
		PidCodification::DataField raw;
		raw.name = key.mid(prefix.size());
		fields << raw;
	}
	return fields;
}

/**
	@brief ElementDataWidget::isWanted
	See the declaration
*/
bool ElementDataWidget::isWanted(const Element *element)
{
	return !fieldsOf(element).isEmpty();
}

/**
	@brief ElementDataWidget::hides
	See the declaration
*/
bool ElementDataWidget::hides(const Element *element, const QString &key)
{
	return element && !element->elementData().m_pid_family.isEmpty()
			&& key.startsWith(PidCodification::dataPrefix());
}

/**
	@brief ElementDataWidget::setElement
	@param element
*/
void ElementDataWidget::setElement(Element *element)
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
			//Undone, redone, changed elsewhere, or a new type (another class)
		connect(m_element, &Element::elementInfoChange, this, &ElementDataWidget::updateUi);
		connect(m_element, &Element::symbolChanged, this, &ElementDataWidget::updateUi);
	}
	if (m_project) {
		connect(m_project, &QETProject::pidCodificationChanged, this, &ElementDataWidget::updateUi);
	}
	updateUi();
}

/**
	@brief ElementDataWidget::decimalSign
	@return the sign the project writes the tenths with; a file has a point
*/
QChar ElementDataWidget::decimalSign() const
{
	return m_project && m_project->pidCodification().effectiveDecimal() == PidCodification::Decimal::Comma
			? QLatin1Char(',') : QLatin1Char('.');
}

/**
	@brief ElementDataWidget::buildRows
	Make the lines of @p fields, in place of those there are
*/
void ElementDataWidget::buildRows(const QList<PidCodification::DataField> &fields)
{
		//A line may be telling a change right now (emptied, it goes): its
		//widgets are deleted later
	for (int i = 0 ; i < m_rows.size() ; ++i)
	{
		const QFormLayout::TakeRowResult taken = m_form->takeRow(0);
		for (QLayoutItem *item : {taken.labelItem, taken.fieldItem})
		{
			if (item && item->widget()) {
				item->widget()->hide();
				item->widget()->deleteLater();
			}
			delete item;
		}
	}
	m_rows.clear();

	for (const PidCodification::DataField &field : fields)
	{
		Row row;
		row.field = field;
			//One the project declares has classes; one shown because it is
			//filled has none, and no name but its key
		const bool declared = !field.classes.isEmpty();

		auto *holder = new QWidget(this);
		auto *line = new QHBoxLayout(holder);
		line->setContentsMargins(0, 0, 0, 0);
		if (field.kind == PidCodification::DataKind::YesNo)
		{
			auto *box = new QCheckBox(holder);
			connect(box, &QCheckBox::clicked, this, &ElementDataWidget::fieldDone);
			row.editor = box;
		}
		else if (field.kind == PidCodification::DataKind::List)
		{
			auto *list = new QComboBox(holder);
			connect(list, &QComboBox::activated, this, &ElementDataWidget::fieldDone);
			row.editor = list;
		}
		else
		{
			auto *edit = new QLineEdit(holder);
			if (field.kind == PidCodification::DataKind::Number) {
					//With a comma or a point, whatever the project writes
				edit->setValidator(new QRegularExpressionValidator(
					QRegularExpression(QStringLiteral("^-?[0-9]*([.,][0-9]*)?$")), edit));
			}
			connect(edit, &QLineEdit::editingFinished, this, &ElementDataWidget::fieldDone);
			row.editor = edit;
		}
		row.editor->setObjectName(field.key());
		row.editor->setToolTip(declared
			? tr("The information %1 of the element, which a text of its symbol may show").arg(field.key())
			: tr("The project declares no data field of this name for this class of symbols: "
				 "emptied, this line goes."));
		line->addWidget(row.editor, 1);
		if (!field.unit.isEmpty()) {
			line->addWidget(new QLabel(field.unit, holder));
		}
		m_form->insertRow(int(m_rows.size()),
						  new QLabel(tr("%1:").arg(declared ? field.title() : field.key()), this), holder);
		m_rows << row;
	}
}

/**
	@brief ElementDataWidget::updateUi
	Show the data of the element, its lines made again when they are others
*/
void ElementDataWidget::updateUi()
{
	if (!m_element) {
		return;
	}
	const QList<PidCodification::DataField> fields = fieldsOf(m_element);
	QList<PidCodification::DataField> built;
	for (const Row &row : std::as_const(m_rows)) {
		built << row.field;
	}
	if (built != fields) {
		buildRows(fields);
	}

	const DiagramContext information = m_element->elementInformations();
	for (Row &row : m_rows)
	{
		const QString held = information.value(row.field.key()).toString();
		const QSignalBlocker blocker(row.editor);
		if (auto *box = qobject_cast<QCheckBox *>(row.editor)) {
			box->setChecked(held == yes);
		}
		else if (auto *list = qobject_cast<QComboBox *>(row.editor))
		{
			list->clear();
			list->addItem(QString(), QString());
			for (const QString &value : std::as_const(row.field.values)) {
				list->addItem(value, value);
			}
				//A value the list has no more is kept, and shown
			int index = held.isEmpty() ? 0 : list->findData(held);
			if (index < 0) {
				list->addItem(held, held);
				index = list->count() - 1;
			}
			list->setCurrentIndex(index);
		}
		else if (auto *edit = qobject_cast<QLineEdit *>(row.editor))
		{
			edit->setText(row.field.kind == PidCodification::DataKind::Number
						  ? QString(held).replace(QLatin1Char('.'), decimalSign()) : held);
			row.shown = edit->text();
		}
	}
}

/**
	@brief ElementDataWidget::valueOf
	@return what the line @p row gives the information which is now @p held;
	@p held itself when the line was not touched, so that a value this line
	cannot show as it is (several lines of text, "false") is not changed by
	the edit of another line
*/
QString ElementDataWidget::valueOf(const Row &row, const QString &held) const
{
	if (auto *box = qobject_cast<QCheckBox *>(row.editor)) {
		if (box->isChecked() == (held == yes)) {
			return held;
		}
		return box->isChecked() ? yes : QString();
	}
	if (auto *list = qobject_cast<QComboBox *>(row.editor)) {
		return list->currentData().toString();
	}
	if (auto *edit = qobject_cast<QLineEdit *>(row.editor))
	{
		if (edit->text() == row.shown) {
			return held;
		}
		QString text = edit->text().trimmed();
		if (row.field.kind == PidCodification::DataKind::Number) {
			text.replace(QLatin1Char(','), QLatin1Char('.'));
		}
		return text;
	}
	return held;
}

/**
	@brief ElementDataWidget::edited
	See the declaration: a line emptied takes its information away
*/
DiagramContext ElementDataWidget::edited() const
{
	if (!m_element) {
		return DiagramContext();
	}
	DiagramContext information = m_element->elementInformations();
	for (const Row &row : m_rows)
	{
		const QString key = row.field.key();
		const QString held = information.value(key).toString();
		const QString value = valueOf(row, held);
		if (value == held) {
			continue;
		}
		if (value.isEmpty()) {
			information.remove(key);
		} else {
			information.addValue(key, value);
		}
	}
	return information;
}

/**
	@brief ElementDataWidget::fieldOf
	See the declaration
*/
QWidget *ElementDataWidget::fieldOf(const QString &key) const
{
	for (const Row &row : m_rows) {
		if (row.field.key() == key) {
			return row.editor;
		}
	}
	return nullptr;
}

/**
	@brief ElementDataWidget::fieldDone
	A line is left, picked or ticked: it is applied when live
*/
void ElementDataWidget::fieldDone()
{
	if (m_live_edit) {
		apply();
	}
}

/**
	@brief ElementDataWidget::associatedUndo
	@return the change of the information of the element, or nullptr when
	the lines are those it has
*/
QUndoCommand *ElementDataWidget::associatedUndo() const
{
	if (!m_element) {
		return nullptr;
	}
	const DiagramContext old_information = m_element->elementInformations();
	const DiagramContext new_information = edited();
	if (new_information == old_information) {
		return nullptr;
	}
	auto *undo = new ChangeElementInformationCommand(m_element, old_information, new_information);
	undo->setText(tr("Change the data of an element"));
	return undo;
}

/**
	@brief ElementDataWidget::apply
	Give the element the lines of the section, with undo
*/
void ElementDataWidget::apply()
{
	if (!m_element || !m_element->diagram()) {
		return;
	}
	if (QUndoCommand *undo = associatedUndo()) {
		m_element->diagram()->undoStack().push(undo);
	}
}

bool ElementDataWidget::setLiveEdit(bool live_edit)
{
	m_live_edit = live_edit;
	return true;
}

QString ElementDataWidget::title() const
{
	return tr("Data");
}
