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
/*
	Edit > Selection > Select by criteria: the items of a selection in a
	tree of categories. Symbols by family (none for an electrical
	selection), then by symbol; pipe lines in branches which cross each
	other ("By size", "By fluid"...), there only when a pipe holds
	something in them; a pipe stays when its value is checked in every
	branch. Nothing selected: the whole folio.
*/
#include "qetapptest.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "diagramcontext.h"
#include "diagramview.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidstatus.h"
#include "pidtag.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/independenttextitem.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "selectioncategories.h"
#include "ui/selectbycriteriadialog.h"

#include <QDomDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUndoCommand>
#include <QUndoStack>

class tst_selectbycriteria : public QObject
{
	Q_OBJECT

	using Node = SelectionCategories::Node;

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_relay, m_lamp, m_valve, m_pump;
	qreal m_next_y = 0;

		// A symbol of two terminals, of @p medium ("" electrical) and of
		// the family @p family ("" none)
	ElementsLocation define(const QString &file, const QString &name,
							const QString &medium, const QString &family)
	{
		const QString medium_attr = medium.isEmpty() ? QString()
								  : QStringLiteral(" medium=\"%1\"").arg(medium);
		const QString family_attr = family.isEmpty() ? QString()
								  : QStringLiteral(" pid_family=\"%1\"").arg(family);
		QDomDocument doc;
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\"%3 "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<names><name lang=\"en\">%1</name></names><description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%2/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%2/>"
			"</description></definition>").arg(name, medium_attr, family_attr));
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/filter"), file, doc.documentElement());
		return ElementsLocation(QStringLiteral("embed://import/filter/") + file, &m_project);
	}

	Element *place(const ElementsLocation &location)
	{
		m_next_y += 100;
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		m_folio->addItem(element);
		element->setPos(0, m_next_y);
		return element;
	}

		// A pipe between two valves (or a wire between two relays)
	Conductor *draw(const ElementsLocation &location)
	{
		Element *a = place(location);
		Element *b = place(location);
		if (!a || !b) return nullptr;
		b->setPos(200, a->pos().y());
		auto *conductor = new Conductor(a->terminals().at(1), b->terminals().at(0));
		m_folio->addItem(conductor);
		return conductor;
	}

	Conductor *pipe(int dn, const QString &fluid)
	{
		Conductor *conductor = draw(m_valve);
		if (!conductor) return nullptr;
		ConductorProperties p = conductor->properties();
		if (dn) p.m_pipe.nominal = PipeDiameter::nominal(dn);
		p.m_pipe.fluid = fluid;
		conductor->setProperties(p);
		return conductor;
	}

	static const Node *find(const QList<Node> &nodes, const QString &label)
	{
		for (const Node &node : nodes) {
			if (node.label == label) return &node;
			if (const Node *found = find(node.children, label)) return found;
		}
		return nullptr;
	}

	static QStringList labels(const QList<Node> &nodes)
	{
		QStringList list;
		for (const Node &node : nodes) list << node.label;
		return list;
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("filter"), NamesList());
			m_relay = define(QStringLiteral("relay.elmt"), QStringLiteral("Relay"), QString(), QString());
			m_lamp = define(QStringLiteral("lamp.elmt"), QStringLiteral("Lamp"), QString(), QString());
			m_valve = define(QStringLiteral("valve.elmt"), QStringLiteral("Valve"),
							 QStringLiteral("pipe"), QStringLiteral("valve"));
			m_pump = define(QStringLiteral("pump.elmt"), QStringLiteral("Pump"),
							QStringLiteral("pipe"), QStringLiteral("equipment"));
			QVERIFY(m_relay.exist() && m_valve.exist());
			m_folio = m_project.addNewDiagram();
		}

			// An electrical selection: the symbols right under "Symbols",
			// no family, no branch "By..."; the wire and the text each a
			// category
		void anElectricalSelectionHasNoBranch()
		{
			Conductor *wire = draw(m_relay);
			QVERIFY(wire);
			Element *lamp = place(m_lamp);
			auto *text = new IndependentTextItem();
			m_folio->addItem(text);
			const QList<QGraphicsItem *> items {wire, wire->terminal1->parentElement(),
												wire->terminal2->parentElement(), lamp, text};
			const SelectionCategories categories(items, PidCodification());

			QCOMPARE(labels(categories.tree()),
					 QStringList({QStringLiteral("Symbols"), QStringLiteral("Conductors"),
								  QStringLiteral("Texts")}));
			const Node &symbols = categories.tree().first();
			QCOMPARE(symbols.count, 3);
			QCOMPARE(labels(symbols.children), QStringList({QStringLiteral("Lamp"), QStringLiteral("Relay")}));
			QCOMPARE(symbols.children.at(1).count, 2);
			QVERIFY(!find(categories.tree(), QStringLiteral("Other symbols")));

				// all checked: all kept; the relays unchecked: they go
			QSet<QString> checked = categories.leaves();
			QCOMPARE(categories.kept(checked).size(), 5);
			checked.remove(symbols.children.at(1).key);
			const QList<QGraphicsItem *> kept = categories.kept(checked);
			QCOMPARE(kept.size(), 3);
			QVERIFY(kept.contains(wire) && kept.contains(lamp) && kept.contains(text));
		}

			// Pipes: "By size" and "By fluid", "No size" last, the fluid
			// written with its name; a pipe stays when checked in both
		void pipesAreCrossedBySizeAndFluid()
		{
			Conductor *a = pipe(100, QStringLiteral("W01"));
			Conductor *b = pipe(100, QStringLiteral("W02"));
			Conductor *c = pipe(0, QStringLiteral("W01"));
			QVERIFY(a && b && c);
			const PidCodification codes = PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\nfluid;W02;Clarified water;E\n"));
			const SelectionCategories categories({a, b, c}, codes);

			QCOMPARE(categories.tree().size(), 1);
			const Node &pipes = categories.tree().first();
			QCOMPARE(pipes.label, QStringLiteral("Pipe lines"));
			QCOMPARE(pipes.count, 3);
				// nothing else is set on them: two branches only
			QCOMPARE(labels(pipes.children),
					 QStringList({QStringLiteral("By size"), QStringLiteral("By fluid")}));
			const Node &by_size = pipes.children.at(0);
			QVERIFY(by_size.crossed);
			QCOMPARE(by_size.count, -1);
			QCOMPARE(by_size.children.size(), 2);
			QVERIFY(by_size.children.at(0).label.contains(QStringLiteral("100")));
			QCOMPARE(by_size.children.at(0).count, 2);
			QCOMPARE(by_size.children.at(1).label, QStringLiteral("No size"));
			const Node &by_fluid = pipes.children.at(1);
			QCOMPARE(by_fluid.children.at(0).label, QStringLiteral("W01 – Raw water"));
			QCOMPARE(by_fluid.children.at(0).count, 2);

				// only the pipes without size
			QSet<QString> checked = categories.leaves();
			checked.remove(by_size.children.at(0).key);
			QCOMPARE(categories.kept(checked), QList<QGraphicsItem *>({c}));
				// and among the others, those of W01 only
			checked = categories.leaves();
			checked.remove(by_size.children.at(1).key);
			checked.remove(by_fluid.children.at(1).key);
			QCOMPARE(categories.kept(checked), QList<QGraphicsItem *>({a}));
				// a whole branch unchecked does not count: every pipe
			checked = categories.leaves();
			for (const Node &leaf : by_fluid.children) checked.remove(leaf.key);
			QCOMPARE(categories.kept(checked).size(), 3);
				// "No size" alone, all else unchecked: the pipe without
				// size, whatever its fluid
			QCOMPARE(categories.kept({by_size.children.at(1).key}), QList<QGraphicsItem *>({c}));
				// nothing checked: none
			QVERIFY(categories.kept({}).isEmpty());
		}

			// Pipes holding nothing: no branch, the category is a leaf
		void pipesWithoutDataMakeALeaf()
		{
			Conductor *a = pipe(0, QString());
			QVERIFY(a);
			const SelectionCategories categories({a}, PidCodification());
			const Node &pipes = categories.tree().first();
			QVERIFY(pipes.children.isEmpty());
			QVERIFY(!pipes.key.isEmpty());
			QCOMPARE(categories.kept(categories.leaves()).size(), 1);
			QVERIFY(categories.kept({}).isEmpty());
		}

			// P&ID symbols: by family, then by symbol; "By functional
			// unit" and "By status of works" once one holds them
		void symbolsBranchByFamilyUnitAndStatus()
		{
			Element *v1 = place(m_valve);
			Element *v2 = place(m_valve);
			Element *pump = place(m_pump);
			Element *relay = place(m_relay);
			QVERIFY(v1 && v2 && pump && relay);
			const QList<QGraphicsItem *> items {v1, v2, pump, relay};

			{
					// no unit, no status: families only
				const SelectionCategories categories(items, PidCodification());
				const Node &symbols = categories.tree().first();
				QCOMPARE(labels(symbols.children),
						 QStringList({QStringLiteral("Equipment"), QStringLiteral("Valves"),
									  QStringLiteral("Other symbols")}));
				QCOMPARE(symbols.children.at(1).count, 2);
				QCOMPARE(symbols.children.at(1).children.first().label, QStringLiteral("Valve"));
			}

			DiagramContext info = v1->elementInformations();
			info.addValue(PidTag::unitKey(), QStringLiteral("U10"));
			v1->setElementInformations(info);
			QUndoCommand *status = PidStatus::change({v2}, QStringLiteral("existing"));
			QVERIFY(status);
			status->redo();
			delete status;

			const SelectionCategories categories(items, PidCodification());
			const Node &symbols = categories.tree().first();
			const Node *by_unit = find(symbols.children, QStringLiteral("By functional unit"));
			const Node *by_status = find(symbols.children, QStringLiteral("By status of works"));
			QVERIFY(by_unit && by_status);
			QCOMPARE(labels(by_unit->children),
					 QStringList({QStringLiteral("U10"), QStringLiteral("No functional unit")}));
			QCOMPARE(by_unit->children.at(1).count, 3);
				// the relay has no status: three symbols only, in order
			QCOMPARE(labels(by_status->children),
					 QStringList({QStringLiteral("To be built"), QStringLiteral("Existing, kept")}));
			QCOMPARE(by_status->children.at(0).count, 2);

				// "Existing" only: the existing valve, and the relay which
				// has no status
			QSet<QString> checked = categories.leaves();
			checked.remove(by_status->children.at(0).key);
			const QList<QGraphicsItem *> kept = categories.kept(checked);
			QCOMPARE(kept.size(), 2);
			QVERIFY(kept.contains(v2) && kept.contains(relay));
		}

			// Nothing selected: the whole folio; else the selection.
			// selectExactly() selects the items kept, nothing else
		void theWholeFolioWhenNothingIsSelected()
		{
			m_folio->clearSelection();
			const QList<QGraphicsItem *> all = SelectionCategories::itemsOf(m_folio);
			QVERIFY(all.size() > 10);

			Element *one = place(m_relay);
			Element *two = place(m_lamp);
			one->setSelected(true);
			two->setSelected(true);
			QCOMPARE(SelectionCategories::itemsOf(m_folio).size(), 2);

			m_folio->selectExactly({two});
			QCOMPARE(m_folio->selectedItems(), QList<QGraphicsItem *>({two}));
			m_folio->clearSelection();
		}

			// The dialog: all checked at first, the count follows
		void theDialogCountsTheItemsKept()
		{
			Conductor *a = pipe(100, QStringLiteral("W01"));
			Conductor *b = pipe(0, QStringLiteral("W01"));
			const QList<QGraphicsItem *> items {a, b, a->terminal1->parentElement()};
			const SelectionCategories categories(items, PidCodification());
			SelectByCriteriaDialog dialog(items, items, PidCodification());
			QVERIFY(!dialog.searchesFolio());
			QCOMPARE(dialog.action(), SelectByCriteriaDialog::Action::Replace);
			QCOMPARE(dialog.checked(), categories.leaves());
			QCOMPARE(dialog.keptCount(), 3);
			dialog.setAllChecked(false);
			QVERIFY(dialog.checked().isEmpty());
			QCOMPARE(dialog.keptCount(), 0);
				// "Pipe lines" checked again: its leaves with it
			QTreeWidgetItem *pipes = nullptr;
			for (int i = 0 ; i < dialog.tree()->topLevelItemCount() ; ++i) {
				if (dialog.tree()->topLevelItem(i)->text(0).startsWith(QStringLiteral("Pipe lines")))
					pipes = dialog.tree()->topLevelItem(i);
			}
			QVERIFY(pipes);
			pipes->setCheckState(0, Qt::Checked);
			QCOMPARE(dialog.keptCount(), 2);
		}

			// The preview applies at each change, Apply when asked
		void thePreviewAndApplyEmitTheItemsKept()
		{
			Conductor *a = pipe(100, QStringLiteral("W01"));
			Conductor *b = pipe(0, QStringLiteral("W01"));
			SelectByCriteriaDialog dialog({a, b}, {a, b}, PidCodification());
			dialog.setZoom(false);
			dialog.setPreview(false);
			QSignalSpy spy(&dialog, &SelectByCriteriaDialog::applied);
			dialog.setAllChecked(false);
			QCOMPARE(spy.count(), 0);
			dialog.apply();
			QCOMPARE(spy.count(), 1);
			QVERIFY(spy.last().at(0).value<QList<QGraphicsItem *>>().isEmpty());
			QCOMPARE(spy.last().at(1).toBool(), false);
				// the preview on: applied at once, then at each change
			dialog.setPreview(true);
			QCOMPARE(spy.count(), 2);
			dialog.setAllChecked(true);
			QCOMPARE(spy.count(), 3);
			QCOMPARE(spy.last().at(0).value<QList<QGraphicsItem *>>().size(), 2);
		}

			// Add and Remove start from the selection of departure; Apply
			// makes the result the next one
		void addAndRemoveFromTheSelection()
		{
			Conductor *sized = pipe(100, QStringLiteral("W01"));
			Conductor *unsized = pipe(0, QStringLiteral("W01"));
			const QList<QGraphicsItem *> folio {sized, unsized};
			SelectByCriteriaDialog dialog({unsized}, folio, PidCodification());
			dialog.setPreview(false);
			dialog.setSearchFolio(true);
				// found: the pipe with a size only
			QTreeWidgetItem *no_size = nullptr;
			for (QTreeWidgetItemIterator it(dialog.tree()) ; *it ; ++it)
				if ((*it)->text(0).startsWith(QStringLiteral("No size"))) no_size = *it;
			QVERIFY(no_size);
			no_size->setCheckState(0, Qt::Unchecked);
			QCOMPARE(dialog.foundItems(), QList<QGraphicsItem *>({sized}));

			QCOMPARE(dialog.resultItems(), QList<QGraphicsItem *>({sized}));
			dialog.setAction(SelectByCriteriaDialog::Action::Add);
			QCOMPARE(dialog.resultItems(), QList<QGraphicsItem *>({unsized, sized}));
			dialog.setAction(SelectByCriteriaDialog::Action::Remove);
			QCOMPARE(dialog.resultItems(), QList<QGraphicsItem *>({unsized}));
			dialog.setAllChecked(true);
			QVERIFY(dialog.resultItems().isEmpty());

				// Add applied: the next action starts from both
			dialog.setAllChecked(false);
			QTreeWidgetItem *dn100 = nullptr;
			for (QTreeWidgetItemIterator it(dialog.tree()) ; *it ; ++it)
				if ((*it)->text(0).contains(QStringLiteral("100"))) dn100 = *it;
			QVERIFY(dn100);
			dialog.setAction(SelectByCriteriaDialog::Action::Add);
			dialog.setAllChecked(true);
			QSignalSpy spy(&dialog, &SelectByCriteriaDialog::applied);
			dialog.apply();
			QCOMPARE(spy.count(), 1);
			QCOMPARE(spy.last().at(0).value<QList<QGraphicsItem *>>().size(), 2);
			dialog.setAction(SelectByCriteriaDialog::Action::Remove);
			dialog.setSearchFolio(false);
			dialog.setAllChecked(false);
			QCOMPARE(dialog.resultItems().size(), 2);
		}

			// From the editor: not modal; the preview selects, Cancel puts
			// the selection back; a change of the folio closes it
		void theDialogFollowsTheFolio()
		{
			QAction *action = QetAppTest::action(QStringLiteral("select_by_criteria"));
			QVERIFY(action);
			Diagram *folio = QetAppTest::diagram();
			QVERIFY(folio);
			QCoreApplication::processEvents();
			QVERIFY(action->isEnabled());
			auto *one = new IndependentTextItem();
			auto *two = new IndependentTextItem();
			folio->addItem(one);
			folio->addItem(two);
			folio->clearSelection();
			one->setSelected(true);
			two->setSelected(true);

			action->trigger();
			auto *dialog = QetAppTest::editor()->findChild<SelectByCriteriaDialog *>();
			QVERIFY(dialog);
			QVERIFY(dialog->isVisible());
			QVERIFY(!dialog->isModal());
			dialog->setZoom(false);
			dialog->setPreview(true);
			dialog->setAllChecked(false);
			QVERIFY(folio->selectedItems().isEmpty());
			dialog->reject();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QCoreApplication::processEvents();
			QCOMPARE(folio->selectedItems().size(), 2);
			QVERIFY(!QetAppTest::editor()->findChild<SelectByCriteriaDialog *>());

				// the editor active again, as a click on it makes it (with
				// no window active, it has no current folio)
			QetAppTest::editor()->activateWindow();
			QVERIFY(QTest::qWaitForWindowActive(QetAppTest::editor()));
			action->trigger();
			QPointer<SelectByCriteriaDialog> again = QetAppTest::editor()->findChild<SelectByCriteriaDialog *>();
			QVERIFY(again);
			again->setAllChecked(false);
			QVERIFY(folio->selectedItems().isEmpty());
				// a step of the undo stack: closed, the selection left
			folio->undoStack().push(new QUndoCommand(QStringLiteral("change")));
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QCoreApplication::processEvents();
			QVERIFY(!again || !again->isVisible());
			QVERIFY(folio->selectedItems().isEmpty());
			delete one;
			delete two;
		}

			// The dialog open, a click on the folio is a zone, taken when
			// the button is released: Add adds what it picked and is
			// found, Remove takes it out; what is not checked is not found
		void aClickOnTheFolioIsAZone()
		{
			QAction *action = QetAppTest::action(QStringLiteral("select_by_criteria"));
			Diagram *folio = QetAppTest::diagram();
			DiagramView *view = QetAppTest::view();
			QVERIFY(action && folio && view);
			auto *one = new IndependentTextItem();
			auto *two = new IndependentTextItem();
			folio->addItem(one);
			folio->addItem(two);
			folio->clearSelection();
			QetAppTest::editor()->activateWindow();
			QVERIFY(QTest::qWaitForWindowActive(QetAppTest::editor()));
			action->trigger();
			QPointer<SelectByCriteriaDialog> dialog = QetAppTest::editor()->findChild<SelectByCriteriaDialog *>();
			QVERIFY(dialog);
			dialog->setPreview(false);
			dialog->setAction(SelectByCriteriaDialog::Action::Add);

				// as QGraphicsScene does: the selection cleared, the item
				// clicked selected, then the button released on the view
			const auto click = [folio, view](QGraphicsItem *item) {
				folio->clearSelection();
				item->setSelected(true);
				QTest::mouseRelease(view->viewport(), Qt::LeftButton);
				QCoreApplication::processEvents();
			};
			click(one);
			QCOMPARE(folio->selectedItems(), QList<QGraphicsItem *>({one}));
			click(two);
			QCOMPARE(folio->selectedItems().size(), 2);
			QCOMPARE(dialog->resultItems().size(), 2);
				// a zone moves the search to the selection
			QVERIFY(!dialog->searchesFolio());

			dialog->setAction(SelectByCriteriaDialog::Action::Remove);
			click(one);
			QCOMPARE(folio->selectedItems(), QList<QGraphicsItem *>({two}));

				// nothing checked: the click finds nothing, nothing changes
			dialog->setAction(SelectByCriteriaDialog::Action::Add);
			dialog->setAllChecked(false);
			click(one);
			QCOMPARE(folio->selectedItems(), QList<QGraphicsItem *>({two}));

			dialog->reject();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QCoreApplication::processEvents();
			QVERIFY(folio->selectedItems().isEmpty());
			delete one;
			delete two;
		}
};

QET_APP_TEST_MAIN(tst_selectbycriteria)
#include "tst_selectbycriteria.moc"
