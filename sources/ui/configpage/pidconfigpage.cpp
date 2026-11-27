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

#include "../../ElementsCollection/elementslocation.h"
#include "../../elementdialog.h"
#include "../../pidlinedesignation.h"
#include "../../pidtag.h"
#include "../../pidtooltip.h"
#include "../../pipediameter.h"
#include "../../pipeproperties.h"
#include "../../qeticons.h"
#include "../../qetproject.h"
#include "../../undocommand/changepidcodificationcommand.h"
#include "../../utils/qetsettings.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>

namespace {
		/// The keys of the models the tab of the texts edits, in its order:
		/// the families of symbols which have a tag, then the lines
	QStringList modelFamilies()
	{
		return {QStringLiteral("equipment"), QStringLiteral("valve"),
				QStringLiteral("instrument"), PidLineDesignation::modelKey()};
	}
	constexpr int group_role = Qt::UserRole;
	constexpr int key_role = Qt::UserRole + 1;
	constexpr int color_column = 2;
		/// On the code of a line of codes: the code it has in the project,
		/// none for a line added or imported
	constexpr int original_role = Qt::UserRole + 2;
		/// On the code of a line of codes: its last code accepted
	constexpr int previous_role = Qt::UserRole + 3;
		/// On the cell "Sizes" of a material: its sizes, each a list of its
		/// outside diameter (tenths of a millimetre), of its DN, 0 for
		/// none, and of whether it is the default one of its DN. They
		/// follow the line when the table is sorted.
	constexpr int sizes_role = Qt::UserRole + 4;
	constexpr int sizes_column = 3;
		/// On a cell of the list of sizes: its outside diameter in tenths of
		/// a millimetre, its DN, 0 for none, or whether it is the default
		/// one; the text and the box of the cell only show it
	constexpr int value_role = Qt::UserRole + 5;
		//The DN, the size of the line, comes first
	constexpr int size_nominal_column = 0;
	constexpr int size_outside_column = 1;
	constexpr int size_default_column = 2;

		//A long tool tip on several lines
	using PidToolTip::wrapped;

	QVariantList sizesToData(const QList<PidCodification::Size> &sizes)
	{
		QVariantList data;
		for (const PidCodification::Size &size : sizes) {
			data.append(QVariant(QVariantList{size.outside, size.nominal, size.by_default}));
		}
		return data;
	}

	QList<PidCodification::Size> sizesFromData(const QVariant &data)
	{
		PidCodification::Entry entry;
		const QVariantList sizes = data.toList();
		for (const QVariant &size : sizes) {
			const QVariantList values = size.toList();
			entry.addSize(values.value(0).toInt(), values.value(1).toInt(), values.value(2).toBool());
		}
		return entry.sizes;
	}

		/// What the cell of the column @p column of the line @p row of a
		/// list of sizes holds, see value_role
	int sizeValue(const QTableWidget *table, int row, int column)
	{
		const QTableWidgetItem *item = table->item(row, column);
		return item ? item->data(value_role).toInt() : 0;
	}

		/// A cell "Sizes", which is not typed in
	QTableWidgetItem *sizesItem(QTableWidgetItem *item, const QList<PidCodification::Size> &sizes)
	{
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		item->setData(sizes_role, sizesToData(sizes));
		return item;
	}

	const QList<PidCodification::List> code_lists{PidCodification::List::Fluids,
												  PidCodification::List::Materials,
												  PidCodification::List::Units,
												  PidCodification::List::Letters};

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

		/// The size of the picture of a mark of signal line, in a list
	const QSize mark_icon_size(72, 20);

		/// A piece of signal line with its mark @p mark, drawn in @p ink as
		/// a folio draws it (Conductor::paint()); @p ratio is the pixel
		/// ratio of the screen
	QPixmap markPixmap(SignalMark::Mark mark, const QColor &ink, qreal ratio)
	{
		QPixmap pixmap(mark_icon_size * ratio);
		pixmap.setDevicePixelRatio(ratio);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing, true);
			//A mark, 10 drawing units high, takes most of the height
		const qreal scale = 1.5;
		painter.translate(0, mark_icon_size.height() / 2.0);
		painter.scale(scale, scale);
		const QList<QPointF> points{QPointF(0, 0), QPointF(mark_icon_size.width() / scale, 0)};
		const QPen pen(ink, 1.0, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
		QPen line = pen;
		if (mark == SignalMark::Dashes) {
			SignalMark::applyDashes(line);
		}
		painter.setBrush(Qt::NoBrush);
		painter.setPen(line);
		painter.drawPath(SignalMark::brokenLine(mark, points));
		painter.setPen(pen);
		painter.drawPath(SignalMark::marksPath(mark, points));
		painter.end();
		return pixmap;
	}

