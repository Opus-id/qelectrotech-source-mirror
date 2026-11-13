// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "diagramcontent.h"
#include "diagramevent/diagrameventaddpipe.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidtee.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "undocommand/deleteqgraphicsitemcommand.h"

#include <QDomDocument>
#include <QGraphicsSceneMouseEvent>
#include <QPointer>
#include <QSet>
#include <QUndoStack>

// Step 5l-1q: a tee placed by a click on a pipe. With the tool "Draw a
// pipe", a click on a pipe starts or ends the new pipe there; outside the
// tool, a drag from a free nozzle released on a pipe does the same. The
// tee (symbol of the project) sits on the grid along the line, exactly on
// it, its way out toward the new pipe; near a bend it takes the place of
// the bend. The pipe is cut through it only, the pieces keeping its data
// and its way; all in one undo step. Refused: too near a nozzle (2.5 mm),
// two tees too near on one pipe, a wire or a signal line, a symbol of tee
// which is not one, a nozzle already connected.
class tst_pipetee : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40, N1 and N2 at 10 from its middle: west and east,
		// or north and south when @p vertical; @p medium on both
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &medium,
								  bool vertical = false)
	{
		const QString attr = medium.isEmpty() ? QString()
											  : QStringLiteral(" medium=\"%1\"").arg(medium);
		const QString nozzles = vertical
				? QStringLiteral("<terminal name=\"N1\" x=\"0\" y=\"-10\" orientation=\"n\" type=\"Generic\"%1/>"
								 "<terminal name=\"N2\" x=\"0\" y=\"10\" orientation=\"s\" type=\"Generic\"%1/>")
				: QStringLiteral("<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%1/>"
								 "<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%1/>");
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<rect x=\"-10\" y=\"-10\" width=\"20\" height=\"20\" style=\"line-style:normal;"
			"line-weight:normal;filling:white;color:black\" antialias=\"false\"/>"
			"%2</description></definition>").arg(uuid, nozzles.arg(attr)));
		return doc.documentElement();
	}

		// A tee as RAC-TE: N1 west and N2 east at 20 from its middle, N3
		// south at 20
	static QDomElement teeDefinition(QDomDocument &doc)
	{
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"pid_family=\"pipe\" pid_run=\"N1 N2 N3\" "
			"width=\"80\" height=\"80\" hotspot_x=\"40\" hotspot_y=\"40\">"
			"<uuid uuid=\"{a5555555-5555-5555-5555-555555555555}\"/>"
			"<names><name lang=\"en\">Tee</name></names>"
			"<description>"
			"<line x1=\"-20\" y1=\"0\" x2=\"20\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-20\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"<terminal name=\"N2\" x=\"20\" y=\"0\" orientation=\"e\" type=\"Generic\" medium=\"pipe\"/>"
			"<terminal name=\"N3\" x=\"0\" y=\"20\" orientation=\"s\" type=\"Generic\" medium=\"pipe\"/>"
			"</description></definition>"));
		return doc.documentElement();
	}

	static QDomElement freeEndDefinition(QDomDocument &doc)
	{
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"pid_family=\"pipe\" pid_role=\"free_end\" "
			"width=\"20\" height=\"20\" hotspot_x=\"10\" hotspot_y=\"10\">"
			"<uuid uuid=\"{a6666666-6666-6666-6666-666666666666}\"/>"
			"<names><name lang=\"en\">Free end</name></names>"
			"<description>"
			"<terminal name=\"N1\" x=\"0\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"</description></definition>"));
		return doc.documentElement();
	}

	QETProject *m_project = nullptr;
	Diagram *m_folio = nullptr;
	ElementsLocation m_pipe, m_vertical, m_electrical, m_tee, m_free_end;
	qreal m_next_y = 0;
	QPointer<DiagramEventAddPipe> m_tool;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project->embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/pipetee"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/pipetee/") + file, m_project);
	}

	static QString path(const QString &file) {
		return QStringLiteral("embed://import/pipetee/") + file;
	}

	void setToolSymbol(const QString &role, const QString &location)
	{
		PidCodification codification = m_project->pidCodification();
		if (location.isEmpty()) {
			codification.tool_symbols.remove(role);
		} else {
			codification.tool_symbols.insert(role, location);
		}
		m_project->setPidCodification(codification);
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

	static Terminal *nozzle(Element *element, const char *name)
	{
		for (Terminal *t : element->terminals())
			if (t->name() == QLatin1String(name)) return t;
		return nullptr;
	}

		// A pipe of fluid W01, DN 100, from @p from to @p to
	Conductor *draw(Terminal *from, Terminal *to)
	{
		auto *pipe = new Conductor(from, to);
		m_folio->addItem(pipe);
		ConductorProperties data = pipe->properties();
		if (pipe->medium() == TerminalData::Pipe) {
			data.m_pipe.fluid = QStringLiteral("W01");
			data.m_pipe.nominal = PipeDiameter::nominal(100);
			data.m_pipe.line_number = QStringLiteral("W01-L");
			pipe->setProperties(data);
		}
		return pipe;
	}

		// A row of its own: a ── pipe ── b, the pipe from (10, y) to (390, y)
	struct Line { Element *a, *b; Conductor *pipe; qreal y; };
	Line line(const ElementsLocation &symbol = ElementsLocation())
	{
		m_next_y += 400;
		const ElementsLocation used = symbol.isElement() ? symbol : m_pipe;
		Line l;
		l.y = m_next_y;
		l.a = place(used, QPointF(0, l.y));
		l.b = place(used, QPointF(400, l.y));
		l.pipe = draw(nozzle(l.a, "N2"), nozzle(l.b, "N1"));
		return l;
	}

	QSet<Element *> elements() const
	{
		const QList<Element *> all = m_folio->elements();
		return QSet<Element *>(all.begin(), all.end());
	}

		// The one element added since @p before, nullptr otherwise
	Element *added(const QSet<Element *> &before) const
	{
		QSet<Element *> now = elements();
		now.subtract(before);
		return now.size() == 1 ? *now.begin() : nullptr;
	}

	static Conductor *pipeOn(Terminal *t)
	{
		return t && t->conductors().size() == 1 ? t->conductors().first() : nullptr;
	}

	static bool joins(const Conductor *c, const Terminal *a, const Terminal *b)
	{
		return c && ((c->terminal1 == a && c->terminal2 == b)
					 || (c->terminal1 == b && c->terminal2 == a));
	}

	static bool hasTheData(const Conductor *c)
	{
		return c && c->properties().m_pipe.fluid == QStringLiteral("W01")
				&& c->properties().m_pipe.nominal == PipeDiameter::nominal(100)
				&& c->properties().m_pipe.line_number == QStringLiteral("W01-L");
	}

	void mouse(QEvent::Type type, const QPointF &at, Qt::MouseButton button, Qt::MouseButtons buttons,
			   Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		QGraphicsSceneMouseEvent event(type);
		event.setScenePos(at);
		event.setButton(button);
		event.setButtons(buttons);
		event.setModifiers(modifiers);
		QCoreApplication::sendEvent(m_folio, &event);
	}

	void click(const QPointF &at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		mouse(QEvent::GraphicsSceneMousePress, at, Qt::LeftButton, Qt::LeftButton, modifiers);
		mouse(QEvent::GraphicsSceneMouseRelease, at, Qt::LeftButton, Qt::NoButton, modifiers);
	}

		// Where the ways of @p tee meet: between its two facing nozzles
	static QPointF middleOf(Element *tee)
	{
		return (nozzle(tee, "N1")->dockConductor() + nozzle(tee, "N2")->dockConductor()) / 2;
	}

		// The elements added since @p before: the tee and the free end
	void addedTeeAndFreeEnd(const QSet<Element *> &before, Element **tee, Element **free_end) const
	{
		*tee = *free_end = nullptr;
		QSet<Element *> now = elements();
		now.subtract(before);
		for (Element *element : std::as_const(now)) {
			(element->isFreeEnd() ? *free_end : *tee) = element;
		}
	}

		// Outside the tool: the mouse pressed on @p from, released at @p at
	void dragFrom(Terminal *from, const QPointF &at)
	{
		mouse(QEvent::GraphicsSceneMousePress, from->mapToScene(from->shape().boundingRect().center()),
			  Qt::LeftButton, Qt::LeftButton);
		mouse(QEvent::GraphicsSceneMouseMove, at, Qt::NoButton, Qt::LeftButton);
		mouse(QEvent::GraphicsSceneMouseRelease, at, Qt::LeftButton, Qt::NoButton);
	}

		// The route of the preview is the path of @p pipe, in one way or the
		// other, on the folio
	static bool sameRoute(const QPainterPath &route, const Conductor *pipe)
	{
		QList<QPointF> a, b;
		for (int i = 0; i < route.elementCount(); ++i) a << QPointF(route.elementAt(i));
		const QPainterPath path = pipe->path();
		for (int i = 0; i < path.elementCount(); ++i) b << pipe->mapToScene(QPointF(path.elementAt(i)));
		if (a == b) return true;
		std::reverse(b.begin(), b.end());
		return a == b;
	}

	void startTool()
	{
		m_tool = new DiagramEventAddPipe(m_folio);
		m_folio->setEventInterface(m_tool);
	}

	void stopTool()
	{
		if (m_tool) m_folio->clearEventInterface();
	}

	private slots:
		void initTestCase()
		{
			m_project = new QETProject();
			m_project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("pipetee"), NamesList());
			QDomDocument a, b, c, d, e;
			m_pipe = add("pipe.elmt", definition(a, "a1111111-1111-1111-1111-111111111111", "pipe"));
			m_vertical = add("vertical.elmt", definition(b, "a2222222-2222-2222-2222-222222222222",
														 "pipe", true));
			m_electrical = add("electrical.elmt", definition(c, "a3333333-3333-3333-3333-333333333333",
															 QString()));
			m_tee = add("tee.elmt", teeDefinition(d));
			m_free_end = add("freeend.elmt", freeEndDefinition(e));
			QVERIFY(m_pipe.exist() && m_vertical.exist() && m_electrical.exist()
					&& m_tee.exist() && m_free_end.exist());
			m_folio = m_project->addNewDiagram();
		}

		void init()
		{
			setToolSymbol(PidCodification::teeRole(), path("tee.elmt"));
			setToolSymbol(PidCodification::freeEndRole(), QString());
		}

		void cleanup()
		{
			stopTool();
		}

			// The tee of the project: its arms, 20 long
		void theTeeOfTheProjectIsMeasured()
		{
			QVERIFY(PidTee::isSet(m_folio));
			QVERIFY(PidTee::symbol(m_folio).exist());
			QCOMPARE(PidTee::armOf(m_folio), 20.0);
				// a symbol with two nozzles is no tee
			setToolSymbol(PidCodification::teeRole(), path("pipe.elmt"));
			QCOMPARE(PidTee::armOf(m_folio), 0.0);
			const Line l = line();
			const PidTee::Spot spot = PidTee::spotAt(m_folio, QPointF(200, l.y + 1));
			QCOMPARE(spot.pipe.data(), l.pipe);
			QCOMPARE(spot.refusal, PidTee::notATee());
		}

			// On a straight part: the nearest point of the line, on the grid
			// along it, exactly on the line across it
		void theTeeSitsOnTheGridOnTheLine()
		{
			m_next_y += 400;
				// a line off the grid, as moving its symbols by 1 unit
				// (Ctrl + arrows) or without the grid leaves it
			const bool snap = Diagram::gridSnap();
			Diagram::setGridSnap(false);
			Element *a = place(m_pipe, QPointF(0, m_next_y + 5));
			Element *b = place(m_pipe, QPointF(400, m_next_y + 5));
			Diagram::setGridSnap(true);
			Conductor *pipe = draw(nozzle(a, "N2"), nozzle(b, "N1"));
			const PidTee::Spot spot = PidTee::spotOn(pipe, QPointF(203, m_next_y + 8), 20);
			QVERIFY(spot.isValid());
			QVERIFY(!spot.corner);
			QCOMPARE(spot.point, QPointF(200, m_next_y + 5));
			QCOMPARE(spot.back, Qet::West);
			QCOMPARE(spot.forward, Qet::East);
				// the way out: aside, toward the other end; in the axis, below
			QCOMPARE(PidTee::branchFacing(spot, QPointF(250, m_next_y - 100)), Qet::North);
			QCOMPARE(PidTee::branchFacing(spot, QPointF(150, m_next_y + 100)), Qet::South);
			QCOMPARE(PidTee::branchFacing(spot, QPointF(600, m_next_y + 5)), Qet::South);
			Diagram::setGridSnap(snap);
		}

			// A vertical line: in its axis, to the right
		void inTheAxisOfAVerticalLineTheWayOutIsToTheRight()
		{
			m_next_y += 400;
			Element *a = place(m_vertical, QPointF(0, m_next_y));
			Element *b = place(m_vertical, QPointF(0, m_next_y + 300));
			Conductor *pipe = draw(nozzle(a, "N2"), nozzle(b, "N1"));
			const PidTee::Spot spot = PidTee::spotOn(pipe, QPointF(2, m_next_y + 150), 20);
			QVERIFY(spot.isValid());
			QCOMPARE(spot.point, QPointF(0, m_next_y + 150));
			QCOMPARE(PidTee::branchFacing(spot, QPointF(0, m_next_y + 600)), Qet::East);
			QCOMPARE(PidTee::branchFacing(spot, QPointF(-100, m_next_y)), Qet::West);
		}

			// 2.5 mm at least from a nozzle (13/11, after 5 mm at the first
			// trial); two tees on one pipe too
		void aTeeKeepsItsDistance()
		{
			QCOMPARE(PidTee::nozzle_room, 10.0);
			const Line l = line();
				// the pipe starts at x = 10 and ends at x = 390: 20 of arm
				// and 10 of room
			QVERIFY(!PidTee::spotOn(l.pipe, QPointF(35, l.y), 20).isValid());
			QVERIFY(!PidTee::spotOn(l.pipe, QPointF(35, l.y), 20).refusal.isEmpty());
			QCOMPARE(PidTee::spotOn(l.pipe, QPointF(40, l.y), 20).point, QPointF(40, l.y));
			QVERIFY(PidTee::spotOn(l.pipe, QPointF(360, l.y), 20).isValid());
			QVERIFY(!PidTee::spotOn(l.pipe, QPointF(365, l.y), 20).isValid());
			const PidTee::Spot one = PidTee::spotOn(l.pipe, QPointF(150, l.y), 20);
			const PidTee::Spot two = PidTee::spotOn(l.pipe, QPointF(190, l.y), 20);
			const PidTee::Spot three = PidTee::spotOn(l.pipe, QPointF(200, l.y), 20);
			QVERIFY(PidTee::clash(one, three, 20).isEmpty());
			QVERIFY(!PidTee::clash(one, two, 20).isEmpty());
		}

			// A wire or a signal line takes no tee: said, nothing done
		void aWireTakesNoTee()
		{
			const Line l = line(m_electrical);
			QVERIFY(l.pipe->medium() != TerminalData::Pipe);
			const PidTee::Spot spot = PidTee::spotAt(m_folio, QPointF(200, l.y));
			QCOMPARE(spot.pipe.data(), l.pipe);
			QVERIFY(!spot.isValid());
			QVERIFY(!spot.refusal.isEmpty());

			startTool();
			const QSet<Element *> before = elements();
			click(QPointF(200, l.y));
			QVERIFY(!m_tool->startsOnPipe());
			QCOMPARE(elements(), before);
			QVERIFY(joins(l.pipe, nozzle(l.a, "N2"), nozzle(l.b, "N1")));
		}

			// With the tool: a click on a pipe starts there, nothing placed;
			// the end near a nozzle below places the tee, its way out down,
			// and cuts the pipe; one undo step
		void aPipeStartsOnAPipe()
		{
			const Line l = line();
			Element *c = place(m_vertical, QPointF(200, l.y + 200));
			Terminal *a = nozzle(l.a, "N2"), *b = nozzle(l.b, "N1"), *to = nozzle(c, "N1");
			const QSet<Element *> before = elements();
			const int steps = m_folio->undoStack().index();

			startTool();
			click(QPointF(203, l.y + 2));
			QVERIFY(m_tool->startsOnPipe());
			QCOMPARE(m_tool->startSpot().point, QPointF(200, l.y));
			QCOMPARE(elements(), before);
			QCOMPARE(m_folio->undoStack().index(), steps);

			click(to->dockConductor() + QPointF(0, 5));
			Element *tee = added(before);
			QVERIFY(tee);
			Terminal *n1 = nozzle(tee, "N1"), *n2 = nozzle(tee, "N2"), *n3 = nozzle(tee, "N3");
			QCOMPARE(n1->orientation(), Qet::West);
			QCOMPARE(n3->orientation(), Qet::South);
			QCOMPARE(n1->dockConductor(), QPointF(180, l.y));
				// the pipe cut: two pieces with its data and its way
			Conductor *first = pipeOn(a), *second = pipeOn(b);
			QVERIFY(joins(first, a, n1));
			QVERIFY(joins(second, n2, b));
			QCOMPARE(first->terminal1, a);
			QCOMPARE(second->terminal1, n2);
			QVERIFY(hasTheData(first) && hasTheData(second));
			QVERIFY(!l.pipe->scene());
				// the new pipe, from the way out, of the DN of the line
			Conductor *branch = pipeOn(n3);
			QVERIFY(joins(branch, n3, to));
			QCOMPARE(branch->properties().m_pipe.nominal, PipeDiameter::nominal(100));
			QVERIFY(!m_tool->startsOnPipe());

				// one step: undone, the pipe whole again
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(elements(), before);
			QCOMPARE(pipeOn(a), l.pipe);
			QVERIFY(joins(l.pipe, a, b));
			QVERIFY(hasTheData(l.pipe));
			QCOMPARE(to->conductors().size(), 0);
			m_folio->undoStack().redo();
			QVERIFY(joins(pipeOn(n3), n3, to));
			QVERIFY(joins(pipeOn(a), a, n1));
		}

			// The branch deleted: the tee stays, its way out free (11/11);
			// the line is joined again only when the tee itself goes
		void deletingTheBranchLeavesTheTee()
		{
			const Line l = line();
			Element *c = place(m_vertical, QPointF(200, l.y + 200));
			const QSet<Element *> before = elements();
			startTool();
			click(QPointF(200, l.y));
			click(nozzle(c, "N1")->dockConductor() + QPointF(0, 5));
			stopTool();
			Element *tee = added(before);
			QVERIFY(tee);
			Conductor *branch = pipeOn(nozzle(tee, "N3"));
			QVERIFY(branch);
			DiagramContent content;
			content.m_other_conductors << branch;
			m_folio->undoStack().push(new DeleteQGraphicsItemCommand(m_folio, content));
			QVERIFY(tee->scene());
			QCOMPARE(nozzle(tee, "N3")->conductors().size(), 0);
			QVERIFY(joins(pipeOn(nozzle(l.a, "N2")), nozzle(l.a, "N2"), nozzle(tee, "N1")));
			QVERIFY(joins(pipeOn(nozzle(l.b, "N1")), nozzle(tee, "N2"), nozzle(l.b, "N1")));
		}

			// From a nozzle above to a pipe: the way out up
		void aPipeEndsOnAPipe()
		{
			const Line l = line();
			Element *c = place(m_vertical, QPointF(100, l.y - 200));
			Terminal *from = nozzle(c, "N2");
			const QSet<Element *> before = elements();
			startTool();
			click(from->dockConductor() + QPointF(0, -5));
			QCOMPARE(m_tool->startNozzle(), from);
			click(QPointF(101, l.y + 2));
			Element *tee = added(before);
			QVERIFY(tee);
			QCOMPARE(nozzle(tee, "N3")->orientation(), Qet::North);
			QVERIFY(joins(pipeOn(from), from, nozzle(tee, "N3")));
			QVERIFY(joins(pipeOn(nozzle(l.a, "N2")), nozzle(l.a, "N2"), nozzle(tee, "N2")));
		}

			// A drag from a nozzle released on a pipe, in the tool and
			// outside it: a tee there
		void aDragReleasedOnAPipe()
		{
			const Line l = line();
			Element *c = place(m_vertical, QPointF(300, l.y + 200));
			Terminal *from = nozzle(c, "N1");
			QSet<Element *> before = elements();
			startTool();
			mouse(QEvent::GraphicsSceneMousePress, from->dockConductor() + QPointF(0, 5),
				  Qt::LeftButton, Qt::LeftButton);
			mouse(QEvent::GraphicsSceneMouseMove, QPointF(300, l.y + 1), Qt::NoButton, Qt::LeftButton);
			mouse(QEvent::GraphicsSceneMouseRelease, QPointF(300, l.y + 1), Qt::LeftButton, Qt::NoButton);
			Element *tee = added(before);
			QVERIFY(tee);
			QVERIFY(joins(pipeOn(from), from, nozzle(tee, "N3")));
			stopTool();

				// outside the tool
			const Line other = line();
			Element *d = place(m_vertical, QPointF(150, other.y + 200));
			Terminal *start = nozzle(d, "N1");
			before = elements();
			const int steps = m_folio->undoStack().index();
			dragFrom(start, QPointF(152, other.y + 1));
			tee = added(before);
			QVERIFY(tee);
			QVERIFY(joins(pipeOn(start), start, nozzle(tee, "N3")));
			QVERIFY(hasTheData(pipeOn(nozzle(other.a, "N2"))));
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(elements(), before);
			QVERIFY(joins(other.pipe, nozzle(other.a, "N2"), nozzle(other.b, "N1")));
		}

			// Outside the tool, a nozzle already connected: refused, said
		void aNozzleAlreadyConnectedTakesNoTee()
		{
			const Line l = line();
			const Line other = line();
			Terminal *full = nozzle(other.a, "N2");
			const QSet<Element *> before = elements();
			QString refusal;
			QVERIFY(PidTee::branchFrom(full, QPointF(200, l.y), &refusal));
			QVERIFY(!refusal.isEmpty());
			QCOMPARE(elements(), before);
			QVERIFY(joins(l.pipe, nozzle(l.a, "N2"), nozzle(l.b, "N1")));
				// no tee in the project: nothing at all, nothing said
			setToolSymbol(PidCodification::teeRole(), QString());
			refusal.clear();
			Element *c = place(m_vertical, QPointF(200, l.y + 200));
			QVERIFY(!PidTee::branchFrom(nozzle(c, "N1"), QPointF(200, l.y), &refusal));
			QVERIFY(refusal.isEmpty());
		}

			// No tee in the project: a click on a pipe starts nothing
		void noTeeInTheProjectNoTee()
		{
			setToolSymbol(PidCodification::teeRole(), QString());
			const Line l = line();
			startTool();
			click(QPointF(200, l.y));
			QVERIFY(!m_tool->startsOnPipe());
			QVERIFY(!m_tool->startNozzle());
		}

			// Both ends on one pipe: two tees, one cut, the new pipe between
			// their ways out
		void bothEndsOnOnePipe()
		{
			const Line l = line();
			Terminal *a = nozzle(l.a, "N2"), *b = nozzle(l.b, "N1");
			const QSet<Element *> before = elements();
			const int steps = m_folio->undoStack().index();
			startTool();
			click(QPointF(100, l.y));
			QVERIFY(m_tool->startsOnPipe());
				// too near the first: refused, still waiting
			click(QPointF(140, l.y));
			QVERIFY(m_tool->startsOnPipe());
			click(QPointF(300, l.y));
			QVERIFY(!m_tool->startsOnPipe());
			QSet<Element *> tees = elements();
			tees.subtract(before);
			QCOMPARE(tees.size(), 2);
			Element *left = nullptr, *right = nullptr;
			for (Element *tee : tees) {
				(tee->pos().x() < 200 ? left : right) = tee;
			}
			QVERIFY(left && right);
			QVERIFY(joins(pipeOn(a), a, nozzle(left, "N1")));
			QVERIFY(joins(pipeOn(nozzle(left, "N2")), nozzle(left, "N2"), nozzle(right, "N1")));
			QVERIFY(joins(pipeOn(b), nozzle(right, "N2"), b));
			QVERIFY(hasTheData(pipeOn(nozzle(left, "N2"))));
			QVERIFY(joins(pipeOn(nozzle(left, "N3")), nozzle(left, "N3"), nozzle(right, "N3")));
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(elements(), before);
			QVERIFY(joins(l.pipe, a, b));
		}

			// Only the pipe clicked is cut: another one passing by the way
			// out of the tee stays whole
		void onlyThePipeClickedIsCut()
		{
			const Line l = line();
			Element *c = place(m_pipe, QPointF(-100, l.y + 20));
			Element *d = place(m_pipe, QPointF(500, l.y + 20));
			Conductor *by = draw(nozzle(c, "N2"), nozzle(d, "N1"));
			Element *e = place(m_vertical, QPointF(200, l.y + 300));
			startTool();
			click(QPointF(200, l.y - 1));
			QVERIFY(m_tool->startsOnPipe());
			click(nozzle(e, "N1")->dockConductor() + QPointF(0, 5));
			QVERIFY(by->scene());
			QVERIFY(joins(by, nozzle(c, "N2"), nozzle(d, "N1")));
			QVERIFY(!l.pipe->scene());
		}

			// Near a bend the tee takes its place: the new pipe goes on
			// along one leg, the way aside takes the other
		void nearABendTheTeeTakesItsPlace()
		{
			m_next_y += 400;
			const qreal y = m_next_y;
			Element *a = place(m_pipe, QPointF(0, y));
			Element *b = place(m_vertical, QPointF(400, y + 300));
			Terminal *from = nozzle(a, "N2"), *to = nozzle(b, "N1");
			Conductor *pipe = draw(from, to);
				// east from a, then down to b: the bend at (400, y)
			const PidTee::Spot spot = PidTee::spotOn(pipe, QPointF(395, y + 2), 20);
			QVERIFY2(spot.isValid(), qPrintable(spot.refusal));
			QVERIFY(spot.corner);
			QCOMPARE(spot.point, QPointF(400, y));
			QCOMPARE(spot.back, Qet::West);
			QCOMPARE(spot.forward, Qet::South);
				// toward the right: on along the first leg; up: along the second
			QCOMPARE(PidTee::branchFacing(spot, QPointF(600, y + 10)), Qet::East);
			QCOMPARE(PidTee::branchFacing(spot, QPointF(390, y - 200)), Qet::North);

			Element *e = place(m_pipe, QPointF(600, y));
			const QSet<Element *> before = elements();
			startTool();
			click(QPointF(395, y + 2));
			QVERIFY(m_tool->startsOnPipe());
			click(nozzle(e, "N1")->dockConductor() + QPointF(0, 5));
			Element *tee = added(before);
			QVERIFY(tee);
				// the run along the first leg and the new pipe, the way aside down
			QCOMPARE(nozzle(tee, "N3")->orientation(), Qet::South);
			QVERIFY(joins(pipeOn(from), from, nozzle(tee, "N1")));
			QVERIFY(joins(pipeOn(to), nozzle(tee, "N3"), to));
			QVERIFY(joins(pipeOn(nozzle(e, "N1")), nozzle(tee, "N2"), nozzle(e, "N1")));
			QVERIFY(hasTheData(pipeOn(to)));
		}

			// Step 5l-1p-ter: the preview shows the route of the pipe placed,
			// to a tee and from a tee (in S to a free end)
		void thePreviewOfAPipeWithATee()
		{
			const Line l = line();
				// from a nozzle above and aside to the pipe: bends
			Element *c = place(m_pipe, QPointF(250, l.y - 200));
			Terminal *from = nozzle(c, "N2");
			startTool();
			click(from->dockConductor() + QPointF(0, -5));
			const QPointF on_pipe(100, l.y + 2);
			mouse(QEvent::GraphicsSceneMouseMove, on_pipe, Qt::NoButton, Qt::NoButton);
			const QPainterPath route = m_folio->conductorRoute();
			QVERIFY(route.elementCount() >= 4);
			click(on_pipe);
			QVERIFY(pipeOn(from));
			QVERIFY(sameRoute(route, pipeOn(from)));

				// from a pipe to a free end, in S
			setToolSymbol(PidCodification::freeEndRole(), path("freeend.elmt"));
			const Line l2 = line();
			m_tool->setSShape(true);
			const QSet<Element *> before = elements();
			click(QPointF(200, l2.y + 1));
			QVERIFY(m_tool->startsOnPipe());
			const QPointF empty(300, l2.y + 200);
			mouse(QEvent::GraphicsSceneMouseMove, empty, Qt::NoButton, Qt::NoButton);
			const QPainterPath route2 = m_folio->conductorRoute();
			QCOMPARE(route2.elementCount(), 6);
			click(empty);
			Element *tee = nullptr, *free_end = nullptr;
			addedTeeAndFreeEnd(before, &tee, &free_end);
			QVERIFY(tee && free_end);
			QVERIFY(sameRoute(route2, pipeOn(free_end->terminals().first())));
		}

			// Step 5l-1p-ter (e): a drag from a nozzle, outside the tool,
			// shows the route of the pipe it draws, to a tee or to a nozzle;
			// the straight line where it draws none, and for a wire
		void aDragShowsTheRouteOfThePipe()
		{
			const auto on = [](Terminal *t) { return t->mapToScene(t->shape().boundingRect().center()); };
			const auto press = [&](Terminal *t) {
				mouse(QEvent::GraphicsSceneMousePress, on(t), Qt::LeftButton, Qt::LeftButton);
			};
			const auto move = [&](const QPointF &at) {
				mouse(QEvent::GraphicsSceneMouseMove, at, Qt::NoButton, Qt::LeftButton);
			};
			const auto release = [&](const QPointF &at) {
				mouse(QEvent::GraphicsSceneMouseRelease, at, Qt::LeftButton, Qt::NoButton);
			};
			const Line l = line();

				// from a nozzle above and aside to the pipe: a tee, bends
			Element *c = place(m_pipe, QPointF(250, l.y - 200));
			Terminal *from = nozzle(c, "N2");
			const QPointF on_pipe(100, l.y + 1);
			press(from);
			move(on_pipe);
			const QPainterPath route = m_folio->conductorRoute();
			QVERIFY(route.elementCount() >= 4);
			release(on_pipe);
			QVERIFY(m_folio->conductorRoute().isEmpty());
			QVERIFY(pipeOn(from));
			QVERIFY(sameRoute(route, pipeOn(from)));

				// to a nozzle
			Element *d = place(m_pipe, QPointF(600, l.y - 100));
			Terminal *start = nozzle(c, "N1");
			Terminal *to = nozzle(d, "N1");
			press(start);
			move(on(to));
			const QPainterPath route2 = m_folio->conductorRoute();
			QVERIFY(route2.elementCount() >= 4);
			release(on(to));
			QVERIFY(joins(pipeOn(start), start, to));
			QVERIFY(sameRoute(route2, pipeOn(start)));

				// to a nozzle already connected, or nowhere: no route
			Terminal *third = nozzle(d, "N2");
			press(third);
			move(on(from));
			QVERIFY(m_folio->conductorRoute().isEmpty());
			move(QPointF(800, l.y - 300));
			QVERIFY(m_folio->conductorRoute().isEmpty());
			release(QPointF(800, l.y - 300));
			QVERIFY(third->conductors().isEmpty());

				// a wire: the straight line, as always
			Element *e1 = place(m_electrical, QPointF(0, l.y - 300));
			Element *e2 = place(m_electrical, QPointF(200, l.y - 250));
			Terminal *wire_from = nozzle(e1, "N2");
			press(wire_from);
			move(on(nozzle(e2, "N1")));
			QVERIFY(m_folio->conductorRoute().isEmpty());
			release(on(nozzle(e2, "N1")));
			QCOMPARE(wire_from->conductors().size(), 1);
		}

			// From an empty place to a pipe: a free end and a tee, one step
		void fromAnEmptyPlaceToAPipe()
		{
			setToolSymbol(PidCodification::freeEndRole(), path("freeend.elmt"));
			const Line l = line();
			const QSet<Element *> before = elements();
			const int steps = m_folio->undoStack().index();
			startTool();
			click(QPointF(200, l.y + 200));
			QVERIFY(m_tool->startsInEmptyPlace());
			click(QPointF(200, l.y + 1));
			QSet<Element *> now = elements();
			now.subtract(before);
			QCOMPARE(now.size(), 2);
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(elements(), before);
		}

			// Step 5l-1p-bis: from a tee, an end nearly in the axis of its
			// way out snaps onto it
		void fromATeeTheAxisOfItsWayOut()
		{
			setToolSymbol(PidCodification::freeEndRole(), path("freeend.elmt"));
			const Line l = line();
			const QSet<Element *> before = elements();
			startTool();
			click(QPointF(200, l.y));
			QVERIFY(m_tool->startsOnPipe());
			const QPointF at(218, l.y + 200);
			click(at);
			Element *tee = nullptr, *free_end = nullptr;
			addedTeeAndFreeEnd(before, &tee, &free_end);
			QVERIFY(tee && free_end);
			QCOMPARE(middleOf(tee), QPointF(200, l.y));
			QCOMPARE(nozzle(tee, "N3")->orientation(), Qet::South);
				// right below the tee, on the grid along the axis
			QCOMPARE(free_end->terminals().first()->dockConductor(),
					 QPointF(200, Diagram::snapToGrid(at).y()));
			QCOMPARE(free_end->terminals().first()->orientation(), Qet::North);
		}

			// Step 5l-1p-bis: from an empty place to a pipe nearly in front,
			// the tee goes right in front of the start; not with Ctrl
		void aTeeGoesInFrontOfTheStart()
		{
			setToolSymbol(PidCodification::freeEndRole(), path("freeend.elmt"));
			startTool();
			for (const bool ctrl : {false, true})
			{
				const Line l = line();
				const QSet<Element *> before = elements();
				const QPointF start(200, l.y + 200);
				click(start);
				QVERIFY(m_tool->startsInEmptyPlace());
				click(QPointF(220, l.y + 1), ctrl ? Qt::ControlModifier : Qt::NoModifier);
				Element *tee = nullptr, *free_end = nullptr;
				addedTeeAndFreeEnd(before, &tee, &free_end);
				QVERIFY(tee && free_end);
				QCOMPARE(middleOf(tee), QPointF(ctrl ? 220 : 200, l.y));
				QCOMPARE(nozzle(tee, "N3")->orientation(), Qet::South);
				QCOMPARE(free_end->terminals().first()->dockConductor(), start);
				QVERIFY(joins(pipeOn(nozzle(tee, "N3")), nozzle(tee, "N3"), free_end->terminals().first()));
			}
				// from a nozzle which leaves away from the pipe: where clicked
			const Line l = line();
			Element *c = place(m_pipe, QPointF(200, l.y + 200));
			const QSet<Element *> before = elements();
			click(nozzle(c, "N2")->dockConductor() + QPointF(5, 5));
			QCOMPARE(m_tool->startNozzle(), nozzle(c, "N2"));
			click(QPointF(230, l.y + 1));
			Element *tee = added(before);
			QVERIFY(tee);
			QCOMPARE(middleOf(tee), QPointF(230, l.y));
		}

			// Step 5l-1q: the symbol of tee, a setting of the presentation
			// of the project, beside the free end
		void theSymbolOfTeeIsASettingOfTheProject()
		{
			QVERIFY(PidCodification::toolSymbolRoles().contains(QStringLiteral("tee")));
			const QString rac_te = QStringLiteral("custom://pid_essai/raccords/RAC-TE Té.elmt");
			PidCodification codification;
			codification.tool_symbols.insert(PidCodification::teeRole(), rac_te);
			QDomDocument doc;
			QDomElement holder = doc.createElement(QStringLiteral("project"));
			holder.appendChild(codification.toXml(doc));
			PidCodification read;
			read.fromXml(holder);
			QCOMPARE(read.toolSymbol(PidCodification::teeRole()), rac_te);
			QVERIFY(codification.presentationToCsv().contains(QStringLiteral("tool_symbol;tee;") + rac_te));
			int skipped = 0;
			const PidCodification csv = PidCodification::fromCsv(
						QStringLiteral("tool_symbol;tee;") + rac_te + QStringLiteral("\n"), &skipped);
			QCOMPARE(csv.toolSymbol(PidCodification::teeRole()), rac_te);
			QCOMPARE(skipped, 0);
		}
};

QET_APP_TEST_MAIN(tst_pipetee)
#include "tst_pipetee.moc"
