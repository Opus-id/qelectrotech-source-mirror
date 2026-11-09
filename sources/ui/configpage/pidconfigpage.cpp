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

#include "../../pipeproperties.h"
#include "../../qeticons.h"
#include "../../qetproject.h"
#include "../../undocommand/changepidcodificationcommand.h"
#include "../../utils/qetsettings.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QMessageBox>
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
		/// On the code of a line of codes: the code it has in the project,
		/// none for a line added or imported
	constexpr int original_role = Qt::UserRole + 2;
		/// On the code of a line of codes: its last code accepted
	constexpr int previous_role = Qt::UserRole + 3;

	const QList<PidCodification::List> code_lists{PidCodification::List::Fluids,
												  PidCodification::List::Materials,
												  PidCodification::List::Units};

	QString codeOf(const QTableWidget *table, int row)
	{
		const QTableWidgetItem *item = table->item(row, 0);
		return item ? item->text().trimmed() : QString();
	}

		/// What the cell "Named by" of a material says
	void showNamedBy(QTableWidgetItem *item, bool outside)
	{
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
		item->setCheckState(outside ? Qt::Checked : Qt::Unchecked);
		item->setText(outside ? QString(PipeDiameter::outsideSign()) : QStringLiteral("DN"));
	}

		/// A cell of a table of codes, sorted in the natural order of the
		/// codes: W2 before W10
	class CodeItem : public QTableWidgetItem
	{
		public:
			using QTableWidgetItem::QTableWidgetItem;
			bool operator<(const QTableWidgetItem &other) const override {
				return PidCodification::codeLessThan(text(), other.text());
			}
			QTableWidgetItem *clone() const override { return new CodeItem(*this); }
	};
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
	m_project->undoStack()->push(new ChangePidCodificationCommand(m_project, m_codification,
																  codeChanges()));
}