		/// The picture of the mark @p mark for a list of marks, in the
		/// colours of the texts of @p palette: that of a line selected too,
		/// which Qt would else make from the other one, hardly seen
	QIcon markIcon(SignalMark::Mark mark, const QPalette &palette, qreal ratio)
	{
		QIcon icon(markPixmap(mark, palette.color(QPalette::Text), ratio));
		icon.addPixmap(markPixmap(mark, palette.color(QPalette::HighlightedText), ratio),
					   QIcon::Selected);
		return icon;
	}
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
	const QStringList list_names{tr("Fluids"), tr("Materials"), tr("Functional units"),
								 tr("Instrument letters")};
	const QStringList object_names{QStringLiteral("fluids"), QStringLiteral("materials"),
								   QStringLiteral("units"), QStringLiteral("letters")};
	for (int i = 0 ; i < code_lists.size() ; ++i)
	{
		const PidCodification::List list = code_lists.at(i);
		const bool fluids = list == PidCodification::List::Fluids;
		const bool materials = list == PidCodification::List::Materials;
		auto *table = new QTableWidget(0, fluids ? 3 : materials ? 4 : 2, this);
		table->setObjectName(object_names.at(i));
		table->setHorizontalHeaderLabels(
					fluids ? QStringList{tr("Code"), tr("Name"), tr("Family")}
						   : materials ? QStringList{tr("Code"), tr("Name"), tr("Named by"), tr("Sizes")}
									   : list == PidCodification::List::Letters
										 ? QStringList{tr("Letters"), tr("Meaning")}
										 : QStringList{tr("Code"), tr("Name")});
		if (list == PidCodification::List::Letters) {
			table->setToolTip(wrapped(tr("The letters of identification of the instruments, offered "
										 "while the letters of a tag are typed: a help only. A tag may "
										 "have others, and changing or removing letters here changes "
										 "no tag.")));
		}
		if (materials) {
			table->horizontalHeaderItem(2)->setToolTip(wrapped(
						tr("Checked: the pipes of this material are named by their outside diameter "
						   "(Ø 110), as plastic pipes; else by their DN, as metal pipes.")));
			table->horizontalHeaderItem(sizes_column)->setToolTip(wrapped(
						tr("The sizes of this material found in trade: the first and the last of "
						   "those which name its pipes, and how many. Its pipes take no other: "
						   "without sizes, none. They are typed in the list beside, which shows "
						   "those of the material of the current line, or read from a CSV file, by "
						   "lines such as size;M04;125;100 (material, outside diameter, DN).")));
		}
		table->setSelectionBehavior(QAbstractItemView::SelectRows);
		table->setSelectionMode(QAbstractItemView::ExtendedSelection);
		table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
							   | QAbstractItemView::AnyKeyPressed);
		table->verticalHeader()->hide();
			//The name takes the width left, the other columns that of what
			//they hold
		for (int column = 0 ; column < table->columnCount() ; ++column) {
			table->horizontalHeader()->setSectionResizeMode(
						column, column == 1 ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
		}
			//The family of a fluid is shown beside its code, before its name
		if (fluids) {
			table->horizontalHeader()->moveSection(2, 1);
		}
			//Sorted by code; a click on a heading sorts by another column
		table->setItemPrototype(new CodeItem());
		table->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
		table->horizontalHeader()->setSortIndicatorShown(true);
		connect(table, &QTableWidget::itemChanged, this, &PidConfigPage::codeItemChanged);
		m_code_tables.insert(list, table);
		if (!materials) {
			m_code_tabs->addTab(table, list_names.at(i));
			continue;
		}

			//Beside the materials, the sizes of the one of the current line
		m_size_table = new QTableWidget(0, 3, this);
		m_size_table->setObjectName(QStringLiteral("sizes"));
		m_size_table->setHorizontalHeaderLabels({tr("DN"), tr("Outside Ø"), tr("Default")});
		m_size_table->horizontalHeaderItem(size_outside_column)->setToolTip(wrapped(
					tr("The outside diameter of this size, in millimetres, to the tenth. A material "
					   "named by its outside diameter needs it; a material named by its DN may do "
					   "without.")));
		m_size_table->horizontalHeaderItem(size_nominal_column)->setToolTip(wrapped(
					tr("The DN of the line this size makes, that of its flange. A material named by "
					   "its DN needs it. An outside diameter has one DN only; a DN may have several "
					   "outside diameters.")));
		m_size_table->horizontalHeaderItem(size_default_column)->setToolTip(wrapped(
					tr("Where a DN has several outside diameters: the one a pipe of this DN takes "
					   "without a question. One by DN; without one, the question is asked.")));
		m_size_table->setSelectionBehavior(QAbstractItemView::SelectRows);
		m_size_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
		m_size_table->setEditTriggers(table->editTriggers());
		m_size_table->verticalHeader()->hide();
			//The box of the default size keeps the width of its title; the
			//two numbers share what is left
		m_size_table->horizontalHeader()->setSectionResizeMode(size_nominal_column, QHeaderView::Stretch);
		m_size_table->horizontalHeader()->setSectionResizeMode(size_outside_column, QHeaderView::Stretch);
		m_size_table->horizontalHeader()->setSectionResizeMode(size_default_column,
																QHeaderView::ResizeToContents);
		connect(m_size_table, &QTableWidget::itemChanged, this, &PidConfigPage::sizeItemChanged);
		connect(table, &QTableWidget::currentCellChanged, this, [this](int row, int, int previous) {
			if (row != previous) {
				showMaterialSizes();
			}
		});
		m_add_size = new QPushButton(tr("Add a size"), this);
		m_add_size->setObjectName(QStringLiteral("add_size"));
		m_remove_size = new QPushButton(tr("Remove the size"), this);
		m_remove_size->setObjectName(QStringLiteral("remove_size"));
		connect(m_add_size, &QPushButton::clicked, this, &PidConfigPage::addSize);
		connect(m_remove_size, &QPushButton::clicked, this, &PidConfigPage::removeSelectedSizes);
		m_size_box = new QGroupBox(tr("Sizes"), this);
		m_size_box->setObjectName(QStringLiteral("size_box"));
		auto *box_layout = new QVBoxLayout(m_size_box);
		box_layout->addWidget(m_size_table);
		auto *size_buttons = new QHBoxLayout();
		size_buttons->addWidget(m_add_size);
		size_buttons->addWidget(m_remove_size);
		size_buttons->addStretch();
		box_layout->addLayout(size_buttons);

		m_equal_dn = new QLineEdit(this);
		m_equal_dn->setObjectName(QStringLiteral("equal_dn"));
			//Empty, it says so: its example must not be taken for a value
		m_equal_dn->setPlaceholderText(tr("None (for example 60/65)"));
		m_equal_dn->setToolTip(wrapped(tr("The DN which are the same size of line, written together: 60/65. "
								  "They compare as the same at a nozzle, and a pipe whose material "
								  "changes takes the one its new material has. Several: 60/65, 125/130.")));
		connect(m_equal_dn, &QLineEdit::editingFinished, this, &PidConfigPage::equalDnEdited);

		auto *splitter = new QSplitter(Qt::Horizontal, this);
		splitter->addWidget(table);
		splitter->addWidget(m_size_box);
			//A wider page gives its width to the materials: the list of
			//sizes, a few numbers, keeps its own unless the handle is dragged
		splitter->setStretchFactor(0, 1);
		splitter->setStretchFactor(1, 0);
		splitter->setChildrenCollapsible(false);
		auto *materials_tab = new QWidget(this);
		auto *tab_layout = new QVBoxLayout(materials_tab);
		tab_layout->addWidget(splitter);
			//Wide enough for a few groups, no more
		m_equal_dn->setMinimumWidth(m_equal_dn->fontMetrics().horizontalAdvance(
										QStringLiteral("60/65, 125/130, 200/225, 250/280")));
		auto *equal_row = new QHBoxLayout();
		equal_row->addWidget(m_equal_dn);
		equal_row->addStretch(1);
		auto *equal = new QFormLayout();
		equal->addRow(tr("Equal DN:"), equal_row);
		tab_layout->addLayout(equal);
		m_code_tabs->addTab(materials_tab, list_names.at(i));
	}
	m_add_entry = new QPushButton(tr("Add"), this);
	m_add_entry->setObjectName(QStringLiteral("add_entry"));
	m_remove_entry = new QPushButton(tr("Remove"), this);
	m_remove_entry->setObjectName(QStringLiteral("remove_entry"));
		//A file holds any part of the page: read and written under all the
		//tabs, where the page says what was read
	m_import = new QPushButton(tr("Import a CSV file..."), this);
	m_import->setObjectName(QStringLiteral("import_csv"));
	m_import->setToolTip(wrapped(tr("Reads into this page what the file has: codes, sizes of materials, "
							"colors, texts, connections, marks of the signal lines. What the "
							"file has not stays as it is.")));
	m_export = new QPushButton(tr("Export as a CSV file..."), this);
	m_export->setObjectName(QStringLiteral("export_csv"));
	m_export->setToolTip(wrapped(tr("The codes, the colors and the texts of this page, which another "
							"project can import.")));
	m_code_message = new QLabel(this);
	m_code_message->setObjectName(QStringLiteral("codification_message"));
	m_code_message->setWordWrap(true);
		//The buttons under the lists of codes act on the list shown
	connect(m_add_entry, &QPushButton::clicked, this, &PidConfigPage::addEntry);
	connect(m_remove_entry, &QPushButton::clicked, this, &PidConfigPage::removeSelectedEntries);
	connect(m_import, &QPushButton::clicked, this, [this]() {
			//Several files at once: the codes, the sizes of the materials...
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
	m_table->setObjectName(QStringLiteral("color_table"));
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

		//The presentation: all but the codification
	m_save_presentation = new QPushButton(tr("Save as default presentation"), this);
	m_save_presentation->setObjectName(QStringLiteral("save_presentation"));
	m_save_presentation->setToolTip(wrapped(tr("Saves the tabs Colors, Texts, Connections and Signal lines, "
								  "and the models of tags, on this computer. New projects will "
								  "start with them; projects that already exist do not change.")));
	connect(m_save_presentation, &QPushButton::clicked, this, &PidConfigPage::saveAsDefaultPresentation);
	m_save_codification = new QPushButton(tr("Save as default codification"), this);
	m_save_codification->setObjectName(QStringLiteral("save_codification"));
	m_save_codification->setToolTip(wrapped(tr("Saves the tab Codification, on this computer: the codes of the "
									   "fluids, materials and functional units, the sizes of the "
									   "materials and the equal DN. New projects will start with "
									   "them; projects that already exist do not change.")));
	connect(m_save_codification, &QPushButton::clicked, this, &PidConfigPage::saveAsDefaultCodification);
	m_default_saved = new QLabel(this);

	for (const QString &category : PidCodification::textCategories())
	{
		auto *size = new QSpinBox(this);
		size->setObjectName(QStringLiteral("text_size_") + category);
		size->setRange(0, 200);
		size->setSuffix(tr(" pt"));
			//A pipe has no symbol: its designation has the size of the text
			//of a conductor
		const bool line = category == QLatin1String("line");
		size->setSpecialValueText(line ? tr("Default size") : tr("Size of the symbol"));
		connect(size, &QSpinBox::valueChanged, this, &PidConfigPage::readTextWidgets);
		m_text_sizes.insert(category, size);

			//Emptying the field gives its former value back: 0 is not obvious
		auto *reset = new QPushButton(line ? tr("Back to the default size")
										   : tr("Back to the size of the symbol"), this);
		reset->setObjectName(QStringLiteral("text_size_reset_") + category);
		connect(reset, &QPushButton::clicked, size, [size]() { size->setValue(0); });
		m_text_size_resets.insert(category, reset);
	}
		//The ink of the labels, then of the designations: each its own
		//group of two choices
	const QStringList ink_names{QStringLiteral("label_ink"), QStringLiteral("designation_ink")};
	const QStringList of_object_names{tr("Color of their symbol"), tr("Color of their pipe")};
	for (int kind = 0 ; kind < 2 ; ++kind)
	{
		auto *group = new QButtonGroup(this);
		m_ink_object[kind] = new QRadioButton(of_object_names.at(kind), this);
		m_ink_object[kind]->setObjectName(ink_names.at(kind) + QStringLiteral("_object"));
		m_ink_fixed[kind] = new QRadioButton(tr("Fixed color"), this);
		m_ink_fixed[kind]->setObjectName(ink_names.at(kind) + QStringLiteral("_fixed"));
		group->addButton(m_ink_object[kind]);
		group->addButton(m_ink_fixed[kind]);
		m_ink_button[kind] = new QPushButton(tr("Choose..."), this);
		m_ink_button[kind]->setObjectName(ink_names.at(kind) + QStringLiteral("_color"));
		connect(m_ink_object[kind], &QRadioButton::toggled, this, [this, kind]() {
			showTextInk(kind);
			readTextWidgets();
		});
		connect(m_ink_button[kind], &QPushButton::clicked, this, [this, kind]() { chooseTextInk(kind); });
	}

		//The models of the tags and of the designations: each field of a
		//family, what the list of "Insert a field" calls it
	const QList<QPair<QString, QString>> tag_fields{
		{QStringLiteral("{unit}"), tr("Functional unit")},
		{QStringLiteral("{id}"), tr("Identifier of the symbol (letters of an instrument)")},
		{QStringLiteral("{train}"), tr("Train")},
		{QStringLiteral("{order}"), tr("Order")},
		{QStringLiteral("{order:2}"), tr("Order on two digits (05)")},
		{QStringLiteral("\\n"), tr("New line")}};
	const QList<QPair<QString, QString>> line_fields{
		{QStringLiteral("{fluid}"), tr("Fluid")},
		{QStringLiteral("{fluid_name}"), tr("Name of the fluid (its code without one)")},
		{QStringLiteral("{line_number}"), tr("Line number")},
		{QStringLiteral("{size}"), tr("Size as drawn (DN 100, Ø 110; DN ? without one)")},
		{QStringLiteral("{dn}"), tr("DN alone (100; ? without one)")},
		{QStringLiteral("{material}"), tr("Material")},
		{QStringLiteral("{material_name}"), tr("Name of the material (its code without one)")},
		{QStringLiteral("{piping_class}"), tr("Piping class")},
		{QStringLiteral("{pn}"), tr("Nominal pressure")},
		{QStringLiteral("{flow}"), tr("Flow")},
		{QStringLiteral("{insulation}"), tr("Insulation as written (Q 80)")},
		{QStringLiteral("{jacket}"), tr("Jacket: code of its material and size (M05 Ø 63)")},
		{QStringLiteral("{jacket_name:in}"), tr("Jacket by the name of its material, after a word (in PVC Ø 63)")},
		{QStringLiteral("\\n"), tr("New line")}};
	for (const QString &family : modelFamilies())
	{
		const bool line = family == PidLineDesignation::modelKey();
		const QList<QPair<QString, QString>> &fields = line ? line_fields : tag_fields;
		auto *model = new QLineEdit(this);
		model->setObjectName(QStringLiteral("tag_model_") + family);
		model->setPlaceholderText(line ? tr("No model: no designation is written")
									   : tr("No model: the labels are typed by hand"));
		QStringList help;
		for (const auto &field : fields) {
			help << field.first + QStringLiteral(" : ") + field.second;
		}
		model->setToolTip(wrapped(tr("Fields the model may hold, between other text:") + QStringLiteral("\n")
								  + help.join(QLatin1Char('\n'))));
		connect(model, &QLineEdit::textChanged, this, &PidConfigPage::readModelWidgets);
		m_tag_models.insert(family, model);

			//A field put where the cursor is, from a list in the words of the
			//user: nothing to remember, no typing mistake
		auto *insert = new QToolButton(this);
		insert->setObjectName(QStringLiteral("insert_field_") + family);
		insert->setText(tr("Insert a field"));
		insert->setPopupMode(QToolButton::InstantPopup);
		auto *menu = new QMenu(insert);
		for (const auto &field : fields) {
			QAction *action = menu->addAction(field.second + QStringLiteral("  ") + field.first);
			action->setData(field.first);
			connect(action, &QAction::triggered, model, [model, action]() {
				model->insert(action->data().toString());
				model->setFocus();
			});
		}
		insert->setMenu(menu);
		m_insert_fields.insert(family, insert);

		auto *preview = new QLabel(this);
		preview->setObjectName(QStringLiteral("model_preview_") + family);
		preview->setTextFormat(Qt::PlainText);
		m_model_previews.insert(family, preview);
	}
	m_nozzle_texts_on_paper = new QCheckBox(tr("Print and export the names and DN of the nozzles"), this);
	m_nozzle_texts_on_paper->setObjectName(QStringLiteral("nozzle_texts_on_paper"));
	m_nozzle_texts_on_paper->setToolTip(wrapped(tr("How the box \"Draw the names and DN of the nozzles\" starts "
										   "in each print and export.")));
	connect(m_nozzle_texts_on_paper, &QCheckBox::toggled, this, &PidConfigPage::readTextWidgets);
	m_show_nozzle_sizes = new QCheckBox(tr("Show the sizes of the nozzles under their names"), this);
	m_show_nozzle_sizes->setObjectName(QStringLiteral("show_nozzle_sizes"));
	connect(m_show_nozzle_sizes, &QCheckBox::toggled, this, &PidConfigPage::readTextWidgets);
	m_nozzle_dn_prefix = new QCheckBox(tr("Write \"DN\" before the sizes of the nozzles"), this);
	m_nozzle_dn_prefix->setObjectName(QStringLiteral("nozzle_dn_prefix"));
	m_nozzle_dn_prefix->setToolTip(wrapped(tr("Unchecked, a nozzle shows \"100\" for \"DN 100\". An outside "
									  "diameter (Ø) and a thread (G) always keep their sign.")));
	connect(m_nozzle_dn_prefix, &QCheckBox::toggled, this, &PidConfigPage::readTextWidgets);
	m_reducer_sizes = new QCheckBox(tr("Write the sizes of the reducers"), this);
	m_reducer_sizes->setObjectName(QStringLiteral("reducer_sizes"));
	m_reducer_sizes->setToolTip(wrapped(tr("A reducer without a label of its own writes its two sizes "
										   "there, in the order of the drawing: \"100 × 80\", "
										   "\"Ø 125 × 110\". A label typed replaces them.")));
	connect(m_reducer_sizes, &QCheckBox::toggled, this, &PidConfigPage::readTextWidgets);
		//Of no use when the sizes are not shown
	connect(m_show_nozzle_sizes, &QCheckBox::toggled, this, [this](bool shown) {
		m_nozzle_dn_prefix->setEnabled(shown && !m_project->isReadOnly());
	});
	m_decimal = new QComboBox(this);
	m_decimal->setObjectName(QStringLiteral("decimal_separator"));
	m_decimal->addItem(tr("Comma (88,9)"), int(PidCodification::Decimal::Comma));
	m_decimal->addItem(tr("Point (88.9)"), int(PidCodification::Decimal::Point));
	m_decimal->setToolTip(wrapped(tr("How the tenths of the sizes are written in this project, whatever the "
							 "computer which opens it; a file always keeps a point.")));
	connect(m_decimal, &QComboBox::activated, this, &PidConfigPage::readTextWidgets);
	m_vertical_texts = new QComboBox(this);
	m_vertical_texts->setObjectName(QStringLiteral("vertical_texts"));
	m_vertical_texts->addItem(tr("As set on each computer"), int(PidCodification::VerticalTexts::Computer));
	m_vertical_texts->addItem(tr("Read from the bottom"), int(PidCodification::VerticalTexts::FromBottom));
	m_vertical_texts->addItem(tr("Read as the symbol is turned"), int(PidCodification::VerticalTexts::AsTurned));
	m_vertical_texts->setToolTip(wrapped(tr("The labels, bubbles and nozzles of a turned P&ID symbol, when the "
									"project does not keep its texts horizontal. Set here, the project "
									"looks the same on every computer; else each one follows its own "
									"setting (Settings > Appearance).")));
	connect(m_vertical_texts, &QComboBox::activated, this, &PidConfigPage::readTextWidgets);
		//The stubs where the insulated part of a line begins and ends, and
		//the mark in its middle (step 7d)
	m_insulation_stub = new QDoubleSpinBox(this);
	m_insulation_stub->setObjectName(QStringLiteral("insulation_stub"));
	m_insulation_stub->setRange(2.0, 50.0);
	m_insulation_stub->setDecimals(1);
	m_insulation_stub->setSingleStep(1.0);
	m_insulation_stub->setSuffix(tr(" mm"));
	m_insulation_stub->setToolTip(wrapped(tr("The hatched (insulation) or dashed (heat tracing) stub drawn where "
											 "the insulated part of a line begins and ends, and the mark in the "
											 "middle of a pipe which asks for one.")));
	connect(m_insulation_stub, &QDoubleSpinBox::valueChanged, this, &PidConfigPage::readTextWidgets);
	m_symbols_follow_pipes = new QCheckBox(tr("Valves and instruments on a line take the color of their line"), this);
	m_symbols_follow_pipes->setObjectName(QStringLiteral("symbols_follow_pipes"));
	m_symbols_follow_pipes->setToolTip(wrapped(tr("When all the lines joined to the symbol have the same color, one "
												  "being enough; else it keeps the color of its family. Fittings and "
												  "ends of lines always follow their line, equipment never.")));
	connect(m_symbols_follow_pipes, &QCheckBox::toggled, this, &PidConfigPage::readTextWidgets);

	for (PipeConnection::Type type : PipeConnection::all())
	{
			//A plain end is drawn with the sockets: the socket its pipe brings
		if (PipeConnection::drawnWith(type) != type) {
			continue;
		}
		auto *drawn = new QCheckBox(PipeConnection::name(type), this);
		drawn->setObjectName(QStringLiteral("drawn_connection_") + PipeConnection::toString(type));
		connect(drawn, &QCheckBox::toggled, this, &PidConfigPage::readConnectionWidgets);
		m_drawn_connections.insert(type, drawn);
	}
	const QStringList family_names{tr("Equipment"), tr("Valves"), tr("Instruments"),
								   tr("Pipe fittings (tee, reducer…)"),
								   tr("Other symbols (no family)")};
	const QStringList &families = PidCodification::connectionFamilies();
	for (int i = 0 ; i < families.size() ; ++i)
	{
		auto *family = new QCheckBox(family_names.value(i), this);
		family->setObjectName(QStringLiteral("drawn_family_") + families.at(i));
		connect(family, &QCheckBox::toggled, this, &PidConfigPage::readConnectionWidgets);
		m_drawn_families.insert(families.at(i), family);
	}

	for (const QString &role : PidCodification::toolSymbolRoles())
	{
		auto *symbol = new QLineEdit(this);
		symbol->setObjectName(QStringLiteral("tool_symbol_") + role);
		symbol->setReadOnly(true);
		symbol->setPlaceholderText(tr("None"));
		m_tool_symbols.insert(role, symbol);
		auto *choose = new QPushButton(tr("Choose..."), this);
		choose->setObjectName(QStringLiteral("choose_tool_symbol_") + role);
		connect(choose, &QPushButton::clicked, this, [this, role]() { chooseToolSymbol(role); });
		m_choose_tool_symbols.insert(role, choose);
		auto *clear = new QPushButton(tr("None"), this);
		clear->setObjectName(QStringLiteral("clear_tool_symbol_") + role);
		connect(clear, &QPushButton::clicked, this, [this, role]() { setToolSymbol(role, QString()); });
		m_clear_tool_symbols.insert(role, clear);
	}

		//The mark of the signal lines of each type of signal: the catalogue
		//of the marks, each with its picture
	QList<QIcon> mark_icons;
	for (SignalMark::Mark mark : SignalMark::all()) {
		mark_icons << markIcon(mark, palette(), devicePixelRatioF());
	}
	for (SignalProperties::SignalType type : SignalProperties::types())
	{
		auto *marks = new QComboBox(this);
		marks->setObjectName(QStringLiteral("signal_mark_") + SignalProperties::typeToString(type));
		marks->setIconSize(mark_icon_size);
			//Drawn as the lines of a list, not as those of a menu: QETStyle
			//gives the highlighted line of a menu an icon made for the small
			//square ones (PM_SmallIconSize), which would squeeze the picture
		marks->setItemDelegate(new QStyledItemDelegate(marks));
		const QList<SignalMark::Mark> all = SignalMark::all();
		for (int i = 0 ; i < all.size() ; ++i) {
			marks->addItem(mark_icons.at(i), SignalMark::name(all.at(i)),
						   SignalMark::toString(all.at(i)));
		}
			//A choice of the user only: a mark this version does not know
			//stays in the project as long as its list is not touched
		connect(marks, &QComboBox::activated, this, [this, type, marks](int index) {
			setSignalMark(type, SignalMark::fromString(marks->itemData(index).toString()));
		});
		m_signal_marks.insert(type, marks);
	}
	m_reset_signal_marks = new QPushButton(tr("Back to the default marks"), this);
	m_reset_signal_marks->setObjectName(QStringLiteral("signal_marks_reset"));
	connect(m_reset_signal_marks, &QPushButton::clicked, this, &PidConfigPage::resetSignalMarks);

		//The options the symbols of a family take when placed new, their
		//words as in the files of the symbols and of the project
	m_option_default_table = new QTableWidget(0, 3, this);
	m_option_default_table->setObjectName(QStringLiteral("option_default_table"));
	m_option_default_table->setHorizontalHeaderLabels({tr("Family"), tr("Option"), tr("Value")});
	m_option_default_table->verticalHeader()->hide();
	m_option_default_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_option_default_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	connect(m_option_default_table, &QTableWidget::itemChanged, this, &PidConfigPage::readOptionDefaultTable);
	m_add_option_default = new QPushButton(tr("Add"), this);
	m_add_option_default->setObjectName(QStringLiteral("add_option_default"));
	connect(m_add_option_default, &QPushButton::clicked, this, [this]() { addOptionDefault(); });
	m_remove_option_default = new QPushButton(tr("Remove"), this);
	m_remove_option_default->setObjectName(QStringLiteral("remove_option_default"));
	connect(m_remove_option_default, &QPushButton::clicked, this, &PidConfigPage::removeSelectedOptionDefaults);

		//The kinds of line: those of ISO 10628-1, then those the project adds
	m_line_kind_table = new QTableWidget(0, 4, this);
	m_line_kind_table->setObjectName(QStringLiteral("line_kind_table"));
	m_line_kind_table->setHorizontalHeaderLabels({tr("Code"), tr("Name"), tr("Width (mm)"), tr("Pattern")});
	m_line_kind_table->verticalHeader()->hide();
	m_line_kind_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_line_kind_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_line_kind_table->setIconSize(QSize(48, 12));
	connect(m_line_kind_table, &QTableWidget::itemChanged, this, &PidConfigPage::readLineKindTable);
	m_add_line_kind = new QPushButton(tr("Add"), this);
	m_add_line_kind->setObjectName(QStringLiteral("add_line_kind"));
	connect(m_add_line_kind, &QPushButton::clicked, this, &PidConfigPage::addLineKind);
	m_remove_line_kind = new QPushButton(tr("Remove"), this);
	m_remove_line_kind->setObjectName(QStringLiteral("remove_line_kind"));
	connect(m_remove_line_kind, &QPushButton::clicked, this, &PidConfigPage::removeSelectedLineKinds);
	m_reset_line_kinds = new QPushButton(tr("Back to the values of ISO 10628-1"), this);
	m_reset_line_kinds->setObjectName(QStringLiteral("line_kinds_reset"));
	connect(m_reset_line_kinds, &QPushButton::clicked, this, &PidConfigPage::resetLineKinds);
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
							   "color set on a pipe line, then its fluid, then the family of the object. "
							   "Insulation stubs without a color take that of their pipe line."),
							colors_tab);
	help->setWordWrap(true);
	layout->addWidget(help);
	layout->addWidget(m_table);
	layout->addLayout(buttons);
	layout->addWidget(m_symbols_follow_pipes);
	auto *stub_row = new QHBoxLayout();
	stub_row->addWidget(m_insulation_stub);
	stub_row->addStretch(1);
	auto *stub_form = new QFormLayout();
	stub_form->addRow(tr("Length of the insulation stubs:"), stub_row);
	layout->addLayout(stub_form);

