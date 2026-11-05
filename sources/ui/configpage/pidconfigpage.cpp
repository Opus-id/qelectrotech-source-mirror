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

#include <QColorDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
	constexpr int group_role = Qt::UserRole;
	constexpr int key_role = Qt::UserRole + 1;
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
	m_project->setPidCodification(m_codification);
}

void PidConfigPage::initWidgets()
{
	m_table = new QTableWidget(0, 3, this);
	m_table->setHorizontalHeaderLabels({tr("Applies to"), tr("Name"), tr("Color")});
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->verticalHeader()->hide();
	m_table->horizontalHeader()->setStretchLastSection(true);
	connect(m_table, &QTableWidget::cellDoubleClicked, this, [this]() { chooseColor(); });

	m_choose = new QPushButton(tr("Choose a color..."), this);
	m_clear = new QPushButton(tr("No color"), this);
	connect(m_choose, &QPushButton::clicked, this, &PidConfigPage::chooseColor);
	connect(m_clear, &QPushButton::clicked, this, &PidConfigPage::clearColor);
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

	auto *tabs = new QTabWidget(this);
	tabs->addTab(colors_tab, tr("Colors"));
	auto *page_layout = new QVBoxLayout(this);
	page_layout->addWidget(tabs);
}

void PidConfigPage::readValuesFromProject()
{
	m_codification = m_project->pidCodification();
	fillTable();
}

void PidConfigPage::adjustReadOnly()
{
	const bool read_only = m_project->isReadOnly();
	m_table->setEnabled(!read_only);
	m_choose->setEnabled(!read_only);
	m_clear->setEnabled(!read_only);
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
	const int row_shown = m_table->currentRow();
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
	if (row_shown >= 0) {
		m_table->selectRow(row_shown);
	}
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
	Ask a colour for the line selected
*/
void PidConfigPage::chooseColor()
{
	const int row = m_table->currentRow();
	if (row < 0) {
		return;
	}
	const auto group = Group(m_table->item(row, 0)->data(group_role).toInt());
	const QString key = m_table->item(row, 0)->data(key_role).toString();
	const QColor color = QColorDialog::getColor(colors(group).value(key, Qt::black), this, title());
	if (color.isValid()) {
		setColor(group, key, color);
	}
}

/**
	@brief PidConfigPage::clearColor
	Take the colour of the line selected away
*/
void PidConfigPage::clearColor()
{
	const int row = m_table->currentRow();
	if (row >= 0) {
		setColor(Group(m_table->item(row, 0)->data(group_role).toInt()),
				 m_table->item(row, 0)->data(key_role).toString(), QColor());
	}
}
