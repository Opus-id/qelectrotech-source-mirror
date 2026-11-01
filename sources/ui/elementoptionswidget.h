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
#ifndef ELEMENTOPTIONSWIDGET_H
#define ELEMENTOPTIONSWIDGET_H

#include "../properties/elementoptions.h"
#include "abstractelementpropertieseditorwidget.h"

#include <QList>
#include <QPair>

class QComboBox;
class QFormLayout;

/**
	@brief The ElementOptionsWidget class
	The tab of the properties of a placed element where the options of its
	symbol are chosen (ElementOptions): one list per option, such as the
	motor of a pump. Live, a choice is applied at once, with undo.
*/
class ElementOptionsWidget : public AbstractElementPropertiesEditorWidget
{
		Q_OBJECT
	public:
		explicit ElementOptionsWidget(Element *element, QWidget *parent = nullptr);

			/// Whether the symbol of @p element offers options, so that this
			/// tab is worth showing
		static bool hasOptions(const Element *element);

		void setElement(Element *element) override;
		void apply() override;
		QUndoCommand *associatedUndo() const override;
		QString title() const override;
		void updateUi() override;
		bool setLiveEdit(bool live_edit) override;

			/// The choices the lists give, the others of the element kept; a
			/// default chosen is no choice, so that the element is saved as
			/// one that chose nothing
		ElementOptions::Choices chosen() const;
			/// The list of the option @p name, or nullptr
		QComboBox *comboFor(const QString &name) const;

	private:
		void buildLists();

		QFormLayout *m_layout = nullptr;
			/// One list per option, with the name of its option
		QList<QPair<QString, QComboBox *>> m_lists;
};

#endif // ELEMENTOPTIONSWIDGET_H
