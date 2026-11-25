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
#include <QPushButton>
#include <QScrollArea>
#include "configdialog.h"
#include "ui/configpage/configpages.h"
#include "qetapp.h"
#include "qet.h"

#include "machine_info.h"

/**
	Constructeur
	@param parent QWidget parent
*/
ConfigDialog::ConfigDialog(QWidget *parent) : QDialog(parent) {
	// liste des pages
	pages_list = new QListWidget();
	pages_list -> setViewMode(QListView::IconMode);
		//Icons of 48 on every screen, a little space: the pages of both
		//windows are all in view on a 1080 screen. Icons of 128 above
		//1080 (64 below) and a space of 16 showed five or six of them,
		//the list scrolled for the rest
	pages_list -> setIconSize(QSize(48, 48));
	pages_list -> setMovement(QListView::Static);
	pages_list -> setMinimumWidth(168);
	pages_list -> setMaximumWidth(168);
	pages_list -> setSpacing(8);
	pages_list -> setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	// pages
	pages_widget = new QStackedWidget();
	// boutons
	buttons = new QDialogButtonBox(
				QDialogButtonBox::Ok
				|QDialogButtonBox::Apply
				|QDialogButtonBox::Cancel);


		//Low screens: each page scrolls on its own (addPage()), the list
		//and the buttons stay. One scroll area around the list and all
		//the pages made every page as tall as the tallest one, so a short
		//page scrolled too, buttons of its own included
	QHBoxLayout *hlayout1 = new QHBoxLayout();
	hlayout1 -> addWidget(pages_list);
	hlayout1 -> addWidget(pages_widget);

	// Add a layout for QDialog
	QVBoxLayout *dialog_layout = new QVBoxLayout(this);
	dialog_layout -> addLayout(hlayout1);
	dialog_layout -> addWidget(buttons);
	setLayout(dialog_layout);

	// connexion signaux / slots
	connect(buttons, &QDialogButtonBox::accepted, this, &ConfigDialog::applyConf);
	connect(buttons, &QDialogButtonBox::rejected, this, &ConfigDialog::reject);
	connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
			this, &ConfigDialog::applyPages);
	connect(pages_list, &QListWidget::currentRowChanged, pages_widget, &QStackedWidget::setCurrentIndex);

	// set maximum a bit smaller than available size = (screen-size - Task-Bar):
	setMaximumSize((int)(0.94 * MachineInfo::instance()->i_max_available_width()),
				   (int)(0.94 * MachineInfo::instance()->i_max_available_height()));
	resize(std::min(1400,maximumWidth()),
		   std::min(1000,maximumHeight()));

#ifdef Q_OS_MACOS
	if (parent) {
		setWindowFlags(Qt::Sheet);
	}
#endif

	QET::trackDialogGeometry(this);
}

/// Destructeur
ConfigDialog::~ConfigDialog()
{
}

/**
	Construit la liste des pages sur la gauche
*/
void ConfigDialog::buildPagesList()
{
	pages_list -> clear();
	foreach(ConfigPage *page, pages) {
		addPageToList(page);
	}
}

/**
	Add the \a page ConfigPage to this configuration dialog.
*/
void ConfigDialog::addPageToList(ConfigPage *page) {
	QListWidgetItem *new_button = new QListWidgetItem(pages_list);
	new_button -> setIcon(page -> icon());
	new_button -> setText(page -> title());
	new_button -> setTextAlignment(Qt::AlignHCenter);
	new_button -> setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

	// An item is as wide as its title, and the items are put on the left of
	// the list: the icons were not one above the other. Every item is given
	// the width of the list instead, where its icon and its title are
	// centred. A title wider than the list keeps its own width, as before.
	const QSize natural = pages_list -> sizeHintForIndex(
				pages_list -> model() -> index(pages_list -> row(new_button), 0));
	const int width = pages_list -> maximumWidth() - 2 * pages_list -> frameWidth()
					  - 2 * pages_list -> spacing();
	if (natural.isValid() && natural.width() < width) {
		new_button -> setSizeHint(QSize(width, natural.height()));
	}
}

/**
	Applique la configuration de toutes les pages
*/
void ConfigDialog::applyConf()
{
	applyPages();
	accept();
}

/**
	@brief ConfigDialog::applyPages
	Apply what every page holds and stay open, to see the result
*/
void ConfigDialog::applyPages()
{
	foreach(ConfigPage *page, pages) {
		page -> applyConf();
	}
}

/**
	Ajoute une page au dialogue de configuration
*/
void ConfigDialog::addPage(ConfigPage *page) {
	if (!page || pages.contains(page)) return;
	pages << page;
		//A scroll area of its own: the page takes the height of the
		//window, and scrolls only when it is taller
	auto scroll = new QScrollArea();
	scroll -> setFrameShape(QFrame::NoFrame);
	scroll -> setWidgetResizable(true);
	scroll -> setWidget(page);
	pages_widget -> addWidget(scroll);
	addPageToList(page);
}

/**
	@brief ConfigDialog::setCurrentPage
	Set the current index to index
	@param index
*/
void ConfigDialog::setCurrentPage(const int index) {
	pages_list->setCurrentRow(index);
}