	auto *texts_tab = new QWidget(this);
	auto *texts_layout = new QVBoxLayout(texts_tab);
	auto *texts_help = new QLabel(tr("A label placed follows its category as long as it has its former "
									 "size: a size set by hand stays."), texts_tab);
	texts_help->setWordWrap(true);
	texts_layout->addWidget(texts_help);
	auto *sizes = new QFormLayout();
	const QStringList names{tr("Labels of equipment:"), tr("Labels of valves:"),
							tr("Labels of instruments:"), tr("Labels of fittings:"),
							tr("Names and DN of nozzles:"), tr("Designations of the lines:")};
	const QStringList categories = PidCodification::textCategories();
	for (int i = 0 ; i < categories.size() ; ++i) {
		auto *row = new QHBoxLayout();
		row->addWidget(m_text_sizes.value(categories.at(i)));
		row->addWidget(m_text_size_resets.value(categories.at(i)));
		row->addStretch();
		sizes->addRow(names.at(i), row);
	}
	texts_layout->addLayout(sizes);
	auto *inks = new QFormLayout();
	const QStringList ink_rows{tr("Color of the labels:"), tr("Color of the designations:")};
	for (int kind = 0 ; kind < 2 ; ++kind) {
		auto *row = new QHBoxLayout();
		row->addWidget(m_ink_object[kind]);
		row->addWidget(m_ink_fixed[kind]);
		row->addWidget(m_ink_button[kind]);
		row->addStretch();
		inks->addRow(ink_rows.at(kind), row);
	}
	texts_layout->addLayout(inks);
	texts_layout->addWidget(m_nozzle_texts_on_paper);
	texts_layout->addWidget(m_show_nozzle_sizes);
	texts_layout->addWidget(m_nozzle_dn_prefix);
	texts_layout->addWidget(m_reducer_sizes);
		//A list of the width of its choices
	auto *decimal_row = new QHBoxLayout();
	decimal_row->addWidget(m_decimal);
	decimal_row->addStretch(1);
	auto *decimal = new QFormLayout();
	decimal->addRow(tr("Decimal separator of the sizes:"), decimal_row);
	auto *vertical_row = new QHBoxLayout();
	vertical_row->addWidget(m_vertical_texts);
	vertical_row->addStretch(1);
	decimal->addRow(tr("Vertical texts of the P&ID symbols:"), vertical_row);
	texts_layout->addLayout(decimal);

