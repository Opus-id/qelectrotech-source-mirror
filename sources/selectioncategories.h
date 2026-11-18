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
#ifndef SELECTION_CATEGORIES_H
#define SELECTION_CATEGORIES_H

#include <QCoreApplication>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

class Diagram;
class PidCodification;
class QGraphicsItem;

/**
	@brief The SelectionCategories class
	What Edit > Selection > Select by criteria shows: the items of a
	selection sorted into a tree of categories, each with a check box, so
	that only those checked stay selected.

	The symbols are sorted by family of objects (P&ID), then by symbol; the
	pipes, the lines of signal, the conductors, the texts... each make a
	category. Beside these, some branches cross the others: "By size",
	"By fluid"... for the pipes, "By type of signal" and "By status of works"
	for the signal lines, "By functional unit" and "By status of works" for
	the symbols. A pipe stays selected when its value is checked
	in every branch of the pipes, a symbol when the symbol is checked and
	its value in every branch of the symbols. A branch where nothing is
	checked does not count (the symbols by family make one branch): only
	"No size" checked keeps the pipes without size, whatever their fluid.
	Nothing checked in a category keeps none of its items.

	A branch is only there when an item of the selection holds something
	else than the default in it (a size, a fluid, a status other than "to be
	built"...): a selection of an electrical diagram has none.

	Nothing here depends on a widget: see SelectByCriteriaDialog.
*/
class SelectionCategories
{
	Q_DECLARE_TR_FUNCTIONS(SelectionCategories)

	public:
			/// A line of the tree
		struct Node
		{
				/// For a leaf, what an item needs checked; empty for a
				/// node with children
			QString key;
			QString label;
				/// How many items it holds; -1 for a branch which crosses
				/// the others ("By size"), where it would say nothing
			int count = -1;
				/// A branch which crosses the others
			bool crossed = false;
			QList<Node> children;
		};

		SelectionCategories(const QList<QGraphicsItem *> &items,
							const PidCodification &codes);

			/// What the filter works on: the selected items of @p diagram,
			/// or all those which can be selected when none is. The
			/// selection filter of the status bar is not followed here:
			/// the tree is a filter of its own.
		static QList<QGraphicsItem *> itemsOf(const Diagram *diagram);
			/// All the items of @p diagram which can be selected
		static QList<QGraphicsItem *> folioItems(const Diagram *diagram);

		const QList<Node> &tree() const { return m_tree; }
		bool isEmpty() const { return m_needs.isEmpty(); }
			/// The keys of all the leaves
		QSet<QString> leaves() const;
			/// The items kept with the leaves @p checked: something is
			/// checked in their category, and their value in each branch
			/// where something is
		QList<QGraphicsItem *> kept(const QSet<QString> &checked) const;
			/// The branch of the leaf @p key: "pipe/size" for a size,
			/// "signal/type" for a type of signal, "symbol" for a symbol;
			/// the leaf itself for a category without branch
		static QString branchOf(const QString &key);
			/// The category of the leaf @p key: "pipe", "signal", "symbol";
			/// the leaf itself for a category without branch
		static QString categoryOf(const QString &key);

	private:
		void addPipes(const QList<QGraphicsItem *> &pipes, const PidCodification &codes);
		void addSignalLines(const QList<QGraphicsItem *> &lines);
		void addSymbols(const QList<QGraphicsItem *> &symbols, const PidCodification &codes);

			/// Each item with the keys it needs checked
		QList<QPair<QGraphicsItem *, QStringList>> m_needs;
		QList<Node> m_tree;
};

#endif
