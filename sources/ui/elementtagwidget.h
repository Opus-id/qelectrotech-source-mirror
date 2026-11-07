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
#ifndef ELEMENTTAGWIDGET_H
#define ELEMENTTAGWIDGET_H

#include "abstractelementpropertieseditorwidget.h"

#include <diagramcontext.h>

#include <QPointer>

class QComboBox;
class QLabel;
class QLineEdit;
class QETProject;

/**
	@brief The ElementTagWidget class
	The tab of the properties of a placed P&ID symbol where its tag is made
	(PidTag): its functional unit, picked in the list of its project or
	typed, its train and its order; the identifier comes from the symbol.
	The tab shows the label the model of the project makes of them, and
	tells when another element of the project already has it. Live, a field
	is applied when it is left, with undo.
*/
class ElementTagWidget : public AbstractElementPropertiesEditorWidget
{
		Q_OBJECT
	public:
		explicit ElementTagWidget(Element *element, QWidget *parent = nullptr);

			/// Whether @p element is a P&ID symbol that takes a tag: an
			/// equipment, a valve or an instrument
		static bool isWanted(const Element *element);

		void setElement(Element *element) override;
		void apply() override;
		QUndoCommand *associatedUndo() const override;
		QString title() const override;
		void updateUi() override;
		bool setLiveEdit(bool live_edit) override;

			/// The information of the element with the fields of the tab and
			/// the label they make
		DiagramContext edited() const;

		QComboBox *unitField() const { return m_unit; }
		QLineEdit *trainField() const { return m_train; }
		QLineEdit *orderField() const { return m_order; }
		QLabel *labelShown() const { return m_label; }
		QLabel *duplicateShown() const { return m_duplicate; }

	private:
		void fillUnits();
		QString unitCode() const;
		void showLabel(const DiagramContext &information);
		void applyIfLive();

		QComboBox *m_unit = nullptr;
		QLabel *m_identifier = nullptr;
		QLineEdit *m_train = nullptr;
		QLineEdit *m_order = nullptr;
		QLabel *m_label = nullptr;
		QLabel *m_duplicate = nullptr;
		QPointer<QETProject> m_project;
};

#endif // ELEMENTTAGWIDGET_H
