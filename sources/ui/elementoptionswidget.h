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

#include "../ElementsCollection/elementslocation.h"
#include "../properties/elementoptions.h"
#include "abstractelementpropertieseditorwidget.h"

#include <QList>
#include <QPair>
#include <QSize>

class QComboBox;
class QFormLayout;
class QIcon;
class QPalette;
class QUndoCommand;

/**
	@brief The ElementOptionsWidget class
	The "Type and options" section of the P&ID tab of the properties of a
	placed element: first the list of the types its symbol can be swapped
	for ("Change type"), then the options of its symbol (ElementOptions),
	one list per option, such as the motor of a pump, then its status of
	works. A choice is applied at once, with undo, in the dialog of the
	element as in the dock (the drawing follows it); a type picked as well,
	as by the context menu.
*/
class ElementOptionsWidget : public AbstractElementPropertiesEditorWidget
{
		Q_OBJECT
	public:
		explicit ElementOptionsWidget(Element *element, QWidget *parent = nullptr);

			/// Whether the symbol of @p element offers options, so that this
			/// tab is worth showing
		static bool hasOptions(const Element *element);
			/// The drawing of the symbol at @p location with @p choices, for a
			/// list or a menu, its ink fit for @p palette
		static QIcon drawingIcon(const ElementsLocation &location,
								 const ElementOptions::Choices &choices,
								 const QPalette &palette);
			/// One undo step giving each of @p elements the value @p value of
			/// its option @p option; nullptr when none changes
		static QUndoCommand *choose(const QList<Element *> &elements,
									const QString &option, const QString &value);
			/// The size of the drawings in the list shown of a type or of an
			/// option
		static QSize listIconSize() { return QSize(64, 32); }

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
			/// The list of the types: the symbol of the element and those
			/// it can be swapped for, by name
		QComboBox *typeList() const { return m_type; }
			/// The symbol of each line of typeList()
		QList<ElementsLocation> types() const { return m_types; }

	protected:
		bool eventFilter(QObject *watched, QEvent *event) override;

	private:
		void buildLists();
		void buildTypeList();
			/// Drawings large enough to be read in the list shown of @p list,
			/// the list closed keeping the height of the other fields
		static void showLargeDrawings(QComboBox *list);
		void typePicked(int index);

		QFormLayout *m_layout = nullptr;
			/// The type of the element, then the options
		QComboBox *m_type = nullptr;
		QList<ElementsLocation> m_types;
			/// One list per option, with the name of its option
		QList<QPair<QString, QComboBox *>> m_lists;
			/// The status of works, for a symbol of a family of objects
		QComboBox *m_status = nullptr;
};

#endif // ELEMENTOPTIONSWIDGET_H
