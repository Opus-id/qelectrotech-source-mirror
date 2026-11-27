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
#include "pidselectiontagwidget.h"

#include "elementtagwidget.h"
#include "multielementinfowidget.h"

#include <diagram.h>
#include <pidcodification.h>
#include <pidtag.h>
#include <placesuggestions.h>
#include <qetgraphicsitem/element.h>
#include <qetgraphicsitem/structureboxitem.h>
#include <qetproject.h>
#include <undocommand/changeelementinformationcommand.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>

namespace {

using Changes = QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>>;

/**
	A change of this editor: one undo step of its own, never joined to the
	change before it (ChangeElementInformationCommand joins the changes of
	the same elements), so that numbering is undone apart from the unit.
*/
class PidSelectionTagCommand : public ChangeElementInformationCommand
{
	public:
		PidSelectionTagCommand(const Changes &changes, const QString &text) :
			ChangeElementInformationCommand(changes)
		{
			setText(text);
		}
		int id() const override {return -1;}
};

	/// A numbered tag whose model shows the train has one: 0 for none
	/// (ElementTagWidget::edited())
void giveTrain(const Element *element, DiagramContext &information)
{
	if (information.value(PidTag::trainKey()).toString().trimmed().isEmpty()
			&& !information.value(PidTag::orderKey()).toString().trimmed().isEmpty()
			&& PidTag::showsTrain(element->pidTagModel())) {
		information.addValue(PidTag::trainKey(), QStringLiteral("0"));
	}
}

	/// What "Number" changes in @p tagged
Changes numberingChanges(QETProject *project, const QList<Element *> &tagged, bool again)
{
	Changes changes;
	if (!project) {
		return changes;
	}
	const QHash<Element *, QString> orders = PidTag::newOrders(
				project, tagged,
				again ? PidTag::Numbering::All : PidTag::Numbering::Unnumbered);
	for (auto it = orders.cbegin() ; it != orders.cend() ; ++it)
	{
		Element *element = it.key();
		const DiagramContext old_information = element->elementInformations();
		DiagramContext information = old_information;
		information.addValue(PidTag::orderKey(), it.value());
		giveTrain(element, information);
		information = element->withPidLabel(information);
		if (information != old_information) {
			changes.insert(QPointer<Element>(element), qMakePair(old_information, information));
		}
	}
	return changes;
}

}

/**
	@brief PidSelectionTagWidget::PidSelectionTagWidget
	@param elements : the selected elements which take information
	@param other_items : how many other selected items are left alone
	@param parent
*/
PidSelectionTagWidget::PidSelectionTagWidget(const QList<Element *> &elements,
											 int other_items,
											 QWidget *parent) :
	PropertiesEditorWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	auto *group = new QGroupBox(tr("P&&ID tag"), this);
	auto *form = new QFormLayout(group);

	m_header = new QLabel(group);
	m_header->setObjectName(QStringLiteral("header"));
	m_header->setWordWrap(true);
	form->addRow(m_header);

	m_unit = new QComboBox(group);
	m_unit->setObjectName(QStringLiteral("unit"));
	m_unit->setEditable(true);
	m_unit->setInsertPolicy(QComboBox::NoInsert);
	m_unit->setToolTip(tr("The functional unit of the P&ID symbols of the selection, picked in the list "
						  "of the project or typed; the same value as their plant"));
	if (QCompleter *completer = m_unit->completer()) {
		completer->setFilterMode(Qt::MatchContains);
		completer->setCaseSensitivity(Qt::CaseInsensitive);
		completer->setCompletionMode(QCompleter::PopupCompletion);
	}
	form->addRow(tr("Functional unit:"), m_unit);

	m_train = new QLineEdit(group);
	m_train->setObjectName(QStringLiteral("train"));
	m_train->setValidator(new QRegularExpressionValidator(
							  QRegularExpression(QStringLiteral("[0-9]*")), m_train));
	m_train->setToolTip(tr("The number of the train of the P&ID symbols of the selection, 0 when there is none"));
	form->addRow(tr("Train:"), m_train);

	m_again = new QCheckBox(tr("Number again the symbols which already have a number"), group);
	m_again->setObjectName(QStringLiteral("again"));
	m_again->setToolTip(tr("Unticked, only the symbols without a number get one. Ticked, every symbol of "
						   "the selection is numbered again, the numbers it had being freed."));
	form->addRow(m_again);

	m_number = new QPushButton(tr("Number"), group);
	m_number->setObjectName(QStringLiteral("number"));
	m_number->setToolTip(tr("Give the P&ID symbols of the selection the next numbers of the project, "
							"folio by folio, then from left to right and from top to bottom. "
							"Done at once, the changes pending first."));
	m_result = new QLabel(group);
	m_result->setObjectName(QStringLiteral("result"));
	m_result->setWordWrap(true);
	auto *number_row = new QHBoxLayout();
	number_row->addWidget(m_number);
	number_row->addWidget(m_result, 1);
	form->addRow(number_row);

	layout->addWidget(group);

	m_information = new MultiElementInfoWidget(elements, other_items, this);
	layout->addWidget(m_information, 1);

	connect(m_unit, &QComboBox::activated, this, &PidSelectionTagWidget::ownFieldDone);
	connect(m_unit->lineEdit(), &QLineEdit::editingFinished, this, &PidSelectionTagWidget::ownFieldDone);
	connect(m_train, &QLineEdit::editingFinished, this, &PidSelectionTagWidget::ownFieldDone);
	connect(m_number, &QPushButton::clicked, this, [this]() { number(); });

	setElements(elements, other_items);
}

