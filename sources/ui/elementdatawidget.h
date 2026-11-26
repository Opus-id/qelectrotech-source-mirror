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
#ifndef ELEMENTDATAWIDGET_H
#define ELEMENTDATAWIDGET_H

#include "abstractelementpropertieseditorwidget.h"

#include <diagramcontext.h>
#include <pidcodification.h>

#include <QList>
#include <QPointer>

class QFormLayout;
class QLabel;
class QETProject;

/**
	@brief The ElementDataWidget class
	The section of the P&ID tab of a placed symbol where its data are typed:
	one line for each datum its project declares for the symbols of its class
	(PidCodification::data_fields; the class is the pid_class of the symbol),
	named in clear, with its unit, as a text, a number, a list or a check box.

	Nothing is kept here: a value is an information of the element, as a
	custom property of QElectroTech is (the key "pid_" then the name of the
	datum), written only when it is filled, which a text of the symbol may
	show. The Informations tab does not show these keys for a P&ID symbol
	(hides()); so that none is ever hidden everywhere, a filled one the
	project does not declare for this class is shown here under its key, as
	a text.

	A standby is installed (PidCodification::standbyKey()) or kept in its
	crate (spareKey()), never both: ticking one unticks the other.

	Live, a line is applied when it is left, with undo.
*/
class ElementDataWidget : public AbstractElementPropertiesEditorWidget
{
		Q_OBJECT
	public:
		explicit ElementDataWidget(Element *element, QWidget *parent = nullptr);

			/// The lines of @p element: the data its project declares for its
			/// class, then the "pid_" information it has filled which the
			/// project does not declare and the Tag section does not show, as
			/// texts named by their key (no label). None for a symbol which
			/// is not a P&ID one.
		static QList<PidCodification::DataField> fieldsOf(const Element *element);
			/// Whether @p element has lines
		static bool isWanted(const Element *element);
			/// The Informations tab is not to show the information @p key of
			/// @p element: a "pid_" one of a P&ID symbol, which has its place
			/// in the P&ID tab. Never for an electrical element.
		static bool hides(const Element *element, const QString &key);

		void setElement(Element *element) override;
		void apply() override;
		QUndoCommand *associatedUndo() const override;
		QString title() const override;
		void updateUi() override;
		bool setLiveEdit(bool live_edit) override;

			/// The information of the element with the lines of the section
		DiagramContext edited() const;
			/// The field of the information @p key (a QLineEdit, a QComboBox
			/// or a QCheckBox), nullptr when there is none
		QWidget *fieldOf(const QString &key) const;
		QLabel *warningShown() const { return m_warning; }

	private:
		struct Row
		{
			PidCodification::DataField field;
			QWidget *editor = nullptr;
				/// The text a line of text was given: while it has it, the
				/// line was not touched
			QString shown;
		};

		void buildRows(const QList<PidCodification::DataField> &fields);
		QString valueOf(const Row &row, const QString &held) const;
		void showWarning();
		void fieldDone();
		QChar decimalSign() const;

		QFormLayout *m_form = nullptr;
		QList<Row> m_rows;
		QLabel *m_warning = nullptr;
		QPointer<QETProject> m_project;
};

#endif // ELEMENTDATAWIDGET_H
