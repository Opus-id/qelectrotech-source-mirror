// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "diagramevent/diagrameventaddpipe.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidfreeend.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QUndoStack>

// Step 5l-1p-quater: a line taken up at its free end. A pipe drawn from the
// nozzle of a free end, or to it, does not stop there: the free end goes and
// the line goes on as one pipe, with the route already drawn and the data of
// the line; from a free end to another, two lines become one, the line drawn
// from winning. One undo step; with the tool, and at the release of a drag
// from a free nozzle; a wire never takes anything up.
class tst_pipefreeend : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40 with two terminals N1 (west) and N2 (east), both
		// with the medium @p medium ("" for none)
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &medium)
	{
		const QString attr = medium.isEmpty() ? QString()
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
			"<uuid uuid=\"{94444444-4444-4444-4444-444444444444}\"/>"
			"<names><name lang=\"en\">Free end</name></names>"
			"<description>"
			"<terminal name=\"N1\" x=\"0\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"</description></definition>"));
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
					QStringLiteral("import/pipefreeend"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/pipefreeend/") + file, m_project);
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

	void mouse(QEvent::Type type, const QPointF &at, Qt::MouseButton button,
			   Qt::MouseButtons buttons)
	{
		QGraphicsSceneMouseEvent event(type);
		event.setScenePos(at);
		event.setButton(button);
		event.setButtons(buttons);
		QCoreApplication::sendEvent(m_folio, &event);
	}

	void click(const QPointF &at)
	{
		mouse(QEvent::GraphicsSceneMousePress, at, Qt::LeftButton, Qt::LeftButton);
		mouse(QEvent::GraphicsSceneMouseRelease, at, Qt::LeftButton, Qt::NoButton);
	}

	void move(const QPointF &at)
	{
		mouse(QEvent::GraphicsSceneMouseMove, at, Qt::NoButton, Qt::NoButton);
	}

	void key(int k)
	{
		QKeyEvent event(QEvent::KeyPress, k, Qt::NoModifier);
		QCoreApplication::sendEvent(m_folio, &event);
	}

		// Outside the tool: the mouse pressed on @p from, released at @p at
	void dragFrom(Terminal *from, const QPointF &at)
	{
		mouse(QEvent::GraphicsSceneMousePress, from->mapToScene(from->shape().boundingRect().center()),
			  Qt::LeftButton, Qt::LeftButton);
		mouse(QEvent::GraphicsSceneMouseMove, at, Qt::NoButton, Qt::LeftButton);
		mouse(QEvent::GraphicsSceneMouseRelease, at, Qt::LeftButton, Qt::NoButton);
	}

		// A point near @p t but not on it: 10 units below its docking point
	static QPointF nearBelow(const Terminal *t) { return t->dockConductor() + QPointF(0, 10); }

	void startTool()
	{
		m_tool = new DiagramEventAddPipe(m_folio);
		m_folio->setEventInterface(m_tool);
	}

	void stopTool()
	{
		m_folio->clearEventInterface();
	}

		// The free end whose nozzle is at @p dock, nullptr for none
	Element *freeEndAt(const QPointF &dock) const
	{
		for (Element *element : m_folio->elements()) {
			if (element->isFreeEnd()
				&& QLineF(element->terminals().first()->dockConductor(), dock).length() < 0.5) {
				return element;
			}
		}
		return nullptr;
	}

		// The points of @p pipe, on the folio
	static QList<QPointF> pointsOf(const Conductor *pipe)
	{
		QList<QPointF> points;
		const QPainterPath path = pipe->path();
		for (int i = 0; i < path.elementCount(); ++i) {
			points << pipe->mapToScene(QPointF(path.elementAt(i)));
		}
		return points;
	}

	static bool near(const QPointF &a, const QPointF &b) { return QLineF(a, b).length() < 0.5; }

		// A line drawn with the tool from @p from to a free end placed in
		// the empty place @p end, with the data @p data; its free end
	Element *lineTo(Terminal *from, const QPointF &end, const PipeProperties &data)
	{
		click(nearBelow(from));
		click(end);
		Element *free_end = freeEndAt(end);
		if (!free_end || from->conductors().size() != 1) return nullptr;
		Conductor *pipe = from->conductors().first();
		ConductorProperties properties = pipe->properties();
		properties.m_pipe = data;
		pipe->setProperties(properties);
		return free_end;
	}

	static PipeProperties data(const QString &line, const QString &fluid, int dn,
							   const QString &material = QString())
	{
		PipeProperties p;
		p.line_number = line;
		p.fluid = fluid;
		if (dn) p.nominal = PipeDiameter::nominal(dn);
		p.material = material;
		return p;
	}

	static Terminal *nozzleOf(Element *e) { return e ? e->terminals().first() : nullptr; }

	private slots:
		void initTestCase()
		{
			m_project = new QETProject();
			m_project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("pipefreeend"), NamesList());
			QDomDocument a, b, c;
			m_electrical = add("electrical.elmt",
							   definition(a, "95555555-5555-5555-5555-555555555555", ""));
			m_pipe = add("pipe.elmt", definition(b, "96666666-6666-6666-6666-666666666666", "pipe"));
			QVERIFY(m_electrical.exist() && m_pipe.exist());
			QVERIFY(add("freeend.elmt", freeEndDefinition(c)).exist());
			m_folio = m_project->addNewDiagram();
			PidCodification codification = m_project->pidCodification();
			codification.tool_symbols.insert(PidCodification::freeEndRole(),
											 QStringLiteral("embed://import/pipefreeend/freeend.elmt"));
			m_project->setPidCodification(codification);
		}

		void init()
		{
			m_next_y += 600;
			startTool();
			QVERIFY(m_tool);
		}

		void cleanup()
		{
			stopTool();
		}

			// What may be taken up: a free end holding one pipe, not a free
			// end without one, a nozzle of another symbol nor a terminal
		void whatMayBeTakenUp()
		{
			Element *a = place(m_pipe, QPointF(0, m_next_y));
			Terminal *from = a->terminals().at(1);
			Element *free_end = lineTo(from, Diagram::snapToGrid(QPointF(200, m_next_y)), {});
			QVERIFY(free_end);
			Terminal *end = nozzleOf(free_end);
			QCOMPARE(PidFreeEnd::pipeOf(end), from->conductors().first());
			QCOMPARE(PidFreeEnd::pipeOf(from), nullptr);
				// on, past the free end: back from the way its nozzle faces
			QCOMPARE(PidFreeEnd::wayOut(end), Qet::East);
			QCOMPARE(PidFreeEnd::wayOut(from), from->orientation());

			Element *b = place(m_pipe, QPointF(400, m_next_y + 200));
			Element *wire = place(m_electrical, QPointF(400, m_next_y + 300));
			QVERIFY(PidFreeEnd::mayJoin(b->terminals().at(0), end));
			QVERIFY(PidFreeEnd::mayJoin(end, b->terminals().at(0)));
				// a terminal never takes up a free end
			QVERIFY(!PidFreeEnd::mayJoin(wire->terminals().at(0), end));
				// nor the nozzle the line comes from
			QVERIFY(!PidFreeEnd::mayJoin(end, from));
				// two free nozzles: no free end to take up
			QVERIFY(!PidFreeEnd::mayJoin(b->terminals().at(0), b->terminals().at(1)));
		}

			// Both ends of one pipe are free ends: nothing to join
		void bothEndsOfOnePipe()
		{
			const QPointF a = Diagram::snapToGrid(QPointF(0, m_next_y));
			const QPointF b = Diagram::snapToGrid(QPointF(200, m_next_y));
			click(a);
			click(b);
			QVERIFY(freeEndAt(a) && freeEndAt(b));
			QVERIFY(!PidFreeEnd::mayJoin(nozzleOf(freeEndAt(a)), nozzleOf(freeEndAt(b))));
		}

			// With the tool, from a free end to a nozzle: one pipe, the route
			// already drawn kept (moved by hand here), the data of the line,
			// the free end gone, one undo step
		void theToolCarriesALineOn()
		{
			Element *a = place(m_pipe, QPointF(0, m_next_y));
			Terminal *from = a->terminals().at(1);
			const QPointF end = Diagram::snapToGrid(QPointF(200, m_next_y + 100));
			Element *free_end = lineTo(from, end, data("L1", "EP", 100));
			QVERIFY(free_end);
			Terminal *end_nozzle = nozzleOf(free_end);
			QCOMPARE(end_nozzle->orientation(), Qet::North);
				// the route moved by hand
			const QPointF d = from->dockConductor();
			const QList<QPointF> by_hand{d, d + QPointF(110, 0), d + QPointF(110, 40),
										 QPointF(end.x(), d.y() + 40), end};
			QVERIFY(from->conductors().first()->setPathPoints(by_hand));

			Element *b = place(m_pipe, QPointF(400, m_next_y + 300));
			Terminal *to = b->terminals().at(0);
			const int steps = m_folio->undoStack().index();
			click(nearBelow(end_nozzle));
			QCOMPARE(m_tool->startNozzle(), nullptr);
			QVERIFY(m_tool->startsOnFreeEnd());
				// hidden while the preview reaches it
			QCOMPARE(free_end->opacity(), 0.0);
			click(nearBelow(to));

			QVERIFY(!freeEndAt(end));
			QVERIFY(from->isLinkedTo(to));
			QCOMPARE(from->conductors().size(), 1);
			Conductor *line = from->conductors().first();
			QCOMPARE(line->properties().m_pipe.line_number, QStringLiteral("L1"));
			QCOMPARE(line->properties().m_pipe.fluid, QStringLiteral("EP"));
			QCOMPARE(line->properties().m_pipe.nominal, PipeDiameter::nominal(100));
				// the route drawn by hand, then on down from where the free end was
			const QList<QPointF> points = pointsOf(line);
			QVERIFY(points.size() >= 5);
			for (int i = 0; i < 4; ++i) {
				QVERIFY2(near(points.at(i), by_hand.at(i)), qPrintable(QString::number(i)));
			}
			QVERIFY(points.at(4).y() > end.y());
			QCOMPARE(m_folio->undoStack().index(), steps + 1);

			m_folio->undoStack().undo();
			QVERIFY(freeEndAt(end));
			QCOMPARE(freeEndAt(end)->opacity(), 1.0);
			QVERIFY(from->isLinkedTo(nozzleOf(freeEndAt(end))));
			QVERIFY(to->conductors().isEmpty());
			m_folio->undoStack().redo();
			QVERIFY(!freeEndAt(end));
			QVERIFY(from->isLinkedTo(to));
		}

			// With the tool, from a nozzle to a free end: the same, the line
			// going on from the nozzle
		void theToolEndsOnAFreeEnd()
		{
			Element *a = place(m_pipe, QPointF(0, m_next_y));
			Terminal *from = a->terminals().at(1);
			const QPointF end = Diagram::snapToGrid(QPointF(200, m_next_y));
			Element *free_end = lineTo(from, end, data("L2", "EU", 0));
			QVERIFY(free_end);

			Element *b = place(m_pipe, QPointF(500, m_next_y));
			Terminal *start = b->terminals().at(0);
			click(nearBelow(start));
			move(nearBelow(nozzleOf(free_end)));
			QCOMPARE(free_end->opacity(), 0.0);
				// away: shown again
			move(QPointF(350, m_next_y + 150));
			QCOMPARE(free_end->opacity(), 1.0);
			click(nearBelow(nozzleOf(free_end)));

			QVERIFY(!freeEndAt(end));
			QVERIFY(start->isLinkedTo(from));
			QCOMPARE(start->conductors().first()->properties().m_pipe.line_number, QStringLiteral("L2"));
		}

			// From a free end to another: one line, the data of the line drawn
			// from, what it leaves empty taken from the other
		void twoLinesBecomeOne()
		{
			Element *a = place(m_pipe, QPointF(0, m_next_y));
			Element *b = place(m_pipe, QPointF(600, m_next_y));
			Terminal *left = a->terminals().at(1), *right = b->terminals().at(0);
			const QPointF end1 = Diagram::snapToGrid(QPointF(200, m_next_y));
			const QPointF end2 = Diagram::snapToGrid(QPointF(400, m_next_y));
			Element *e1 = lineTo(left, end1, data("L1", "EP", 100));
			Element *e2 = lineTo(right, end2, data("L2", QString(), 150, QStringLiteral("PVC")));
			QVERIFY(e1 && e2);
			const int steps = m_folio->undoStack().index();
			click(nearBelow(nozzleOf(e1)));
			click(nearBelow(nozzleOf(e2)));

			QVERIFY(!freeEndAt(end1) && !freeEndAt(end2));
			QVERIFY(left->isLinkedTo(right));
			const PipeProperties p = left->conductors().first()->properties().m_pipe;
			QCOMPARE(p.line_number, QStringLiteral("L1"));
			QCOMPARE(p.fluid, QStringLiteral("EP"));
			QCOMPARE(p.material, QStringLiteral("PVC"));
			QCOMPARE(p.nominal, PipeDiameter::nominal(100));
				// straight on: no bend
			for (const QPointF &p : pointsOf(left->conductors().first())) {
				QVERIFY(qAbs(p.y() - left->dockConductor().y()) < 0.5);
			}
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
			m_folio->undoStack().undo();
			QVERIFY(freeEndAt(end1) && freeEndAt(end2));
		}

			// What is set aside is said
		void theDataSetAside()
		{
			QStringList dropped;
			const PipeProperties p = PidFreeEnd::mergedData(data("L1", "EP", 100),
															data("L2", "EP", 150, QStringLiteral("PVC")),
															&dropped);
			QCOMPARE(p.line_number, QStringLiteral("L1"));
			QCOMPARE(p.material, QStringLiteral("PVC"));
			QCOMPARE(dropped.size(), 2);
			QVERIFY(dropped.at(0).contains(QStringLiteral("150")));
			QVERIFY(dropped.at(1).contains(QStringLiteral("L2")));
				// nothing differs: nothing said
			PidFreeEnd::mergedData(data("L1", "EP", 100), data(QString(), "EP", 100), &dropped);
			QVERIFY(dropped.isEmpty());
		}

			// Esc gives the free end back
		void escShowsTheFreeEndAgain()
		{
			Element *a = place(m_pipe, QPointF(0, m_next_y));
			Element *free_end = lineTo(a->terminals().at(1),
									   Diagram::snapToGrid(QPointF(200, m_next_y)), {});
			QVERIFY(free_end);
			click(nearBelow(nozzleOf(free_end)));
			QCOMPARE(free_end->opacity(), 0.0);
			key(Qt::Key_Escape);
			QCOMPARE(free_end->opacity(), 1.0);
			QVERIFY(!m_tool->startsOnFreeEnd());
		}

			// Outside the tool: a drag from a free nozzle released on a free
			// end takes it up; a drag from a free end draws nothing (5l-1h)
		void aDragEndsOnAFreeEnd()
		{
			Element *a = place(m_pipe, QPointF(0, m_next_y));
			Terminal *from = a->terminals().at(1);
			const QPointF end = Diagram::snapToGrid(QPointF(200, m_next_y));
			Element *free_end = lineTo(from, end, data("L3", "EP", 0));
			QVERIFY(free_end);
			stopTool();

			Element *b = place(m_pipe, QPointF(500, m_next_y));
			Element *c = place(m_pipe, QPointF(500, m_next_y + 200));
				// from the free end: nothing
			dragFrom(nozzleOf(free_end), nearBelow(c->terminals().at(0)));
			QVERIFY(freeEndAt(end));
			QVERIFY(c->terminals().at(0)->conductors().isEmpty());
				// to the free end, near its nozzle
			Terminal *start = b->terminals().at(0);
			dragFrom(start, nozzleOf(free_end)->dockConductor() + QPointF(0, 5));
			QVERIFY(!freeEndAt(end));
			QVERIFY(start->isLinkedTo(from));
			QCOMPARE(start->conductors().first()->properties().m_pipe.line_number, QStringLiteral("L3"));
		}

			// Wires: a drag from a terminal near a free end does as before
		void wiresAsBefore()
		{
			Element *a = place(m_pipe, QPointF(0, m_next_y));
			const QPointF end = Diagram::snapToGrid(QPointF(200, m_next_y));
			Element *free_end = lineTo(a->terminals().at(1), end, {});
			QVERIFY(free_end);
			stopTool();
			Element *wire = place(m_electrical, QPointF(500, m_next_y));
			dragFrom(wire->terminals().at(0), nozzleOf(free_end)->dockConductor() + QPointF(0, 5));
			QVERIFY(freeEndAt(end));
			QVERIFY(wire->terminals().at(0)->conductors().isEmpty());
		}
};

QET_APP_TEST_MAIN(tst_pipefreeend)
#include "tst_pipefreeend.moc"