void PidConfigPage::initWidgets()
{
	m_code_tabs = new QTabWidget(this);
	m_code_tabs->setObjectName(QStringLiteral("code_lists"));
	const QStringList list_names{tr("Fluids"), tr("Materials"), tr("Functional units")};
	const QStringList object_names{QStringLiteral("fluids"), QStringLiteral("materials"),
								   QStringLiteral("units")};
	for (int i = 0 ; i < code_lists.size() ; ++i)
	{
		const PidCodification::List list = code_lists.at(i);
		const bool fluids = list == PidCodification::List::Fluids;
		const bool materials = list == PidCodification::List::Materials;
		auto *table = new QTableWidget(0, fluids ? 3 : materials ? 4 : 2, this);
		table->setObjectName(object_names.at(i));
		table->setHorizontalHeaderLabels(
					fluids ? QStringList{tr("Code"), tr("Name"), tr("Family")}
						   : materials ? QStringList{tr("Code"), tr("Name"), tr("Named by"), tr("Series")}
									   : QStringList{tr("Code"), tr("Name")});
		if (materials) {
			table->horizontalHeaderItem(2)->setToolTip(
						tr("Checked: the pipes of this material are named by their outside diameter "
						   "(Ø 110), as plastic pipes; else by their DN, as metal pipes."));
			table->horizontalHeaderItem(3)->setToolTip(
						tr("The sizes of this material found in trade, separated by /, such as "
						   "25/32/40/50/63; a tenth for an outside diameter, such as 88,9. Its pipes "
						   "take no other: without a series, none."));
		}
		table->setSelectionBehavior(QAbstractItemView::SelectRows);
		table->setSelectionMode(QAbstractItemView::ExtendedSelection);
		table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
							   | QAbstractItemView::AnyKeyPressed);
		table->verticalHeader()->hide();
		table->horizontalHeader()->setStretchLastSection(true);
			//Sorted by code; a click on a heading sorts by another column
		table->setItemPrototype(new CodeItem());
		table->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
		table->horizontalHeader()->setSortIndicatorShown(true);
		connect(table, &QTableWidget::itemChanged, this, &PidConfigPage::codeItemChanged);
		m_code_tables.insert(list, table);
		m_code_tabs->addTab(table, list_names.at(i));
	}
	m_add_entry = new QPushButton(tr("Add"), this);
	m_add_entry->setObjectName(QStringLiteral("add_entry"));
	m_remove_entry = new QPushButton(tr("Remove"), this);
	m_remove_entry->setObjectName(QStringLiteral("remove_entry"));
	m_import = new QPushButton(tr("Import a CSV file..."), this);
	m_export = new QPushButton(tr("Export as a CSV file..."), this);
	m_export->setToolTip(tr("The codes, the colors and the texts of this page, which another "
							"project can import."));
	m_code_message = new QLabel(this);
	m_code_message->setObjectName(QStringLiteral("codification_message"));
	m_code_message->setWordWrap(true);
		//The buttons under the tabs act on the tab shown
	connect(m_add_entry, &QPushButton::clicked, this, [this]() {
		if (m_code_tabs->currentWidget() == m_diameters_tab) {
			addDiameter(m_od_table);
		} else {
			addEntry();
		}
	});
	connect(m_remove_entry, &QPushButton::clicked, this, [this]() {
		if (m_code_tabs->currentWidget() == m_diameters_tab) {
			removeSelectedDiameters(m_od_table);
		} else {
			removeSelectedEntries();
		}
	});

		//The table joining the outside diameters of the project to the DN
		//each may be joined to
	m_od_table = new QTableWidget(0, 2, this);
	m_od_table->setObjectName(QStringLiteral("outside_diameters"));
	m_od_table->setHorizontalHeaderLabels({tr("Outside diameter (Ø)"), tr("DN it may be joined to")});
	m_od_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_od_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_od_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
								| QAbstractItemView::AnyKeyPressed);
	m_od_table->verticalHeader()->hide();
	m_od_table->horizontalHeader()->setStretchLastSection(true);
	m_od_table->setItemPrototype(new CodeItem());
	m_od_table->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
	m_od_table->horizontalHeader()->setSortIndicatorShown(true);
	connect(m_od_table, &QTableWidget::itemChanged, this, &PidConfigPage::diameterItemChanged);
	m_od_table->horizontalHeaderItem(1)->setToolTip(
				tr("The DN of the flanges a pipe of this outside diameter may be joined to, separated "
				   "by /, such as 100/125. Empty: a nozzle of another kind is marked."));
	m_diameters_tab = new QWidget(this);
	m_diameters_tab->setObjectName(QStringLiteral("diameters"));
	auto *diameters_layout = new QVBoxLayout(m_diameters_tab);
	auto *diameters_help = new QLabel(
				tr("Which DN an outside diameter is joined to (a collar and flange): a nozzle and a pipe "
				   "the table does not join are marked, and a pipe whose material changes from one kind "
				   "to the other takes the size the table gives. The sizes themselves are the series of "
				   "the materials."), m_diameters_tab);
	diameters_help->setWordWrap(true);
	diameters_layout->addWidget(diameters_help);
	diameters_layout->addWidget(m_od_table);
	m_code_tabs->addTab(m_diameters_tab, tr("Diameters"));
	connect(m_import, &QPushButton::clicked, this, [this]() {
			//Several files at once: the codes, the diameters...
		const QStringList paths = QFileDialog::getOpenFileNames(
					this, tr("Import a P&ID codification"), QString(),
					tr("CSV files (*.csv *.txt);;All files (*)"));
		if (!paths.isEmpty()) {
			importCsv(paths);
		}
	});
	connect(m_export, &QPushButton::clicked, this, [this]() {
		QString path = QFileDialog::getSaveFileName(
					this, tr("Export the P&ID codification"), QString(),
					tr("CSV files (*.csv)"));
		if (path.isEmpty()) {
			return;
		}
		if (QFileInfo(path).suffix().isEmpty()) {
			path += QStringLiteral(".csv");
		}
		exportCsv(path);
	});

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
	m_save_codification = new QPushButton(tr("Save as default codification"), this);
	m_save_codification->setObjectName(QStringLiteral("save_codification"));
	m_save_codification->setToolTip(tr("New projects will start with these codes, the series of these "
									   "materials and this table of diameters; the palette is saved apart. "
									   "Projects that already exist do not change."));
	connect(m_save_codification, &QPushButton::clicked, this, &PidConfigPage::saveAsDefaultCodification);
	m_default_saved = new QLabel(this);

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
	m_show_nozzle_sizes = new QCheckBox(tr("Show the sizes of the nozzles under their names"), this);
	m_show_nozzle_sizes->setObjectName(QStringLiteral("show_nozzle_sizes"));
	connect(m_show_nozzle_sizes, &QCheckBox::toggled, this, &PidConfigPage::readTextWidgets);
	m_decimal = new QComboBox(this);
	m_decimal->setObjectName(QStringLiteral("decimal_separator"));
	m_decimal->addItem(tr("Comma (88,9)"), int(PidCodification::Decimal::Comma));
	m_decimal->addItem(tr("Point (88.9)"), int(PidCodification::Decimal::Point));
	m_decimal->setToolTip(tr("How the tenths of the sizes are written in this project, whatever the "
							 "computer which opens it; a file always keeps a point."));
	connect(m_decimal, &QComboBox::activated, this, &PidConfigPage::readTextWidgets);

	for (PipeConnection::Type type : PipeConnection::all())
	{
		if (!PipeConnection::hasMark(type)) {
			continue;
		}
		auto *drawn = new QCheckBox(PipeConnection::name(type), this);
		drawn->setObjectName(QStringLiteral("drawn_connection_") + PipeConnection::toString(type));
		connect(drawn, &QCheckBox::toggled, this, &PidConfigPage::readConnectionWidgets);
		m_drawn_connections.insert(type, drawn);
	}
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
	texts_layout->addWidget(m_show_nozzle_sizes);
	auto *decimal = new QFormLayout();
	decimal->addRow(tr("Decimal separator of the sizes:"), m_decimal);
	texts_layout->addLayout(decimal);
	texts_layout->addStretch();

	auto *connections_tab = new QWidget(this);
	auto *connections_layout = new QVBoxLayout(connections_tab);
	auto *connections_help = new QLabel(tr("The connections checked are drawn at the end of the "
										   "nozzles, 1 mm from the symbol, in its color; a pipe "
										   "arriving on a flange draws the counter-flange. A nozzle "
										   "may say otherwise (properties of the element, Nozzles). "
										   "Two symbols placed against each other draw nothing "
										   "between them."), connections_tab);
	connections_help->setWordWrap(true);
	connections_layout->addWidget(connections_help);
	for (PipeConnection::Type type : PipeConnection::all()) {
		if (QCheckBox *drawn = m_drawn_connections.value(type)) {
			connections_layout->addWidget(drawn);
		}
	}
	connections_layout->addStretch();

	auto *codes_tab = new QWidget(this);
	auto *codes_layout = new QVBoxLayout(codes_tab);
	auto *codes_help = new QLabel(tr("A pipe line keeps the code of its fluid and of its material: a name "
									 "changed here touches no pipe, a code changed is given to the pipes "
									 "and symbols bearing it when this page is applied, a code removed "
									 "stays on them as free text."), codes_tab);
	codes_help->setWordWrap(true);
	codes_layout->addWidget(codes_help);
	codes_layout->addWidget(m_code_tabs);
	auto *code_buttons = new QHBoxLayout();
	code_buttons->addWidget(m_add_entry);
	code_buttons->addWidget(m_remove_entry);
	code_buttons->addStretch();
	code_buttons->addWidget(m_import);
	code_buttons->addWidget(m_export);
	codes_layout->addLayout(code_buttons);
	codes_layout->addWidget(m_code_message);

	auto *tabs = new QTabWidget(this);
	tabs->addTab(codes_tab, tr("Codification"));
	tabs->addTab(colors_tab, tr("Colors"));
	tabs->addTab(texts_tab, tr("Texts"));
	tabs->addTab(connections_tab, tr("Connections"));

	auto *palette = new QHBoxLayout();
	palette->addStretch();
	palette->addWidget(m_default_saved);
	palette->addWidget(m_save_codification);
	palette->addWidget(m_save_palette);

	auto *page_layout = new QVBoxLayout(this);
	page_layout->addWidget(tabs);
	page_layout->addLayout(palette);
}