		//The models, each with its example under it
	auto *models_box = new QGroupBox(tr("Models"), texts_tab);
	auto *models_layout = new QVBoxLayout(models_box);
	auto *models_help = new QLabel(tr("A model puts the data of a symbol or of a pipe together, between other "
									  "text: \"{unit} {id} {train}{order:2}\" gives \"U01 P 105\". A tag without "
									  "a model is typed by hand; a pipe without one writes no designation."),
								   models_box);
	models_help->setWordWrap(true);
	models_layout->addWidget(models_help);
	auto *models = new QFormLayout();
	const QStringList model_names{tr("Tags of equipment:"), tr("Tags of valves:"),
								  tr("Tags of instruments:"), tr("Designations of the lines:")};
	const QStringList families = modelFamilies();
	for (int i = 0 ; i < families.size() ; ++i) {
		auto *row = new QHBoxLayout();
		row->addWidget(m_tag_models.value(families.at(i)), 1);
		row->addWidget(m_insert_fields.value(families.at(i)));
		models->addRow(model_names.at(i), row);
		models->addRow(QString(), m_model_previews.value(families.at(i)));
	}
	models_layout->addLayout(models);
	texts_layout->addWidget(models_box);
	texts_layout->addStretch();

	auto *connections_tab = new QWidget(this);
	auto *connections_layout = new QVBoxLayout(connections_tab);
		//Short: the drawing shows the rest (README-PID tells it)
	auto *connections_help = new QLabel(tr("The connections checked are drawn at the end of the "
										   "nozzles, on the symbols checked; a plain end follows "
										   "\"Socket\". The pipe draws its own part facing it. A "
										   "nozzle may say otherwise (properties of the element, "
										   "Nozzles)."), connections_tab);
	connections_help->setWordWrap(true);
	connections_layout->addWidget(connections_help);
		//The connections drawn, beside the families of symbols they are
		//drawn on
	auto *types_column = new QVBoxLayout();
	types_column->addWidget(new QLabel(tr("Connections drawn:"), connections_tab));
	for (PipeConnection::Type type : PipeConnection::all()) {
		if (QCheckBox *drawn = m_drawn_connections.value(type)) {
			types_column->addWidget(drawn);
		}
	}
	types_column->addStretch();
	auto *families_column = new QVBoxLayout();
	families_column->addWidget(new QLabel(tr("On the symbols:"), connections_tab));
	for (const QString &family : PidCodification::connectionFamilies()) {
		families_column->addWidget(m_drawn_families.value(family));
	}
	families_column->addStretch();
	auto *columns = new QHBoxLayout();
	columns->addLayout(types_column);
	columns->addSpacing(40);
	columns->addLayout(families_column);
	columns->addStretch();
	connections_layout->addLayout(columns);

		//The symbols the tools place by themselves
	auto *tools_box = new QGroupBox(tr("Symbols placed by the tools"), connections_tab);
	auto *tools_layout = new QVBoxLayout(tools_box);
	auto *tools_help = new QLabel(tr("With the tool \"Draw a pipe\", a click in an empty place starts or "
									 "ends the pipe on the free end, a click on a pipe on the tee, "
									 "which is taken into the project; a drag from a nozzle released "
									 "on a pipe too. None: the tool asks for a nozzle. A process "
									 "connection drawn from or to a free end closes the line with the "
									 "blind flange. \"Correct\" on a diamond of two sizes inserts the "
									 "reducer."), tools_box);
	tools_help->setWordWrap(true);
	tools_layout->addWidget(tools_help);
	auto *tools_form = new QFormLayout();
	const QStringList role_names{tr("Free end:"), tr("Tee:"), tr("Blind flange:"), tr("Reducer:")};
	const QStringList &roles = PidCodification::toolSymbolRoles();
	for (int i = 0 ; i < roles.size() ; ++i) {
		auto *row = new QHBoxLayout();
		row->addWidget(m_tool_symbols.value(roles.at(i)), 1);
		row->addWidget(m_choose_tool_symbols.value(roles.at(i)));
		row->addWidget(m_clear_tool_symbols.value(roles.at(i)));
		tools_form->addRow(role_names.value(i), row);
	}
	tools_layout->addLayout(tools_form);
	connections_layout->addWidget(tools_box);

		//The threads and their DN, fixed in the code: shown, not set
	auto *threads_box = new QGroupBox(tr("Pipe threads and their DN (EN 10255)"), connections_tab);
	auto *threads_layout = new QVBoxLayout(threads_box);
	auto *threads_help = new QLabel(tr("A threaded nozzle changed to another connection, or back, "
									   "changes its size by this table; a pipe drawn from a threaded "
									   "nozzle takes its DN. A fixed table, which cannot be changed."),
									threads_box);
	threads_help->setWordWrap(true);
	threads_layout->addWidget(threads_help);
		//Two rows, a column by thread: two lines high whatever the number
		//of threads, it no longer made the tab the tallest of the page
	const QList<QPair<int, int>> &pairs = PipeDiameter::threadTable();
	auto *threads = new QTableWidget(2, int(pairs.size()), threads_box);
	threads->setObjectName(QStringLiteral("thread_table"));
	threads->setVerticalHeaderLabels({tr("Thread"), tr("DN")});
	threads->horizontalHeader()->hide();
	threads->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	threads->setEditTriggers(QAbstractItemView::NoEditTriggers);
	for (int i = 0 ; i < pairs.size() ; ++i) {
		threads->setItem(0, i, new QTableWidgetItem(PipeDiameter::thread(pairs.at(i).first).text()));
		threads->setItem(1, i, new QTableWidgetItem(PipeDiameter::nominal(pairs.at(i).second).text()));
	}
		//No wider than its columns, no empty cells beside them; narrower,
		//it scrolls sideways, the scroll bar under its two rows
	threads->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	threads->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	threads->resizeColumnsToContents();
	threads->setMaximumWidth(threads->verticalHeader()->sizeHint().width()
							 + threads->horizontalHeader()->length() + 2 * threads->frameWidth());
	threads->setFixedHeight(threads->rowHeight(0) + threads->rowHeight(1) + 2 * threads->frameWidth()
							+ threads->horizontalScrollBar()->sizeHint().height());
		//Ctrl+C: the cells selected, as they are shown: a line by row,
		//a tab between columns
	auto *copy = new QShortcut(QKeySequence(QKeySequence::Copy), threads);
	copy->setContext(Qt::WidgetShortcut);
	connect(copy, &QShortcut::activated, threads, [threads]() {
		QStringList lines;
		for (int i = 0 ; i < threads->rowCount() ; ++i) {
			QStringList cells;
			for (int j = 0 ; j < threads->columnCount() ; ++j) {
				if (threads->item(i, j)->isSelected()) {
					cells << threads->item(i, j)->text();
				}
			}
			if (!cells.isEmpty()) {
				lines << cells.join(QLatin1Char('\t'));
			}
		}
		QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
	});
		//The table takes the free width first, up to its columns; only
		//what is left goes beside it
	auto *threads_row = new QHBoxLayout();
	threads_row->addWidget(threads, 1);
	threads_row->addStretch();
	threads_layout->addLayout(threads_row);
	connections_layout->addWidget(threads_box);
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
	codes_layout->addLayout(code_buttons);

	auto *tabs = new QTabWidget(this);
	tabs->addTab(codes_tab, tr("Codification"));
	tabs->addTab(colors_tab, tr("Colors"));
	tabs->addTab(texts_tab, tr("Texts"));
	tabs->addTab(connections_tab, tr("Connections"));

		//(not "signals_tab": "signals" is a word of Qt)
	auto *signal_tab = new QWidget(this);
	auto *signal_layout = new QVBoxLayout(signal_tab);
	auto *signal_help = new QLabel(tr("A signal line is drawn with the mark of its type of signal, "
									  "repeated along it. By default the marks of ISO 14617, and "
									  "those of ANSI/ISA-5.1 for the two types it has not."),
								   signal_tab);
	signal_help->setWordWrap(true);
	signal_layout->addWidget(signal_help);
	auto *signal_form = new QFormLayout();
	for (SignalProperties::SignalType type : SignalProperties::types()) {
			//A list of the width of its choices
		auto *row = new QHBoxLayout();
		row->addWidget(m_signal_marks.value(type));
		row->addStretch(1);
		signal_form->addRow(tr("%1:", "a type of signal, before the list of its marks")
							.arg(SignalProperties::typeName(type)), row);
	}
	signal_layout->addLayout(signal_form);
	auto *signal_buttons = new QHBoxLayout();
	signal_buttons->addWidget(m_reset_signal_marks);
	signal_buttons->addStretch();
	signal_layout->addLayout(signal_buttons);
	signal_layout->addStretch();
	tabs->addTab(signal_tab, tr("Signal lines"));

	auto *lines_tab = new QWidget(this);
	auto *lines_layout = new QVBoxLayout(lines_tab);
	auto *lines_help = new QLabel(tr("How the pipes of each kind of line are drawn: its width in mm and its "
									 "pattern. The three kinds of ISO 10628-1 are always there; a kind added "
									 "is offered on the page of a pipe. A pipe keeps the code of its kind: one "
									 "removed is drawn as a main line."), lines_tab);
	lines_help->setWordWrap(true);
	lines_layout->addWidget(lines_help);
	lines_layout->addWidget(m_line_kind_table);
	auto *line_buttons = new QHBoxLayout();
	line_buttons->addWidget(m_add_line_kind);
	line_buttons->addWidget(m_remove_line_kind);
	line_buttons->addStretch();
	line_buttons->addWidget(m_reset_line_kinds);
	lines_layout->addLayout(line_buttons);
	tabs->addTab(lines_tab, tr("Lines"));

	auto *symbols_tab = new QWidget(this);
	auto *symbols_layout = new QVBoxLayout(symbols_tab);
		//Short: README-PID tells the rest
	auto *symbols_help = new QLabel(tr("The options a symbol of a family takes when it is placed new, "
									   "such as instrument / shape / oval: the words of the symbol "
									   "files. A symbol placed before keeps its own. The simplest way: "
									   "right click on a symbol placed, Options, \"Make these choices "
									   "the defaults of the project\"."), symbols_tab);
	symbols_help->setWordWrap(true);
	symbols_layout->addWidget(symbols_help);
	symbols_layout->addWidget(m_option_default_table);
	auto *option_buttons = new QHBoxLayout();
	option_buttons->addWidget(m_add_option_default);
	option_buttons->addWidget(m_remove_option_default);
	option_buttons->addStretch();
	symbols_layout->addLayout(option_buttons);
	tabs->addTab(symbols_tab, tr("Symbols"));