/**
	@brief PidSelectionTagWidget::taggedElements
	See the declaration
*/
QList<Element *> PidSelectionTagWidget::taggedElements(const QList<Element *> &elements)
{
	QList<Element *> list;
	for (Element *element : elements) {
		if (ElementTagWidget::isWanted(element) && !element->pidTagModel().isEmpty()) {
			list << element;
		}
	}
	return list;
}

/**
	@brief PidSelectionTagWidget::isWanted
	See the declaration
*/
bool PidSelectionTagWidget::isWanted(const QList<Element *> &elements)
{
	return !taggedElements(elements).isEmpty();
}

/**
	@brief PidSelectionTagWidget::setElements
	Edit @p elements, forgetting any pending change
*/
void PidSelectionTagWidget::setElements(const QList<Element *> &elements, int other_items)
{
	for (const auto &element : std::as_const(m_tagged)) {
		if (element) {
			disconnect(element.data(), nullptr, this, nullptr);
		}
	}
	if (m_project) {
		disconnect(m_project, nullptr, this, nullptr);
	}

	m_information->setElements(elements, other_items);
	m_selected = elements.size();
	m_tagged.clear();
	m_project = nullptr;
	for (Element *element : taggedElements(elements)) {
		m_tagged << QPointer<Element>(element);
			//Undone, redone, or changed by the information below
		connect(element, &Element::elementInfoChange, this, &PidSelectionTagWidget::elementInfoChanged);
		if (!m_project && element->diagram()) {
			m_project = element->diagram()->project();
		}
	}
	if (m_project) {
		connect(m_project, &QETProject::pidCodificationChanged, this, &PidSelectionTagWidget::readElements);
	}
	m_result->clear();
	readElements();
}

QList<Element *> PidSelectionTagWidget::tagged() const
{
	QList<Element *> list;
	for (const auto &element : m_tagged) {
		if (element) {
			list << element.data();
		}
	}
	return list;
}

QETProject *PidSelectionTagWidget::project() const
{
	return m_project.data();
}

/**
	@brief PidSelectionTagWidget::fillUnits
	The units offered, each once, as in the Tag tab of one symbol
	(ElementTagWidget::fillUnits()): first the plant the structure boxes or
	the folios give, when it is the same for every symbol (only offered),
	then the units of the codification, then the other plants of the project
*/
void PidSelectionTagWidget::fillUnits()
{
	const QSignalBlocker blocker(m_unit);
	m_unit->clear();
	m_unit->addItem(QString(), QString());
	QETProject *project = this->project();
	if (!project) {
		return;
	}
	const PidCodification &codification = project->pidCodification();
	QStringList offered;
	auto offer = [this, &codification, &offered](const QString &code) {
		if (code.isEmpty() || offered.contains(code)) {
			return;
		}
		offered << code;
		QString name;
		for (const PidCodification::Entry &unit : codification.functional_units) {
			if (unit.code == code) name = unit.name;
		}
		m_unit->addItem(name.isEmpty() ? code : QStringLiteral("%1 – %2").arg(code, name), code);
	};

	QString given;
	bool first = true;
	bool same = true;
	for (Element *element : tagged()) {
		if (!element->diagram()) continue;
		const QString plant = StructureBoxItem::referencePlace(element).plant.trimmed();
		if (first) {
			given = plant;
			first = false;
		} else if (plant != given) {
			same = false;
			break;
		}
	}
	if (same) {
		offer(given);
	}
	for (const PidCodification::Entry &unit : codification.functional_units) {
		offer(unit.code);
	}
	for (const QString &plant : PlaceSuggestions::plants(project)) {
		offer(plant);
	}
}

