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

#include <functional>

class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

/**
	@brief The PidConfigPage class
	What is set for the P&IDs of a project, in tabs. Its colours, by family
	of fluid, by family of object and by status of works (PidCodification):
	the code knows no colour of its own, what has none is drawn in black, as
	before. The size of its texts by category: what has none keeps the size
	of its symbol.
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
			/// What asks a colour, the colour dialog unless a test says
			/// otherwise; it answers an invalid colour when cancelled
		void setColorAsker(const std::function<QColor(const QColor &)> &asker) { m_ask_color = asker; }
			/// Keep the colours and the settings of texts of this page as
			/// those of the new projects, or none when it has none; other
			/// projects stay as they are
		void saveAsDefaultPalette();

	protected:
		void initWidgets() override;
		void initLayout() override;
		void readValuesFromProject() override;
		void adjustReadOnly() override;
		bool eventFilter(QObject *watched, QEvent *event) override;

	private:
		void fillTable();
		void chooseColor();
		QList<int> selectedRows() const;
		QMap<QString, QColor> &colors(Group group);
		void readTextWidgets();
		void undoLastChange();
		void redoLastChange();

		QTableWidget *m_table = nullptr;
		QPushButton *m_choose = nullptr;
		QPushButton *m_clear = nullptr;
		QPushButton *m_save_palette = nullptr;
		QLabel *m_palette_saved = nullptr;
			/// The size of each category of texts, 0 for that of the symbol
		QMap<QString, QSpinBox *> m_text_sizes;
			/// What gives each category of texts back the size of the symbol
		QMap<QString, QPushButton *> m_text_size_resets;
		QCheckBox *m_colored_labels = nullptr;
		QCheckBox *m_nozzle_texts_on_paper = nullptr;
			/// What the page shows, applied to the project when validated
		PidCodification m_codification;
		std::function<QColor(const QColor &)> m_ask_color;
};

#endif // PIDCONFIGPAGE_H
