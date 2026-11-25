// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "diagramevent/diagrameventaddelement.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidlinemagnet.h"
#include "pidnozzlemagnet.h"
#include "qetapp.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QUndoStack>

// Step 8r-4 a, b: the magnet of the lines. A symbol of a line (a valve, a
// tee by its run...) placed or dragged near a straight segment of a pipe
// comes onto its axis, turned along it, so that the drop breaks the pipe
// there; Space then turns it end for end; Ctrl held, equipment, a symbol
// whose run does not fit on the segment: nothing.
class tst_linemagnet : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40: N1 (west) and N2 (east) at 10 from its middle,
		// N3 (south) when @p tee; nozzles of pipes; @p extra on <definition>
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &extra,
								  bool tee = false)
	{
		const QString branch = tee
				? QStringLiteral("<terminal name=\"N3\" x=\"0\" y=\"10\" orientation=\"s\" "
								 "type=\"Generic\" medium=\"pipe\"/>")
				: QString();
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" %2"
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<rect x=\"-10\" y=\"-10\" width=\"20\" height=\"20\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:normal;filling:white;color:black\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\" medium=\"pipe\"/>"
			"%3</description></definition>").arg(uuid, extra, branch));
		return doc.documentElement();
	}

	QETProject *m_project = nullptr;
	Diagram *m_folio = nullptr;
	ElementsLocation m_valve, m_tee, m_equipment;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project->embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/linemagnet"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/linemagnet/") + file, m_project);
	}

	Element *place(const ElementsLocation &location, const QPointF &pos, qreal rotation = 0)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		m_folio->addItem(element);
		element->setPos(pos);
		element->setRotation(rotation);
		return element;
	}

	qreal nextRow()
	{
		m_next_y += 350;
		return m_next_y;
	}

	static Terminal *nozzle(Element *element, const char *name)
	{
		for (Terminal *t : element->terminals())
			if (t->name() == QLatin1String(name)) return t;
		return nullptr;
	}

	Conductor *pipe(Terminal *from, Terminal *to)
	{
		auto *conductor = new Conductor(from, to);
		m_folio->addItem(conductor);
		return conductor;
	}

		// Equipment at (0, y) and (300, y), a horizontal pipe between them
		// on y, from x 10 to x 290
	void horizontalLine(qreal y, Element **a, Element **b)
	{
		*a = place(m_equipment, QPointF(0, y));
		*b = place(m_equipment, QPointF(300, y));
		pipe(nozzle(*a, "N2"), nozzle(*b, "N1"));
	}

		// Equipment turned a quarter at (100, y) and (100, y + 300), a
		// vertical pipe between them on x 100, from y + 10 to y + 290
	void verticalLine(qreal y, Element **a, Element **b)
	{
		*a = place(m_equipment, QPointF(100, y), 90);
		*b = place(m_equipment, QPointF(100, y + 300), 90);
		pipe(nozzle(*a, "N2"), nozzle(*b, "N1"));
	}

	QList<Element *> elements() const
	{
		QList<Element *> found;
		for (QGraphicsItem *item : m_folio->items()) {
			if (Element *e = qgraphicsitem_cast<Element *>(item)) found << e;
		}
		return found;
	}

		// The mouse moved to @p at while @p event places, with @p modifiers
	static void moveTo(DiagramEventAddElement &event, const QPointF &at,
					   Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
		move.setScenePos(at);
		move.setModifiers(modifiers);
		event.mouseMoveEvent(&move);
	}

		// Released at @p at: the symbol it placed
	Element *release(DiagramEventAddElement &event, const QPointF &at,
					 Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		const QList<Element *> before = elements();
		QGraphicsSceneMouseEvent up(QEvent::GraphicsSceneMouseRelease);
		up.setScenePos(at);
		up.setButton(Qt::LeftButton);
		up.setModifiers(modifiers);
		event.mouseReleaseEvent(&up);
		Element *added = nullptr;
		for (Element *e : elements()) {
			if (!before.contains(e) && e != event.pendingElement()) added = e;
		}
		return added;
	}

	Element *placeWithMouse(ElementsLocation location, const QPointF &at,
							Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		DiagramEventAddElement event(location, m_folio, at);
		moveTo(event, at, modifiers);
		return release(event, at, modifiers);
	}

		// The mouse pressed on the body of @p element, moved by @p by and
		// released there, with @p modifiers held
	void drag(Element *element, const QPointF &by, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
			//Under the middle, away from the nozzles: a press near one
			//draws a pipe from it
		const QPointF start = element->scenePos() + QPointF(0, 5);
		const QPointF end = start + by;
		m_folio->clearSelection();
		QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
		press.setScenePos(start);
		press.setButtonDownScenePos(Qt::LeftButton, start);
		press.setButton(Qt::LeftButton);
		press.setButtons(Qt::LeftButton);
		QCoreApplication::sendEvent(m_folio, &press);
		QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
		move.setScenePos(end);
		move.setLastScenePos(start);
		move.setButtonDownScenePos(Qt::LeftButton, start);
		move.setButtons(Qt::LeftButton);
		move.setModifiers(modifiers);
		QCoreApplication::sendEvent(m_folio, &move);
		QGraphicsSceneMouseEvent up(QEvent::GraphicsSceneMouseRelease);
		up.setScenePos(end);
		up.setButtonDownScenePos(Qt::LeftButton, start);
		up.setButton(Qt::LeftButton);
		up.setButtons(Qt::NoButton);
		up.setModifiers(modifiers);
		QCoreApplication::sendEvent(m_folio, &up);
	}

	private slots:
		void cleanupTestCase()
		{
			QETApp::unregisterProject(m_project);
		}

		void initTestCase()
		{
			m_project = new QETProject();
			QETApp::registerProject(m_project);
			m_project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("linemagnet"), NamesList());
			QDomDocument a, b, c;
			m_valve = add("valve.elmt", definition(a, "b1111111-1111-1111-1111-111111111111",
												   "pid_inline=\"true\" pid_family=\"valve\" "));
			m_tee = add("tee.elmt", definition(b, "b2222222-2222-2222-2222-222222222222",
											   "pid_run=\"N1 N2 N3\" pid_family=\"pipe\" pid_role=\"junction\" ",
											   true));
			m_equipment = add("equipment.elmt", definition(c, "b3333333-3333-3333-3333-333333333333",
														   "pid_family=\"equipment\" "));
			QVERIFY(m_valve.exist() && m_tee.exist() && m_equipment.exist());
			m_folio = m_project->addNewDiagram();
		}

			// (a) Near a horizontal pipe: onto its axis, the pipe broken
			// there at the drop; the segment shown meanwhile, not after
		void aValveComesOntoTheAxisOfAPipe()
		{
			const qreal y = nextRow();
			Element *a = nullptr, *b = nullptr;
			horizontalLine(y, &a, &b);
			DiagramEventAddElement event(m_valve, m_folio, QPointF(151, y + 7));
				// on the grid at (150, y + 10), 10 off the axis: caught
			moveTo(event, QPointF(151, y + 7));
			QCOMPARE(event.pendingElement()->pos(), QPointF(150, y));
			const QLineF shown = PidLineMagnet::marked();
			QCOMPARE(shown.p1().y(), y);
			QCOMPARE(shown.p2().y(), y);
			QVERIFY(qMin(shown.p1().x(), shown.p2().x()) <= 140 && qMax(shown.p1().x(), shown.p2().x()) >= 160);
			Element *v = release(event, QPointF(151, y + 7));
			QVERIFY(v);
			QCOMPARE(v->pos(), QPointF(150, y));
			QCOMPARE(v->rotation(), 0.0);
			QVERIFY(nozzle(a, "N2")->isLinkedTo(nozzle(v, "N1")));
			QVERIFY(nozzle(v, "N2")->isLinkedTo(nozzle(b, "N1")));
		}

		void notTooFarNorWithCtrl()
		{
			const qreal y = nextRow();
			Element *a = nullptr, *b = nullptr;
			horizontalLine(y, &a, &b);
				// 30 off the axis: beyond the reach of the magnet
			Element *far = placeWithMouse(m_valve, QPointF(151, y + 31));
			QVERIFY(far);
			QCOMPARE(far->pos(), QPointF(150, y + 30));
				// Ctrl held: on the grid, the pipe left whole
			Element *loose = placeWithMouse(m_valve, QPointF(81, y + 7), Qt::ControlModifier);
			QVERIFY(loose);
			QCOMPARE(loose->pos(), QPointF(80, y + 10));
			QVERIFY(nozzle(a, "N2")->isLinkedTo(nozzle(b, "N1")));
			QVERIFY(!PidLineMagnet::marked().length());
		}

			// (b) Near a vertical pipe: turned a quarter, onto its axis; Space
			// turns it end for end, along the pipe still
		void aValveTurnsAlongAVerticalPipe()
		{
			const qreal y = nextRow();
			Element *a = nullptr, *b = nullptr;
			verticalLine(y, &a, &b);
			Element *v = placeWithMouse(m_valve, QPointF(107, y + 151));
			QVERIFY(v);
			QCOMPARE(v->pos(), QPointF(100, y + 150));
			QCOMPARE(v->rotation(), 90.0);
			QVERIFY(nozzle(a, "N2")->isLinkedTo(nozzle(v, "N1")));
			QVERIFY(nozzle(v, "N2")->isLinkedTo(nozzle(b, "N1")));

			const qreal y2 = nextRow();
			verticalLine(y2, &a, &b);
			DiagramEventAddElement event(m_valve, m_folio, QPointF(107, y2 + 151));
			moveTo(event, QPointF(107, y2 + 151));
			Element *pending = event.pendingElement();
			QCOMPARE(pending->rotation(), 90.0);
			QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
			event.keyPressEvent(&space);
			QCOMPARE(pending->rotation(), 270.0);
			QCOMPARE(pending->pos(), QPointF(100, y2 + 150));
				// off the line: its own turn back, end for end of what it was
			moveTo(event, QPointF(400, y2 + 150));
			QCOMPARE(pending->rotation(), 180.0);
				// Space off the line: a quarter, as ever
			event.keyPressEvent(&space);
			QCOMPARE(pending->rotation(), 270.0);
				// on the line again: along it as it is
			moveTo(event, QPointF(107, y2 + 151));
			QCOMPARE(pending->rotation(), 270.0);
			Element *w = release(event, QPointF(107, y2 + 151));
			QVERIFY(w);
			QCOMPARE(w->rotation(), 270.0);
			QVERIFY(nozzle(a, "N2")->isLinkedTo(nozzle(w, "N2")));
			QVERIFY(nozzle(w, "N1")->isLinkedTo(nozzle(b, "N1")));
		}

			// A tee by its run, its branch left free; equipment never
		void aTeeByItsRunEquipmentNever()
		{
			const qreal y = nextRow();
			Element *a = nullptr, *b = nullptr;
			horizontalLine(y, &a, &b);
			Element *t = placeWithMouse(m_tee, QPointF(151, y + 7));
			QVERIFY(t);
			QCOMPARE(t->pos(), QPointF(150, y));
			QVERIFY(nozzle(a, "N2")->isLinkedTo(nozzle(t, "N1")));
			QVERIFY(nozzle(t, "N3")->conductors().isEmpty());

			const qreal y2 = nextRow();
			horizontalLine(y2, &a, &b);
			Element *e = placeWithMouse(m_equipment, QPointF(151, y2 + 7));
			QVERIFY(e);
			QCOMPARE(e->pos(), QPointF(150, y2 + 10));
			QVERIFY(nozzle(a, "N2")->isLinkedTo(nozzle(b, "N1")));
		}

			// Only where the whole run fits on the segment, and never a pipe
			// of the symbol moved
		void onlyWhereTheRunFits()
		{
			const qreal y = nextRow();
			Element *a = nullptr, *b = nullptr;
			horizontalLine(y, &a, &b);
				// the run from x 280 to x 300, past the end of the pipe (290)
			Element *v = place(m_valve, QPointF(290, y + 10));
			QVERIFY(PidLineMagnet::find(v, 20, {v}).isNull());
				// from x 260 to x 280: caught, but not when its symbols are
				// moved too
			Element *w = place(m_valve, QPointF(270, y + 10));
			const PidLineMagnet::Snap snap = PidLineMagnet::find(w, 20, {w});
			QVERIFY(!snap.isNull());
			QCOMPARE(snap.step, QPointF(0, -10));
			QCOMPARE(snap.turn, 0.0);
			QVERIFY(PidLineMagnet::find(w, 20, {w, a}).isNull());
			QVERIFY(PidLineMagnet::find(w, 0, {w}).isNull());
		}

			// Dragged: onto the axis, turned along it; one undo puts it back,
			// its turn too
		void aValveDraggedOntoAPipeAndOneUndo()
		{
				// a row of its own, at the top, above those of nextRow()
			const qreal y = 20;
			Element *a = nullptr, *b = nullptr;
			verticalLine(y, &a, &b);
			Element *v = place(m_valve, QPointF(300, y + 150));
			QVERIFY(v);
			const int steps = m_folio->undoStack().count();
				// to (110, y + 150) on the grid: 10 off the axis
			drag(v, QPointF(-190, 0));
			QCOMPARE(v->pos(), QPointF(100, y + 150));
			QCOMPARE(v->rotation(), 90.0);
			QVERIFY(nozzle(a, "N2")->isLinkedTo(nozzle(v, "N1")));
			QVERIFY(!PidLineMagnet::marked().length());
			QCOMPARE(m_folio->undoStack().count(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(v->pos(), QPointF(300, y + 150));
			QCOMPARE(v->rotation(), 0.0);
			QVERIFY(nozzle(a, "N2")->isLinkedTo(nozzle(b, "N1")));
				// Ctrl held: where the mouse is, not turned (20 off the axis,
				// within reach without it)
			drag(v, QPointF(-180, 0), Qt::ControlModifier);
			QCOMPARE(v->pos(), QPointF(120, y + 150));
			QCOMPARE(v->rotation(), 0.0);
			QVERIFY(nozzle(a, "N2")->isLinkedTo(nozzle(b, "N1")));
		}
};

QET_APP_TEST_MAIN(tst_linemagnet)
#include "tst_linemagnet.moc"
