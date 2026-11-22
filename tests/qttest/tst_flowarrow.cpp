// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "autobreakconductor.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "properties/elementdata.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QUndoCommand>

// Step 8a: the flow direction arrow (pid_role="flow_arrow"), a drawing on
// the line, as the PipeFlowArrow of DEXPI. A mark on the line, no piece,
// as a spec break or an insulation limit: the pipes are drawn into its
// middle, it draws no connection; unlike them, nothing stops there: size,
// specification, fluid, line number, flow and insulation go through it.
class tst_flowarrow : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40: N1 (west) and N2 (east) at 10 from its middle,
		// nozzles of pipes; @p extra on <definition>
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &extra)
	{
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" %2"
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\" medium=\"pipe\"/>"
			"</description></definition>").arg(uuid, extra));
		return doc.documentElement();
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_arrow, m_valve, m_equipment, m_spec_break, m_limit;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/flowarrow"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/flowarrow/") + file, &m_project);
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

		// A row of its own, the symbols @p gap apart
	QList<Element *> row(const QList<ElementsLocation> &symbols, qreal gap = 200)
	{
		m_next_y += 200;
		QList<Element *> placed;
		qreal x = 0;
		for (const ElementsLocation &symbol : symbols) {
			placed << place(symbol, QPointF(x, m_next_y));
			x += gap;
		}
		return placed;
	}

	static Terminal *nozzle(Element *element, const char *name)
	{
		for (Terminal *t : element->terminals())
			if (t->name() == QLatin1String(name)) return t;
		return nullptr;
	}

	Conductor *draw(Terminal *from, Terminal *to)
	{
		auto *pipe = new Conductor(from, to);
		m_folio->addItem(pipe);
		return pipe;
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("flowarrow"), NamesList());
			QDomDocument a, b, c, d, e;
			m_arrow = add("arrow.elmt", definition(a, "81111111-1111-1111-1111-111111111111",
												   "pid_inline=\"true\" pid_family=\"pipe\" "
												   "pid_role=\"flow_arrow\" "));
			m_valve = add("valve.elmt", definition(b, "82222222-2222-2222-2222-222222222222",
												   "pid_inline=\"true\" pid_family=\"valve\" "));
			m_equipment = add("equipment.elmt", definition(c, "83333333-3333-3333-3333-333333333333",
														   "pid_family=\"equipment\" "));
			m_spec_break = add("break.elmt", definition(d, "84444444-4444-4444-4444-444444444444",
														"pid_inline=\"true\" pid_family=\"pipe\" "
														"pid_role=\"spec_break\" "));
			m_limit = add("limit.elmt", definition(e, "85555555-5555-5555-5555-555555555555",
												   "pid_inline=\"true\" pid_family=\"pipe\" "
												   "pid_role=\"insulation_limit\" "));
			QVERIFY(m_arrow.exist() && m_valve.exist() && m_equipment.exist()
					&& m_spec_break.exist() && m_limit.exist());
			m_folio = m_project.addNewDiagram();
		}

			// The roles of a mark on the line, the arrow one of them; the
			// arrow neither a spec break nor an insulation limit
		void theArrowIsAMarkOnTheLine()
		{
			QVERIFY(ElementData::isLineMarkRole(QStringLiteral("flow_arrow")));
			QVERIFY(ElementData::isLineMarkRole(QStringLiteral("spec_break")));
			QVERIFY(ElementData::isLineMarkRole(QStringLiteral("insulation_limit")));
			QVERIFY(!ElementData::isLineMarkRole(QStringLiteral("reducer")));
			QVERIFY(!ElementData::isLineMarkRole(QStringLiteral("junction")));
			QVERIFY(!ElementData::isLineMarkRole(QString()));

			const auto e = row({m_arrow, m_valve});
			QVERIFY(e[0]->isFlowArrow() && e[0]->isLineMark() && e[0]->isPipeInline());
			QVERIFY(!e[0]->isSpecBreak() && !e[0]->isInsulationLimit());
			QVERIFY(!e[1]->isFlowArrow() && !e[1]->isLineMark());
		}

			// Drawn as a spec break: no connection drawn at its nozzles,
			// each pipe going on into its middle
		void thePipesAreDrawnIntoIt()
		{
			const auto e = row({m_equipment, m_arrow, m_equipment});
			Conductor *p1 = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"));
			Conductor *p2 = draw(nozzle(e[1], "N2"), nozzle(e[2], "N1"));
			QVERIFY(!nozzle(e[1], "N1")->drawsConnectionMark());
			QVERIFY(!nozzle(e[1], "N2")->drawsConnectionMark());
			const QLineF into = p1->lineIntoSpecBreak(nozzle(e[1], "N1"));
			QVERIFY(!into.isNull());
				// up to the middle of the arrow, between its nozzles
			QCOMPARE(p1->mapToScene(into.p2()), e[1]->pos());
			QVERIFY(!p2->lineIntoSpecBreak(nozzle(e[1], "N2")).isNull());
				// not at the equipment, a piece
			QVERIFY(p1->lineIntoSpecBreak(nozzle(e[0], "N2")).isNull());
		}

			// Unlike a spec break or an insulation limit, nothing stops at it:
			// size, specification, fluid, line number and flow go through, the
			// insulation draws no stub there
		void whatALineCarriesGoesThroughIt()
		{
			const auto e = row({m_equipment, m_arrow, m_equipment});
			Conductor *p1 = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"));
			Conductor *p2 = draw(nozzle(e[1], "N2"), nozzle(e[2], "N1"));
			for (Conductor::LineReach reach : {Conductor::LineReach::Size, Conductor::LineReach::Spec,
											   Conductor::LineReach::Fluid, Conductor::LineReach::LineNumber,
											   Conductor::LineReach::Flow}) {
				QVERIFY(p1->inlineRun(reach).contains(p2));
			}
			for (Conductor *pipe : {p1, p2}) {
				ConductorProperties data = pipe->properties();
				data.m_pipe.insulation_kind = PipeProperties::Insulation;
				pipe->setProperties(data);
			}
			QVERIFY(!p1->insulationEndsAt(nozzle(e[1], "N1")));
			QVERIFY(!p2->insulationEndsAt(nozzle(e[1], "N2")));
			QVERIFY(p1->insulationEndsAt(nozzle(e[0], "N2")));

				// the same row with a spec break: the specification stops
			const auto s = row({m_equipment, m_spec_break, m_equipment});
			Conductor *q1 = draw(nozzle(s[0], "N2"), nozzle(s[1], "N1"));
			Conductor *q2 = draw(nozzle(s[1], "N2"), nozzle(s[2], "N1"));
			QVERIFY(!q1->inlineRun(Conductor::LineReach::Spec).contains(q2));
			QVERIFY(q1->inlineRun(Conductor::LineReach::Size).contains(q2));
		}

			// Dropped on a pipe which shows its designation (step 7b): the two
			// halves keep its data, one only shows the designation
		void aPipeCutByTheArrowShowsOneDesignation()
		{
			const auto e = row({m_equipment, m_equipment}, 400);
			Conductor *whole = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"));
			ConductorProperties data = whole->properties();
			data.m_show_text = true;
			data.m_pipe.line_number = QStringLiteral("47121");
			data.m_pipe.material = QStringLiteral("S1");
			whole->setProperties(data);
			Element *arrow = place(m_arrow, QPointF(200, m_next_y));
			QVERIFY(arrow);

			const bool was = m_project.autoBreakConductor();
			m_project.setAutoBreakConductor(true);
			QList<Conductor *> handled;
			QSet<Terminal *> used;
			auto *undo = new QUndoCommand();
			autoBreakConductors(m_folio, arrow, undo, handled, used);
			m_folio->undoStack().push(undo);
			m_project.setAutoBreakConductor(was);

			QCOMPARE(nozzle(arrow, "N1")->conductors().size(), 1);
			QCOMPARE(nozzle(arrow, "N2")->conductors().size(), 1);
			Conductor *left = nozzle(arrow, "N1")->conductors().first();
			Conductor *right = nozzle(arrow, "N2")->conductors().first();
			QVERIFY(left != right);
			int shown = 0;
			for (Conductor *half : {left, right}) {
				QCOMPARE(half->properties().m_pipe.line_number, QStringLiteral("47121"));
				QCOMPARE(half->properties().m_pipe.material, QStringLiteral("S1"));
				if (half->properties().m_show_text) ++shown;
			}
			QCOMPARE(shown, 1);
			QVERIFY(left->inlineRun(Conductor::LineReach::LineNumber).contains(right));
		}

			// Change type: an arrow never becomes a spec break, where the
			// specification of the line would stop; another mark which stops
			// nothing of the specification, it may
		void changeTypeKeepsWhatStops()
		{
			Element *arrow = row({m_arrow}).first();
			QVERIFY(!arrow->canChangeLocationTo(m_spec_break));
			QVERIFY(arrow->canChangeLocationTo(m_limit));
		}
};

QET_APP_TEST_MAIN(tst_flowarrow)
#include "tst_flowarrow.moc"
