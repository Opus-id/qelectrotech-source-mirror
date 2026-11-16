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
#include "selectbycriteriadialog.h"

#include "../qeticons.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <functional>

namespace {
	const int key_role = Qt::UserRole;
		/// The last choice of the user, on by default
	const QString preview_key = QStringLiteral("diagrameditor/select_by_criteria_preview");
		/// The last choice of the user, off by default
	const QString zoom_key = QStringLiteral("diagrameditor/select_by_criteria_zoom");
}

SelectByCriteriaDialog::SelectByCriteriaDialog(const QList<QGraphicsItem *> &selection,
											   const QList<QGraphicsItem *> &folio,
											   const PidCodification &codes,
											   QWidget *parent) :
	QDialog(parent),
	m_codes(codes),
	m_departure(selection),
	m_in_selection(selection, codes),
	m_in_folio(folio, codes)
{
	setWindowTitle(tr("Select by criteria"));
	auto *layout = new QVBoxLayout(this);

	auto *intro = new QLabel(tr("What is checked is found; the action says what the "
								"selection becomes. A pipe line or a symbol is found when "
								"it is checked in every branch \"By…\" where something is "
								"checked."), this);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	auto *form = new QFormLayout();
		//Where to search
	auto *search = new QHBoxLayout();
	m_search_selection = new QRadioButton(
		tr("the selection (%n)", "", int(selection.size())), this);
	m_search_folio = new QRadioButton(tr("the whole folio"), this);
	m_search_selection->setEnabled(!selection.isEmpty());
	(selection.isEmpty() ? m_search_folio : m_search_selection)->setChecked(true);
	search->addWidget(m_search_selection);
	search->addWidget(m_search_folio);
	search->addStretch();
	form->addRow(tr("Search in:"), search);
	connect(m_search_folio, &QRadioButton::toggled, this, [this] { fillTree(); });

		//What the selection becomes: Replace at each opening
	auto *actions = new QHBoxLayout();
	actions->setSpacing(0);
	m_actions = new QButtonGroup(this);
	const QList<QPair<Action, QString>> action_names {
		{Action::Replace, tr("Replace")},
		{Action::Add,     tr("Add")},
		{Action::Remove,  tr("Remove")}};
	const QList<QIcon> icons {QET::Icons::SelectionReplace, QET::Icons::SelectionAdd,
							  QET::Icons::SelectionRemove};
	const QStringList tips {
		tr("The selection becomes what is found"),
		tr("What is found is added to the selection"),
		tr("What is found is taken out of the selection")};
	for (int i = 0 ; i < action_names.size() ; ++i) {
		auto *button = new QPushButton(icons.at(i), action_names.at(i).second, this);
		button->setCheckable(true);
		button->setToolTip(tips.at(i));
		button->setChecked(action_names.at(i).first == Action::Replace);
		m_actions->addButton(button, int(action_names.at(i).first));
		actions->addWidget(button);
	}
	actions->addStretch();
	form->addRow(tr("Action:"), actions);
	connect(m_actions, &QButtonGroup::idClicked, this, [this](int id) {
		m_action = Action(id);
		updateCount();
	});
	layout->addLayout(form);

	m_tree = new QTreeWidget(this);
	m_tree->setHeaderHidden(true);
	m_tree->setColumnCount(1);
	m_tree->header()->setStretchLastSection(true);
	layout->addWidget(m_tree, 1);

	m_count = new QLabel(this);
	layout->addWidget(m_count);

	auto *boxes = new QHBoxLayout();
	m_preview = new QCheckBox(tr("Preview on the folio"), this);
	m_preview->setToolTip(tr("The selection of the folio follows the boxes as they are checked"));
	m_preview->setChecked(QSettings().value(preview_key, true).toBool());
	m_zoom = new QCheckBox(tr("Zoom on the selection"), this);
	m_zoom->setToolTip(tr("The view frames the objects kept each time they are selected"));
	m_zoom->setChecked(QSettings().value(zoom_key, false).toBool());
	boxes->addWidget(m_preview);
	boxes->addWidget(m_zoom);
	boxes->addStretch();
	layout->addLayout(boxes);
	connect(m_preview, &QCheckBox::toggled, this, [this](bool on) {
		QSettings().setValue(preview_key, on);
		preview();
	});
	connect(m_zoom, &QCheckBox::toggled, this, [this](bool on) {
		QSettings().setValue(zoom_key, on);
		if (on) preview();
	});

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply
										 | QDialogButtonBox::Cancel, this);
	QPushButton *all = buttons->addButton(tr("Check all"), QDialogButtonBox::ActionRole);
	QPushButton *none = buttons->addButton(tr("Uncheck all"), QDialogButtonBox::ActionRole);
	connect(all, &QPushButton::clicked, this, [this] { setAllChecked(true); });
	connect(none, &QPushButton::clicked, this, [this] { setAllChecked(false); });
	connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
			this, &SelectByCriteriaDialog::apply);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

		//Checking a node checks its leaves one by one: count once
	connect(m_tree, &QTreeWidget::itemChanged, this, &SelectByCriteriaDialog::scheduleCount);
	fillTree();
	resize(460, 600);
}