		//Under the tabs, what acts on the whole page: a file read or
		//written, which may hold a part of any tab, and what the page says
		//of it; then the defaults of the new projects
		//All the buttons in a row, what they say under them: that of the
		//file on the left, that of the defaults on the right
	auto *page_buttons = new QHBoxLayout();
	page_buttons->addWidget(m_import);
	page_buttons->addWidget(m_export);
	page_buttons->addStretch();
	page_buttons->addWidget(m_save_codification);
	page_buttons->addWidget(m_save_presentation);

	auto *messages = new QHBoxLayout();
	messages->addWidget(m_code_message, 1);
	messages->addWidget(m_default_saved, 0, Qt::AlignRight | Qt::AlignTop);

		//Each tab scrolls on its own, when it is taller than the window:
		//the tab bar and the buttons under the tabs stay in view
	scrollTabs(tabs);

	auto *page_layout = new QVBoxLayout(this);
	page_layout->addWidget(tabs);
	page_layout->addLayout(page_buttons);
	page_layout->addLayout(messages);
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
	}
	fillTable();
	m_equal_dn->setText(m_codification.equalNominalsText());

		//Each change of a widget reads them all: they are all set first
	const PidCodification codification = m_codification;
	for (auto it = m_text_sizes.cbegin() ; it != m_text_sizes.cend() ; ++it) {
		const QSignalBlocker blocker(it.value());
		it.value()->setValue(codification.text_sizes.value(it.key()));
	}
	const bool of_object[2] = {codification.colored_labels, codification.colored_designations};
	const QColor fixed[2] = {codification.label_color, codification.designation_color};
	for (int kind = 0 ; kind < 2 ; ++kind) {
		const QSignalBlocker object_blocker(m_ink_object[kind]);
		const QSignalBlocker fixed_blocker(m_ink_fixed[kind]);
		m_ink_object[kind]->setChecked(of_object[kind]);
		m_ink_fixed[kind]->setChecked(!of_object[kind]);
			//The colour of their object: the page keeps the fixed colour
			//it had, which the project does not
		if (!of_object[kind]) {
			m_ink_color[kind] = fixed[kind];
		}
		showTextInk(kind);
	}
	const QSignalBlocker paper_blocker(m_nozzle_texts_on_paper);
	m_nozzle_texts_on_paper->setChecked(codification.nozzle_texts_on_paper);
	const QSignalBlocker sizes_blocker(m_show_nozzle_sizes);
	m_show_nozzle_sizes->setChecked(codification.show_nozzle_sizes);
	const QSignalBlocker prefix_blocker(m_nozzle_dn_prefix);
	m_nozzle_dn_prefix->setChecked(codification.nozzle_dn_prefix);
	m_nozzle_dn_prefix->setEnabled(codification.show_nozzle_sizes && !m_project->isReadOnly());
	const QSignalBlocker reducer_blocker(m_reducer_sizes);
	m_reducer_sizes->setChecked(codification.reducer_sizes);
		//Not set yet: that of the language, which the project will keep
	const QSignalBlocker decimal_blocker(m_decimal);
	m_decimal->setCurrentIndex(m_decimal->findData(int(codification.effectiveDecimal())));
	const QSignalBlocker vertical_blocker(m_vertical_texts);
	m_vertical_texts->setCurrentIndex(m_vertical_texts->findData(int(codification.vertical_texts)));
	const QSignalBlocker stub_blocker(m_insulation_stub);
	m_insulation_stub->setValue(codification.insulation_stub);
	const QSignalBlocker follow_blocker(m_symbols_follow_pipes);
	m_symbols_follow_pipes->setChecked(codification.symbols_follow_pipes);
	for (auto it = m_drawn_connections.cbegin() ; it != m_drawn_connections.cend() ; ++it) {
		const QSignalBlocker blocker(it.value());
		it.value()->setChecked(codification.drawsConnection(it.key()));
	}
	for (auto it = m_drawn_families.cbegin() ; it != m_drawn_families.cend() ; ++it) {
		const QSignalBlocker blocker(it.value());
		it.value()->setChecked(codification.drawn_families.contains(it.key()));
	}
	for (auto it = m_tag_models.cbegin() ; it != m_tag_models.cend() ; ++it) {
		const QSignalBlocker blocker(it.value());
		it.value()->setText(codification.tag_models.value(it.key()));
	}
	showModelPreviews();
	showToolSymbols();
	showSignalMarks();
	showOptionDefaults();
	showLineKinds();
}

/**
	@brief PidConfigPage::showOptionDefaults
	See the declaration
*/
void PidConfigPage::showOptionDefaults()
{
	const QSignalBlocker blocker(m_option_default_table);
	m_option_default_table->setRowCount(0);
	const QMap<QString, ElementOptions::Choices> defaults = m_codification.option_defaults;
	for (auto family = defaults.cbegin() ; family != defaults.cend() ; ++family) {
		for (auto it = family.value().cbegin() ; it != family.value().cend() ; ++it)
		{
			const int row = m_option_default_table->rowCount();
			m_option_default_table->insertRow(row);
			m_option_default_table->setItem(row, 0, new QTableWidgetItem(family.key()));
			m_option_default_table->setItem(row, 1, new QTableWidgetItem(it.key()));
			m_option_default_table->setItem(row, 2, new QTableWidgetItem(it.value()));
		}
	}
}

/**
	@brief PidConfigPage::readOptionDefaultTable
	See the declaration. A line being typed, not whole yet, is kept in the
	table and left out of the project.
*/
void PidConfigPage::readOptionDefaultTable()
{
	const QSignalBlocker blocker(m_option_default_table);
	m_codification.option_defaults.clear();
	for (int row = 0 ; row < m_option_default_table->rowCount() ; ++row)
	{
		QStringList words;
		for (int column = 0 ; column < 3 ; ++column) {
			if (!m_option_default_table->item(row, column)) {
				m_option_default_table->setItem(row, column, new QTableWidgetItem());
			}
			words << m_option_default_table->item(row, column)->text().trimmed();
		}
			//The first line of an option is the one kept
		const bool twice = m_codification.option_defaults.value(words.at(0)).contains(words.at(1));
		const bool taken = !words.at(2).isEmpty() && !twice
				&& m_codification.setOptionDefault(words.at(0), words.at(1), words.at(2));
		const bool empty = words.join(QString()).isEmpty();
		for (int column = 0 ; column < 3 ; ++column)
		{
			QTableWidgetItem *item = m_option_default_table->item(row, column);
			item->setBackground(taken || empty ? QBrush() : QBrush(QColor(255, 200, 120)));
			item->setToolTip(taken || empty ? QString()
											: twice ? wrapped(tr("This option of this family is "
																 "already given a value above: "
																 "this line is left out."))
													: wrapped(tr("A family, an option and a value are "
																 "needed, in letters, digits, \"_\" "
																 "and \"-\": this line is left out.")));
		}
	}
}

/**
	@brief PidConfigPage::addOptionDefault
	See the declaration
*/
void PidConfigPage::addOptionDefault(const QString &family, const QString &option, const QString &value)
{
	const int row = m_option_default_table->rowCount();
	{
		const QSignalBlocker blocker(m_option_default_table);
		m_option_default_table->insertRow(row);
		m_option_default_table->setItem(row, 0, new QTableWidgetItem(family));
		m_option_default_table->setItem(row, 1, new QTableWidgetItem(option));
		m_option_default_table->setItem(row, 2, new QTableWidgetItem(value));
	}
	readOptionDefaultTable();
	if (family.isEmpty()) {
		m_option_default_table->setCurrentCell(row, 0);
		m_option_default_table->editItem(m_option_default_table->item(row, 0));
	}
}

/**
	@brief PidConfigPage::removeSelectedOptionDefaults
	See the declaration
*/
void PidConfigPage::removeSelectedOptionDefaults()
{
	QList<int> rows;
	for (const QModelIndex &index : m_option_default_table->selectionModel()->selectedRows()) {
		rows << index.row();
	}
	std::sort(rows.begin(), rows.end(), std::greater<int>());
	{
		const QSignalBlocker blocker(m_option_default_table);
		for (int row : std::as_const(rows)) {
			m_option_default_table->removeRow(row);
		}
	}
	readOptionDefaultTable();
}

namespace {
		/// A picture of a line of @p width mm drawn with @p pattern, at four
		/// pixels by mm as the folio at 100 %
	QIcon linePicture(qreal width, PidCodification::LinePattern pattern)
	{
		QPixmap picture(48, 12);
		picture.fill(Qt::transparent);
		QPainter painter(&picture);
		QPen pen(Qt::black, qMax(1.0, width * 4.0), Qt::SolidLine, Qt::FlatCap);
		const QList<qreal> dashes = PidCodification::dashPattern(pattern);
		if (!dashes.isEmpty()) {
			pen.setDashPattern(dashes);
		}
		painter.setPen(pen);
		painter.drawLine(QPointF(0, 6), QPointF(48, 6));
		painter.end();
		return QIcon(picture);
	}
}

/**
	@brief PidConfigPage::showLineKinds
	See the declaration. The code and the name of a kind of ISO 10628-1
	cannot be changed: its name is translated.
*/
void PidConfigPage::showLineKinds()
{
	const QSignalBlocker blocker(m_line_kind_table);
	m_line_kind_table->setRowCount(0);
	const QList<PidCodification::LineKindEntry> kinds = m_codification.line_kinds;
	for (const PidCodification::LineKindEntry &kind : kinds)
	{
		const int row = m_line_kind_table->rowCount();
		m_line_kind_table->insertRow(row);
		const bool iso = PidCodification::isIsoLineKind(kind.code);
		auto *code = new QTableWidgetItem(linePicture(kind.width, kind.pattern), kind.code);
		auto *name = new QTableWidgetItem(iso ? m_codification.lineKindName(kind.code) : kind.name);
		if (iso) {
			code->setFlags(code->flags() & ~Qt::ItemIsEditable);
			name->setFlags(name->flags() & ~Qt::ItemIsEditable);
		}
		m_line_kind_table->setItem(row, 0, code);
		m_line_kind_table->setItem(row, 1, name);
		m_line_kind_table->setItem(row, 2, new QTableWidgetItem(m_codification.locale().toString(kind.width)));
		auto *pattern = new QComboBox(m_line_kind_table);
		pattern->setObjectName(QStringLiteral("line_pattern_") + QString::number(row));
		for (PidCodification::LinePattern known : PidCodification::linePatterns()) {
			pattern->addItem(linePicture(0.5, known), PidCodification::linePatternName(known), int(known));
		}
		pattern->setCurrentIndex(pattern->findData(int(kind.pattern)));
		pattern->setEnabled(!m_project->isReadOnly());
		connect(pattern, &QComboBox::activated, this, &PidConfigPage::readLineKindTable);
		m_line_kind_table->setCellWidget(row, 3, pattern);
	}
}