/**
	@brief PidSelectionTagWidget::readElements
	Show the unit and the train the symbols share, or that they differ,
	forgetting what was typed
*/
void PidSelectionTagWidget::readElements()
{
	const QList<Element *> list = tagged();
	auto shared = [&list](const QString &key, QString *initial, bool *mixed) {
		initial->clear();
		*mixed = false;
		bool first = true;
		for (Element *element : list) {
			const QString value = element->elementInformations().value(key).toString();
			if (first) {
				*initial = value;
				first = false;
			} else if (value != *initial) {
				*mixed = true;
				initial->clear();
				return;
			}
		}
	};
	shared(PidTag::unitKey(), &m_unit_initial, &m_unit_mixed);
	shared(PidTag::trainKey(), &m_train_initial, &m_train_mixed);

	m_updating = true;
	fillUnits();
	{
		const QSignalBlocker b1(m_unit), b2(m_train);
		int index = -1;
		for (int i = 1 ; i < m_unit->count() ; ++i) {
			if (m_unit->itemData(i).toString() == m_unit_initial) index = i;
		}
		if (index > 0 && !m_unit_initial.isEmpty()) {
			m_unit->setCurrentIndex(index);
		} else {
			m_unit->setCurrentIndex(0);
			m_unit->setEditText(m_unit_initial);
		}
		m_unit->lineEdit()->setPlaceholderText(m_unit_mixed ? tr("Multiple values") : QString());
		m_train->setText(m_train_initial);
		m_train->setPlaceholderText(m_train_mixed ? tr("Multiple values") : QString());
	}
	m_updating = false;

	if (list.size() == m_selected) {
		m_header->setText(tr("P&ID symbols edited: %1").arg(list.size()));
	} else {
		m_header->setText(tr("P&ID symbols edited: %1 of %2 (the others have no model of tag)")
						  .arg(list.size()).arg(m_selected));
	}
	m_number->setEnabled(!list.isEmpty());
}

QString PidSelectionTagWidget::unitCode() const
{
	const QString text = m_unit->currentText().trimmed();
	const int index = m_unit->findText(text);
	if (index > 0) {
		return m_unit->itemData(index).toString();
	}
	return text;
}

	/// A field left alone, or given back what all the symbols had, changes
	/// nothing; emptied while they all had one value, it clears it
bool PidSelectionTagWidget::unitIsSet() const
{
	const QString code = unitCode();
	return m_unit_mixed ? !code.isEmpty() : code != m_unit_initial;
}

bool PidSelectionTagWidget::trainIsSet() const
{
	const QString train = m_train->text().trimmed();
	return m_train_mixed ? !train.isEmpty() : train != m_train_initial;
}

/**
	@brief PidSelectionTagWidget::associatedUndo
	@return the change the unit and the train make, or nullptr. "Number"
	and the information below make their own (numberingUndo(),
	MultiElementInfoWidget::associatedUndo()).
*/
QUndoCommand *PidSelectionTagWidget::associatedUndo() const
{
	const bool unit = unitIsSet();
	const bool train = trainIsSet();
	if (!unit && !train) {
		return nullptr;
	}
	Changes changes;
	for (Element *element : tagged())
	{
		const DiagramContext old_information = element->elementInformations();
		DiagramContext information = old_information;
		if (unit) {
			const QString code = unitCode();
			if (code.isEmpty()) information.remove(PidTag::unitKey());
			else information.addValue(PidTag::unitKey(), code);
		}
		if (train) {
			const QString number = m_train->text().trimmed();
			if (number.isEmpty()) information.remove(PidTag::trainKey());
			else information.addValue(PidTag::trainKey(), number);
		}
		giveTrain(element, information);
		information = element->withPidLabel(information);
		if (information != old_information) {
			changes.insert(QPointer<Element>(element), qMakePair(old_information, information));
		}
	}
	if (changes.isEmpty()) {
		return nullptr;
	}
	return new PidSelectionTagCommand(changes, tr("Tag the selected symbols"));
}