const SelectionCategories &SelectByCriteriaDialog::categories() const
{
	return searchesFolio() ? m_in_folio : m_in_selection;
}

/**
	@brief SelectByCriteriaDialog::fillTree
	The tree of the categories searched, every leaf checked; the categories
	open, their branches closed
*/
void SelectByCriteriaDialog::fillTree()
{
		//A leaf there already keeps its box, the criteria with it
	QHash<QString, Qt::CheckState> before;
	for (QTreeWidgetItemIterator it(m_tree) ; *it ; ++it) {
		const QString key = (*it)->data(0, key_role).toString();
		if (!key.isEmpty()) {
			before.insert(key, (*it)->checkState(0));
		}
	}
	const QSignalBlocker blocker(m_tree);
	m_tree->clear();
	std::function<QTreeWidgetItem *(const SelectionCategories::Node &)> make =
			[&make, &before](const SelectionCategories::Node &node) {
		auto *item = new QTreeWidgetItem();
		item->setText(0, node.count < 0
						  ? node.label
						  : QStringLiteral("%1 (%2)").arg(node.label).arg(node.count));
		if (node.crossed) {
			QFont font = item->font(0);
			font.setItalic(true);
			item->setFont(0, font);
		}
		if (node.children.isEmpty()) {
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
			item->setData(0, key_role, node.key);
			item->setCheckState(0, before.value(node.key, Qt::Checked));
		} else {
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
			for (const SelectionCategories::Node &child : node.children) {
				item->addChild(make(child));
			}
		}
		return item;
	};
	for (const SelectionCategories::Node &node : categories().tree()) {
		QTreeWidgetItem *item = make(node);
		m_tree->addTopLevelItem(item);
		item->setExpanded(true);
	}
	updateCount();
}

QSet<QString> SelectByCriteriaDialog::checked() const
{
	QSet<QString> keys;
	for (QTreeWidgetItemIterator it(m_tree) ; *it ; ++it) {
		const QString key = (*it)->data(0, key_role).toString();
		if (!key.isEmpty() && (*it)->checkState(0) == Qt::Checked) {
			keys << key;
		}
	}
	return keys;
}

QList<QGraphicsItem *> SelectByCriteriaDialog::foundItems() const
{
		//The folio's categories have every branch of the selection's: a
		//branch it has besides has nothing checked and does not count
	const QList<QGraphicsItem *> all = m_in_folio.kept(checked());
	if (searchesFolio()) {
		return all;
	}
	const QSet<QGraphicsItem *> pool(m_departure.cbegin(), m_departure.cend());
	QList<QGraphicsItem *> found;
	for (QGraphicsItem *item : all) {
		if (pool.contains(item)) {
			found << item;
		}
	}
	return found;
}

QList<QGraphicsItem *> SelectByCriteriaDialog::resultItems() const
{
		//After a zone, in Add or Remove the boxes only set what the next
		//zones find
	if (m_zoned && m_action != Action::Replace) {
		return m_departure;
	}
	return combined(foundItems());
}