/**
	@brief PidConfigPage::readLineKindTable
	See the declaration. A line being typed, not whole yet, is kept in the
	table and left out of the project; so is a second line of a code.
*/
void PidConfigPage::readLineKindTable()
{
	const QSignalBlocker blocker(m_line_kind_table);
	m_codification.line_kinds = PidCodification::isoLineKinds();
	QStringList read;
	for (int row = 0 ; row < m_line_kind_table->rowCount() ; ++row)
	{
		for (int column = 0 ; column < 3 ; ++column) {
			if (!m_line_kind_table->item(row, column)) {
				m_line_kind_table->setItem(row, column, new QTableWidgetItem());
			}
		}
		PidCodification::LineKindEntry kind;
		kind.code = m_line_kind_table->item(row, 0)->text().trimmed();
		kind.name = m_line_kind_table->item(row, 1)->text().trimmed();
		bool ok = false;
		kind.width = m_line_kind_table->item(row, 2)->text().replace(QLatin1Char(','), QLatin1Char('.'))
					 .trimmed().toDouble(&ok);
		if (auto *pattern = qobject_cast<QComboBox *>(m_line_kind_table->cellWidget(row, 3))) {
			kind.pattern = PidCodification::LinePattern(pattern->currentData().toInt());
		}
		const bool twice = read.contains(kind.code);
		const bool taken = ok && !twice && m_codification.setLineKind(kind);
		if (taken) {
			read << kind.code;
		}
		const bool empty = kind.code.isEmpty() && kind.name.isEmpty();
		for (int column = 0 ; column < 3 ; ++column)
		{
			QTableWidgetItem *item = m_line_kind_table->item(row, column);
			item->setBackground(taken || empty ? QBrush() : QBrush(QColor(255, 200, 120)));
			item->setToolTip(taken || empty ? QString()
											: twice ? wrapped(tr("This code is already given to a kind "
																 "of line above: this line is left out."))
													: wrapped(tr("A code in letters, digits, \"_\" and "
																 "\"-\", and a width from 0.1 to 5 mm are "
																 "needed: this line is left out.")));
		}
		if (taken) {
			m_line_kind_table->item(row, 0)->setIcon(linePicture(kind.width, kind.pattern));
		}
	}
}

/**
	@brief PidConfigPage::addLineKind
	See the declaration
*/
void PidConfigPage::addLineKind()
{
	const int row = m_line_kind_table->rowCount();
	{
		const QSignalBlocker blocker(m_line_kind_table);
		m_line_kind_table->insertRow(row);
		m_line_kind_table->setItem(row, 0, new QTableWidgetItem());
		m_line_kind_table->setItem(row, 1, new QTableWidgetItem());
		m_line_kind_table->setItem(row, 2, new QTableWidgetItem(m_codification.locale().toString(0.5)));
		auto *pattern = new QComboBox(m_line_kind_table);
		pattern->setObjectName(QStringLiteral("line_pattern_") + QString::number(row));
		for (PidCodification::LinePattern known : PidCodification::linePatterns()) {
			pattern->addItem(linePicture(0.5, known), PidCodification::linePatternName(known), int(known));
		}
		connect(pattern, &QComboBox::activated, this, &PidConfigPage::readLineKindTable);
		m_line_kind_table->setCellWidget(row, 3, pattern);
	}
	readLineKindTable();
	m_line_kind_table->setCurrentCell(row, 0);
	m_line_kind_table->editItem(m_line_kind_table->item(row, 0));
}

/**
	@brief PidConfigPage::removeSelectedLineKinds
	See the declaration
*/
void PidConfigPage::removeSelectedLineKinds()
{
	QList<int> rows;
	for (const QModelIndex &index : m_line_kind_table->selectionModel()->selectedRows()) {
			//The code of a kind of ISO 10628-1 is not editable
		const QTableWidgetItem *code = m_line_kind_table->item(index.row(), 0);
		if (!code || (code->flags() & Qt::ItemIsEditable)) {
			rows << index.row();
		}
	}
	std::sort(rows.begin(), rows.end(), std::greater<int>());
	{
		const QSignalBlocker blocker(m_line_kind_table);
		for (int row : std::as_const(rows)) {
			m_line_kind_table->removeRow(row);
		}
	}
	readLineKindTable();
}

/**
	@brief PidConfigPage::resetLineKinds
	See the declaration
*/
void PidConfigPage::resetLineKinds()
{
	for (const PidCodification::LineKindEntry &kind : PidCodification::isoLineKinds()) {
		m_codification.setLineKind(kind);
	}
	showLineKinds();
}

/**
	@brief PidConfigPage::showSignalMarks
	The mark of each type of signal, in its list: the one its lines are
	drawn with
*/
void PidConfigPage::showSignalMarks()
{
	for (auto it = m_signal_marks.cbegin() ; it != m_signal_marks.cend() ; ++it) {
		const QSignalBlocker blocker(it.value());
		it.value()->setCurrentIndex(it.value()->findData(
										SignalMark::toString(m_codification.signalMark(it.key()))));
	}
}

/**
	@brief PidConfigPage::setSignalMark
	See the declaration
*/
void PidConfigPage::setSignalMark(SignalProperties::SignalType type, SignalMark::Mark mark)
{
	m_codification.setSignalMark(type, mark);
	showSignalMarks();
}

/**
	@brief PidConfigPage::resetSignalMarks
	See the declaration. What a later version set for a type this one does
	not know stays.
*/
void PidConfigPage::resetSignalMarks()
{
	for (SignalProperties::SignalType type : SignalProperties::types()) {
		m_codification.setSignalMark(type, SignalMark::defaultMark(type));
	}
	showSignalMarks();
}

/**
	@brief PidConfigPage::showToolSymbols
	The name of the symbol of each role of the tools, its place in the tool
	tip; in orange, with its place, a symbol not found
*/
void PidConfigPage::showToolSymbols()
{
	for (auto it = m_tool_symbols.cbegin() ; it != m_tool_symbols.cend() ; ++it)
	{
		QLineEdit *field = it.value();
		const QString path = m_codification.toolSymbol(it.key());
			//A project given turns any path into one of its own collection
		const ElementsLocation location = path.startsWith(QLatin1String("embed://"))
				? ElementsLocation(path, m_project) : ElementsLocation(path);
		const bool found = !path.isEmpty() && location.isElement() && location.exist();
		field->setText(path.isEmpty() ? QString() : found ? location.name() : path);
		field->setToolTip(path.isEmpty() ? QString()
										 : found ? path
												 : wrapped(tr("This symbol is not found: the tool will "
															  "place nothing. Choose another one.")));
		field->setStyleSheet(path.isEmpty() || found ? QString()
													 : QStringLiteral("background: rgb(255, 200, 120);"));
	}
}

/**
	@brief PidConfigPage::chooseToolSymbol
	Ask the symbol of the role @p role in the collections
*/
void PidConfigPage::chooseToolSymbol(const QString &role)
{
	const ElementsLocation location = ElementDialog::getOpenElementLocation(this);
	if (location.isElement()) {
		setToolSymbol(role, location.collectionPath(true));
	}
}

