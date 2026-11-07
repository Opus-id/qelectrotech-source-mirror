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
#include "pidconfigpage.h"

#include "../../qeticons.h"
#include "../../qetproject.h"
#include "../../undocommand/changepidcodificationcommand.h"
#include "../../utils/qetsettings.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>

namespace {
	constexpr int group_role = Qt::UserRole;
	constexpr int key_role = Qt::UserRole + 1;
	constexpr int color_column = 2;
}

/**
	@brief PidConfigPage::PidConfigPage
	@param project
	@param parent
*/
PidConfigPage::PidConfigPage(QETProject *project, QWidget *parent) :
	ProjectConfigPage(project, parent)
{
	init();

		//The window of the properties of the project blocks the editor, and
		//its undo, as long as it is open: what was applied here can be taken
		//back from here. Only that: another step could take a folio away
		//from under the other pages of the window.
	connect(new QShortcut(QKeySequence::Undo, this), &QShortcut::activated,
			this, &PidConfigPage::undoLastChange);
	connect(new QShortcut(QKeySequence::Redo, this), &QShortcut::activated,
			this, &PidConfigPage::redoLastChange);
	if (m_project) {
		connect(m_project, &QETProject::pidCodificationChanged,
				this, &PidConfigPage::readValuesFromProject);
	}
}

/**
	@brief PidConfigPage::undoLastChange
	Undo the last step of the project if it is a change of this page
*/
void PidConfigPage::undoLastChange()
{
	if (!m_project) {
		return;
	}
	QUndoStack *stack = m_project->undoStack();
	const int index = stack->index();
	if (index > 0 && dynamic_cast<const ChangePidCodificationCommand *>(stack->command(index - 1))) {
		stack->undo();
	}
}

/**
	@brief PidConfigPage::redoLastChange
	Redo the next step of the project if it is a change of this page
*/
void PidConfigPage::redoLastChange()
{
	if (!m_project) {
		return;
	}
	QUndoStack *stack = m_project->undoStack();
	const int index = stack->index();
	if (index < stack->count() && dynamic_cast<const ChangePidCodificationCommand *>(stack->command(index))) {
		stack->redo();
	}
}

/**
	@return the title of this page
*/
QString PidConfigPage::title() const
{
	return tr("P&ID", "configuration page title");
}

/**
	@return the icon of this page
*/
QIcon PidConfigPage::icon() const
{
	return QET::Icons::PidDiagram;
}

/**
	@brief PidConfigPage::applyProjectConf
	Give the project the colours of this page
*/
void PidConfigPage::applyProjectConf()
{
	m_project->undoStack()->push(new ChangePidCodificationCommand(m_project, m_codification));
}

