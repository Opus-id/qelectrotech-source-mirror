// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "diagramevent/diagrameventaddpipe.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "pidcodification.h"
#include "qetproject.h"
#include "ui/configpage/pidconfigpage.h"

#include <QDomDocument>
#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QUndoStack>

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

		// A free end: one nozzle N1 at its point of insertion, facing west
	static QDomElement freeEndDefinition(QDomDocument &doc)
	{
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"pid_family=\"pipe\" pid_role=\"free_end\" "
			"width=\"20\" height=\"20\" hotspot_x=\"10\" hotspot_y=\"10\">"
			"<uuid uuid=\"{93333333-3333-3333-3333-333333333333}\"/>"
			"<names><name lang=\"en\">Free end</name></names>"
			"<description>"
			"<terminal name=\"N1\" x=\"0\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"</description></definition>"));
		return doc.documentElement();
	}

		// The symbol of free end of the project: @p path, none when empty
	void setFreeEnd(const QString &path)
	{
		PidCodification codification = m_project->pidCodification();
		if (path.isEmpty()) {
			codification.tool_symbols.remove(PidCodification::freeEndRole());
		} else {
			codification.tool_symbols.insert(PidCodification::freeEndRole(), path);
		}
		m_project->setPidCodification(codification);
	}

	static QString freeEndPath() { return QStringLiteral("embed://import/pipetool/freeend.elmt"); }

		// The free ends on the folio
	QList<Element *> freeEnds() const
	{
		QList<Element *> found;
		for (Element *element : m_folio->elements()) {
			if (element->isFreeEnd()) found << element;
		}
		return found;
	}

		// The free end whose nozzle is at @p dock, nullptr for none
	Element *freeEndAt(const QPointF &dock) const
	{
		for (Element *element : freeEnds()) {
			if (QLineF(element->terminals().first()->dockConductor(), dock).length() < 0.5) {
				return element;
			}
		}
		return nullptr;
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
		   Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		QGraphicsSceneMouseEvent event(type);
		event.setScenePos(at);
		event.setButton(button);
		event.setButtons(buttons);
		event.setModifiers(modifiers);
		QCoreApplication::sendEvent(m_folio, &event);
	}

	void click(const QPointF &at, Qt::MouseButton button = Qt::LeftButton)
	{
		mouse(QEvent::GraphicsSceneMousePress, at, button, button);
		mouse(QEvent::GraphicsSceneMouseRelease, at, button, Qt::NoButton);
	}

		// A left click with Ctrl held
	void ctrlClick(const QPointF &at)
	{
		mouse(QEvent::GraphicsSceneMousePress, at, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
		mouse(QEvent::GraphicsSceneMouseRelease, at, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
	}

		// The dashed line which shows the pipe being drawn
	QLineF preview() const
	{
		for (QGraphicsItem *item : m_folio->items()) {
			if (auto *line = qgraphicsitem_cast<QGraphicsLineItem *>(item)) {
				if (line->zValue() >= 1000000) return line->line();
			}
		}
		return QLineF();
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
			QDomDocument a, b, c;
			m_electrical = add("electrical.elmt",
					   definition(a, "91111111-1111-1111-1111-111111111111", ""));
			m_pipe = add("pipe.elmt",
				     definition(b, "92222222-2222-2222-2222-222222222222", "pipe"));
			QVERIFY(m_electrical.exist() && m_pipe.exist());
			QVERIFY(add("freeend.elmt", freeEndDefinition(c)).exist());
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
			setFreeEnd(QString());
		}

		void twoClicksNearTwoNozzlesDrawAPipe()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
			const int steps = m_folio->undoStack().index();

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
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
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

			// Step 5l-1p: the way the nozzle of a free end faces, the
			// simplest way from where the pipe leaves
		void theFreeEndFacesThePipe()
		{
			const QPointF o(0, 0);
				// straight in front: back along it
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::East, QPointF(100, 0)), Qet::West);
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::North, QPointF(0, -100)), Qet::South);
				// in front and aside: one bend, back to the axis
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::East, QPointF(100, 50)), Qet::North);
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::East, QPointF(100, -50)), Qet::South);
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::South, QPointF(50, 100)), Qet::West);
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::West, QPointF(-100, 50)), Qet::North);
				// beside or behind: out, aside, back
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::East, QPointF(-100, 50)), Qet::East);
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::East, QPointF(0, 50)), Qet::East);
				// right behind: round
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::East, QPointF(-100, 0)), Qet::South);
				// from an empty place: the longer way
			QCOMPARE(DiagramEventAddPipe::dominantFacing(o, QPointF(100, 50)), Qet::East);
			QCOMPARE(DiagramEventAddPipe::dominantFacing(o, QPointF(-100, 50)), Qet::West);
			QCOMPARE(DiagramEventAddPipe::dominantFacing(o, QPointF(30, 50)), Qet::South);
			QCOMPARE(DiagramEventAddPipe::dominantFacing(o, QPointF(30, -50)), Qet::North);
		}

			// Step 5l-1p-ter: a route in S, in front and aside: the free end
			// faces back toward the start; else as in L
		void aRouteInSFacesBackTowardTheStart()
		{
			const QPointF o(0, 0);
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::East, QPointF(100, 50), true), Qet::West);
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::East, QPointF(100, -50), true), Qet::West);
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::South, QPointF(50, 100), true), Qet::North);
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::West, QPointF(-100, 50), true), Qet::East);
				// no room for two bends: in L
			const qreal near = DiagramEventAddPipe::sShapeRoom() - 10;
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::East, QPointF(near, 50), true), Qet::North);
				// straight, beside, behind: as in L
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::East, QPointF(100, 0), true), Qet::West);
			QCOMPARE(DiagramEventAddPipe::freeEndFacing(o, Qet::East, QPointF(-100, 50), true), Qet::East);

				// the same rule for both ends, whichever is free
			using End = DiagramEventAddPipe::PipeEnd;
			End nozzle{o, Qet::East, false}, free_end{QPointF(100, 50), Qet::East, true};
			DiagramEventAddPipe::facingOfFreeEnds(nozzle, free_end, true);
			QCOMPARE(nozzle.facing, Qet::East);
			QCOMPARE(free_end.facing, Qet::West);
			End start{QPointF(-150, 50), Qet::West, true}, west{o, Qet::West, false};
			DiagramEventAddPipe::facingOfFreeEnds(start, west, true);
			QCOMPARE(start.facing, Qet::East);
			QCOMPARE(west.facing, Qet::West);
			DiagramEventAddPipe::facingOfFreeEnds(start, west, false);
			QCOMPARE(start.facing, Qet::North);
			End a{o, Qet::North, true}, b{QPointF(200, 100), Qet::North, true};
			DiagramEventAddPipe::facingOfFreeEnds(a, b, true);
			QCOMPARE(a.facing, Qet::East);
			QCOMPARE(b.facing, Qet::West);
		}

			// Step 5l-1p-ter: a pipe in S to an empty place has two bends; the
			// choice is kept for the next pipe, in L again with a new tool
		void aPipeInSHasTwoBends()
		{
			setFreeEnd(freeEndPath());
			auto bends = [](const Conductor *pipe) {
				const QPainterPath path = pipe->path();
				int count = 0;
				for (int i = 2; i < path.elementCount(); ++i) {
					const QLineF a(QPointF(path.elementAt(i - 2)), QPointF(path.elementAt(i - 1)));
					const QLineF b(QPointF(path.elementAt(i - 1)), QPointF(path.elementAt(i)));
					if (a.length() > 0.5 && b.length() > 0.5
							&& qAbs(a.angleTo(b)) > 1 && qAbs(a.angleTo(b) - 360) > 1) {
						++count;
					}
				}
				return count;
			};

			QVERIFY(!m_tool->sShape());
			m_tool->setSShape(true);
			for (int i = 0; i < 2; ++i) {
				const auto ends = row(m_pipe, m_pipe);
				QVERIFY(ends.first && ends.second);
				Terminal *from = ends.first->terminals().at(1);
				click(nearBelow(from));
				const QPointF end = Diagram::snapToGrid(QPointF(150, m_next_y + 100));
				click(QPointF(150, m_next_y + 100));
				Element *free_end = freeEndAt(end);
				QVERIFY(free_end);
				QCOMPARE(free_end->terminals().first()->orientation(), Qet::West);
				QCOMPARE(bends(from->conductors().first()), 2);
					// kept for the next pipe
				QVERIFY(m_tool->sShape());
			}
				// in L: one bend
			m_tool->setSShape(false);
			const auto ends = row(m_pipe, m_pipe);
			Terminal *from = ends.first->terminals().at(1);
			click(nearBelow(from));
			click(QPointF(150, m_next_y + 100));
			QCOMPARE(bends(from->conductors().first()), 1);
				// a new tool: in L
			m_tool->setSShape(true);
			m_folio->clearEventInterface();
			startTool();
			QVERIFY(!m_tool->sShape());
		}

			// Step 5l-1p: from a nozzle to an empty place, a free end there,
			// facing the pipe; one undo step
		void aPipeEndsInAnEmptyPlaceOnAFreeEnd()
		{
			setFreeEnd(freeEndPath());
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			const int elements = m_folio->elements().size();
			const int steps = m_folio->undoStack().index();

			click(nearBelow(from));
			QCOMPARE(m_tool->startNozzle(), from);
			const QPointF end = Diagram::snapToGrid(QPointF(150, m_next_y + 100));
			click(QPointF(150, m_next_y + 100));
			Element *free_end = freeEndAt(end);
			QVERIFY(free_end);
			Terminal *nozzle = free_end->terminals().first();
				// in front and below the nozzle, east: one bend, from above
			QCOMPARE(nozzle->orientation(), Qet::North);
			QVERIFY(from->isLinkedTo(nozzle));
			QCOMPARE(int(from->conductors().first()->medium()), int(TerminalData::Pipe));
			QCOMPARE(m_folio->elements().size(), elements + 1);
				// the tool waits for the next pipe
			QVERIFY(!m_tool->startNozzle() && !m_tool->startsInEmptyPlace());
				// the free end and the pipe: one step
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(m_folio->elements().size(), elements);
			QCOMPARE(from->conductors().size(), 0);
			m_folio->undoStack().redo();
			QVERIFY(from->isLinkedTo(nozzle));
		}

			// Step 5l-1p: from an empty place to a nozzle: nothing placed
			// before the end
		void aPipeStartsInAnEmptyPlaceOnAFreeEnd()
		{
			setFreeEnd(freeEndPath());
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *to = ends.first->terminals().at(0);
			const int elements = m_folio->elements().size();
			const int steps = m_folio->undoStack().index();

			click(QPointF(-150, m_next_y));
			QVERIFY(m_tool->startsInEmptyPlace());
			QCOMPARE(m_tool->startPoint(), Diagram::snapToGrid(QPointF(-150, m_next_y)));
			QCOMPARE(m_folio->elements().size(), elements);
			QCOMPARE(m_folio->undoStack().index(), steps);

			click(nearBelow(to));
			Element *free_end = freeEndAt(Diagram::snapToGrid(QPointF(-150, m_next_y)));
			QVERIFY(free_end);
				// straight in front of the nozzle, west: back along it
			QCOMPARE(free_end->terminals().first()->orientation(), Qet::East);
			QVERIFY(free_end->terminals().first()->isLinkedTo(to));
				// the pipe starts at the free end
			QCOMPARE(to->conductors().first()->terminal1, free_end->terminals().first());
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(m_folio->elements().size(), elements);
		}

			// Step 5l-1p: from an empty place to another: two free ends, the
			// longer way first, then one bend
		void aPipeBetweenTwoEmptyPlaces()
		{
			setFreeEnd(freeEndPath());
			m_next_y += 200;
			const QPointF a = Diagram::snapToGrid(QPointF(400, m_next_y));
			const QPointF b = Diagram::snapToGrid(QPointF(600, m_next_y + 100));
			const int elements = m_folio->elements().size();
			const int steps = m_folio->undoStack().index();
			click(a);
			click(b);
			Element *start = freeEndAt(a), *end = freeEndAt(b);
			QVERIFY(start && end);
			QCOMPARE(start->terminals().first()->orientation(), Qet::East);
			QCOMPARE(end->terminals().first()->orientation(), Qet::North);
			QVERIFY(start->terminals().first()->isLinkedTo(end->terminals().first()));
			QCOMPARE(m_folio->elements().size(), elements + 2);
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(m_folio->elements().size(), elements);
		}

			// Step 5l-1p: no symbol of free end, or one not found, or a click
			// on a symbol: no empty place, nothing starts
		void anEmptyPlaceNeedsTheSymbolOfTheProject()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			const QPointF empty(100, m_next_y + 100);
			const int elements = m_folio->elements().size();

			click(empty);
			QVERIFY(!m_tool->startsInEmptyPlace());
			setFreeEnd(QStringLiteral("embed://import/pipetool/missing.elmt"));
			QVERIFY(DiagramEventAddPipe::freeEndSymbol(m_folio).isNull());
			click(empty);
			QVERIFY(!m_tool->startsInEmptyPlace());
			setFreeEnd(freeEndPath());
			QVERIFY(DiagramEventAddPipe::freeEndSymbol(m_folio).exist());
				// on a symbol: no empty place (one without nozzles, which
				// the magnet would catch)
			Element *symbol = place(m_electrical, QPointF(400, m_next_y));
			QVERIFY(symbol);
			click(symbol->sceneBoundingRect().center());
			QVERIFY(!m_tool->startsInEmptyPlace());
			QVERIFY(!m_tool->startNozzle());
				// started there, a click on the same point is too short
			click(empty);
			QVERIFY(m_tool->startsInEmptyPlace());
			click(empty);
			QVERIFY(m_tool->startsInEmptyPlace());
			QCOMPARE(m_folio->elements().size(), elements + 1);
			key(Qt::Key_Escape);
			QVERIFY(!m_tool->startsInEmptyPlace());
		}

			// Step 5l-1p: the release of a drag in an empty place waits for a
			// click, as before
		void aDragReleasedInAnEmptyPlaceWaits()
		{
			setFreeEnd(freeEndPath());
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			const int elements = m_folio->elements().size();
			drag(nearBelow(from), QPointF(100, m_next_y + 100));
			QCOMPARE(m_tool->startNozzle(), from);
			QCOMPARE(m_folio->elements().size(), elements);
			click(QPointF(100, m_next_y + 100));
			QCOMPARE(m_folio->elements().size(), elements + 1);
			QCOMPARE(from->conductors().size(), 1);
		}

			// Step 5l-1p-bis: within 10° of an axis of its start, the end
			// snaps onto it, in the preview as at the click; not at 15°,
			// nor with Ctrl
		void aNearlyStraightPipeIsStraight()
		{
			setFreeEnd(freeEndPath());
			QCOMPARE(DiagramEventAddPipe::axisAngle, 10.0);
			QVERIFY(DiagramEventAddPipe::nearAxis(QPointF(), Qet::East, QPointF(200, 35)));
			QVERIFY(!DiagramEventAddPipe::nearAxis(QPointF(), Qet::East, QPointF(200, 36)));
			QVERIFY(!DiagramEventAddPipe::nearAxis(QPointF(), Qet::West, QPointF(200, 0)));
			QVERIFY(DiagramEventAddPipe::nearAxis(QPointF(), Qet::North, QPointF(-20, -200)));

				// horizontal, 5°
			m_next_y += 200;
			const QPointF a = Diagram::snapToGrid(QPointF(400, m_next_y));
			const QPointF mouse_at = a + QPointF(200, 17);
			const QPointF on_axis(Diagram::snapToGrid(mouse_at).x(), a.y());
			QVERIFY(Diagram::snapToGrid(mouse_at).y() != a.y());
			click(a);
			QVERIFY(m_tool->startsInEmptyPlace());
			mouse(QEvent::GraphicsSceneMouseMove, mouse_at, Qt::NoButton, Qt::NoButton);
			QCOMPARE(preview().p2(), on_axis);
			QCOMPARE(m_tool->endPoint(mouse_at, true), on_axis);
			QCOMPARE(m_tool->endPoint(mouse_at, false), Diagram::snapToGrid(mouse_at));
			click(mouse_at);
			Element *start = freeEndAt(a), *end = freeEndAt(on_axis);
			QVERIFY(start && end);
				// straight: the ends face each other
			QCOMPARE(start->terminals().first()->orientation(), Qet::East);
			QCOMPARE(end->terminals().first()->orientation(), Qet::West);
			QVERIFY(start->terminals().first()->isLinkedTo(end->terminals().first()));

				// vertical, upward, 7°
			m_next_y += 400;
			const QPointF b = Diagram::snapToGrid(QPointF(400, m_next_y));
			click(b);
			click(b + QPointF(-30, -250));
			const QPointF up(b.x(), Diagram::snapToGrid(b + QPointF(-30, -250)).y());
			QVERIFY(freeEndAt(up));
			QCOMPARE(freeEndAt(up)->terminals().first()->orientation(), Qet::South);

				// 17°: on the grid only
			m_next_y += 200;
			const QPointF c = Diagram::snapToGrid(QPointF(400, m_next_y));
			click(c);
			click(c + QPointF(200, 60));
			QVERIFY(freeEndAt(Diagram::snapToGrid(c + QPointF(200, 60))));

				// Ctrl: on the grid only, the preview too
			m_next_y += 200;
			const QPointF d = Diagram::snapToGrid(QPointF(400, m_next_y));
			click(d);
			mouse(QEvent::GraphicsSceneMouseMove, d + QPointF(200, 17), Qt::NoButton, Qt::NoButton,
				  Qt::ControlModifier);
			QCOMPARE(preview().p2(), Diagram::snapToGrid(d + QPointF(200, 17)));
			ctrlClick(d + QPointF(200, 17));
			QVERIFY(freeEndAt(Diagram::snapToGrid(d + QPointF(200, 17))));
			QVERIFY(!freeEndAt(QPointF(Diagram::snapToGrid(d + QPointF(200, 17)).x(), d.y())));
		}

			// Step 5l-1p-bis: from a nozzle, its way out only, in front
		void fromANozzleItsWayOutOnly()
		{
			setFreeEnd(freeEndPath());
			m_next_y += 200;
			Element *s = place(m_pipe, QPointF(0, m_next_y));
			QVERIFY(s);
			Terminal *west = s->terminals().at(0), *east = s->terminals().at(1);
			const QPointF e = east->dockConductor(), w = west->dockConductor();

				// in front of the east nozzle: straight
			click(nearBelow(east));
			QCOMPARE(m_tool->startNozzle(), east);
			click(e + QPointF(200, 17));
			Element *end = freeEndAt(QPointF(Diagram::snapToGrid(e + QPointF(200, 17)).x(), e.y()));
			QVERIFY(end);
			QVERIFY(east->isLinkedTo(end->terminals().first()));

				// behind the west nozzle, beyond its symbol: on the grid only
			click(nearBelow(west));
			QCOMPARE(m_tool->startNozzle(), west);
			const QPointF behind = w + QPointF(400, 17);
			click(behind);
			QVERIFY(freeEndAt(Diagram::snapToGrid(behind)));
			QVERIFY(west->isLinkedTo(freeEndAt(Diagram::snapToGrid(behind))->terminals().first()));
		}

			// Step 5l-1p-bis: started in an empty place nearly in front of
			// the nozzle it ends at, the start moves onto its axis (preview
			// too); not with Ctrl, nor onto a place which is not empty
		void aStartInAnEmptyPlaceMovesOntoTheAxis()
		{
			setFreeEnd(freeEndPath());
			m_next_y += 200;
			Element *s = place(m_pipe, QPointF(0, m_next_y));
			QVERIFY(s);
			Terminal *west = s->terminals().at(0);
			const QPointF start = Diagram::snapToGrid(QPointF(-300, m_next_y + 30));
			const QPointF moved(start.x(), west->dockConductor().y());
			click(start);
			QVERIFY(m_tool->startsInEmptyPlace());
			QCOMPARE(m_tool->startPointFor(west, true), moved);
			QCOMPARE(m_tool->startPointFor(west, false), start);
				// over the nozzle, the preview starts on its axis; away, back
			mouse(QEvent::GraphicsSceneMouseMove, nearBelow(west), Qt::NoButton, Qt::NoButton);
			QCOMPARE(preview().p1(), moved);
			QCOMPARE(preview().p2(), west->dockConductor());
			mouse(QEvent::GraphicsSceneMouseMove, QPointF(-150, m_next_y + 100), Qt::NoButton, Qt::NoButton);
			QCOMPARE(preview().p1(), start);
			click(nearBelow(west));
			Element *free_end = freeEndAt(moved);
			QVERIFY(free_end);
			QCOMPARE(free_end->terminals().first()->orientation(), Qet::East);
			QVERIFY(free_end->terminals().first()->isLinkedTo(west));
			QVERIFY(!freeEndAt(start));

				// with Ctrl, where it was clicked
			m_next_y += 200;
			s = place(m_pipe, QPointF(0, m_next_y));
			QVERIFY(s);
			west = s->terminals().at(0);
			const QPointF start2 = Diagram::snapToGrid(QPointF(-300, m_next_y + 30));
			click(start2);
			ctrlClick(nearBelow(west));
			QVERIFY(freeEndAt(start2));
			QVERIFY(freeEndAt(start2)->terminals().first()->isLinkedTo(west));

				// a symbol on the axis: the start stays
			m_next_y += 200;
			s = place(m_pipe, QPointF(0, m_next_y));
			Element *in_the_way = place(m_electrical, QPointF(-300, m_next_y));
			QVERIFY(s && in_the_way);
			west = s->terminals().at(0);
			const QPointF start3 = Diagram::snapToGrid(QPointF(-300, m_next_y + 30));
			click(start3);
			QVERIFY(m_tool->startsInEmptyPlace());
			click(nearBelow(west));
			QVERIFY(freeEndAt(start3));
		}

			// Step 5l-1p: the symbol of free end, a setting of the
			// presentation of the project: file, CSV, page with undo
		void theSymbolOfFreeEndIsASettingOfTheProject()
		{
			const QString rac_ext = QStringLiteral("custom://pid_essai/extremites/RAC-EXT Extrémité libre.elmt");
			PidCodification codification;
			QVERIFY(!codification.hasTextSettings());
			codification.tool_symbols.insert(PidCodification::freeEndRole(), rac_ext);
			QVERIFY(codification.hasTextSettings());
				// the file
			QDomDocument doc;
			QDomElement holder = doc.createElement(QStringLiteral("project"));
			holder.appendChild(codification.toXml(doc));
			const QDomElement written = holder.firstChildElement().firstChildElement(QStringLiteral("tool_symbol"));
			QCOMPARE(written.attribute(QStringLiteral("role")), QStringLiteral("free_end"));
			QCOMPARE(written.attribute(QStringLiteral("location")), rac_ext);
			PidCodification read;
			read.fromXml(holder);
			QCOMPARE(read.toolSymbol(PidCodification::freeEndRole()), rac_ext);
			QVERIFY(read == codification);
				// the presentation, a role not known left out
			QVERIFY(codification.presentationToCsv().contains(QStringLiteral("tool_symbol;free_end;") + rac_ext));
			int skipped = 0;
			const PidCodification csv = PidCodification::fromCsv(
						QStringLiteral("symbole_outil;free_end;") + rac_ext
						+ QStringLiteral("\ntool_symbol;cross;custom://x.elmt\n"), &skipped);
			QCOMPARE(csv.toolSymbol(PidCodification::freeEndRole()), rac_ext);
			QCOMPARE(skipped, 1);
				// the default presentation brings it to a project without one
			QCOMPARE(PidCodification().completedWith(codification).toolSymbol(PidCodification::freeEndRole()),
					 rac_ext);

				// the page: its name shown, given to the project with undo
			PidConfigPage page(m_project);
			auto *field = page.findChild<QLineEdit *>(QStringLiteral("tool_symbol_free_end"));
			QVERIFY(field);
			QVERIFY(field->text().isEmpty());
			page.setToolSymbol(PidCodification::freeEndRole(), freeEndPath());
			QCOMPARE(field->text(), QStringLiteral("Free end"));
			page.applyProjectConf();
			QCOMPARE(m_project->pidCodification().toolSymbol(PidCodification::freeEndRole()), freeEndPath());
			m_project->undoStack()->undo();
			QVERIFY(m_project->pidCodification().toolSymbol(PidCodification::freeEndRole()).isEmpty());
				// one not found: its place, in orange
			page.setToolSymbol(PidCodification::freeEndRole(), QStringLiteral("embed://nowhere.elmt"));
			QCOMPARE(field->text(), QStringLiteral("embed://nowhere.elmt"));
			QVERIFY(!field->styleSheet().isEmpty());
			page.setToolSymbol(PidCodification::freeEndRole(), QString());
			QVERIFY(field->text().isEmpty());
		}
};

QET_APP_TEST_MAIN(tst_pipetool)
#include "tst_pipetool.moc"
