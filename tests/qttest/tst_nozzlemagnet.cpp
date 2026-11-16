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

		// A free end (pid_role="free_end"): one nozzle N1 at its point of
		// insertion, facing west
	static QDomElement freeEndDefinition(QDomDocument &doc)
	{
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"pid_family=\"pipe\" pid_role=\"free_end\" "
			"width=\"20\" height=\"20\" hotspot_x=\"10\" hotspot_y=\"10\">"
			"<uuid uuid=\"{85333333-3333-3333-3333-333333333333}\"/>"
			"<names><name lang=\"en\">Free end</name></names>"
			"<description>"
			"<terminal name=\"N1\" x=\"0\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"</description></definition>"));
		return doc.documentElement();
	}

	QETProject *m_project = nullptr;
	Diagram *m_folio = nullptr;
	ElementsLocation m_electrical, m_pipe, m_free_end;
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

		// A pipe from @p from to @p to, with the line number @p line
	Conductor *pipe(Terminal *from, Terminal *to, const QString &line = QString())
	{
		auto *conductor = new Conductor(from, to);
		m_folio->addItem(conductor);
		if (!line.isEmpty()) {
			ConductorProperties properties = conductor->properties();
			properties.m_pipe.line_number = line;
			conductor->setProperties(properties);
		}
		return conductor;
	}

		// The line from N2 of a symbol placed at (0, @p y) into the void,
		// ended by a free end at (@p end_x, @p y); the free end
	Element *lineIntoTheVoid(qreal y, qreal end_x, Element **from)
	{
		Element *a = place(m_pipe, QPointF(0, y));
		Element *free_end = place(m_free_end, QPointF(end_x, y));
		if (!a || !free_end) return nullptr;
		pipe(east(a), free_end->terminals().at(0), QStringLiteral("L1"));
		*from = a;
		return free_end;
	}

	bool onFolio(const Element *element) const
	{
		return elements().contains(const_cast<Element *>(element));
	}

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
			QDomDocument a, b, c;
			m_electrical = add("electrical.elmt",
					   definition(a, "83111111-1111-1111-1111-111111111111", ""));
			m_pipe = add("pipe.elmt",
				     definition(b, "84222222-2222-2222-2222-222222222222", "pipe"));
			m_free_end = add("free_end.elmt", freeEndDefinition(c));
			QVERIFY(m_electrical.exist() && m_pipe.exist() && m_free_end.exist());
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
				// N1 of b 9.4 from N2 of a, 8 off its pipe
			Element *b = place(m_pipe, QPointF(25, y + 8));
			Element *c = place(m_pipe, QPointF(200, y));
			QVERIFY(a && b && c);
				// N2 of a takes a pipe: full, b not on that pipe
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

			// Two symbols against each other are joined, with no pipe: a
			// third one is not drawn to their nozzles in contact (essai 5 of
			// the user, 16/11); the one dragged away from its partner is
			// still drawn back to it within the radius
		void aNozzleAgainstAnotherDoesNotCatch()
		{
			const qreal y = nextRow();
			Element *a = place(m_pipe, QPointF(0, y));
			Element *b = place(m_pipe, QPointF(20, y));
			QVERIFY(a && b);
			QVERIFY(east(a)->touchesNozzle());
				// N2 of c, at (6, y + 15), faces N1 of b, at (10, y), 15.5
				// away: taken, it touches N2 of a; N1 of a is 21.9 away
			Element *c = place(m_pipe, QPointF(-4, y + 15));
			QVERIFY(c);
			QVERIFY(PidNozzleMagnet::find({c}, 20).isNull());
				// b moved: N2 of a is free again, b comes back against it
			b->QGraphicsItem::setPos(QPointF(26, y + 2));
			const PidNozzleMagnet::Snap back = PidNozzleMagnet::find({b}, 20);
			QVERIFY(!back.isNull());
			QCOMPARE(back.target, east(a));
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
				// nothing drawn between them: no pipe, or the hidden one of no
				// length of the automatic connection
			for (const Conductor *pipe : west(b)->conductors()) {
				QVERIFY(pipe->endsTouch());
			}
				// Shift held: where the mouse is, on the grid
			const qreal y2 = nextRow();
			Element *c = place(m_pipe, QPointF(3, y2));
			Element *d = placeWithMouse(m_pipe, QPointF(31, y2 + 2), Qt::ShiftModifier);
			QVERIFY(c && d);
			QCOMPARE(d->pos(), QPointF(30, y2));
			QVERIFY(!west(d)->touchesNozzle());
		}

			// The automatic connection, both kinds, as before the magnet: two
			// nozzles brought against each other are joined by a hidden pipe
			// of no length (Conductor::endsTouch()), which keeps the line
			// whole when one of them is dragged away; a nozzle farther in
			// line still gets its pipe
		void theAutomaticConnectionJoinsNozzlesAgainstEachOther()
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
				QCOMPARE(west(b)->conductors().size(), 1);
				QVERIFY(west(b)->conductors().first()->endsTouch());
				QCOMPARE(east(a)->conductors().size(), 1);
				QCOMPARE(east(b)->conductors().size(), 1);
				QCOMPARE(west(far)->conductors().size(), 1);
					// dragged away, Shift held: the pipe of the classic automatic
					// connection stays and the line stays whole (the live one
					// takes away a wire whose ends no longer face each other)
				if (!live_mode) {
					drag(b, QPointF(0, 60), Qt::ShiftModifier);
					QCOMPARE(west(b)->conductors().size(), 1);
					QVERIFY(!west(b)->conductors().first()->endsTouch());
				}
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

			// A symbol dropped across a pipe breaks it (5l-1y) when its nozzles
			// are near the pipe, not exactly on it: placed with the mouse, on
			// the grid, they may be up to half a grid away from a pipe drawn
			// off the grid (from a nozzle brought there by the magnet)
		void aSymbolDroppedAcrossAPipeOffTheGridBreaksIt()
		{
			for (const qreal dy : {0.0, 3.0, 4.0, 5.0, -5.0})
			{
				const qreal y = nextRow();
				Element *a = place(m_pipe, QPointF(0, y + dy));
				Element *b = place(m_pipe, QPointF(300, y + dy));
				QVERIFY(a && b);
				pipe(east(a), west(b));
				Element *v = placeWithMouse(m_pipe, QPointF(151, y + 2));
				QVERIFY(v);
				QCOMPARE(v->pos(), QPointF(150, y));
				QVERIFY2(east(a)->isLinkedTo(west(v)) && west(b)->isLinkedTo(east(v)),
					 qPrintable(QStringLiteral("pipe %1 off the nozzles").arg(dy)));
			}
		}

			// (a) A symbol dropped by a nozzle on the free end of a line takes
			// the end: the free end goes, the pipe comes to the nozzle, its
			// route and data kept; the other nozzle stays free; one undo
		void aSymbolDroppedOnAFreeEndTakesIt()
		{
			const qreal y = nextRow();
			Element *a = nullptr;
			Element *free_end = lineIntoTheVoid(y, 200, &a);
			QVERIFY(free_end);
			const int steps = m_folio->undoStack().count();
				// the mouse at x 216: on the grid at 220, N1 at 210; caught by
				// the end at 200
			Element *v = placeWithMouse(m_pipe, QPointF(216, y + 2));
			QVERIFY(v);
			QCOMPARE(v->pos(), QPointF(210, y));
			QVERIFY(!onFolio(free_end));
			QVERIFY(east(a)->isLinkedTo(west(v)));
			QCOMPARE(west(v)->conductors().size(), 1);
			QCOMPARE(west(v)->conductors().first()->properties().m_pipe.line_number,
				 QStringLiteral("L1"));
			QVERIFY(east(v)->conductors().isEmpty());
			QVERIFY(!PidNozzleMagnet::marked());
			QCOMPARE(m_folio->undoStack().count(), steps + 1);
			m_folio->undoStack().undo();
			QVERIFY(onFolio(free_end));
			QVERIFY(east(a)->isLinkedTo(free_end->terminals().at(0)));
		}

			// (a) the other way: the symbol on the end of the line, its N2 on
			// the free end, or, Shift held, across it: the free end goes, never
			// a pipe back from it to the symbol
		void aSymbolDroppedAcrossAFreeEndTakesIt()
		{
			const qreal y = nextRow();
			Element *a = nullptr;
			Element *free_end = lineIntoTheVoid(y, 200, &a);
			QVERIFY(free_end);
				// on the grid at 190: N1 at 180 on the line, N2 at 200 on the end
			Element *v = placeWithMouse(m_pipe, QPointF(191, y + 2));
			QVERIFY(v);
			QCOMPARE(v->pos(), QPointF(190, y));
			QVERIFY(!onFolio(free_end));
			QVERIFY(east(a)->isLinkedTo(west(v)));
			QVERIFY(east(v)->conductors().isEmpty());

			const qreal y2 = nextRow();
			Element *c = nullptr;
			Element *free_end2 = lineIntoTheVoid(y2, 200, &c);
			QVERIFY(free_end2);
				// on the grid at 200: N1 at 190 on the line, the end inside
			Element *w = placeWithMouse(m_pipe, QPointF(201, y2 + 2), Qt::ShiftModifier);
			QVERIFY(w);
			QCOMPARE(w->pos(), QPointF(200, y2));
			QVERIFY(!onFolio(free_end2));
			QVERIFY(east(c)->isLinkedTo(west(w)));
			QVERIFY(east(w)->conductors().isEmpty());
		}

			// (b) A symbol dropped on the pipe of a nozzle already taken, near
			// it: nozzle against nozzle, the pipe goes on from the other nozzle
			// of the symbol, with its data; one undo
		void aSymbolDroppedOnThePipeOfATakenNozzleComesAgainstIt()
		{
			const qreal y = nextRow();
			Element *a = place(m_pipe, QPointF(0, y));
			Element *b = place(m_pipe, QPointF(300, y));
			QVERIFY(a && b);
			pipe(east(a), west(b), QStringLiteral("L2"));
			const int steps = m_folio->undoStack().count();
				// on the grid at 30: N1 at 20 on the pipe, 10 from N2 of a
			Element *v = placeWithMouse(m_pipe, QPointF(31, y + 2));
			QVERIFY(v);
			QCOMPARE(v->pos(), QPointF(20, y));
			QVERIFY(east(a)->conductors().isEmpty());
			QVERIFY(west(v)->touchesNozzle());
			QVERIFY(east(v)->isLinkedTo(west(b)));
			QCOMPARE(east(v)->conductors().first()->properties().m_pipe.line_number,
				 QStringLiteral("L2"));
			QCOMPARE(m_folio->undoStack().count(), steps + 1);
			m_folio->undoStack().undo();
			QVERIFY(east(a)->isLinkedTo(west(b)));

				// off the pipe, though near the nozzle: not drawn
			const qreal y2 = nextRow();
			Element *c = place(m_pipe, QPointF(0, y2));
			Element *d = place(m_pipe, QPointF(300, y2));
			QVERIFY(c && d);
			pipe(east(c), west(d));
			Element *w = placeWithMouse(m_pipe, QPointF(31, y2 + 12));
			QVERIFY(w);
			QCOMPARE(w->pos(), QPointF(30, y2 + 10));

				// Shift held: where the mouse is, the pipe broken as before
			const qreal y3 = nextRow();
			Element *e = place(m_pipe, QPointF(0, y3));
			Element *f = place(m_pipe, QPointF(300, y3));
			QVERIFY(e && f);
			pipe(east(e), west(f));
			Element *x = placeWithMouse(m_pipe, QPointF(31, y3 + 2), Qt::ShiftModifier);
			QVERIFY(x);
			QCOMPARE(x->pos(), QPointF(30, y3));
			QVERIFY(east(e)->isLinkedTo(west(x)));
			QVERIFY(east(x)->isLinkedTo(west(f)));
		}

			// (b) the same, dragged
		void aSymbolDraggedOnThePipeOfATakenNozzleComesAgainstIt()
		{
			const qreal y = nextRow();
			Element *a = place(m_pipe, QPointF(0, y));
			Element *b = place(m_pipe, QPointF(300, y));
			Element *v = place(m_pipe, QPointF(100, y + 100));
			QVERIFY(a && b && v);
			pipe(east(a), west(b));
			const int steps = m_folio->undoStack().count();
			drag(v, QPointF(-69, -98));
			QCOMPARE(v->pos(), QPointF(20, y));
			QVERIFY(east(a)->conductors().isEmpty());
			QVERIFY(east(v)->isLinkedTo(west(b)));
			QCOMPARE(m_folio->undoStack().count(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(v->pos(), QPointF(100, y + 100));
			QVERIFY(east(a)->isLinkedTo(west(b)));
		}

			// The mark of the nozzle caught is drawn on top of everything,
			// above a thick pipe (essai 9 of the user, 16/11), and goes with
			// the mark
		void theMarkIsOnTop()
		{
			const qreal y = nextRow();
			Element *a = nullptr;
			Element *free_end = lineIntoTheVoid(y, 200, &a);
			QVERIFY(free_end);
			Terminal *end = free_end->terminals().at(0);
			auto spotsOn = [this](const QPointF &at) {
				int found = 0;
				for (QGraphicsItem *item : m_folio->items(at)) {
					if (item->zValue() >= 1000000 && !qgraphicsitem_cast<Terminal *>(item)) ++found;
				}
				return found;
			};
			PidNozzleMagnet::mark(end);
			QCOMPARE(PidNozzleMagnet::marked(), end);
			QCOMPARE(spotsOn(end->dockConductor()), 1);
				// marked again: still one
			PidNozzleMagnet::mark(end);
			QCOMPARE(spotsOn(end->dockConductor()), 1);
			PidNozzleMagnet::clearMark();
			QCOMPARE(spotsOn(end->dockConductor()), 0);
			QVERIFY(!PidNozzleMagnet::marked());
		}

			// Never a pipe backwards: a pipe shorter than the symbol dropped on
			// it does not go back from its far end to the other nozzle
		void neverAPipeBackwards()
		{
			const qreal y = nextRow();
			Element *a = place(m_pipe, QPointF(0, y));
				// N1 of b at 25
			Element *b = place(m_pipe, QPointF(35, y));
			QVERIFY(a && b);
			pipe(east(a), west(b));
				// on the grid at 30: N1 at 20 on the pipe, N2 at 40 past b;
				// against N2 of a, N2 would be at 30, still past b: not drawn
			Element *v = placeWithMouse(m_pipe, QPointF(31, y + 2));
			QVERIFY(v);
			QCOMPARE(v->pos(), QPointF(30, y));
			QVERIFY(east(a)->isLinkedTo(west(v)));
			QVERIFY(!west(b)->isLinkedTo(east(v)));
		}
};

QET_APP_TEST_MAIN(tst_nozzlemagnet)
#include "tst_nozzlemagnet.moc"