void PidConfigPage::initWidgets()
{
	m_table = new QTableWidget(0, 3, this);
	m_table->setHorizontalHeaderLabels({tr("Applies to"), tr("Name"), tr("Color")});
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->verticalHeader()->hide();
	m_table->horizontalHeader()->setStretchLastSection(false);
		//A click in the colour of a line asks it at once; with Ctrl or Shift
		//it is a selection, for the buttons
	connect(m_table, &QTableWidget::cellClicked, this, [this](int, int column) {
		if (column == color_column && QApplication::keyboardModifiers() == Qt::NoModifier) {
			chooseColor();
		}
	});
	connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int column) {
		if (column != color_column) {
			chooseColor();
		}
	});

	m_table->viewport()->installEventFilter(this);

	m_choose = new QPushButton(tr("Choose a color..."), this);
	m_clear = new QPushButton(tr("No color"), this);
	connect(m_choose, &QPushButton::clicked, this, &PidConfigPage::chooseColor);
	connect(m_clear, &QPushButton::clicked, this, [this]() { setColorOfSelection(QColor()); });

	m_save_palette = new QPushButton(tr("Save as default palette"), this);
	m_save_palette->setObjectName(QStringLiteral("save_palette"));
	m_save_palette->setToolTip(tr("New projects will start with these colors and these texts. "
								  "Projects that already exist do not change."));
	connect(m_save_palette, &QPushButton::clicked, this, &PidConfigPage::saveAsDefaultPalette);
	m_palette_saved = new QLabel(this);

	for (const QString &category : PidCodification::textCategories())
	{
		auto *size = new QSpinBox(this);
		size->setObjectName(QStringLiteral("text_size_") + category);
		size->setRange(0, 200);
		size->setSuffix(tr(" pt"));
		size->setSpecialValueText(tr("Size of the symbol"));
		connect(size, &QSpinBox::valueChanged, this, &PidConfigPage::readTextWidgets);
		m_text_sizes.insert(category, size);

			//Emptying the field gives its former value back: 0 is not obvious
		auto *reset = new QPushButton(tr("Back to the size of the symbol"), this);
		reset->setObjectName(QStringLiteral("text_size_reset_") + category);
		connect(reset, &QPushButton::clicked, size, [size]() { size->setValue(0); });
		m_text_size_resets.insert(category, reset);
	}
	m_colored_labels = new QCheckBox(tr("Labels take the color of their symbol"), this);
	m_colored_labels->setObjectName(QStringLiteral("colored_labels"));
	connect(m_colored_labels, &QCheckBox::toggled, this, &PidConfigPage::readTextWidgets);
	m_nozzle_texts_on_paper = new QCheckBox(tr("Print and export the names and DN of the nozzles"), this);
	m_nozzle_texts_on_paper->setObjectName(QStringLiteral("nozzle_texts_on_paper"));
	m_nozzle_texts_on_paper->setToolTip(tr("How the box \"Draw the names and DN of the nozzles\" starts "
										   "in each print and export."));
	connect(m_nozzle_texts_on_paper, &QCheckBox::toggled, this, &PidConfigPage::readTextWidgets);
}

void PidConfigPage::initLayout()
{
	auto *buttons = new QHBoxLayout();
	buttons->addWidget(m_choose);
	buttons->addWidget(m_clear);
	buttons->addStretch();

	auto *colors_tab = new QWidget(this);
	auto *layout = new QVBoxLayout(colors_tab);
	auto *help = new QLabel(tr("What has no color here is drawn in black. A status comes first, then a "
							   "color set on a pipe line, then its fluid, then the family of the object."),
							colors_tab);
	help->setWordWrap(true);
	layout->addWidget(help);
	layout->addWidget(m_table);
	layout->addLayout(buttons);

	auto *texts_tab = new QWidget(this);
	auto *texts_layout = new QVBoxLayout(texts_tab);
	auto *texts_help = new QLabel(tr("A label placed follows its category as long as it has its former "
									 "size: a size set by hand stays."), texts_tab);
	texts_help->setWordWrap(true);
	texts_layout->addWidget(texts_help);
	auto *sizes = new QFormLayout();
	const QStringList names{tr("Labels of equipment:"), tr("Labels of valves:"),
							tr("Labels of instruments:"), tr("Names and DN of nozzles:")};
	const QStringList categories = PidCodification::textCategories();
	for (int i = 0 ; i < categories.size() ; ++i) {
		auto *row = new QHBoxLayout();
		row->addWidget(m_text_sizes.value(categories.at(i)));
		row->addWidget(m_text_size_resets.value(categories.at(i)));
		row->addStretch();
		sizes->addRow(names.at(i), row);
	}
	texts_layout->addLayout(sizes);
	texts_layout->addWidget(m_colored_labels);
	texts_layout->addWidget(m_nozzle_texts_on_paper);
	texts_layout->addStretch();

	auto *tabs = new QTabWidget(this);
	tabs->addTab(colors_tab, tr("Colors"));
	tabs->addTab(texts_tab, tr("Texts"));

	auto *palette = new QHBoxLayout();
	palette->addStretch();
	palette->addWidget(m_palette_saved);
	palette->addWidget(m_save_palette);

	auto *page_layout = new QVBoxLayout(this);
	page_layout->addWidget(tabs);
	page_layout->addLayout(palette);
}