/**
	@brief PidConfigPage::setToolSymbol
	See the declaration
*/
void PidConfigPage::setToolSymbol(const QString &role, const QString &location)
{
	if (location.trimmed().isEmpty()) {
		m_codification.tool_symbols.remove(role);
	} else {
		m_codification.tool_symbols.insert(role, location.trimmed());
	}
	showToolSymbols();
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
	m_size_table->setEditTriggers(m_code_tables.value(PidCodification::List::Materials)->editTriggers());
	m_add_size->setEnabled(!read_only);
	m_remove_size->setEnabled(!read_only);
	m_equal_dn->setReadOnly(read_only);
		//The boxes of the default sizes too
	decorateSizeRows();
	m_show_nozzle_sizes->setEnabled(!read_only);
	m_nozzle_dn_prefix->setEnabled(!read_only && m_show_nozzle_sizes->isChecked());
	m_reducer_sizes->setEnabled(!read_only);
	m_decimal->setEnabled(!read_only);
	m_vertical_texts->setEnabled(!read_only);
	m_insulation_stub->setEnabled(!read_only);
	m_symbols_follow_pipes->setEnabled(!read_only);
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
	for (int kind = 0 ; kind < 2 ; ++kind) {
		m_ink_object[kind]->setEnabled(!read_only);
		m_ink_fixed[kind]->setEnabled(!read_only);
		showTextInk(kind);
	}
	for (QLineEdit *model : std::as_const(m_tag_models)) {
		model->setReadOnly(read_only);
	}
	for (QToolButton *insert : std::as_const(m_insert_fields)) {
		insert->setEnabled(!read_only);
	}
	m_nozzle_texts_on_paper->setEnabled(!read_only);
	for (QCheckBox *drawn : std::as_const(m_drawn_connections)) {
		drawn->setEnabled(!read_only);
	}
	for (QCheckBox *family : std::as_const(m_drawn_families)) {
		family->setEnabled(!read_only);
	}
	for (QPushButton *button : std::as_const(m_choose_tool_symbols)) {
		button->setEnabled(!read_only);
	}
	for (QPushButton *button : std::as_const(m_clear_tool_symbols)) {
		button->setEnabled(!read_only);
	}
	for (QComboBox *marks : std::as_const(m_signal_marks)) {
		marks->setEnabled(!read_only);
	}
	m_reset_signal_marks->setEnabled(!read_only);
	m_option_default_table->setEditTriggers(m_code_tables.value(PidCodification::List::Fluids)->editTriggers());
	m_add_option_default->setEnabled(!read_only);
	m_remove_option_default->setEnabled(!read_only);
	m_line_kind_table->setEditTriggers(m_code_tables.value(PidCodification::List::Fluids)->editTriggers());
	m_add_line_kind->setEnabled(!read_only);
	m_remove_line_kind->setEnabled(!read_only);
	m_reset_line_kinds->setEnabled(!read_only);
	for (int row = 0 ; row < m_line_kind_table->rowCount() ; ++row) {
		if (QWidget *pattern = m_line_kind_table->cellWidget(row, 3)) {
			pattern->setEnabled(!read_only);
		}
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
	m_codification.drawn_families.clear();
	for (const QString &family : PidCodification::connectionFamilies()) {
		const QCheckBox *drawn = m_drawn_families.value(family);
		if (drawn && drawn->isChecked()) {
			m_codification.drawn_families << family;
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
	m_codification.colored_labels = m_ink_object[0]->isChecked();
	m_codification.label_color = m_codification.colored_labels ? QColor() : m_ink_color[0];
	m_codification.colored_designations = m_ink_object[1]->isChecked();
	m_codification.designation_color = m_codification.colored_designations ? QColor() : m_ink_color[1];
	m_codification.nozzle_texts_on_paper = m_nozzle_texts_on_paper->isChecked();
	m_codification.show_nozzle_sizes = m_show_nozzle_sizes->isChecked();
	m_codification.nozzle_dn_prefix = m_nozzle_dn_prefix->isChecked();
	m_codification.reducer_sizes = m_reducer_sizes->isChecked();
	m_codification.vertical_texts = PidCodification::VerticalTexts(m_vertical_texts->currentData().toInt());
	m_codification.insulation_stub = m_insulation_stub->value();
	m_codification.symbols_follow_pipes = m_symbols_follow_pipes->isChecked();
		//Set only when it is chosen: else it waits to be fixed with codes
	const auto decimal = PidCodification::Decimal(m_decimal->currentData().toInt());
	if (decimal != m_codification.effectiveDecimal()) {
		m_codification.decimal = decimal;
			//The sizes of the page are written with it
		showSizes();
		decorateSizeRows();
	}
}

/**
	@brief PidConfigPage::readModelWidgets
	See the declaration. A model emptied is none: the family has no model.
	The models of other families, read from a file, stay as they are.
*/
void PidConfigPage::readModelWidgets()
{
	for (auto it = m_tag_models.cbegin() ; it != m_tag_models.cend() ; ++it) {
		const QString model = it.value()->text();
		if (model.trimmed().isEmpty()) {
			m_codification.tag_models.remove(it.key());
		} else {
			m_codification.tag_models.insert(it.key(), model);
		}
	}
	showModelPreviews();
}

/**
	@brief PidConfigPage::showModelPreviews
	What each model makes of an example: a tag of the first functional unit
	of the project (else U01), train 1, order 5; a pipe of its first fluid
	and material (else W01 and ST), DN 100, line L100, class A1, PN 10, flow
	50 m3/h. A new line of the model is shown as one.
*/
void PidConfigPage::showModelPreviews()
{
	const PidCodification &codes = m_codification;
	auto firstCode = [](const QList<PidCodification::Entry> &list, const QString &other) {
		for (const PidCodification::Entry &entry : list) {
			if (!entry.code.trimmed().isEmpty()) {
				return entry.code.trimmed();
			}
		}
		return other;
	};
	const QString unit = firstCode(codes.functional_units, QStringLiteral("U01"));
	PipeProperties pipe;
	pipe.fluid = firstCode(codes.fluids, QStringLiteral("W01"));
	pipe.material = firstCode(codes.materials, QStringLiteral("ST"));
	pipe.nominal = PipeDiameter::nominal(100);
	pipe.line_number = QStringLiteral("L100");
	pipe.piping_class = QStringLiteral("A1");
		//As it is typed: the number alone, which a model writes "PN {pn}"
	pipe.nominal_pressure = QStringLiteral("10");
	pipe.flow = QStringLiteral("50 m3/h");

	for (auto it = m_model_previews.cbegin() ; it != m_model_previews.cend() ; ++it) {
		const QString model = codes.tag_models.value(it.key());
		QString example;
		if (model.trimmed().isEmpty()) {
			example.clear();
		} else if (it.key() == PidLineDesignation::modelKey()) {
			example = PidLineDesignation::text(model, pipe, codes);
		} else {
			const QString id = it.key() == QLatin1String("equipment")  ? QStringLiteral("P")
							 : it.key() == QLatin1String("valve")      ? QStringLiteral("V")
							 : firstCode(codes.instrument_letters, QStringLiteral("FT"));
			example = PidTag::label(model, unit, id, QStringLiteral("1"), QStringLiteral("5"));
		}
		it.value()->setText(example.isEmpty() ? QString() : tr("Example: %1").arg(example));
		it.value()->setVisible(!example.isEmpty());
	}
}

/**
	@brief PidConfigPage::showSizes
	Write in its cell what sizes each material has, those its pipes are named
	by, with the separator of the page: the first, the last, how many; a
	material without one is in orange
*/
void PidConfigPage::showSizes()
{
	QTableWidget *table = m_code_tables.value(PidCodification::List::Materials);
	const QSignalBlocker blocker(table);
	const QLocale locale = m_codification.locale();
	for (int row = 0 ; row < table->rowCount() ; ++row)
	{
		const PidCodification::Entry *entry = m_codification.material(codeOf(table, row));
		QTableWidgetItem *cell = table->item(row, sizes_column);
		if (!cell) {
			continue;
		}
		const QList<PipeDiameter> sizes = entry ? PidCodification::sizesOf(*entry)
												: QList<PipeDiameter>();
		cell->setText(sizes.isEmpty() ? QString()
					  : sizes.size() == 1 ? sizes.first().text(locale)
					  : QStringLiteral("%1 … %2 (%3)").arg(sizes.first().text(locale),
														   sizes.last().text(locale))
						.arg(sizes.size()));
		const bool missing = !codeOf(table, row).isEmpty() && sizes.isEmpty();
		for (int column = 0 ; column < table->columnCount() ; ++column) {
			if (QTableWidgetItem *item = table->item(row, column)) {
				item->setBackground(missing ? QBrush(QColor(255, 200, 120)) : QBrush());
			}
		}
		cell->setToolTip(missing ? wrapped(tr("Without sizes, no pipe of this material can be given a size."))
								 : QString());
	}
}

/**
	@brief PidConfigPage::saveAsDefaultPresentation
	See the declaration
*/
void PidConfigPage::saveAsDefaultPresentation()
{
	QetSettings::setPidPresentation(m_codification.presentationToCsv());
	m_default_saved->setText(m_codification.hasColors() || m_codification.hasTextSettings()
							 || m_codification.hasSignalMarks()
							 ? tr("Presentation saved") : tr("Default presentation removed"));
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
		//The stubs of the insulated parts of the lines (step 7d)
	add(ObjectFamily, tr("Insulation"), PidCodification::insulationColorKey(),
		tr("Stubs of an insulation (hatches)"));
	add(ObjectFamily, tr("Insulation"), PidCodification::tracingColorKey(),
		tr("Stubs of a heat tracing (dashes)"));
		//The jacket of the double containment pipes (step 8r-1)
	add(ObjectFamily, tr("Jacket"), PidCodification::jacketColorKey(),
		tr("Jacket of a double containment pipe"));

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
	@brief PidConfigPage::chooseTextInk
	See the declaration
*/
void PidConfigPage::chooseTextInk(int kind)
{
	const QColor current = m_ink_color[kind].isValid() ? m_ink_color[kind] : QColor(Qt::black);
	const QColor color = m_ask_color ? m_ask_color(current)
									 : QColorDialog::getColor(current, this, title());
	if (!color.isValid()) {
		return;
	}
		//Black is what a text has without a colour: none written
	m_ink_color[kind] = color == QColor(Qt::black) ? QColor() : color;
	showTextInk(kind);
	readTextWidgets();
}

/**
	@brief PidConfigPage::showTextInk
	See the declaration
*/
void PidConfigPage::showTextInk(int kind)
{
	QPixmap swatch(16, 16);
	swatch.fill(m_ink_color[kind].isValid() ? m_ink_color[kind] : QColor(Qt::black));
	m_ink_button[kind]->setIcon(QIcon(swatch));
	m_ink_button[kind]->setEnabled(m_ink_fixed[kind]->isChecked() && !m_project->isReadOnly());
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
				table->setItem(row, sizes_column, sizesItem(new CodeItem(), entry.sizes));
			} else if (table->columnCount() > 2) {
				table->setItem(row, 2, new CodeItem(entry.family));
			}
		}
		table->setSortingEnabled(true);
			//The list of sizes shows those of the first material, which is
			//not selected for all that
		if (list == PidCodification::List::Materials && table->rowCount()) {
			table->setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
		}
	}
	showSizes();
	showMaterialSizes();
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
				//Its sizes are kept whole, whatever names its pipes
			const QTableWidgetItem *cell = table->item(row, sizes_column);
			entry.sizes = sizesFromData(cell ? cell->data(sizes_role) : QVariant());
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
		showSizes();
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
			//Letters of instruments: no tag bears them
		m_code_message->setText(!original.isEmpty() && original != code
								&& list != PidCodification::List::Letters
								? tr("The pipes and the symbols bearing the code %1 will take the code %2 "
									 "when this page is applied.").arg(original, code)
								: QString());
	}
	readCodeTable(list);
		//The list of sizes is named after the code, and its lines depend on
		//what names the pipes
	if (list == PidCodification::List::Materials) {
		if (item->column() == 0) {
			showMaterialSizes();
		} else {
			decorateSizeRows();
		}
	}
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
			sizesItem(table->item(row, sizes_column), {});
		}
	}
	table->setCurrentCell(row, 0);
	table->editItem(table->item(row, 0));
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
				list != PidCodification::List::Units
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
	if (list == PidCodification::List::Materials) {
		showMaterialSizes();
	}
}

/**
	@brief PidConfigPage::showMaterialSizes
	See the declaration
*/
void PidConfigPage::showMaterialSizes()
{
	const QTableWidget *materials = m_code_tables.value(PidCodification::List::Materials);
	const int row = materials->currentRow();
	const QString code = row >= 0 ? codeOf(materials, row) : QString();
	const QTableWidgetItem *cell = row >= 0 ? materials->item(row, sizes_column) : nullptr;
		//A line without its code yet is no material
	const bool shown = cell && !code.isEmpty();
	m_size_box->setTitle(shown ? tr("Sizes of %1").arg(code) : tr("Sizes"));
	m_size_box->setEnabled(shown);
	{
		const QSignalBlocker blocker(m_size_table);
		m_size_table->setRowCount(0);
		const QList<PidCodification::Size> sizes = shown ? sizesFromData(cell->data(sizes_role))
														 : QList<PidCodification::Size>();
		for (const PidCodification::Size &size : sizes)
		{
			const int line = m_size_table->rowCount();
			m_size_table->insertRow(line);
			const QMap<int, QVariant> values{{size_nominal_column, size.nominal},
											 {size_outside_column, size.outside},
											 {size_default_column, size.by_default}};
			for (auto it = values.cbegin() ; it != values.cend() ; ++it) {
				auto *item = new QTableWidgetItem();
				item->setData(value_role, it.value());
				m_size_table->setItem(line, it.key(), item);
			}
		}
	}
	decorateSizeRows();
}

/**
	@brief PidConfigPage::decorateSizeRows
	See the declaration
*/
void PidConfigPage::decorateSizeRows()
{
	const QTableWidget *materials = m_code_tables.value(PidCodification::List::Materials);
	const int row = materials->currentRow();
	const QTableWidgetItem *named = row >= 0 ? materials->item(row, 2) : nullptr;
	const bool by_outside = named && named->checkState() == Qt::Checked;
	const bool read_only = m_project && m_project->isReadOnly();
	const QLocale locale = m_codification.locale();
	const QSignalBlocker blocker(m_size_table);
		//How many outside diameters each DN has: a default one is chosen
		//among several
	QMap<int, int> outsides;
	for (int line = 0 ; line < m_size_table->rowCount() ; ++line) {
		if (sizeValue(m_size_table, line, size_outside_column)) {
			++outsides[sizeValue(m_size_table, line, size_nominal_column)];
		}
	}
	for (int line = 0 ; line < m_size_table->rowCount() ; ++line)
	{
		const int outside = sizeValue(m_size_table, line, size_outside_column);
		const int nominal = sizeValue(m_size_table, line, size_nominal_column);
		QTableWidgetItem *outside_item = m_size_table->item(line, size_outside_column);
		QTableWidgetItem *nominal_item = m_size_table->item(line, size_nominal_column);
		QTableWidgetItem *default_item = m_size_table->item(line, size_default_column);
		if (!outside_item || !nominal_item || !default_item) {
			continue;
		}
		outside_item->setText(outside ? PipeDiameter::number(outside, locale) : QString());
		nominal_item->setText(nominal ? QString::number(nominal) : QString());
			//A box where there is something to choose
		const bool choice = by_outside && outside && nominal && outsides.value(nominal) > 1;
		default_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
							   | (choice && !read_only ? Qt::ItemIsUserCheckable : Qt::NoItemFlags));
		default_item->setData(Qt::CheckStateRole,
							  choice ? QVariant(default_item->data(value_role).toBool() ? Qt::Checked
																						 : Qt::Unchecked)
									 : QVariant());
			//What names the pipes of the material is missing
		const bool missing = (outside || nominal) && !(by_outside ? outside : nominal);
		const QString lacking = !missing ? QString()
								: by_outside ? tr("The outside diameter is missing: the pipes of this "
												  "material are named by it.")
											 : tr("The DN is missing: the pipes of this material are "
												  "named by it.");
		for (QTableWidgetItem *item : {outside_item, nominal_item, default_item}) {
			item->setBackground(missing ? QBrush(QColor(255, 200, 120)) : QBrush());
			item->setToolTip(wrapped(lacking));
		}
	}
}