void PidConfigPage::readValuesFromProject()
{
	m_codification = m_project->pidCodification();
	m_code_message->clear();
	showCodification(true, true);
}

/**
	@brief PidConfigPage::showCodification
	Show the codification of this page in all its tabs
	@param fill_codes : the tables of codes are filled again
	@param codes_from_project : its codes are those of the project, which a
	code changed will be taken from
*/
void PidConfigPage::showCodification(bool fill_codes, bool codes_from_project)
{
	if (fill_codes) {
		fillCodeTables(codes_from_project);
		fillDiameterTables();
	}
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
	const QSignalBlocker sizes_blocker(m_show_nozzle_sizes);
	m_show_nozzle_sizes->setChecked(codification.show_nozzle_sizes);
		//Not set yet: that of the language, which the project will keep
	const QSignalBlocker decimal_blocker(m_decimal);
	m_decimal->setCurrentIndex(m_decimal->findData(int(codification.effectiveDecimal())));
	for (auto it = m_drawn_connections.cbegin() ; it != m_drawn_connections.cend() ; ++it) {
		const QSignalBlocker blocker(it.value());
		it.value()->setChecked(codification.drawsConnection(it.key()));
	}
}

void PidConfigPage::adjustReadOnly()
{
	const bool read_only = m_project->isReadOnly();
	for (QTableWidget *table : std::as_const(m_code_tables)) {
		table->setEditTriggers(read_only ? QAbstractItemView::NoEditTriggers
										 : QAbstractItemView::DoubleClicked
										   | QAbstractItemView::EditKeyPressed
										   | QAbstractItemView::AnyKeyPressed);
	}
	m_add_entry->setEnabled(!read_only);
	m_remove_entry->setEnabled(!read_only);
	m_od_table->setEditTriggers(read_only ? QAbstractItemView::NoEditTriggers
										  : QAbstractItemView::DoubleClicked
											| QAbstractItemView::EditKeyPressed
											| QAbstractItemView::AnyKeyPressed);
	m_show_nozzle_sizes->setEnabled(!read_only);
	m_decimal->setEnabled(!read_only);
	m_import->setEnabled(!read_only);
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
	for (QCheckBox *drawn : std::as_const(m_drawn_connections)) {
		drawn->setEnabled(!read_only);
	}
}