void PidConfigPage::readValuesFromProject()
{
	m_codification = m_project->pidCodification();
	fillTable();

		//Each change of a widget reads them all: they are all set first
	const PidCodification codification = m_codification;
	for (auto it = m_text_sizes.cbegin() ; it != m_text_sizes.cend() ; ++it) {
		const QSignalBlocker blocker(it.value());
		it.value()->setValue(codification.text_sizes.value(it.key()));
	}
	const QSignalBlocker colored_blocker(m_colored_labels);
	m_colored_labels->setChecked(codification.colored_labels);
	const QSignalBlocker paper_blocker(m_nozzle_texts_on_paper);
	m_nozzle_texts_on_paper->setChecked(codification.nozzle_texts_on_paper);
}

void PidConfigPage::adjustReadOnly()
{
	const bool read_only = m_project->isReadOnly();
	m_table->setEnabled(!read_only);
	m_choose->setEnabled(!read_only);
	m_clear->setEnabled(!read_only);
	for (QSpinBox *size : std::as_const(m_text_sizes)) {
		size->setEnabled(!read_only);
	}
	for (QPushButton *reset : std::as_const(m_text_size_resets)) {
		reset->setEnabled(!read_only);
	}
	m_colored_labels->setEnabled(!read_only);
	m_nozzle_texts_on_paper->setEnabled(!read_only);
}

/**
	@brief PidConfigPage::readTextWidgets
	Take what the tab of the texts shows
*/
void PidConfigPage::readTextWidgets()
{
	m_codification.text_sizes.clear();
	for (auto it = m_text_sizes.cbegin() ; it != m_text_sizes.cend() ; ++it) {
		if (it.value()->value() > 0) {
			m_codification.text_sizes.insert(it.key(), it.value()->value());
		}
	}
	m_codification.colored_labels = m_colored_labels->isChecked();
	m_codification.nozzle_texts_on_paper = m_nozzle_texts_on_paper->isChecked();
}

/**
	@brief PidConfigPage::saveAsDefaultPalette
	See the declaration
*/
void PidConfigPage::saveAsDefaultPalette()
{
	QetSettings::setPidPalette(m_codification.paletteToCsv());
	m_palette_saved->setText(m_codification.hasColors() || m_codification.hasTextSettings()
							 ? tr("Palette saved") : tr("Default palette removed"));
}

/**
	@brief PidConfigPage::eventFilter
	A press in the colour of a line of a selection of several would end that
	selection: it asks the colour of all of them instead.
*/
bool PidConfigPage::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == m_table->viewport() && event->type() == QEvent::MouseButtonPress)
	{
		const auto *mouse = static_cast<QMouseEvent *>(event);
		const QModelIndex index = m_table->indexAt(mouse->position().toPoint());
		if (mouse->button() == Qt::LeftButton && mouse->modifiers() == Qt::NoModifier
			&& index.isValid() && index.column() == color_column
			&& selectedRows().size() > 1 && selectedRows().contains(index.row()))
		{
			chooseColor();
			return true;
		}
	}
	return ProjectConfigPage::eventFilter(watched, event);
}

/**
	@return the colours of @p group
*/
QMap<QString, QColor> &PidConfigPage::colors(Group group)
{
	switch (group) {
		case FluidFamily:  return m_codification.fluid_family_colors;
		case ObjectFamily: return m_codification.object_colors;
		case Status:       return m_codification.status_colors;
	}
	return m_codification.status_colors;
}