/**
	@brief PidConfigPage::storeSizes
	See the declaration: the sizes go to the cell of the material, which
	follows its line, then to the codification of the page
	@param changed : the cell just changed, whose default size wins over
	another of the same DN
*/
bool PidConfigPage::storeSizes(const QTableWidgetItem *changed)
{
	QTableWidget *materials = m_code_tables.value(PidCodification::List::Materials);
	const int row = materials->currentRow();
	QTableWidgetItem *cell = row >= 0 ? materials->item(row, sizes_column) : nullptr;
	if (!cell) {
		return false;
	}
	PidCodification::Entry entry;
	QList<int> defaults;
	for (int line = 0 ; line < m_size_table->rowCount() ; ++line)
	{
		const int outside = sizeValue(m_size_table, line, size_outside_column);
		const int nominal = sizeValue(m_size_table, line, size_nominal_column);
			//A line added, not filled yet
		if (!outside && !nominal) {
			continue;
		}
		if (!entry.addSize(outside, nominal)) {
			return false;
		}
		if (outside && nominal && sizeValue(m_size_table, line, size_default_column)) {
			if (changed && changed->row() == line) {
				defaults.append(outside);
			} else {
				defaults.prepend(outside);
			}
		}
	}
		//One default by DN, the one just changed last; none where there is
		//nothing to choose
	for (int outside : std::as_const(defaults)) {
		entry.setDefaultSize(outside);
	}
	for (PidCodification::Size &size : entry.sizes) {
		if (size.by_default && entry.outsideCount(size.nominal) < 2) {
			size.by_default = false;
		}
	}
	{
		const QSignalBlocker blocker(m_size_table);
		for (int line = 0 ; line < m_size_table->rowCount() ; ++line)
		{
			const int outside = sizeValue(m_size_table, line, size_outside_column);
			bool by_default = false;
			for (const PidCodification::Size &size : std::as_const(entry.sizes)) {
				if (outside && size.outside == outside) {
					by_default = size.by_default;
				}
			}
			if (QTableWidgetItem *item = m_size_table->item(line, size_default_column)) {
				item->setData(value_role, by_default);
			}
		}
	}
	{
		const QSignalBlocker blocker(materials);
		cell->setData(sizes_role, sizesToData(entry.sizes));
	}
	readCodeTable(PidCodification::List::Materials);
	return true;
}

/**
	@brief PidConfigPage::sizeItemChanged
	A size typed in the list: an outside diameter to the tenth, a whole DN,
	or the box of the default one. What cannot be read, or is in the list
	already, gets its value back, and it is said.
*/
void PidConfigPage::sizeItemChanged(QTableWidgetItem *item)
{
	const QLocale locale = m_codification.locale();
	const QVariant before = item->data(value_role);
	QVariant value;
	QString refused;
	if (item->column() == size_default_column)
	{
		value = item->checkState() == Qt::Checked;
	}
	else
	{
		const bool outside = item->column() == size_outside_column;
		const QString text = item->text().trimmed();
		bool ok = false;
		const PipeDiameter typed = PipeDiameter::fromText(
					text, outside ? PipeDiameter::Outside : PipeDiameter::Nominal, &ok);
		if (!ok || (typed.isSet() && (outside ? !typed.isOutside() : !typed.isNominal()))) {
			refused = outside ? tr("%1 is not an outside diameter: a number of millimetres, to "
								   "the tenth.").arg(text)
							  : tr("%1 is not a DN: a whole number.").arg(text);
		}
		value = !typed.isSet() ? 0 : outside ? typed.tenths() : typed.tenths() / 10;
	}
	if (refused.isEmpty())
	{
		{
			const QSignalBlocker blocker(m_size_table);
			item->setData(value_role, value);
		}
		if (!storeSizes(item))
		{
			const QSignalBlocker blocker(m_size_table);
			item->setData(value_role, before);
			refused = tr("This size is in the list already: an outside diameter has one DN only, "
						 "and a DN without outside diameter is listed once.");
		}
	}
		//Written as the page writes it, or the value back
	decorateSizeRows();
	m_code_message->setText(refused);
}

/**
	@brief PidConfigPage::addSize
	See the declaration: the line stays at the bottom, the list being put in
	order when it is shown again
*/
void PidConfigPage::addSize()
{
	if (!m_size_box->isEnabled()) {
		return;
	}
	const int row = m_size_table->rowCount();
	{
		const QSignalBlocker blocker(m_size_table);
		m_size_table->insertRow(row);
		for (int column = 0 ; column < m_size_table->columnCount() ; ++column) {
			m_size_table->setItem(row, column, new QTableWidgetItem());
		}
	}
	decorateSizeRows();
		//From its first cell, the DN: the tabulation goes on to the others
	m_size_table->setCurrentCell(row, size_nominal_column);
	m_size_table->editItem(m_size_table->item(row, size_nominal_column));
}

/**
	@brief PidConfigPage::removeSelectedSizes
	See the declaration
*/
void PidConfigPage::removeSelectedSizes()
{
	QList<int> rows;
	for (const QModelIndex &index : m_size_table->selectionModel()->selectedRows()) {
		rows << index.row();
	}
	if (rows.isEmpty()) {
		return;
	}
	std::sort(rows.begin(), rows.end(), std::greater<int>());
	{
		const QSignalBlocker blocker(m_size_table);
		for (int row : std::as_const(rows)) {
			m_size_table->removeRow(row);
		}
	}
	storeSizes();
	decorateSizeRows();
	m_code_message->clear();
}

/**
	@brief PidConfigPage::equalDnEdited
	The DN which are the same size of line, typed together; a text which
	cannot be read gets its value back, and it is said
*/
void PidConfigPage::equalDnEdited()
{
	const QString text = m_equal_dn->text();
	const QString before = m_codification.equalNominalsText();
	PidCodification typed;
	if (!typed.setEqualNominalsText(text)) {
		m_code_message->setText(tr("%1 cannot be read: write together the DN which are the same "
								   "size of line, such as 60/65.").arg(text));
	} else if (typed.equalNominalsText() != before) {
		m_codification.equal_nominals = typed.equal_nominals;
		m_code_message->clear();
	}
	m_equal_dn->setText(m_codification.equalNominalsText());
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
			//Letters of instruments: no tag bears them
		if (list == PidCodification::List::Letters) {
			continue;
		}
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
	QMap<QString, QList<PidCodification::Size>> sizes_alone;
	const PidCodification read = PidCodification::fromCsv(text, &skipped, &sizes_alone);
		//The sizes of a material the file does not list go to this material
		//of the page, when the file brings no codes: with codes, or for a
		//material the page has not, they are lines left out
	QStringList unknown;
	int replaced = 0;
	for (auto it = sizes_alone.begin() ; it != sizes_alone.end() ; )
	{
		const PidCodification::Entry *material = read.hasCodeLists() ? nullptr
																	 : m_codification.material(it.key());
		if (material) {
			replaced += material->sizes.isEmpty() ? 0 : 1;
			++it;
			continue;
		}
		skipped += it.value().size();
		if (!read.hasCodeLists()) {
			unknown << it.key();
		}
		it = sizes_alone.erase(it);
	}
	if (read.isEmpty() && sizes_alone.isEmpty()) {
		m_code_message->setText(
					unknown.isEmpty()
					? tr("Nothing could be read from %1. Each line is expected as: "
						 "kind;code;name;family, kind being \"fluid\", \"material\", "
						 "\"unit\" or \"letters\".").arg(path)
					: tr("%1 gives sizes to materials this page has not: %2. Nothing changed.")
					  .arg(path, unknown.join(QStringLiteral(", "))));
		return false;
	}
	if (read.hasCodeLists() && m_codification.hasCodeLists()
		&& !confirm(tr("Replace the codes of this page with %1 fluids, %2 materials, %3 "
					   "functional units and %4 instrument letters?").arg(read.fluids.size())
					.arg(read.materials.size()).arg(read.functional_units.size())
					.arg(read.instrument_letters.size()))) {
		return false;
	}
	if (replaced && !confirm(tr("Replace the sizes of %n material(s) of this page?", "", replaced))) {
		return false;
	}
	m_codification = read.completedWith(m_codification);
		//A file may say the default marks of the signal lines, to take those
		//of the page away: they are not kept
	const bool signal_marks = !read.signal_marks.isEmpty();
	m_codification.dropDefaultSignalMarks();
		//A code read is no code renamed: no line keeps a former code
	if (read.hasCodeLists()) {
		fillCodeTables(false);
	}
	if (!sizes_alone.isEmpty())
	{
			//To the lines of the materials, which keep their former codes
		QTableWidget *materials = m_code_tables.value(PidCodification::List::Materials);
		{
			const QSignalBlocker blocker(materials);
			for (int row = 0 ; row < materials->rowCount() ; ++row) {
				QTableWidgetItem *cell = materials->item(row, sizes_column);
				if (cell && sizes_alone.contains(codeOf(materials, row))) {
					cell->setData(sizes_role, sizesToData(sizes_alone.value(codeOf(materials, row))));
				}
			}
		}
		readCodeTable(PidCodification::List::Materials);
		showMaterialSizes();
	}
	showCodification(false, false);
		//What the files brought: codes, else sizes alone; and a
		//presentation (colours, texts, connections), which replaces that of
		//the page
	const bool presentation = read.hasColors() || read.hasTextSettings();
	QStringList what;
	const int data_fields = int(read.data_fields.size());
	if (read.hasCodeLists() || (sizes_alone.isEmpty() && !presentation && !signal_marks && !data_fields)) {
		what << tr("%1 fluids, %2 materials, %3 functional units and %4 instrument letters")
				.arg(read.fluids.size()).arg(read.materials.size())
				.arg(read.functional_units.size()).arg(read.instrument_letters.size());
	} else if (!sizes_alone.isEmpty()) {
		what << tr("the sizes of %n material(s)", "", int(sizes_alone.size()));
	}
	if (presentation) {
		what << tr("the presentation (tabs Colors, Texts and Connections)");
	}
		//The marks of the signal lines apart: a file may bring them alone
	if (signal_marks) {
		what << tr("the marks of the signal lines");
	}
		//So are the data of the symbols, which replace those of the page
	if (data_fields) {
		what << tr("%n data field(s) of the symbols", "", data_fields);
	}
	QString message = tr("Read: %1. Apply this page to keep them.").arg(what.join(QStringLiteral("; ")));
	if (skipped) {
		message += QStringLiteral(" ") + tr("%n line(s) could not be read.", "", skipped);
	}
	if (!unknown.isEmpty()) {
		message += QStringLiteral(" ") + tr("Materials this page has not: %1.")
										 .arg(unknown.join(QStringLiteral(", ")));
	}
		//A material without sizes: none of its pipes can have a size
	const QStringList without = read.hasCodeLists() ? m_codification.materialsWithoutSizes() : QStringList();
	if (!without.isEmpty()) {
		message += QStringLiteral(" ") + tr("Materials without sizes: %1.").arg(without.join(QStringLiteral(", ")));
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