/**
	@brief PidConfigPage::readConnectionWidgets
	Take what the tab of the connections shows, in the order of the lists
*/
void PidConfigPage::readConnectionWidgets()
{
	m_codification.drawn_connections.clear();
	for (PipeConnection::Type type : PipeConnection::all()) {
		const QCheckBox *drawn = m_drawn_connections.value(type);
		if (drawn && drawn->isChecked()) {
			m_codification.drawn_connections << type;
		}
	}
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
	m_codification.show_nozzle_sizes = m_show_nozzle_sizes->isChecked();
		//Set only when it is chosen: else it waits to be fixed with codes
	const auto decimal = PidCodification::Decimal(m_decimal->currentData().toInt());
	if (decimal != m_codification.effectiveDecimal()) {
		m_codification.decimal = decimal;
			//The sizes of the page are written with it
		showSeries();
		fillDiameterTables();
	}
}

/**
	@brief PidConfigPage::showSeries
	Write in its cell the series of each material, with the separator of the
	page; a material without series is in orange
*/
void PidConfigPage::showSeries()
{
	QTableWidget *table = m_code_tables.value(PidCodification::List::Materials);
	const QSignalBlocker blocker(table);
	const QLocale locale = m_codification.locale();
	for (int row = 0 ; row < table->rowCount() ; ++row)
	{
		const PidCodification::Entry *entry = m_codification.material(codeOf(table, row));
		QTableWidgetItem *series = table->item(row, 3);
		if (!series) {
			continue;
		}
		if (entry) {
			series->setText(PidCodification::seriesToText(entry->series, locale));
			series->setData(previous_role, series->text());
		}
		const bool missing = !codeOf(table, row).isEmpty() && (!entry || entry->series.isEmpty());
		for (int column = 0 ; column < table->columnCount() ; ++column) {
			if (QTableWidgetItem *item = table->item(row, column)) {
				item->setBackground(missing ? QBrush(QColor(255, 200, 120)) : QBrush());
			}
		}
		series->setToolTip(missing ? tr("Without a series, no pipe of this material can be given a size.")
								   : QString());
	}
}

/**
	@brief PidConfigPage::saveAsDefaultPalette
	See the declaration
*/
void PidConfigPage::saveAsDefaultPalette()
{
	QetSettings::setPidPalette(m_codification.paletteToCsv());
	m_default_saved->setText(m_codification.hasColors() || m_codification.hasTextSettings()
							 ? tr("Palette saved") : tr("Default palette removed"));
}

