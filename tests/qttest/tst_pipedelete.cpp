// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "autoconnect.h"
#include "diagram.h"
#include "diagramcontent.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "undocommand/deleteqgraphicsitemcommand.h"

// A symbol a line runs through (a valve, the run of a tee) taken away
// leaves one pipe where it stood, with the data of the pipe on its N1 side:
// the reverse of a symbol dropped on a pipe. The branch of a tee goes with
// it; a side touching another symbol gives the pipe left that symbol's
// nozzle; both sides touching, a pipe picked with the symbol, a side with
// nothing, a symbol with no run: nothing is joined. Symbols removed one
// after the other leave one pipe. One step to undo. Wires are as before.
class tst_pipedelete : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40: N1 (west) and N2 (east) at 10 from its middle,
		// N3 (south) when @p tee; @p medium on each, @p extra on <definition>
	static QDomElement definition(QDomDocument &doc, const QString &uuid,
				      const QString &medium, const QString &extra, bool tee = false)
	{
		const QString attr = medium.isEmpty()
				? QString()
				: QStringLiteral(" medium=\"%1\"").arg(medium);
		const QString branch = tee
				? QStringLiteral("<terminal name=\"N3\" x=\"0\" y=\"10\" orientation=\"s\" "
						 "type=\"Generic\"%1/>").arg(attr)
				: QString();
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" %3"
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%2/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%2/>"
			"%4</description></definition>").arg(uuid, attr, extra, branch));
		return doc.documentElement();
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_valve, m_tee, m_equipment, m_electrical;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/pdelete"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/pdelete/") + file, &m_project);
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

		// A row of its own, @p count symbols 200 apart (or @p step)
	QList<Element *> row(const QList<ElementsLocation> &symbols, qreal step = 200)
	{
		m_next_y += 200;
		QList<Element *> placed;
		qreal x = 0;
		for (const ElementsLocation &symbol : symbols) {
			placed << place(symbol, QPointF(x, m_next_y));
			x += step;
		}
		return placed;
	}

	static Terminal *nozzle(Element *element, const char *name)
	{
		for (Terminal *t : element->terminals())
			if (t->name() == QLatin1String(name)) return t;
		return nullptr;
	}

	Conductor *draw(Terminal *from, Terminal *to, const QString &fluid = QString(), int dn = 0)
	{
		auto *pipe = new Conductor(from, to);
		m_folio->addItem(pipe);
		if (!fluid.isEmpty() || dn) {
			ConductorProperties data = pipe->properties();
			data.m_pipe.fluid = fluid;
			if (dn) data.m_pipe.nominal = PipeDiameter::nominal(dn);
			data.m_pipe.line_number = fluid + QStringLiteral("-L");
			pipe->setProperties(data);
		}
		return pipe;
	}

		// Deleted as the editor does: the selection read, then cleared
	void remove(const QList<QGraphicsItem *> &items)
	{
		m_folio->clearSelection();
		for (QGraphicsItem *item : items) item->setSelected(true);
		DiagramContent content(m_folio);
		m_folio->clearSelection();
		m_folio->undoStack().push(new DeleteQGraphicsItemCommand(m_folio, content));
	}

		// The only pipe on @p t, or nullptr
	static Conductor *pipeOn(Terminal *t)
	{
		return t->conductors().size() == 1 ? t->conductors().first() : nullptr;
	}

	static bool joins(const Conductor *c, const Terminal *a, const Terminal *b)
	{
		return c && ((c->terminal1 == a && c->terminal2 == b)
			     || (c->terminal1 == b && c->terminal2 == a));
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("pdelete"), NamesList());
			QDomDocument a, b, c, d;
			m_valve = add("valve.elmt", definition(a, "81111111-1111-1111-1111-111111111111",
							       "pipe", "pid_inline=\"true\" "));
			m_tee = add("tee.elmt", definition(b, "82222222-2222-2222-2222-222222222222",
							   "pipe", "pid_run=\"N1 N2 N3\" ", true));
			m_equipment = add("equipment.elmt", definition(c, "83333333-3333-3333-3333-333333333333",
								       "pipe", QString()));
			m_electrical = add("electrical.elmt", definition(d, "84444444-4444-4444-4444-444444444444",
									 QString(), QString()));
			QVERIFY(m_valve.exist() && m_tee.exist() && m_equipment.exist() && m_electrical.exist());
			m_folio = m_project.addNewDiagram();
		}

			// a ── P1 ── valve ── P2 ── b: one pipe a ── b, with the data
			// of P1 (the N1 side), even where P2 differs (a reducer: the
			// far end shows the difference, as anywhere); undo, redo
		void aValveTakenAwayLeavesOnePipe()
		{
			const auto e = row({m_equipment, m_valve, m_equipment});
			Terminal *a = nozzle(e[0], "N2"), *b = nozzle(e[2], "N1");
			Conductor *p1 = draw(a, nozzle(e[1], "N1"), QStringLiteral("W01"), 100);
			Conductor *p2 = draw(nozzle(e[1], "N2"), b, QStringLiteral("W02"), 80);
			const PipeProperties data = p1->properties().m_pipe;

			remove({e[1]});
			Conductor *joined = pipeOn(a);
			QVERIFY(joins(joined, a, b));
			QCOMPARE(pipeOn(b), joined);
			QCOMPARE(joined->medium(), TerminalData::Pipe);
			QVERIFY(joined->properties().m_pipe == data);
			QVERIFY(joined->scene() == m_folio);
			QVERIFY(!p1->scene() && !p2->scene());
			QCOMPARE(m_folio->undoStack().count() > 0, true);

			m_folio->undoStack().undo();
			QCOMPARE(pipeOn(a), p1);
			QCOMPARE(pipeOn(b), p2);
			QVERIFY(!joined->scene());
			QVERIFY(p2->properties().m_pipe.fluid == QStringLiteral("W02"));

			m_folio->undoStack().redo();
			QVERIFY(joins(pipeOn(a), a, b));
			QCOMPARE(pipeOn(a)->uuid(), joined->uuid());
		}

			// P1 drawn from the valve out to a: the pipe left runs from b to a
		void theWayOfThePipeIsKept()
		{
			const auto e = row({m_equipment, m_valve, m_equipment});
			Terminal *a = nozzle(e[0], "N2"), *b = nozzle(e[2], "N1");
			draw(nozzle(e[1], "N1"), a, QStringLiteral("W01"), 100);
			draw(nozzle(e[1], "N2"), b);
			remove({e[1]});
			Conductor *joined = pipeOn(a);
			QVERIFY(joined);
			QCOMPARE(joined->terminal1, b);
			QCOMPARE(joined->terminal2, a);
		}

			// The run of a tee is joined, its branch goes with it
		void aTeeJoinsItsRunAndDropsItsBranch()
		{
			const auto e = row({m_equipment, m_tee, m_equipment});
			Element *c = place(m_equipment, QPointF(200, m_next_y + 150));
			QVERIFY(c);
			c->setRotation(-90);
			Terminal *a = nozzle(e[0], "N2"), *b = nozzle(e[2], "N1");
			draw(a, nozzle(e[1], "N1"), QStringLiteral("W01"), 100);
			draw(nozzle(e[1], "N2"), b, QStringLiteral("W01"), 100);
			Terminal *branch_end = nozzle(c, "N2");
			draw(nozzle(e[1], "N3"), branch_end, QStringLiteral("W01"), 50);
			remove({e[1]});
			QVERIFY(joins(pipeOn(a), a, b));
			QVERIFY(branch_end->conductors().isEmpty());
		}

			// The valve against a, with no pipe: the pipe on the other side
			// goes to a's nozzle, with its own data
		void aSideTouchingAnotherSymbolTakesItsNozzle()
		{
			m_next_y += 200;
			Element *a = place(m_equipment, QPointF(0, m_next_y));
			Element *valve = place(m_valve, QPointF(20, m_next_y));
			Element *b = place(m_equipment, QPointF(300, m_next_y));
			QVERIFY(a && valve && b);
			Terminal *touched = nozzle(a, "N2");
			QCOMPARE(touched->dockConductor(), nozzle(valve, "N1")->dockConductor());
			QVERIFY(touched->conductors().isEmpty());
			Conductor *p2 = draw(nozzle(valve, "N2"), nozzle(b, "N1"), QStringLiteral("W03"), 65);
			const PipeProperties data = p2->properties().m_pipe;
			remove({valve});
			Conductor *joined = pipeOn(touched);
			QVERIFY(joins(joined, touched, nozzle(b, "N1")));
			QVERIFY(joined->properties().m_pipe == data);
		}

			// Against a symbol on both sides: no line data to take, nothing
		void touchingOnBothSidesJoinsNothing()
		{
			m_next_y += 200;
			Element *a = place(m_equipment, QPointF(0, m_next_y));
			Element *valve = place(m_valve, QPointF(20, m_next_y));
			Element *b = place(m_equipment, QPointF(40, m_next_y));
			QVERIFY(a && valve && b);
			QCOMPARE(nozzle(b, "N1")->dockConductor(), nozzle(valve, "N2")->dockConductor());
			remove({valve});
			QVERIFY(nozzle(a, "N2")->conductors().isEmpty());
			QVERIFY(nozzle(b, "N1")->conductors().isEmpty());
		}

			// A pipe on one side only goes, as before
		void aPipeOnOneSideOnlyGoes()
		{
			const auto e = row({m_equipment, m_valve});
			Terminal *a = nozzle(e[0], "N2");
			draw(a, nozzle(e[1], "N1"), QStringLiteral("W01"), 100);
			remove({e[1]});
			QVERIFY(a->conductors().isEmpty());
		}

			// a ── v1 ── v2 ── b, both valves (and the pipe between them)
			// removed together: one pipe a ── b, data of the N1 side of v1
		void valvesInARowLeaveOnePipe()
		{
			const auto e = row({m_equipment, m_valve, m_valve, m_equipment});
			Terminal *a = nozzle(e[0], "N2"), *b = nozzle(e[3], "N1");
			Conductor *p1 = draw(a, nozzle(e[1], "N1"), QStringLiteral("W01"), 100);
			Conductor *middle = draw(nozzle(e[1], "N2"), nozzle(e[2], "N1"), QStringLiteral("W09"), 100);
			draw(nozzle(e[2], "N2"), b, QStringLiteral("W02"), 100);
			const PipeProperties data = p1->properties().m_pipe;
			remove({e[1], e[2], middle});
			Conductor *joined = pipeOn(a);
			QVERIFY(joins(joined, a, b));
			QVERIFY(joined->properties().m_pipe == data);
			QCOMPARE(m_folio->undoStack().count() > 0, true);
			m_folio->undoStack().undo();
			QCOMPARE(pipeOn(a), p1);
		}

			// A pipe picked with the valve is taken away as asked
		void aPipePickedWithTheSymbolIsNotJoined()
		{
			const auto e = row({m_equipment, m_valve, m_equipment});
			Terminal *a = nozzle(e[0], "N2"), *b = nozzle(e[2], "N1");
			Conductor *p1 = draw(a, nozzle(e[1], "N1"), QStringLiteral("W01"), 100);
			draw(nozzle(e[1], "N2"), b, QStringLiteral("W01"), 100);
			remove({e[1], p1});
			QVERIFY(a->conductors().isEmpty());
			QVERIFY(b->conductors().isEmpty());
		}

			// A symbol with no run (equipment) takes its pipes, as before
		void aSymbolWithoutARunIsNotJoined()
		{
			const auto e = row({m_equipment, m_equipment, m_equipment});
			Terminal *a = nozzle(e[0], "N2"), *b = nozzle(e[2], "N1");
			draw(a, nozzle(e[1], "N1"), QStringLiteral("W01"), 100);
			draw(nozzle(e[1], "N2"), b, QStringLiteral("W01"), 100);
			remove({e[1]});
			QVERIFY(a->conductors().isEmpty());
			QVERIFY(b->conductors().isEmpty());
		}

			// Wires: as before, a symbol between two wires takes them
		void wiresAreAsBefore()
		{
			const auto e = row({m_electrical, m_electrical, m_electrical});
			Terminal *a = nozzle(e[0], "N2"), *b = nozzle(e[2], "N1");
			draw(a, nozzle(e[1], "N1"));
			draw(nozzle(e[1], "N2"), b);
			remove({e[1]});
			QVERIFY(a->conductors().isEmpty());
			QVERIFY(b->conductors().isEmpty());
		}

			// With live automatic connections, deleting as the editor does
			// (one step with AutoConnect::update()): one pipe, not two
		void liveAutoConnectAddsNoSecondPipe()
		{
			const bool was = m_project.liveAutoConnect();
			m_project.setLiveAutoConnect(true);
			const auto e = row({m_equipment, m_valve, m_equipment});
			Terminal *a = nozzle(e[0], "N2"), *b = nozzle(e[2], "N1");
			draw(a, nozzle(e[1], "N1"), QStringLiteral("W01"), 100);
			draw(nozzle(e[1], "N2"), b, QStringLiteral("W01"), 100);

			m_folio->clearSelection();
			e[1]->setSelected(true);
			DiagramContent content(m_folio);
			m_folio->clearSelection();
			auto *undo = new QUndoCommand(QStringLiteral("Delete"));
			new DeleteQGraphicsItemCommand(m_folio, content, undo);
			AutoConnect::update(m_folio, {}, {}, undo, {e[1]});
			m_folio->undoStack().push(undo);
			m_project.setLiveAutoConnect(was);

			QCOMPARE(a->conductors().size(), 1);
			QCOMPARE(b->conductors().size(), 1);
			QVERIFY(joins(pipeOn(a), a, b));
			QVERIFY(pipeOn(a)->properties().m_pipe.fluid == QStringLiteral("W01"));
		}
};

QET_APP_TEST_MAIN(tst_pipedelete)
#include "tst_pipedelete.moc"