QList<QGraphicsItem *> SelectByCriteriaDialog::combined(const QList<QGraphicsItem *> &found) const
{
	if (m_action == Action::Replace) {
		return found;
	}
	const QSet<QGraphicsItem *> found_set(found.cbegin(), found.cend());
	QList<QGraphicsItem *> result;
	for (QGraphicsItem *item : m_departure) {
		if (m_action == Action::Add || !found_set.contains(item)) {
			result << item;
		}
	}
	if (m_action == Action::Add) {
		const QSet<QGraphicsItem *> departure(m_departure.cbegin(), m_departure.cend());
		for (QGraphicsItem *item : found) {
			if (!departure.contains(item)) {
				result << item;
			}
		}
	}
	return result;
}

int SelectByCriteriaDialog::keptCount() const
{
	return int(resultItems().size());
}

void SelectByCriteriaDialog::setAllChecked(bool checked)
{
	{
		const QSignalBlocker blocker(m_tree);
		for (QTreeWidgetItemIterator it(m_tree) ; *it ; ++it) {
			if ((*it)->childCount() == 0) {
				(*it)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
			}
		}
	}
	m_tree->viewport()->update();
	updateCount();
}

bool SelectByCriteriaDialog::previews() const
{
	return m_preview->isChecked();
}

bool SelectByCriteriaDialog::zooms() const
{
	return m_zoom->isChecked();
}

void SelectByCriteriaDialog::setPreview(bool preview)
{
	m_preview->setChecked(preview);
}

void SelectByCriteriaDialog::setZoom(bool zoom)
{
	m_zoom->setChecked(zoom);
}

void SelectByCriteriaDialog::setAction(Action action)
{
	m_actions->button(int(action))->setChecked(true);
	m_action = action;
	updateCount();
}

bool SelectByCriteriaDialog::searchesFolio() const
{
	return m_search_folio->isChecked();
}

void SelectByCriteriaDialog::setSearchFolio(bool folio)
{
	(folio ? m_search_folio : m_search_selection)->setChecked(true);
}

void SelectByCriteriaDialog::apply()
{
	const QList<QGraphicsItem *> result = resultItems();
	emit applied(result, zooms());
	setDeparture(result);
		//The tree of what is now searched, the boxes kept
	fillTree();
}

void SelectByCriteriaDialog::zone(const QList<QGraphicsItem *> &picked)
{
	const QSet<QGraphicsItem *> in(picked.cbegin(), picked.cend());
	QList<QGraphicsItem *> found;
	for (QGraphicsItem *item : m_in_folio.kept(checked())) {
		if (in.contains(item)) {
			found << item;
		}
	}
	const QList<QGraphicsItem *> result = combined(found);
	emit applied(result, false);
	m_zoned = true;
	setDeparture(result);
		//The boxes now sort what the zones took; the tree, and the
		//criteria, stay as they are
	if (!m_departure.isEmpty()) {
		const QSignalBlocker block_selection(m_search_selection);
		const QSignalBlocker block_folio(m_search_folio);
		m_search_selection->setChecked(true);
	}
	showCount();
}

/**
	@brief SelectByCriteriaDialog::setDeparture
	@p items are the selection the next action starts from
*/
void SelectByCriteriaDialog::setDeparture(const QList<QGraphicsItem *> &items)
{
	m_departure = items;
	m_in_selection = SelectionCategories(m_departure, m_codes);
	m_search_selection->setText(tr("the selection (%n)", "", int(m_departure.size())));
	m_search_selection->setEnabled(!m_departure.isEmpty());
	if (m_departure.isEmpty() && !searchesFolio()) {
		const QSignalBlocker block_selection(m_search_selection);
		const QSignalBlocker block_folio(m_search_folio);
		m_search_folio->setChecked(true);
	}
}

void SelectByCriteriaDialog::preview()
{
	if (previews()) {
		emit applied(resultItems(), zooms());
	}
}

void SelectByCriteriaDialog::scheduleCount()
{
	if (m_count_pending) {
		return;
	}
	m_count_pending = true;
	QTimer::singleShot(0, this, &SelectByCriteriaDialog::updateCount);
}

void SelectByCriteriaDialog::updateCount()
{
	m_count_pending = false;
	showCount();
	preview();
}

void SelectByCriteriaDialog::showCount()
{
	const int found = int(foundItems().size());
	m_count->setText(QStringLiteral("<b>%1</b> (%2)")
					 .arg(tr("%n object(s) will be selected", "", keptCount()),
						  tr("%n found", "", found)));
}