/**
	@brief PidConfigPage::saveAsDefaultCodification
	See the declaration
*/
void PidConfigPage::saveAsDefaultCodification()
{
	const bool codes = m_codification.hasCodes();
	QetSettings::setPidDefaultCodification(codes ? m_codification.codesToCsv() : QString());
	m_default_saved->setText(codes ? tr("Codification saved") : tr("Default codification removed"));
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

/**
	@brief PidConfigPage::fillCodeTables
	One line per entry of each list of codes of the page
	@param from_project : the codes are those of the project; otherwise
	(a file read) no line has a former code, nothing is renamed
*/
void PidConfigPage::fillCodeTables(bool from_project)
{
	for (PidCodification::List list : code_lists)
	{
		QTableWidget *table = m_code_tables.value(list);
		const QSignalBlocker blocker(table);
			//A line moved while it is filled would get the cells of another
		table->setSortingEnabled(false);
		table->setRowCount(0);
		for (const PidCodification::Entry &entry : m_codification.entries(list))
		{
			const int row = table->rowCount();
			table->insertRow(row);
			auto *code = new CodeItem(entry.code);
			code->setData(original_role, from_project ? entry.code : QString());
			code->setData(previous_role, entry.code);
			table->setItem(row, 0, code);
			table->setItem(row, 1, new CodeItem(entry.name));
			if (list == PidCodification::List::Materials) {
				auto *named = new CodeItem();
				showNamedBy(named, entry.outside_diameter);
				table->setItem(row, 2, named);
				table->setItem(row, 3, new CodeItem());
			} else if (table->columnCount() > 2) {
				table->setItem(row, 2, new CodeItem(entry.family));
			}
		}
		table->setSortingEnabled(true);
		table->resizeColumnToContents(0);
	}
	showSeries();
}

/**
	@brief PidConfigPage::readCodeTable
	Take the list @p list from its table; a line without a code yet (just
	added) is not an entry
*/
void PidConfigPage::readCodeTable(PidCodification::List list)
{
	const QTableWidget *table = m_code_tables.value(list);
	QList<PidCodification::Entry> entries;
	for (int row = 0 ; row < table->rowCount() ; ++row)
	{
		PidCodification::Entry entry;
		entry.code = codeOf(table, row);
		if (entry.code.isEmpty()) {
			continue;
		}
		const QTableWidgetItem *name = table->item(row, 1);
		const QTableWidgetItem *third = table->item(row, 2);
		entry.name = name ? name->text().trimmed() : QString();
		if (list == PidCodification::List::Materials) {
			entry.outside_diameter = third && third->checkState() == Qt::Checked;
			const QTableWidgetItem *series = table->item(row, 3);
				//A DN is whole: a tenth left after a change of kind is dropped
			for (int tenths : PidCodification::seriesFromText(series ? series->text() : QString())) {
				if (entry.outside_diameter || tenths % 10 == 0) {
					entry.series << tenths;
				}
			}
		} else {
			entry.family = third ? third->text().trimmed() : QString();
		}
		entries << entry;
	}
	m_codification.entries(list) = entries;
		//The families of the fluids have a colour
	if (list == PidCodification::List::Fluids) {
		fillTable();
	}
	if (list == PidCodification::List::Materials) {
		showSeries();
	}
}

/**
	@brief PidConfigPage::codeItemChanged
	A code may be neither empty nor twice in its list: it gets its last one
	back
*/
void PidConfigPage::codeItemChanged(QTableWidgetItem *item)
{
	QTableWidget *table = item->tableWidget();
	const PidCodification::List list = m_code_tables.key(table);
	if (list == PidCodification::List::Materials && item->column() == 2)
	{
			//Its text follows the box
		const QSignalBlocker blocker(table);
		showNamedBy(item, item->checkState() == Qt::Checked);
	}
	if (list == PidCodification::List::Materials && item->column() == 3)
	{
			//Sizes separated by /, whole for a DN
		const QTableWidgetItem *named = table->item(item->row(), 2);
		const bool outside = named && named->checkState() == Qt::Checked;
		bool ok = false;
		const QList<int> sizes = PidCodification::seriesFromText(item->text(), &ok);
		const bool whole = std::all_of(sizes.cbegin(), sizes.cend(), [](int tenths) { return tenths % 10 == 0; });
		if (!ok || (!outside && !whole)) {
			const QSignalBlocker blocker(table);
			item->setText(item->data(previous_role).toString());
			m_code_message->setText(tr("Write the sizes separated by /, such as 25/32/40; a tenth "
									   "after a comma for an outside diameter only, such as 88,9."));
			return;
		}
		m_code_message->clear();
	}
	if (item->column() == 0)
	{
		const QString code = item->text().trimmed();
		const QString previous = item->data(previous_role).toString();
		QString refused;
		if (code.isEmpty() && !previous.isEmpty()) {
			refused = tr("A code cannot be empty.");
		}
		for (int row = 0 ; row < table->rowCount() && refused.isEmpty() && !code.isEmpty() ; ++row) {
			if (row != item->row() && codeOf(table, row) == code) {
				refused = tr("The code %1 is already in this list.").arg(code);
			}
		}
		const QSignalBlocker blocker(table);
		if (!refused.isEmpty()) {
			item->setText(previous);
			m_code_message->setText(refused);
			return;
		}
		item->setText(code);
		item->setData(previous_role, code);
			//A line added takes its place now, and stays in view
		if (!code.isEmpty() && !table->isSortingEnabled()) {
			table->setSortingEnabled(true);
		}
		table->scrollToItem(item);
		const QString original = item->data(original_role).toString();
		m_code_message->setText(!original.isEmpty() && original != code
								? tr("The pipes and the symbols bearing the code %1 will take the code %2 "
									 "when this page is applied.").arg(original, code)
								: QString());
	}
	readCodeTable(list);
}

/**
	@return the list of codes shown
*/
PidCodification::List PidConfigPage::shownList() const
{
	return code_lists.value(m_code_tabs->currentIndex(), PidCodification::List::Fluids);
}

/**
	@brief PidConfigPage::addEntry
	See the declaration
*/
void PidConfigPage::addEntry()
{
	QTableWidget *table = m_code_tables.value(shownList());
	const int row = table->rowCount();
	{
		const QSignalBlocker blocker(table);
			//The line stays at the bottom until it has its code
		table->setSortingEnabled(false);
		table->insertRow(row);
		for (int column = 0 ; column < table->columnCount() ; ++column) {
			table->setItem(row, column, new CodeItem());
		}
		if (shownList() == PidCodification::List::Materials) {
			showNamedBy(table->item(row, 2), false);
		}
	}
	table->setCurrentCell(row, 0);
	table->editItem(table->item(row, 0));
}

/**
	@brief PidConfigPage::fillDiameterTables
	One line per outside diameter of the table of the page
*/
void PidConfigPage::fillDiameterTables()
{
	const QSignalBlocker blocker(m_od_table);
	m_od_table->setSortingEnabled(false);
	m_od_table->setRowCount(0);
	const QLocale locale = m_codification.locale();
	for (auto it = m_codification.outside_diameters.cbegin() ;
		 it != m_codification.outside_diameters.cend() ; ++it)
	{
		const int row = m_od_table->rowCount();
		m_od_table->insertRow(row);
		auto *item = new CodeItem(PipeDiameter::number(it.key(), locale));
		item->setData(previous_role, item->text());
		auto *joined = new CodeItem(PidCodification::diametersToText(it.value()));
		joined->setData(previous_role, joined->text());
		m_od_table->setItem(row, 0, item);
		m_od_table->setItem(row, 1, joined);
	}
	m_od_table->setSortingEnabled(true);
}

/**
	@brief PidConfigPage::diameterItemChanged
	An outside diameter is a number to the tenth, once in the table; the DN
	joined to it are whole numbers separated by /. Anything else gets its last
	value back.
*/
void PidConfigPage::diameterItemChanged(QTableWidgetItem *item)
{
	QTableWidget *table = item->tableWidget();
	const QString text = item->text().trimmed();
	const QString previous = item->data(previous_role).toString();
	QString refused, written;
	if (item->column() == 0)
	{
		const int value = PipeDiameter::tenthsOf(text);
		written = PipeDiameter::number(value, m_codification.locale());
		if (text.isEmpty() && previous.isEmpty()) {
			return;
		} else if (!value) {
			refused = tr("An outside diameter is a number of millimetres, to the tenth.");
		}
		for (int row = 0 ; row < table->rowCount() && refused.isEmpty() ; ++row) {
			if (row != item->row() && PipeDiameter::tenthsOf(codeOf(table, row)) == value) {
				refused = tr("%1 is already in this list.").arg(written);
			}
		}
	}
	else
	{
		bool ok = false;
		written = PidCodification::diametersToText(PidCodification::diametersFromText(text, &ok));
		if (!ok) {
			refused = tr("Write the DN separated by /, such as 100/125.");
		}
	}
	{
		const QSignalBlocker blocker(table);
		if (!refused.isEmpty()) {
			item->setText(previous);
		} else {
			item->setText(written);
			item->setData(previous_role, written);
		}
		if (item->column() == 0 && !item->text().isEmpty() && !table->isSortingEnabled()) {
			table->setSortingEnabled(true);
			table->scrollToItem(item);
		}
	}
	m_code_message->setText(refused);
	readDiameterTables();
}

/**
	@brief PidConfigPage::readDiameterTables
	Take the table of the page; a line without a diameter yet (just added)
	is none
*/
void PidConfigPage::readDiameterTables()
{
	m_codification.outside_diameters.clear();
	for (int row = 0 ; row < m_od_table->rowCount() ; ++row)
	{
		const int value = PipeDiameter::tenthsOf(codeOf(m_od_table, row));
		const QTableWidgetItem *joined = m_od_table->item(row, 1);
		if (value) {
			m_codification.outside_diameters.insert(
						value, PidCodification::diametersFromText(joined ? joined->text() : QString()));
		}
	}
}

/**
	@brief PidConfigPage::addDiameter
	See the declaration
*/
void PidConfigPage::addDiameter(QTableWidget *table)
{
	const int row = table->rowCount();
	{
		const QSignalBlocker blocker(table);
			//The line stays at the bottom until it has its diameter
		table->setSortingEnabled(false);
		table->insertRow(row);
		for (int column = 0 ; column < table->columnCount() ; ++column) {
			table->setItem(row, column, new CodeItem());
		}
	}
	table->setCurrentCell(row, 0);
	table->editItem(table->item(row, 0));
}

/**
	@brief PidConfigPage::removeSelectedDiameters
	See the declaration
*/
void PidConfigPage::removeSelectedDiameters(QTableWidget *table)
{
	QList<int> rows;
	for (const QModelIndex &index : table->selectionModel()->selectedRows()) {
		rows << index.row();
	}
	std::sort(rows.begin(), rows.end(), std::greater<int>());
	{
		const QSignalBlocker blocker(table);
		for (int row : std::as_const(rows)) {
			table->removeRow(row);
		}
	}
	m_code_message->clear();
	readDiameterTables();
}

/**
	@brief PidConfigPage::codeMessage
	See the declaration
*/
QString PidConfigPage::codeMessage() const
{
	return m_code_message->text();
}

/**
	@brief PidConfigPage::removeSelectedEntries
	See the declaration
*/
void PidConfigPage::removeSelectedEntries()
{
	const PidCodification::List list = shownList();
	QTableWidget *table = m_code_tables.value(list);
	QList<int> rows;
	for (const QModelIndex &index : table->selectionModel()->selectedRows()) {
		rows << index.row();
	}
	if (rows.isEmpty()) {
		return;
	}
	int users = 0;
	for (int row : std::as_const(rows)) {
		users += ChangePidCodificationCommand::usersOf(
					m_project, list, table->item(row, 0)->data(original_role).toString());
	}
	if (users)
	{
		const QString question =
				list == PidCodification::List::Fluids
				? tr("%n pipe line(s) or valve(s) bear the codes removed: they will keep them as "
					 "free text. Remove them?", "", users)
				: list == PidCodification::List::Materials
				  ? tr("%n pipe line(s) bear the codes removed: they will keep them as free text. "
					   "Remove them?", "", users)
				  : tr("%n symbol(s) bear the codes removed: they will keep them as free text. "
					   "Remove them?", "", users);
		if (!confirm(question)) {
			return;
		}
	}
	std::sort(rows.begin(), rows.end(), std::greater<int>());
	{
		const QSignalBlocker blocker(table);
		for (int row : std::as_const(rows)) {
			table->removeRow(row);
		}
	}
	m_code_message->clear();
	readCodeTable(list);
}

/**
	@brief PidConfigPage::codeChanges
	See the declaration
*/
ChangePidCodificationCommand::CodeChanges PidConfigPage::codeChanges() const
{
	ChangePidCodificationCommand::CodeChanges changes;
	for (PidCodification::List list : code_lists)
	{
		const QTableWidget *table = m_code_tables.value(list);
		QMap<QString, QString> &map = list == PidCodification::List::Fluids ? changes.fluids
									: list == PidCodification::List::Materials ? changes.materials
																			   : changes.units;
		for (int row = 0 ; row < table->rowCount() ; ++row)
		{
			const QString original = table->item(row, 0)->data(original_role).toString();
			const QString code = codeOf(table, row);
			if (!original.isEmpty() && !code.isEmpty() && code != original) {
				map.insert(original, code);
			}
		}
	}
	return changes;
}

/**
	@brief PidConfigPage::confirm
	@return the answer of the user to @p question
*/
bool PidConfigPage::confirm(const QString &question)
{
	if (m_confirm) {
		return m_confirm(question);
	}
	return QMessageBox::question(this, title(), question, QMessageBox::Yes | QMessageBox::No)
			== QMessageBox::Yes;
}

/**
	@brief PidConfigPage::importCsv
	See the declaration
*/
bool PidConfigPage::importCsv(const QString &path)
{
	return importCsv(QStringList{path});
}

/**
	@brief PidConfigPage::importCsv
	See the declaration: the files read one after the other, as one
*/
bool PidConfigPage::importCsv(const QStringList &paths)
{
	QStringList unreadable;
	const QString text = PidCodification::textOfFiles(paths, &unreadable);
	const QString path = paths.join(QStringLiteral(", "));
	if (!unreadable.isEmpty()) {
		m_code_message->setText(tr("The file %1 could not be read.").arg(unreadable.join(QStringLiteral(", "))));
		return false;
	}
	int skipped = 0;
	const PidCodification read = PidCodification::fromCsv(text, &skipped);
	if (read.isEmpty()) {
		m_code_message->setText(tr("Nothing could be read from %1. Each line is expected as: "
								   "kind;code;name;family, kind being \"fluid\", \"material\" "
								   "or \"unit\".").arg(path));
		return false;
	}
	if (read.hasCodeLists() && m_codification.hasCodeLists()
		&& !confirm(tr("Replace the codes of this page with %1 fluids, %2 materials and %3 "
					   "functional units?").arg(read.fluids.size()).arg(read.materials.size())
					.arg(read.functional_units.size()))) {
		return false;
	}
	if (read.hasDiameters() && m_codification.hasDiameters()
		&& !confirm(tr("Replace the table of the outside diameters of this page with %n line(s)?", "",
					   int(read.outside_diameters.size())))) {
		return false;
	}
	m_codification = read.completedWith(m_codification);
		//A code read is no code renamed: no line keeps a former code
	if (read.hasCodeLists()) {
		fillCodeTables(false);
	}
	showCodification(false, false);
	if (read.hasDiameters()) {
		fillDiameterTables();
	}
	QString read_text = tr("%1 fluids, %2 materials and %3 functional units read")
						.arg(read.fluids.size()).arg(read.materials.size()).arg(read.functional_units.size());
	if (read.hasDiameters()) {
		read_text += tr(", with %n outside diameter(s) in the table", "", int(read.outside_diameters.size()));
	}
	QString message = tr("%1: apply this page to keep them.").arg(read_text);
	if (skipped) {
		message += QStringLiteral(" ") + tr("%n line(s) could not be read.", "", skipped);
	}
		//A material without series: none of its pipes can have a size
	const QStringList without = read.hasCodeLists() ? m_codification.materialsWithoutSeries() : QStringList();
	if (!without.isEmpty()) {
		message += QStringLiteral(" ") + tr("Materials without series: %1.").arg(without.join(QStringLiteral(", ")));
	}
	m_code_message->setText(message);
	return true;
}

/**
	@brief PidConfigPage::exportCsv
	See the declaration
*/
bool PidConfigPage::exportCsv(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		m_code_message->setText(tr("The file %1 could not be written.").arg(path));
		return false;
	}
		//With its mark of UTF-8, a spreadsheet reads the accents
	file.write("\xEF\xBB\xBF");
	file.write(m_codification.toCsv().toUtf8());
	file.close();
	m_code_message->setText(tr("Codification written in %1.").arg(path));
	return true;
}