/**
	@brief PidConfigPage::fillTable
	One line per family of fluid of the codification (or with a colour), per
	family of object and per status
*/
void PidConfigPage::fillTable()
{
	const QList<int> rows_selected = selectedRows();
	m_table->setRowCount(0);
	auto add = [this](Group group, const QString &group_name, const QString &key, const QString &name) {
		const int row = m_table->rowCount();
		m_table->insertRow(row);
		auto *what = new QTableWidgetItem(group_name);
		what->setData(group_role, int(group));
		what->setData(key_role, key);
		m_table->setItem(row, 0, what);
		m_table->setItem(row, 1, new QTableWidgetItem(name));
		auto *cell = new QTableWidgetItem();
		const QColor color = colors(group).value(key);
		if (color.isValid()) {
			cell->setBackground(color);
			cell->setText(color.name());
			cell->setForeground(color.lightness() < 128 ? Qt::white : Qt::black);
		}
		m_table->setItem(row, 2, cell);
	};

	QStringList families = m_codification.fluid_family_colors.keys();
	for (const PidCodification::Entry &fluid : std::as_const(m_codification.fluids)) {
		if (!fluid.family.isEmpty() && !families.contains(fluid.family)) {
			families << fluid.family;
		}
	}
	families.sort();
	for (const QString &family : std::as_const(families)) {
		add(FluidFamily, tr("Family of fluids"), family, family);
	}

	const QStringList objects{tr("Pipe lines"), tr("Equipment"), tr("Valves"),
							  tr("Instruments"), tr("Signal lines")};
	const QStringList object_keys = PidCodification::objectFamilies();
	for (int i = 0 ; i < object_keys.size() ; ++i) {
		add(ObjectFamily, tr("Family of objects"), object_keys.at(i), objects.at(i));
	}

	const QStringList statuses{tr("Existing, kept"), tr("To be demolished")};
	const QStringList status_keys = PidCodification::statuses();
	for (int i = 0 ; i < status_keys.size() ; ++i) {
		add(Status, tr("Status of works"), status_keys.at(i), statuses.at(i));
	}
	m_table->resizeColumnsToContents();
		//The lines keep their place: what was selected stays so
	for (int row : rows_selected) {
		if (row >= m_table->rowCount()) {
			continue;
		}
		for (int column = 0 ; column < m_table->columnCount() ; ++column) {
			m_table->item(row, column)->setSelected(true);
		}
	}
}

/**
	@return the lines selected, in order
*/
QList<int> PidConfigPage::selectedRows() const
{
	QList<int> rows;
	for (const QModelIndex &index : m_table->selectionModel()->selectedRows()) {
		rows << index.row();
	}
	std::sort(rows.begin(), rows.end());
	return rows;
}

/**
	@brief PidConfigPage::setColor
	See the declaration
*/
void PidConfigPage::setColor(Group group, const QString &key, const QColor &color)
{
	if (color.isValid()) {
		colors(group).insert(key, color);
	} else {
		colors(group).remove(key);
	}
	fillTable();
}

/**
	@brief PidConfigPage::chooseColor
	Ask a colour for the lines selected, starting from that of the first
*/
void PidConfigPage::chooseColor()
{
	const QList<int> rows = selectedRows();
	if (rows.isEmpty()) {
		return;
	}
	const QTableWidgetItem *first = m_table->item(rows.first(), 0);
	const auto group = Group(first->data(group_role).toInt());
	const QColor current = colors(group).value(first->data(key_role).toString(), Qt::black);
	const QColor color = m_ask_color ? m_ask_color(current)
									 : QColorDialog::getColor(current, this, title());
	if (color.isValid()) {
		setColorOfSelection(color);
	}
}

/**
	@brief PidConfigPage::setColorOfSelection
	See the declaration
*/
void PidConfigPage::setColorOfSelection(const QColor &color)
{
	for (int row : selectedRows())
	{
		const QTableWidgetItem *item = m_table->item(row, 0);
		const auto group = Group(item->data(group_role).toInt());
		const QString key = item->data(key_role).toString();
		if (color.isValid()) {
			colors(group).insert(key, color);
		} else {
			colors(group).remove(key);
		}
	}
	fillTable();
}
