// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "diagramcontent.h"
#include "diagramevent/diagrameventaddpipe.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidprocessconnection.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "signalproperties.h"
#include "undocommand/deleteqgraphicsitemcommand.h"

#include <QDomDocument>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QUndoStack>

// A process connection (step 6b-3c2): the thin line from the process to an
// instrument bubble, drawn in one gesture with the tool "Draw a pipe". From
// a signal terminal while the tool draws process connections, or from a
// free nozzle or a free end to a signal terminal. Two rules: a free end of a
// pipe is closed by the blind flange of the project, placed by the tool,
// and the line leaves from that flange; any other free nozzle takes the
// line itself. One undo step. The blind flange placed by the tool goes with
// the pipe it closes; one placed by hand stays.
class tst_processconnection : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40 with two terminals N1 (west) and N2 (east), both
		// with the medium @p medium
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &medium)
	{
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<rect x=\"-10\" y=\"-10\" width=\"20\" height=\"20\" style=\"line-style:normal;"
			"line-weight:normal;filling:white;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"%2\"/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\" medium=\"%2\"/>"
			"</description></definition>").arg(uuid, medium));
		return doc.documentElement();
	}

		// A free end: one nozzle N1 at its point of insertion, facing west
	static QDomElement freeEndDefinition(QDomDocument &doc)
	{
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"pid_family=\"pipe\" pid_role=\"free_end\" "
			"width=\"20\" height=\"20\" hotspot_x=\"10\" hotspot_y=\"10\">"
			"<uuid uuid=\"{a3333333-3333-3333-3333-333333333333}\"/>"
			"<names><name lang=\"en\">Free end</name></names>"
			"<description>"
			"<terminal name=\"N1\" x=\"0\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"</description></definition>"));
		return doc.documentElement();
	}

		// A blind flange: its nozzle N1 facing west and its signal terminal
		// S1 at the same place, facing east (step 6b-3c1)
	static QDomElement blindFlangeDefinition(QDomDocument &doc)
	{
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"pid_family=\"pipe\" width=\"20\" height=\"20\" hotspot_x=\"10\" hotspot_y=\"10\">"
			"<uuid uuid=\"{a5555555-5555-5555-5555-555555555555}\"/>"
			"<names><name lang=\"en\">Blind flange</name></names>"
			"<description>"
			"<line x1=\"0\" y1=\"-5\" x2=\"0\" y2=\"5\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
			"<terminal name=\"N1\" x=\"0\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"<terminal name=\"S1\" x=\"0\" y=\"0\" orientation=\"e\" type=\"Generic\" medium=\"signal\"/>"
			"</description></definition>"));
		return doc.documentElement();
	}

	static QString blindFlangePath() { return QStringLiteral("embed://import/process/blindflange.elmt"); }

		// The blind flange of the project: @p path, none when empty
	void setBlindFlange(const QString &path)
	{
		PidCodification codification = m_project->pidCodification();
		if (path.isEmpty()) {
			codification.tool_symbols.remove(PidCodification::blindFlangeRole());
		} else {
			codification.tool_symbols.insert(PidCodification::blindFlangeRole(), path);
		}
		m_project->setPidCodification(codification);
	}

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project->embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/process"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/process/") + file, m_project);
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

		// A line to close: a symbol with its nozzle N2 piped to a free end
		// 100 to the east, and a bubble above the free end; on a row of
		// their own. The pipe has the line number W01.
	struct Line
	{
		Element *symbol = nullptr;
		Element *free_end = nullptr;
		Element *bubble = nullptr;
		Conductor *pipe = nullptr;
		Terminal *nozzle() const { return free_end->terminals().first(); }
		Terminal *signal() const { return bubble->terminals().at(1); }
	};
	Line lineWithAFreeEnd()
	{
		m_next_y += 200;
		Line line;
		line.symbol = place(m_pipe, QPointF(0, m_next_y));
		line.free_end = place(m_free_end, QPointF(110, m_next_y));
		line.bubble = place(m_signal, QPointF(150, m_next_y - 80));
		if (!line.symbol || !line.free_end || !line.bubble) return line;
		line.pipe = line.symbol->terminals().at(1)->connectTo(line.nozzle());
		if (line.pipe) {
			ConductorProperties properties = line.pipe->properties();
			properties.m_pipe.line_number = QStringLiteral("W01");
			line.pipe->setProperties(properties);
		}
		return line;
	}

		// The blind flange the tool placed whose nozzle is at @p dock
	Element *toolFlangeAt(const QPointF &dock) const
	{
		for (Element *element : m_folio->elements()) {
			if (!element->isToolPlaced()) continue;
			for (Terminal *t : element->terminals()) {
				if (t->medium() == TerminalData::Pipe && QLineF(t->dockConductor(), dock).length() < 0.5) {
					return element;
				}
			}
		}
		return nullptr;
	}

	static Terminal *terminalOf(const Element *element, TerminalData::Medium medium)
	{
		for (Terminal *t : element->terminals()) {
			if (t->medium() == medium) return t;
		}
		return nullptr;
	}

	void mouse(QEvent::Type type, const QPointF &at, Qt::MouseButton button, Qt::MouseButtons buttons)
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

		// A point near @p t but not on it: 10 units below its docking point
	static QPointF nearBelow(const Terminal *t) { return t->dockConductor() + QPointF(0, 10); }

		// Deleted as the editor does when the user asks it
	void removeAsTheUser(const QList<QGraphicsItem *> &items)
	{
		m_folio->clearSelection();
		for (QGraphicsItem *item : items) item->setSelected(true);
		DiagramContent content(m_folio);
		DeleteQGraphicsItemCommand::addLonelyFreeEnds(content);
		m_folio->clearSelection();
		m_folio->undoStack().push(new DeleteQGraphicsItemCommand(m_folio, content));
	}

		// A process connection from the bubble to the free end of @p line:
		// the blind flange placed, @return it
	Element *closeFromTheBubble(const Line &line)
	{
		m_tool->setSignalType(SignalProperties::ProcessConnection);
		click(nearBelow(line.signal()));
		click(nearBelow(line.nozzle()));
		return toolFlangeAt(line.nozzle()->dockConductor());
	}

	static bool isProcessConnection(const Conductor *line)
	{
		return line && line->medium() == TerminalData::Signal
				&& line->properties().m_signal.signal_type == SignalProperties::ProcessConnection;
	}

	QETProject *m_project = nullptr;
	Diagram *m_folio = nullptr;
	ElementsLocation m_pipe, m_signal, m_free_end, m_flange;
	qreal m_next_y = 0;
	QPointer<DiagramEventAddPipe> m_tool;

	private slots:
		void initTestCase()
		{
			m_project = new QETProject();
			m_project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("process"), NamesList());
			QDomDocument a, b, c, d;
			m_pipe = add("pipe.elmt", definition(a, "a2222222-2222-2222-2222-222222222222", "pipe"));
			m_signal = add("signal.elmt", definition(b, "a4444444-4444-4444-4444-444444444444", "signal"));
			m_free_end = add("freeend.elmt", freeEndDefinition(c));
			m_flange = add("blindflange.elmt", blindFlangeDefinition(d));
			QVERIFY(m_pipe.exist() && m_signal.exist() && m_free_end.exist() && m_flange.exist());
			m_folio = m_project->addNewDiagram();
		}

		void init()
		{
			setBlindFlange(blindFlangePath());
			m_tool = new DiagramEventAddPipe(m_folio);
			m_folio->setEventInterface(m_tool);
			QVERIFY(m_tool);
		}

		void cleanup()
		{
			m_folio->clearEventInterface();
			QVERIFY(!m_tool);
		}

			// The blind flange: a symbol a tool places, set by the project
		void theBlindFlangeIsASettingOfTheProject()
		{
			QVERIFY(PidCodification::toolSymbolRoles().contains(QStringLiteral("blind_flange")));
			const QString flange = QStringLiteral("custom://pid/52_closures/closure_blind_flange.elmt");
			PidCodification codification;
			codification.tool_symbols.insert(PidCodification::blindFlangeRole(), flange);
			QDomDocument doc;
			QDomElement holder = doc.createElement(QStringLiteral("project"));
			holder.appendChild(codification.toXml(doc));
			PidCodification read;
			read.fromXml(holder);
			QCOMPARE(read.toolSymbol(PidCodification::blindFlangeRole()), flange);
			QVERIFY(codification.presentationToCsv().contains(QStringLiteral("tool_symbol;blind_flange;") + flange));
			int skipped = 0;
			const PidCodification csv = PidCodification::fromCsv(
						QStringLiteral("tool_symbol;blind_flange;") + flange + QStringLiteral("\n"), &skipped);
			QCOMPARE(csv.toolSymbol(PidCodification::blindFlangeRole()), flange);
			QCOMPARE(skipped, 0);
		}

			// Rule 2: from the bubble to a free nozzle, the line on the nozzle
			// itself, which takes no pipe any more
		void fromTheBubbleToAFreeNozzle()
		{
			m_next_y += 200;
			Element *bubble = place(m_signal, QPointF(0, m_next_y));
			Element *symbol = place(m_pipe, QPointF(200, m_next_y));
			QVERIFY(bubble && symbol);
			Terminal *signal = bubble->terminals().at(1);
			Terminal *nozzle = symbol->terminals().at(0);
			const int steps = m_folio->undoStack().index();
			const int elements = m_folio->elements().size();

			m_tool->setSignalType(SignalProperties::ProcessConnection);
			click(nearBelow(signal));
			QVERIFY(m_tool->drawsSignalLine());
				// the nozzle catches the line, shown as a thin line
			move(nearBelow(nozzle));
			QVERIFY(!m_tool->signalPreview().isEmpty());
			QCOMPARE(m_tool->signalPreviewRoute().constLast(), nozzle->dockConductor());
			click(nearBelow(nozzle));

			QCOMPARE(nozzle->conductors().size(), 1);
			Conductor *line = nozzle->conductors().first();
			QVERIFY(signal->isLinkedTo(nozzle));
			QVERIFY(isProcessConnection(line));
				// no flange: nothing placed
			QCOMPARE(m_folio->elements().size(), elements);
				// a nozzle with a process connection takes no pipe
			QVERIFY(!DiagramEventAddPipe::isFreeNozzle(nozzle));
				// one step
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
			m_folio->undoStack().undo();
			QCOMPARE(nozzle->conductors().size(), 0);
			QVERIFY(DiagramEventAddPipe::isFreeNozzle(nozzle));
		}

			// Another type of signal ends on a signal terminal only, as before
		void anotherTypeEndsOnASignalTerminalOnly()
		{
			m_next_y += 200;
			Element *bubble = place(m_signal, QPointF(0, m_next_y));
			Element *symbol = place(m_pipe, QPointF(200, m_next_y));
			QVERIFY(bubble && symbol);
			Terminal *signal = bubble->terminals().at(1);
			Terminal *nozzle = symbol->terminals().at(0);

			m_tool->setSignalType(SignalProperties::Pneumatic);
			click(nearBelow(signal));
			click(nearBelow(nozzle));
			QCOMPARE(nozzle->conductors().size(), 0);
			QCOMPARE(signal->conductors().size(), 0);
				// still waiting for its end
			QVERIFY(m_tool->drawsSignalLine());
			key(Qt::Key_Escape);
		}

			// Rule 2 the other way: from a free nozzle, a signal terminal ends
			// the line as a process connection, whatever the type of the tool
		void fromAFreeNozzleToTheBubble()
		{
			m_next_y += 200;
			Element *symbol = place(m_pipe, QPointF(0, m_next_y));
			Element *bubble = place(m_signal, QPointF(200, m_next_y));
			QVERIFY(bubble && symbol);
			Terminal *nozzle = symbol->terminals().at(1);
			Terminal *signal = bubble->terminals().at(0);
			const int steps = m_folio->undoStack().index();

			QCOMPARE(int(m_tool->signalType()), int(SignalProperties::Undefined));
			click(nearBelow(nozzle));
			QCOMPARE(m_tool->startNozzle(), nozzle);
				// near the bubble: the thin line in place of the pipe
			move(nearBelow(signal));
			QVERIFY(!m_tool->signalPreview().isEmpty());
			QCOMPARE(m_tool->signalPreviewRoute().constLast(), signal->dockConductor());
				// away from it: the pipe again
			move(QPointF(100, m_next_y + 150));
			QVERIFY(m_tool->signalPreview().isEmpty());
			move(nearBelow(signal));
			click(nearBelow(signal));

			QCOMPARE(signal->conductors().size(), 1);
			Conductor *line = signal->conductors().first();
			QVERIFY(nozzle->isLinkedTo(signal));
			QVERIFY(isProcessConnection(line));
				// a signal line, whichever end it was drawn from
			QCOMPARE(line->terminal1, signal);
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
			QVERIFY(!m_tool->startNozzle());
		}

			// Rule 1: from the bubble to a free end, which the blind flange
			// of the project closes; the line leaves from the flange
		void aFreeEndIsClosedByTheBlindFlange()
		{
			const Line line = lineWithAFreeEnd();
			QVERIFY(line.pipe);
			const QPointF dock = line.nozzle()->dockConductor();
			Terminal *start = line.symbol->terminals().at(1);
			const int steps = m_folio->undoStack().index();
			const int elements = m_folio->elements().size();

			Element *flange = closeFromTheBubble(line);
			QVERIFY(flange);
				// the free end gone, the flange in its place, turned the same way
			QVERIFY(!line.free_end->scene());
			QCOMPARE(m_folio->elements().size(), elements);
			Terminal *n1 = terminalOf(flange, TerminalData::Pipe);
			Terminal *s1 = terminalOf(flange, TerminalData::Signal);
			QCOMPARE(n1->orientation(), Qet::West);
				// the pipe goes on to it, its data kept
			QCOMPARE(start->conductors().size(), 1);
			Conductor *pipe = start->conductors().first();
			QVERIFY(start->isLinkedTo(n1));
			QCOMPARE(pipe->properties().m_pipe.line_number, QStringLiteral("W01"));
				// the process connection from its signal terminal
			QVERIFY(s1->isLinkedTo(line.signal()));
			QVERIFY(isProcessConnection(s1->conductors().first()));
			QVERIFY(QLineF(s1->dockConductor(), dock).length() < 0.01);

				// one step
			QCOMPARE(m_folio->undoStack().index(), steps + 1);
			m_folio->undoStack().undo();
			QVERIFY(line.free_end->scene());
			QVERIFY(!flange->scene());
			QVERIFY(start->isLinkedTo(line.nozzle()));
			QCOMPARE(line.signal()->conductors().size(), 0);
			m_folio->undoStack().redo();
			QVERIFY(!line.free_end->scene());
			QVERIFY(flange->scene());
			QVERIFY(start->isLinkedTo(n1));
			QVERIFY(s1->isLinkedTo(line.signal()));
		}

			// Rule 1 the other way: from the free end to the bubble; the free
			// end, hidden while a pipe goes on from it, is shown again while
			// the bubble catches the line
		void fromTheFreeEndToTheBubble()
		{
			const Line line = lineWithAFreeEnd();
			QVERIFY(line.pipe);
			const QPointF dock = line.nozzle()->dockConductor();

			click(nearBelow(line.nozzle()));
			QVERIFY(m_tool->startsOnFreeEnd());
			QCOMPARE(line.free_end->opacity(), 0.0);
			move(nearBelow(line.signal()));
			QVERIFY(!m_tool->signalPreview().isEmpty());
			QCOMPARE(line.free_end->opacity(), 1.0);
				// it leaves out of the flange, away from the pipe
			const QList<QPointF> route = m_tool->signalPreviewRoute();
			QVERIFY(route.size() >= 2);
			QVERIFY(QLineF(route.first(), dock).length() < 0.01);
			QVERIFY(route.at(1).x() >= dock.x());
			click(nearBelow(line.signal()));

			Element *flange = toolFlangeAt(dock);
			QVERIFY(flange);
			QVERIFY(!line.free_end->scene());
			QVERIFY(terminalOf(flange, TerminalData::Signal)->isLinkedTo(line.signal()));
		}

			// No blind flange set: nothing done, the status bar says where to
			// choose one
		void withoutABlindFlangeNothingIsDrawn()
		{
			setBlindFlange(QString());
			const Line line = lineWithAFreeEnd();
			QVERIFY(line.pipe);
			const int steps = m_folio->undoStack().index();

			QVERIFY(!closeFromTheBubble(line));
			QVERIFY(line.free_end->scene());
			QCOMPARE(line.signal()->conductors().size(), 0);
			QCOMPARE(m_folio->undoStack().index(), steps);
			QString message;
			QVERIFY(!PidProcessConnection::draw(line.signal(), line.nozzle(), &message));
			QVERIFY(message.contains(QStringLiteral("P&ID")));
		}

			// The blind flange placed by the tool goes with the pipe it closes,
			// its process connection with it; the process connection deleted
			// alone, it stays
		void theToolFlangeGoesWithItsPipe()
		{
			const Line line = lineWithAFreeEnd();
			QVERIFY(line.pipe);
			Element *flange = closeFromTheBubble(line);
			QVERIFY(flange);
			Terminal *s1 = terminalOf(flange, TerminalData::Signal);
			Conductor *measure = s1->conductors().first();
			Conductor *pipe = terminalOf(flange, TerminalData::Pipe)->conductors().first();

			removeAsTheUser({measure});
			QVERIFY(flange->scene());
			m_folio->undoStack().undo();
			QVERIFY(measure->scene());

			removeAsTheUser({pipe});
			QVERIFY(!flange->scene());
			QVERIFY(!measure->scene());
			QCOMPARE(line.signal()->conductors().size(), 0);
			m_folio->undoStack().undo();
			QVERIFY(flange->scene());
			QVERIFY(measure->scene());
		}

			// A blind flange placed by hand stays when its pipe goes
		void aFlangePlacedByHandStays()
		{
			m_next_y += 200;
			Element *symbol = place(m_pipe, QPointF(0, m_next_y));
			Element *flange = place(m_flange, QPointF(110, m_next_y));
			QVERIFY(symbol && flange);
			Conductor *pipe = symbol->terminals().at(1)->connectTo(terminalOf(flange, TerminalData::Pipe));
			QVERIFY(pipe);
			QVERIFY(!flange->isToolPlaced());
			removeAsTheUser({pipe});
			QVERIFY(flange->scene());
		}

			// The mark of the flange placed by the tool, saved only when set
		void theMarkIsSavedOnlyWhenSet()
		{
			m_next_y += 200;
			Element *flange = place(m_flange, QPointF(0, m_next_y));
			QVERIFY(flange);
			QDomDocument doc;
			QHash<Terminal *, int> ids;
			QVERIFY(!flange->toXml(doc, ids).hasAttribute(QStringLiteral("pid_tool_placed")));
			flange->setToolPlaced(true);
			ids.clear();
			QDomElement xml = flange->toXml(doc, ids);
			QCOMPARE(xml.attribute(QStringLiteral("pid_tool_placed")), QStringLiteral("true"));

			int state = 0;
			Element *read = ElementFactory::Instance()->createElement(m_flange, nullptr, &state);
			QVERIFY(read && !state);
			QHash<int, Terminal *> terminals;
			QVERIFY(read->fromXml(xml, terminals));
			QVERIFY(read->isToolPlaced());
			delete read;
		}
};

QET_APP_TEST_MAIN(tst_processconnection)
#include "tst_processconnection.moc"
