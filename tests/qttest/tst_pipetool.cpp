// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "diagramevent/diagrameventaddpipe.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPointer>

// The tool which draws pipes: a click near a free nozzle starts a pipe, a
// second click or the release of a drag near another one ends it, in one
// undo step. While it runs a click never picks nor moves a symbol, the
// tool stays on after each pipe, a right click or Esc takes back the pipe
// being drawn and a second one leaves the tool. Wires are not drawn with it.
class tst_pipetool : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40 with two terminals N1 (west) and N2 (east), both
		// with the medium @p medium ("" for none)
	static QDomElement definition(QDomDocument &doc, const QString &uuid,
				      const QString &medium)
	{
		const QString attr = medium.isEmpty()
				? QString()
				: QStringLiteral(" medium=\"%1\"").arg(medium);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<rect x=\"-10\" y=\"-10\" width=\"20\" height=\"20\" style=\"line-style:normal;"
			"line-weight:normal;filling:white;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%2/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%2/>"
			"</description></definition>").arg(uuid, attr));
		return doc.documentElement();
	}

	QETProject *m_project = nullptr;
	Diagram *m_folio = nullptr;
	ElementsLocation m_electrical, m_pipe;
	qreal m_next_y = 0;
	QPointer<DiagramEventAddPipe> m_tool;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project->embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/pipetool"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/pipetool/") + file, m_project);
	}

	Element *place(const ElementsLocation &location, const QPointF &pos)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		m_folio->addItem(element);
		element->setPos(pos);
		return element;
	}

		// Two symbols on a row of their own, 200 apart
	QPair<Element *, Element *> row(const ElementsLocation &first,
					const ElementsLocation &second)
	{
		m_next_y += 200;
		return {place(first, QPointF(0, m_next_y)), place(second, QPointF(200, m_next_y))};
	}

	void mouse(QEvent::Type type, const QPointF &at, Qt::MouseButton button,
		   Qt::MouseButtons buttons)
	{
		QGraphicsSceneMouseEvent event(type);
		event.setScenePos(at);
		event.setButton(button);
		event.setButtons(buttons);
		QCoreApplication::sendEvent(m_folio, &event);
	}

	void click(const QPointF &at, Qt::MouseButton button = Qt::LeftButton)
	{
		mouse(QEvent::GraphicsSceneMousePress, at, button, button);
		mouse(QEvent::GraphicsSceneMouseRelease, at, button, Qt::NoButton);
	}

	void drag(const QPointF &from, const QPointF &to)
	{
		mouse(QEvent::GraphicsSceneMousePress, from, Qt::LeftButton, Qt::LeftButton);
		mouse(QEvent::GraphicsSceneMouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton);
		mouse(QEvent::GraphicsSceneMouseMove, to, Qt::NoButton, Qt::LeftButton);
		mouse(QEvent::GraphicsSceneMouseRelease, to, Qt::LeftButton, Qt::NoButton);
	}

	void key(int k)
	{
		QKeyEvent event(QEvent::KeyPress, k, Qt::NoModifier);
		QCoreApplication::sendEvent(m_folio, &event);
	}

		// A point near @p t but not on it: 10 units below its docking point
	QPointF nearBelow(Terminal *t)
	{
		return t->dockConductor() + QPointF(0, 10);
	}

	void startTool()
	{
		m_tool = new DiagramEventAddPipe(m_folio);
		m_folio->setEventInterface(m_tool);
	}

	private slots:
		void initTestCase()
		{
			m_project = new QETProject();
			m_project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("pipetool"), NamesList());
			QDomDocument a, b;
			m_electrical = add("electrical.elmt",
					   definition(a, "91111111-1111-1111-1111-111111111111", ""));
			m_pipe = add("pipe.elmt",
				     definition(b, "92222222-2222-2222-2222-222222222222", "pipe"));
			QVERIFY(m_electrical.exist() && m_pipe.exist());
			m_folio = m_project->addNewDiagram();
		}

		void init()
		{
			startTool();
			QVERIFY(m_tool);
		}

		void cleanup()
		{
			m_folio->clearEventInterface();
			QVERIFY(!m_tool);
		}

		void twoClicksNearTwoNozzlesDrawAPipe()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
			const int steps = m_folio->undoStack().count();

			click(nearBelow(from));
			QCOMPARE(m_tool->startNozzle(), from);
			click(nearBelow(to));
			QCOMPARE(from->conductors().size(), 1);
			QCOMPARE(to->conductors().size(), 1);
			QCOMPARE(int(from->conductors().first()->medium()), int(TerminalData::Pipe));
				// the tool stays on, ready for the next pipe
			QVERIFY(m_tool);
			QCOMPARE(m_tool->startNozzle(), nullptr);
				// one undo step
			QCOMPARE(m_folio->undoStack().count(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(from->conductors().size(), 0);
		}

		void aDragFromANozzleToAnotherDrawsAPipe()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
			drag(nearBelow(from), nearBelow(to));
			QCOMPARE(from->conductors().size(), 1);
			QVERIFY(from->isLinkedTo(to));
		}

		void aClickNeverPicksNorMovesASymbol()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Element *symbol = ends.first;
			const QPointF pos = symbol->pos();
			drag(symbol->sceneBoundingRect().center(),
			     symbol->sceneBoundingRect().center() + QPointF(60, 80));
			QVERIFY(m_folio->selectedItems().isEmpty());
			QCOMPARE(symbol->pos(), pos);
				// far from any nozzle: nothing at all
			click(QPointF(-1000, -1000));
			QVERIFY(m_folio->selectedItems().isEmpty());
		}

		void aSecondClickNowhereKeepsThePipeWaiting()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
			click(nearBelow(from));
			click(QPointF(100, m_next_y + 100));
			QCOMPARE(m_tool->startNozzle(), from);
			click(nearBelow(to));
			QVERIFY(from->isLinkedTo(to));
		}

		void rightClickTakesThePipeBackThenLeavesTheTool()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			click(nearBelow(from));
			QCOMPARE(m_tool->startNozzle(), from);
			click(QPointF(100, m_next_y), Qt::RightButton);
			QVERIFY(m_tool);
			QCOMPARE(m_tool->startNozzle(), nullptr);
			QCOMPARE(from->conductors().size(), 0);
			click(QPointF(100, m_next_y), Qt::RightButton);
			QVERIFY(!m_tool);
			QVERIFY(!m_folio->eventInterface());
		}

		void escTakesThePipeBackThenLeavesTheTool()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			click(nearBelow(ends.first->terminals().at(1)));
			QVERIFY(m_tool->startNozzle());
			key(Qt::Key_Escape);
			QVERIFY(m_tool);
			QCOMPARE(m_tool->startNozzle(), nullptr);
			key(Qt::Key_Escape);
			QVERIFY(!m_tool);
		}

		void wiresAreNotDrawnWithTheTool()
		{
			const auto ends = row(m_electrical, m_electrical);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
			click(nearBelow(from));
			QCOMPARE(m_tool->startNozzle(), nullptr);
			click(nearBelow(to));
			QCOMPARE(from->conductors().size(), 0);
				// from a nozzle to a wire terminal: not caught
			const auto mixed = row(m_pipe, m_electrical);
			QVERIFY(mixed.first && mixed.second);
			Terminal *nozzle = mixed.first->terminals().at(1);
			click(nearBelow(nozzle));
			click(nearBelow(mixed.second->terminals().at(0)));
			QCOMPARE(nozzle->conductors().size(), 0);
		}

			// The right click on a symbol: the free nozzle of the selected
			// symbols nearest to the click, whatever the distance
		void theNozzleRightClickedIsTheNearestFreeOne()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Element *symbol = ends.first;
			Terminal *west = symbol->terminals().at(0);
			Terminal *east = symbol->terminals().at(1);
			const QList<QGraphicsItem *> selected{symbol};
			QCOMPARE(DiagramEventAddPipe::nearestFreeNozzle(selected, west->dockConductor()
									+ QPointF(-50, 0)), west);
			QCOMPARE(DiagramEventAddPipe::nearestFreeNozzle(selected, east->dockConductor()
									+ QPointF(3, 40)), east);
				// a connected nozzle is skipped
			click(nearBelow(east));
			click(nearBelow(ends.second->terminals().at(0)));
			QCOMPARE(east->conductors().size(), 1);
			QCOMPARE(DiagramEventAddPipe::nearestFreeNozzle(selected, east->dockConductor()), west);
				// no nozzle among wire terminals, nor in what is no symbol
			const auto wires = row(m_electrical, m_electrical);
			QVERIFY(wires.first);
			QCOMPARE(DiagramEventAddPipe::nearestFreeNozzle({wires.first}, QPointF()), nullptr);
			QCOMPARE(DiagramEventAddPipe::nearestFreeNozzle({}, QPointF()), nullptr);
		}

		void aPipeStartedFromANozzleEndsWithOneClick()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
			m_tool->startFrom(from);
			QCOMPARE(m_tool->startNozzle(), from);
			click(nearBelow(to));
			QVERIFY(from->isLinkedTo(to));
				// not from a wire terminal, nor from a nozzle already connected
			const auto wires = row(m_electrical, m_electrical);
			QVERIFY(wires.first);
			m_tool->startFrom(wires.first->terminals().at(1));
			QCOMPARE(m_tool->startNozzle(), nullptr);
			m_tool->startFrom(from);
			QCOMPARE(m_tool->startNozzle(), nullptr);
		}

		void aConnectedNozzleIsNeitherMarkedNorAStart()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
				// started again: it marks the nozzles of the folio when it starts
			startTool();
			const int marks = m_tool->markCount();
			QVERIFY(marks >= 4);
			click(nearBelow(from));
			click(nearBelow(to));
			QVERIFY(from->isLinkedTo(to));
			QCOMPARE(m_tool->markCount(), marks - 2);
				// the full nozzle starts nothing; undone, it is free again
			click(to->dockConductor() + QPointF(-5, 0));
			QCOMPARE(m_tool->startNozzle(), nullptr);
			m_folio->undoStack().undo();
			QCOMPARE(m_tool->markCount(), marks);
		}
};

QET_APP_TEST_MAIN(tst_pipetool)
#include "tst_pipetool.moc"
