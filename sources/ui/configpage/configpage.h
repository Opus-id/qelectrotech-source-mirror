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
#ifndef CONFIGPAGE_H
#define CONFIGPAGE_H

#include <QFrame>
#include <QScrollArea>
#include <QTabWidget>
#include <QWidget>

/**
	This abstract class specifies methods all derived classes should
	implement.
*/
class ConfigPage : public QWidget {
	Q_OBJECT
	public:
	/**
		Constructor
		@param parent Parent QWidget
	*/
	ConfigPage(QWidget *parent) : QWidget(parent) {};
	/// Destructor
	~ConfigPage() override {};
	/// Apply the configuration after user input
	virtual void applyConf() = 0;
	/// @return the configuration page title
	virtual QString title() const = 0;
	/// @return the configuration page icon
	virtual QIcon icon() const = 0;

	protected:
	/**
		Put each tab of @p tabs in a scroll area of its own, once they
		are all added: a tab widget is as tall as its tallest tab, so a
		short tab scrolled with the tallest one; now only a tab taller
		or wider than the window scrolls, the tab bar and what the page
		holds beside the tabs stay in view.
	*/
	static void scrollTabs(QTabWidget *tabs)
	{
		const int current = tabs->currentIndex();
		for (int i = 0 ; i < tabs->count() ; ++i) {
			QWidget *tab = tabs->widget(i);
			const QString title = tabs->tabText(i);
			const QIcon icon = tabs->tabIcon(i);
			auto *scroll = new QScrollArea();
			scroll->setFrameShape(QFrame::NoFrame);
			scroll->setWidgetResizable(true);
			tabs->removeTab(i);
			scroll->setWidget(tab);
			tabs->insertTab(i, scroll, icon, title);
		}
		tabs->setCurrentIndex(current);
	}
};

#endif
