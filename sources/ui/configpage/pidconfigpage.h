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
#ifndef PIDCONFIGPAGE_H
#define PIDCONFIGPAGE_H

#include "../../pidcodification.h"
#include "projectconfigpages.h"

class QPushButton;
class QTableWidget;

/**
	@brief The PidConfigPage class
	What is set for the P&IDs of a project, in tabs. Its colours, by family
	of fluid, by family of object and by status of works (PidCodification):
	the code knows no colour of its own, what has none is drawn in black, as
	before.
*/
class PidConfigPage : public ProjectConfigPage
{
		Q_OBJECT
	public:
			/// What a colour is set for
		enum Group { FluidFamily, ObjectFamily, Status };

		explicit PidConfigPage(QETProject *project, QWidget *parent = nullptr);

		QString title() const override;
		QIcon icon() const override;
		void applyProjectConf() override;

			/// Give the entry @p key of @p group the colour @p color, none
			/// when it is invalid
		void setColor(Group group, const QString &key, const QColor &color);
			/// Give the lines selected the colour @p color, none when it is
			/// invalid
		void setColorOfSelection(const QColor &color);

	protected:
		void initWidgets() override;
		void initLayout() override;
		void readValuesFromProject() override;
		void adjustReadOnly() override;

	private:
		void fillTable();
		void chooseColor();
		QList<int> selectedRows() const;
		QMap<QString, QColor> &colors(Group group);

		QTableWidget *m_table = nullptr;
		QPushButton *m_choose = nullptr;
		QPushButton *m_clear = nullptr;
			/// What the page shows, applied to the project when validated
		PidCodification m_codification;
};

#endif // PIDCONFIGPAGE_H