/**
	@brief PidSelectionTagWidget::numberingUndo
	See the declaration
*/
QUndoCommand *PidSelectionTagWidget::numberingUndo() const
{
	const Changes changes = numberingChanges(project(), tagged(), m_again->isChecked());
	if (changes.isEmpty()) {
		return nullptr;
	}
	return new PidSelectionTagCommand(changes, tr("Number the selected symbols"));
}

void PidSelectionTagWidget::push(QUndoCommand *undo)
{
	if (!undo) {
		return;
	}
	const QList<Element *> list = tagged();
	if (list.isEmpty() || !list.first()->diagram()) {
		delete undo;
		return;
	}
	m_applying = true;
	list.first()->diagram()->undoStack().push(undo);
	m_applying = false;
	readElements();
		//The information below reads the elements again at once: a field
		//of it left alone gives back what it read, which would undo this
		//change on its next apply. Nothing of it is pending here: it was
		//applied before (apply()), or it applies itself (live edit).
	m_information->updateUi();
}

/**
	@brief PidSelectionTagWidget::apply
	The unit, the train and the information below, as one undo step
*/
void PidSelectionTagWidget::apply()
{
	QUndoCommand *own = associatedUndo();
	QUndoCommand *information = m_information->associatedUndo();
	if (!information) {
		push(own);
		return;
	}
	delete information;
	if (!own) {
		m_information->apply();
		return;
	}
	delete own;

		//The information first, then the unit and the train on what it
		//gave: a field of the information left alone gives back the value
		//the elements had when it was filled
	const QList<Element *> list = tagged();
	if (list.isEmpty() || !list.first()->diagram()) {
		m_information->apply();
		return;
	}
	QUndoStack &stack = list.first()->diagram()->undoStack();
	m_applying = true;
	stack.beginMacro(tr("Tag the selected symbols"));
	m_information->apply();
	if (QUndoCommand *tag = associatedUndo()) {
		stack.push(tag);
	}
	stack.endMacro();
	m_applying = false;
	readElements();
	m_information->updateUi();
}

/**
	@brief PidSelectionTagWidget::number
	See the declaration
*/
int PidSelectionTagWidget::number()
{
	if (m_live_edit) {
		push(associatedUndo());
	} else {
		apply();
	}
	const Changes changes = numberingChanges(project(), tagged(), m_again->isChecked());
	if (changes.isEmpty()) {
		m_result->setText(m_again->isChecked()
						  ? tr("Nothing to change: the numbers are those already held.")
						  : tr("Nothing to number: every symbol has a number."));
		return 0;
	}
	push(new PidSelectionTagCommand(changes, tr("Number the selected symbols")));
	m_result->setText(tr("%n symbol(s) numbered", "", int(changes.size())));
	return int(changes.size());
}

/**
	@brief PidSelectionTagWidget::ownFieldDone
	The unit or the train is left, or a unit picked: applied when live
*/
void PidSelectionTagWidget::ownFieldDone()
{
	if (m_updating || !m_live_edit) {
		return;
	}
	push(associatedUndo());
}

/**
	@brief PidSelectionTagWidget::elementInfoChanged
	Something else than this editor changed a symbol (undo, the information
	below): read them again once it is done
*/
void PidSelectionTagWidget::elementInfoChanged()
{
	if (m_applying || m_refresh_pending) {
		return;
	}
	m_refresh_pending = true;
	QTimer::singleShot(0, this, [this]() {
		m_refresh_pending = false;
		readElements();
	});
}

void PidSelectionTagWidget::reset()
{
	m_information->reset();
	readElements();
}

QString PidSelectionTagWidget::title() const
{
	return tr("Tag and information");
}

void PidSelectionTagWidget::updateUi()
{
	m_information->updateUi();
	readElements();
}

bool PidSelectionTagWidget::setLiveEdit(bool live_edit)
{
	m_live_edit = live_edit;
	m_information->setLiveEdit(live_edit);
	return true;
}

void PidSelectionTagWidget::setUnitText(const QString &text)
{
	m_unit->setEditText(text);
}

void PidSelectionTagWidget::setTrainText(const QString &text)
{
	m_train->setText(text);
}

void PidSelectionTagWidget::setNumberAgain(bool again)
{
	m_again->setChecked(again);
}

QString PidSelectionTagWidget::unitText() const
{
	return unitCode();
}

QString PidSelectionTagWidget::trainText() const
{
	return m_train->text();
}

QStringList PidSelectionTagWidget::offeredUnits() const
{
	QStringList list;
	for (int i = 1 ; i < m_unit->count() ; ++i) {
		list << m_unit->itemData(i).toString();
	}
	return list;
}
