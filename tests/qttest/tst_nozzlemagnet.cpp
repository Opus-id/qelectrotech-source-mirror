// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "diagramevent/diagrameventaddelement.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidnozzlemagnet.h"
#include "qetapp.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QGraphicsSceneMouseEvent>
#include <QUndoStack>

// The magnet of the nozzles (P&ID, 5l-1c). A symbol placed or dragged with
// the mouse near a free nozzle of another symbol comes against it exactly,
// so the two nozzles touch: the fine move with Ctrl + arrows, one unit at a
// time, could leave them a unit apart, each drawing its connection.
class tst_nozzlemagnet : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40 with two terminals N1 (west, x -10) and N2
		// (east, x 10), both with the medium @p medium ("" for none)
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
			"<rect x=\"-10\" y=\"-10\" width=\"20\" height=\"20\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:normal;filling:white;color:black\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%2/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%2/>"
			"</description></definition>").arg(uuid, attr));
		return doc.documentElement();
	}

	QETProject *m_project = nullptr;
	Diagram *m_folio = nullptr;
	ElementsLocation m_electrical, m_pipe;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project->embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/nozzlemagnet"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/nozzlemagnet/") + file, m_project);
	}

		// Placed exactly at @p pos, off the grid if need be, as after a
		// fine move with Ctrl + arrows
	Element *place(const ElementsLocation &location, const QPointF &pos)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		m_folio->addItem(element);
		element->QGraphicsItem::setPos(pos);
		return element;
	}

	qreal nextRow()
	{
		m_next_y += 200;
		return m_next_y;
	}

	static Terminal *west(Element *e) { return e->terminals().at(0); }
	static Terminal *east(Element *e) { return e->terminals().at(1); }

	QList<Element *> elements() const
	{
		QList<Element *> found;
		for (QGraphicsItem *item : m_folio->items()) {
			if (Element *e = qgraphicsitem_cast<Element *>(item)) found << e;
		}
		return found;
	}

		// The mouse pressed on the body of @p element, moved by @p by and
		// released there, with @p modifiers held
	void drag(Element *element, const QPointF &by,
		  Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		const QPointF start = element->scenePos() + QPointF(0, 5);
		const QPointF end = start + by;
		m_folio->clearSelection();

		QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
		press.setScenePos(start);
		press.setButtonDownScenePos(Qt::LeftButton, start);
		press.setButton(Qt::LeftButton);
		press.setButtons(Qt::LeftButton);
		press.setModifiers(modifiers);
		QCoreApplication::sendEvent(m_folio, &press);

		QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
		move.setScenePos(end);
		move.setLastScenePos(start);
		move.setButtonDownScenePos(Qt::LeftButton, start);
		move.setButtons(Qt::LeftButton);
		move.setModifiers(modifiers);
		QCoreApplication::sendEvent(m_folio, &move);

		QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
		release.setScenePos(end);
		release.setButtonDownScenePos(Qt::LeftButton, start);
		release.setButton(Qt::LeftButton);
		release.setButtons(Qt::NoButton);
		release.setModifiers(modifiers);
		QCoreApplication::sendEvent(m_folio, &release);
	}

		// A symbol of @p location placed with the mouse at @p at, as the
		// panel of the collection does: what it brings to the folio
	Element *placeWithMouse(ElementsLocation location, const QPointF &at,
				Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		const QList<Element *> before = elements();
		DiagramEventAddElement event(location, m_folio, at);
		QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
		move.setScenePos(at);
		move.setModifiers(modifiers);
		event.mouseMoveEvent(&move);
		QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
		release.setScenePos(at);
		release.setButton(Qt::LeftButton);
		release.setModifiers(modifiers);
		event.mouseReleaseEvent(&release);
		Element *added = nullptr;
		for (Element *e : elements()) {
			if (!before.contains(e) && e != event.pendingElement()) added = e;
		}
		return added;
	}

	private slots:
		void cleanupTestCase()
		{
			QETApp::unregisterProject(m_project);
		}

		void initTestCase()
		{
			m_project = new QETProject();
				//The placing finds the symbols through the projects of the app
			QETApp::registerProject(m_project);
			m_project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("nozzlemagnet"), NamesList());
			QDomDocument a, b;
			m_electrical = add("electrical.elmt",
					   definition(a, "83111111-1111-1111-1111-111111111111", ""));
			m_pipe = add("pipe.elmt",
				     definition(b, "84222222-2222-2222-2222-222222222222", "pipe"));
			QVERIFY(m_electrical.exist() && m_pipe.exist());
			m_folio = m_project->addNewDiagram();
		}

		void aFreeNozzleFacingItIsFound()
		{
			const qreal y = nextRow();
			Element *a = place(m_pipe, QPointF(3, y));
			Element *b = place(m_pipe, QPointF(28, y + 4));
			QVERIFY(a && b);
				// N1 of b, x 18, faces N2 of a, x 13
			const PidNozzleMagnet::Snap snap = PidNozzleMagnet::find({b}, 20);
			QVERIFY(!snap.isNull());
			QCOMPARE(snap.own, west(b));
			QCOMPARE(snap.target, east(a));
			QCOMPARE(snap.step, QPointF(-5, -4));
				// too far, or no radius (Shift held)
			QVERIFY(PidNozzleMagnet::find({b}, 5).isNull());
			QVERIFY(PidNozzleMagnet::find({b}, 0).isNull());
				// both moved together: nothing to come against
			QVERIFY(PidNozzleMagnet::find({a, b}, 20).isNull());
		}

		void nozzlesThatDoNotFaceEachOtherDoNotCatch()
		{
			const qreal y = nextRow();
			Element *a = place(m_pipe, QPointF(0, y));
				// both N1 point west, 12 apart
			Element *b = place(m_pipe, QPointF(0, y + 12));
			QVERIFY(a && b);
			QVERIFY(!PidNozzleMagnet::faces(west(a), west(b)));
			QVERIFY(PidNozzleMagnet::faces(east(a), west(b)));
			QVERIFY(PidNozzleMagnet::find({b}, 20).isNull());
		}

		void aTakenNozzleOrAWireTerminalDoesNotCatch()
		{
			const qreal y = nextRow();
			Element *a = place(m_pipe, QPointF(0, y));
			Element *b = place(m_pipe, QPointF(25, y));
			Element *c = place(m_pipe, QPointF(200, y));
			QVERIFY(a && b && c);
				// N2 of a takes a pipe: full
			m_folio->addItem(new Conductor(east(a), west(c)));
			QVERIFY(PidNozzleMagnet::find({b}, 20).isNull());
				// a wire terminal near a nozzle, or two wire terminals
			const qreal y2 = nextRow();
			Element *wire = place(m_electrical, QPointF(0, y2));
			Element *nozzle = place(m_pipe, QPointF(25, y2));
			Element *wire2 = place(m_electrical, QPointF(-25, y2));
			QVERIFY(wire && nozzle && wire2);
			QVERIFY(PidNozzleMagnet::find({nozzle}, 20).isNull());
			QVERIFY(PidNozzleMagnet::find({wire2}, 20).isNull());
		}

			// The whole gesture: a symbol placed with the mouse
		void aSymbolPlacedNearANozzleComesAgainstIt()
		{
			const qreal y = nextRow();
				// N2 of a at x 13, off the grid
			Element *a = place(m_pipe, QPointF(3, y));
			QVERIFY(a);
				// the mouse at x 31: on the grid at 30, N1 at 20; caught
			Element *b = placeWithMouse(m_pipe, QPointF(31, y + 2));
			QVERIFY(b);
			QCOMPARE(b->pos(), QPointF(23, y));
			QVERIFY(west(b)->touchesNozzle());
			QVERIFY(east(a)->touchesNozzle());
				// the marked nozzle is shown no more
			QVERIFY(!PidNozzleMagnet::marked());
				// no pipe of no length between them
			QCOMPARE(west(b)->conductors().size(), 0);
				// Shift held: where the mouse is, on the grid
			const qreal y2 = nextRow();
			Element *c = place(m_pipe, QPointF(3, y2));
			Element *d = placeWithMouse(m_pipe, QPointF(31, y2 + 2), Qt::ShiftModifier);
			QVERIFY(c && d);
			QCOMPARE(d->pos(), QPointF(30, y2));
			QVERIFY(!west(d)->touchesNozzle());
		}

			// The automatic connection, both kinds: two nozzles against each
			// other touch, no pipe of no length between them; a nozzle
			// farther in line still gets its pipe
		void twoNozzlesAgainstEachOtherGetNoPipe()
		{
			const bool auto_conductor = m_project->autoConductor();
			const bool live = m_project->liveAutoConnect();
			for (const bool live_mode : {false, true})
			{
				m_project->setAutoConductor(true);
				m_project->setLiveAutoConnect(live_mode);
				const qreal y = nextRow();
				Element *a = place(m_pipe, QPointF(3, y));
				Element *far = place(m_pipe, QPointF(300, y));
				QVERIFY(a && far);
				Element *b = placeWithMouse(m_pipe, QPointF(31, y + 2));
				QVERIFY(b);
				QVERIFY(west(b)->touchesNozzle());
				QCOMPARE(west(b)->conductors().size(), 0);
				QCOMPARE(east(a)->conductors().size(), 0);
				QCOMPARE(east(b)->conductors().size(), 1);
				QCOMPARE(west(far)->conductors().size(), 1);
			}
			m_project->setAutoConductor(auto_conductor);
			m_project->setLiveAutoConnect(live);
		}

		void aWireSymbolPlacedNearATerminalStaysOnTheGrid()
		{
			const qreal y = nextRow();
			Element *a = place(m_electrical, QPointF(3, y));
			QVERIFY(a);
			Element *b = placeWithMouse(m_electrical, QPointF(31, y + 2));
			QVERIFY(b);
			QCOMPARE(b->pos(), QPointF(30, y));
		}

			// The whole gesture: a symbol dragged with the mouse
		void aSymbolDraggedNearANozzleComesAgainstItAndOneUndoPutsItBack()
		{
			const qreal y = nextRow();
			Element *a = place(m_pipe, QPointF(3, y));
			Element *b = place(m_pipe, QPointF(100, y));
			QVERIFY(a && b);
			const int steps = m_folio->undoStack().count();
				// dragged by 69, 2: on the grid at 30, y + 0; caught by N2 of a
			drag(b, QPointF(-69, 2));
			QCOMPARE(b->pos(), QPointF(23, y));
			QVERIFY(west(b)->touchesNozzle());
			QVERIFY(!PidNozzleMagnet::marked());
			QCOMPARE(m_folio->undoStack().count(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(b->pos(), QPointF(100, y));
			QVERIFY(!west(b)->touchesNozzle());
				// Shift held: on the grid, not against it
			drag(b, QPointF(-69, 2), Qt::ShiftModifier);
			QCOMPARE(b->pos(), QPointF(30, y));
			QVERIFY(!west(b)->touchesNozzle());
		}
};

QET_APP_TEST_MAIN(tst_nozzlemagnet)
#include "tst_nozzlemagnet.moc"
