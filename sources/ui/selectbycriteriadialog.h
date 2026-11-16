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
#ifndef SELECT_BY_CRITERIA_DIALOG_H
#define SELECT_BY_CRITERIA_DIALOG_H

#include "../pidcodification.h"
#include "../selectioncategories.h"

#include <QDialog>
#include <QGraphicsItem>
#include <QList>
#include <QSet>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QRadioButton;
class QTreeWidget;

/**
	@brief The SelectByCriteriaDialog class
	Edit > Selection > Select by criteria: the categories of the selection,
	or of the whole folio (SelectionCategories), in a tree of check boxes,
	all checked; what is checked is "found".

	What the selection becomes depends on the action: Replace, the one at
	each opening, selects what is found; Add adds it to the selection of
	departure, Remove takes it out. The selection of departure is the one
	at the opening, then the one each Apply leaves, so that actions follow
	each other. Below the tree, how many items will be selected.

	The result is applied (applied()) by Apply, by OK, and at each change
	when "Preview on the folio" is checked; "Zoom on the selection" asks the
	view to frame it each time. Both boxes keep the last choice of the user
	(settings). Who opened the dialog puts the selection and the view back
	on Cancel.

	It is not modal, so that the folio can be zoomed and scrolled while
	it is open: it keeps its own copy of the categories.
*/
class SelectByCriteriaDialog : public QDialog
{
	Q_OBJECT

	public:
		enum class Action { Replace, Add, Remove };

			/**
				@p selection: the selection at the opening; @p folio: all
				the items of the folio; @p codes: the codification of the
				project, which writes the values. The tree starts on the
				selection, on the folio when nothing is selected.
			*/
		SelectByCriteriaDialog(const QList<QGraphicsItem *> &selection,
							   const QList<QGraphicsItem *> &folio,
							   const PidCodification &codes,
							   QWidget *parent = nullptr);

			/// The keys of the leaves checked
		QSet<QString> checked() const;
			/// The items found: those which pass the leaves checked
		QList<QGraphicsItem *> foundItems() const;
			/// What the selection becomes, by the action
		QList<QGraphicsItem *> resultItems() const;
			/// How many items resultItems() holds
		int keptCount() const;
		void setAllChecked(bool checked);
		QTreeWidget *tree() const { return m_tree; }
		bool previews() const;
		bool zooms() const;
		void setPreview(bool preview);
		void setZoom(bool zoom);
		Action action() const { return m_action; }
		void setAction(Action action);
		bool searchesFolio() const;
		void setSearchFolio(bool folio);
			/// Apply the result, which becomes the selection of departure
			/// of the next action, as the Apply button does
		void apply();

	signals:
			/// Select @p items and nothing else, and frame them in the
			/// view when @p zoom
		void applied(const QList<QGraphicsItem *> &items, bool zoom);

	private:
		const SelectionCategories &categories() const;
		void fillTree();
		void scheduleCount();
		void updateCount();
		void preview();

		const PidCodification m_codes;
		QList<QGraphicsItem *> m_departure;
			/// Those of m_departure
		SelectionCategories m_in_selection;
		const SelectionCategories m_in_folio;
		Action m_action = Action::Replace;
		QTreeWidget *m_tree = nullptr;
		QLabel *m_count = nullptr;
		QRadioButton *m_search_selection = nullptr;
		QRadioButton *m_search_folio = nullptr;
		QButtonGroup *m_actions = nullptr;
		QCheckBox *m_preview = nullptr;
		QCheckBox *m_zoom = nullptr;
		bool m_count_pending = false;
};

#endif
