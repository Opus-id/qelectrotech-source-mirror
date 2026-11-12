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
#include "../../undocommand/changepidcodificationcommand.h"
#include "projectconfigpages.h"

#include <functional>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTableWidgetItem;

/**
	@brief The PidConfigPage class
	What is set for the P&IDs of a project, in tabs. Its codification: the
	codes of its fluids, materials and functional units, edited in a table
	or read from a file. A pipe line keeps a code: a name changed touches no
	pipe, a code changed is given to the pipes and symbols bearing it when
	the page is applied, a code removed stays on them as free text, and a
	code is unique in its list. Its colours, by family
	of fluid, by family of object and by status of works (PidCodification):
	the code knows no colour of its own, what has none is drawn in black, as
	before. The sizes of each material, an outside diameter and its DN each,
	in a list beside the materials, which shows those of the material of the
	current line; and the DN of the project which are the same size of line.
	The size of its texts by category: what has none keeps the size
	of its symbol. The connections drawn at the end of the nozzles: none by
	default.
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
			/// Keep the codes, the sizes of the materials and the equal DN
			/// of this page as those of the new projects, or none
			/// when it has none; apart from the palette, other projects stay
			/// as they are
		void saveAsDefaultCodification();

			/// Read the file @p path in this page: its codes replace those of
			/// the page, its colours and texts those of the page when it has
			/// some; the project takes them when the page is applied. A file
			/// without codes may give sizes to the materials of the page:
			/// they replace those of the materials it names, the others
			/// keep theirs. False when nothing could be read, or when the
			/// user said no.
		bool importCsv(const QString &path);
			/// The same with the files @p paths, read one after the other as
			/// one: a file of codes and one of sizes, for instance
		bool importCsv(const QStringList &paths);
			/// Write the codification of this page, codes then palette, in
			/// the file @p path, which importCsv() reads back
		bool exportCsv(const QString &path);
			/// The codes changed in this page, which the pipes and symbols
			/// bearing them will take when it is applied
		ChangePidCodificationCommand::CodeChanges codeChanges() const;
			/// What asks a question, a message box unless a test says
			/// otherwise; it answers true for yes
		void setConfirmer(const std::function<bool(const QString &)> &confirm) { m_confirm = confirm; }
			/// The table of the list @p list
		QTableWidget *codeTable(PidCodification::List list) const { return m_code_tables.value(list); }
			/// Add an empty line to the list shown, and edit its code
		void addEntry();
			/// Remove the lines selected of the list shown, after a question
			/// when pipes or symbols bear their codes
		void removeSelectedEntries();
			/// What the page says of the last change of its codes
		QString codeMessage() const;
			/// The sizes of the material of the current line of the
			/// materials: its DN, its outside diameter, and a box checked
			/// for the default outside diameter of a DN which has several
		QTableWidget *sizeTable() const { return m_size_table; }
			/// Add an empty line to the sizes shown, and edit it
		void addSize();
			/// Remove the lines selected of the sizes shown
		void removeSelectedSizes();

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
		void readConnectionWidgets();
		void undoLastChange();
		void redoLastChange();
		void showCodification(bool fill_codes, bool codes_from_project);
		void fillCodeTables(bool from_project);
		void codeItemChanged(QTableWidgetItem *item);
		void readCodeTable(PidCodification::List list);
		PidCodification::List shownList() const;
		bool confirm(const QString &question);
		void showSizes();
			/// Fill the list of sizes with those of the material of the
			/// current line of the materials; none, greyed, without one
		void showMaterialSizes();
			/// How each line of the list of sizes looks: its numbers as the
			/// page writes them, its box where there is a default one to
			/// choose, in orange the line which lacks what names the pipes
		void decorateSizeRows();
			/// Give the material shown the sizes of the list; false, nothing
			/// changed, when two lines cannot go together
		bool storeSizes(const QTableWidgetItem *changed = nullptr);
		void sizeItemChanged(QTableWidgetItem *item);
		void equalDnEdited();

		QTableWidget *m_size_table = nullptr;
		QGroupBox *m_size_box = nullptr;
		QPushButton *m_add_size = nullptr;
		QPushButton *m_remove_size = nullptr;
			/// The DN of the project which are the same size of line, typed
			/// together: "60/65"
		QLineEdit *m_equal_dn = nullptr;

		QCheckBox *m_show_nozzle_sizes = nullptr;
			/// Whether a DN under a nozzle is written "DN 100" or "100"
		QCheckBox *m_nozzle_dn_prefix = nullptr;
		QComboBox *m_decimal = nullptr;

		QTabWidget *m_code_tabs = nullptr;
		QMap<PidCodification::List, QTableWidget *> m_code_tables;
		QPushButton *m_add_entry = nullptr;
		QPushButton *m_remove_entry = nullptr;
		QPushButton *m_import = nullptr;
		QPushButton *m_export = nullptr;
		QLabel *m_code_message = nullptr;
		std::function<bool(const QString &)> m_confirm;

		QTableWidget *m_table = nullptr;
		QPushButton *m_choose = nullptr;
		QPushButton *m_clear = nullptr;
		QPushButton *m_save_palette = nullptr;
		QPushButton *m_save_codification = nullptr;
			/// What the page says of the last default saved
		QLabel *m_default_saved = nullptr;
			/// The size of each category of texts, 0 for that of the symbol
		QMap<QString, QSpinBox *> m_text_sizes;
			/// What gives each category of texts back the size of the symbol
		QMap<QString, QPushButton *> m_text_size_resets;
		QCheckBox *m_colored_labels = nullptr;
		QCheckBox *m_nozzle_texts_on_paper = nullptr;
			/// Whether the mark of each connection is drawn at the end of
			/// the nozzles; a plain end has none
		QMap<PipeConnection::Type, QCheckBox *> m_drawn_connections;
			/// Whether the connections are drawn on each family of symbols,
			/// by key of PidCodification::connectionFamilies()
		QMap<QString, QCheckBox *> m_drawn_families;
			/// What the page shows, applied to the project when validated
		PidCodification m_codification;
		std::function<QColor(const QColor &)> m_ask_color;
};

#endif // PIDCONFIGPAGE_H
