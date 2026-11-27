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
#ifndef PIDSELECTIONTAGWIDGET_H
#define PIDSELECTIONTAGWIDGET_H

#include <PropertiesEditor/propertieseditorwidget.h>

#include <QPointer>

class Element;
class MultiElementInfoWidget;
class QCheckBox;
class QComboBox;
class QETProject;
class QLabel;
class QLineEdit;
class QPushButton;

/**
	@brief The PidSelectionTagWidget class
	The tag of several P&ID symbols at once, above the information of the
	selection (MultiElementInfoWidget, kept as it is): "on dessine d'abord,
	on repère ensuite". It edits the symbols of the selection which a model
	of tag of their project labels (taggedElements()), the others being left
	alone:
	- the functional unit, the plant of QElectroTech, the very key the
	  "Plant" row below writes, the units of the codification offered;
	- the train;
	- "Number": the symbols without an order get the next ones, folio by
	  folio, then from left to right and from top to bottom
	  (PidTag::newOrders()); with "Number again", every symbol of the
	  selection, the orders it had being freed.
	Giving a unit or a train numbers nothing. Each change is one undo step,
	never joined to another one.
*/
class PidSelectionTagWidget : public PropertiesEditorWidget
{
		Q_OBJECT

	public:
		explicit PidSelectionTagWidget(const QList<Element *> &elements,
									   int other_items = 0,
									   QWidget *parent = nullptr);

			/// The elements of @p elements whose tag a model of their project
			/// makes: equipments, valves and instruments
		static QList<Element *> taggedElements(const QList<Element *> &elements);
			/// @p elements has at least one of them: the selection takes this
			/// editor rather than MultiElementInfoWidget alone
		static bool isWanted(const QList<Element *> &elements);

		void setElements(const QList<Element *> &elements, int other_items = 0);
		MultiElementInfoWidget *informationWidget() const {return m_information;}

		void apply() override;
		void reset() override;
		QUndoCommand *associatedUndo() const override;
		QString title() const override;
		void updateUi() override;
		bool setLiveEdit(bool live_edit) override;

			/// "Number", as the button: the pending changes applied first
			/// @return how many symbols were given an order
		int number();
			/// The change "Number" would make, or nullptr when none
		QUndoCommand *numberingUndo() const;

			/// As the user would: type, tick
		void setUnitText(const QString &text);
		void setTrainText(const QString &text);
		void setNumberAgain(bool again);
		QString unitText() const;
		QString trainText() const;
		bool unitIsMixed() const {return m_unit_mixed;}
		bool trainIsMixed() const {return m_train_mixed;}
			/// The units offered, as their codes, in their order
		QStringList offeredUnits() const;

	private:
		QList<Element *> tagged() const;
		QETProject *project() const;
		QString unitCode() const;
		bool unitIsSet() const;
		bool trainIsSet() const;
		void fillUnits();
		void readElements();
		void showState();
		void ownFieldDone();
		void elementInfoChanged();
		void push(QUndoCommand *undo);

		MultiElementInfoWidget *m_information = nullptr;
		QList<QPointer<Element>> m_tagged;
		int m_selected = 0;
		QPointer<QETProject> m_project;

		QLabel *m_header = nullptr;
		QComboBox *m_unit = nullptr;
		QLineEdit *m_train = nullptr;
		QCheckBox *m_again = nullptr;
		QPushButton *m_number = nullptr;
		QLabel *m_result = nullptr;

		QString m_unit_initial;
		QString m_train_initial;
		bool m_unit_mixed = false;
		bool m_train_mixed = false;
		bool m_updating = false;
		bool m_applying = false;
		bool m_refresh_pending = false;
};

#endif // PIDSELECTIONTAGWIDGET_H
