/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "diagram.h"
#include "editor/elementscene.h"
#include "factory/elementfactory.h"
#include "pidflow.h"
#include "properties/terminaldata.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "ui/pipepropertieswidget.h"

#include <QDomDocument>
#include <QLabel>

/**
	The direction of flow of the pipes (step 8a-bis), found from the
	symbols, never written: the role of a nozzle (pid_flow_role="in" / "out")
	in its symbol; the direction going through the symbols a line runs
	through, a reducer, a tee or a cross by its rule; a disagreement said;
	the page of a pipe and its tool tip.
*/
class tst_pipeflow : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40: N1 west, N2 east, 20 from its origin, with the
		// attributes @p attributes, N1 and N2 those of @p n1 and @p n2, and
		// @p more terminals
	static QString symbol(const QString &uuid, const QString &attributes,
						  const QString &n1 = QString(), const QString &n2 = QString(),
						  const QString &more = QString())
	{
		return QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" %2 "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/>"
			"<names><name lang=\"en\">Piece</name></names><description>"
			"<terminal uuid=\"{7f000000-0000-0000-0000-000000000001}\" name=\"N1\" x=\"-20\" y=\"0\" "
			"orientation=\"w\" type=\"Generic\" medium=\"pipe\" %3/>"
			"<terminal uuid=\"{7f000000-0000-0000-0000-000000000002}\" name=\"N2\" x=\"20\" y=\"0\" "
			"orientation=\"e\" type=\"Generic\" medium=\"pipe\" %4/>%5"
			"</description></definition>").arg(uuid, attributes, n1, n2, more);
	}

	static QString south()
	{
		return QStringLiteral(
			"<terminal uuid=\"{7f000000-0000-0000-0000-000000000003}\" name=\"N3\" x=\"0\" y=\"20\" "
			"orientation=\"s\" type=\"Generic\" medium=\"pipe\"/>");
	}

	static QString in()  { return QStringLiteral("pid_flow_role=\"in\""); }
	static QString out() { return QStringLiteral("pid_flow_role=\"out\""); }

	static QList<QPair<QString, QString>> symbols()
	{
		return {
				//A valve: no direction of its own
			{QStringLiteral("valve.elmt"), symbol(QStringLiteral("7f111111-1111-1111-1111-111111111111"),
					QStringLiteral("pid_family=\"valve\" pid_inline=\"true\""))},
			{QStringLiteral("check.elmt"), symbol(QStringLiteral("7f111112-1111-1111-1111-111111111111"),
					QStringLiteral("pid_family=\"valve\" pid_inline=\"true\""), in(), out())},
			{QStringLiteral("arrow.elmt"), symbol(QStringLiteral("7f111113-1111-1111-1111-111111111111"),
					QStringLiteral("pid_family=\"pipe\" pid_inline=\"true\" pid_role=\"flow_arrow\""),
					in(), out())},
				//Equipment: the suction and the discharge of a pump
			{QStringLiteral("pump.elmt"), symbol(QStringLiteral("7f111114-1111-1111-1111-111111111111"),
					QStringLiteral("pid_family=\"equipment\""), in(), out())},
			{QStringLiteral("tank.elmt"), symbol(QStringLiteral("7f111115-1111-1111-1111-111111111111"),
					QStringLiteral("pid_family=\"equipment\""), QString(), QString(), south())},
			{QStringLiteral("tee.elmt"), symbol(QStringLiteral("7f111116-1111-1111-1111-111111111111"),
					QStringLiteral("pid_family=\"pipe\" pid_role=\"junction\" pid_run=\"N1 N2 N3\""),
					QString(), QString(), south())},
				//A reducer: N1 the large side
			{QStringLiteral("reducer.elmt"), symbol(QStringLiteral("7f111117-1111-1111-1111-111111111111"),
					QStringLiteral("pid_family=\"pipe\" pid_role=\"reducer\" pid_large=\"N1\""))},
		};
	}

	static Element *place(QETProject &project, Diagram *folio, const QPointF &pos, const QString &file)
	{
		const QString dir = QStringLiteral("import/flow");
		if (!project.embeddedElementCollection()->exist(dir)) {
			project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("flow"), NamesList());
			for (const auto &entry : symbols()) {
				QDomDocument doc;
				doc.setContent(entry.second);
				project.embeddedElementCollection()->addElementDefinition(dir, entry.first,
																		  doc.documentElement());
			}
		}
		const ElementsLocation location(QStringLiteral("embed://import/flow/") + file, &project);
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		folio->addItem(element);
		element->setPos(pos);
		return element;
	}

	static Terminal *nozzle(const Element *element, const char *name)
	{
		for (Terminal *t : element->terminals()) {
			if (t->baseName() == QLatin1String(name)) return t;
		}
		return nullptr;
	}

		// A folio, and pipes drawn between the symbols placed on it
	struct Scene {
		QETProject project;
		Diagram *folio = nullptr;
		int x = 100;

		Scene()
		{
			folio = project.addNewDiagram();
		}
			// The next symbol along a row, 200 to the right of the last one
		Element *add(const char *file, qreal y = 200)
		{
			Element *element = place(project, folio, QPointF(x, y), QLatin1String(file));
			x += 200;
			return element;
		}
			// A tank 200 under @p element
		Element *place_below(const Element *element)
		{
			return place(project, folio, element->pos() + QPointF(0, 200), QStringLiteral("tank.elmt"));
		}
		Conductor *pipe(Element *a, const char *na, Element *b, const char *nb)
		{
			Terminal *from = nozzle(a, na);
			Terminal *to = nozzle(b, nb);
			if (!from || !to) return nullptr;
			auto *conductor = new Conductor(from, to);
			folio->addItem(conductor);
			return conductor;
		}
	};

	private slots:
			// ---- the logic alone ----
		void aLinkGoesTheOtherWay()
		{
			PidFlow::Solver solver;
			const int a = solver.addNode(), b = solver.addNode(), c = solver.addNode(),
					  d = solver.addNode();
			solver.link(a, b);
			solver.link(b, c);
			solver.fix(a, false, 7);
			solver.solve();
			QCOMPARE(solver.state(a), PidFlow::Solver::State::Out);
			QCOMPARE(solver.state(b), PidFlow::Solver::State::In);
			QCOMPARE(solver.state(c), PidFlow::Solver::State::Out);
			QCOMPARE(solver.state(d), PidFlow::Solver::State::Unknown);
			QCOMPARE(solver.sources(c), QList<int>{7});
		}

		void twoSourcesWhichDoNotAgreeAreADisagreement()
		{
			PidFlow::Solver solver;
			const int a = solver.addNode(), b = solver.addNode(), c = solver.addNode();
			solver.link(a, b);
			solver.link(b, c);
			solver.fix(a, false, 1);
			solver.fix(c, true, 2);
			solver.solve();
			QCOMPARE(solver.state(b), PidFlow::Solver::State::Conflict);
			QCOMPARE(solver.sources(b), (QList<int>{1, 2}));

				// two which agree are no disagreement
			PidFlow::Solver agreed;
			const int e = agreed.addNode(), f = agreed.addNode();
			agreed.link(e, f);
			agreed.fix(e, false, 1);
			agreed.fix(f, true, 2);
			agreed.solve();
			QCOMPARE(agreed.state(f), PidFlow::Solver::State::In);
		}

		void theLastNozzleOfATeeGoesTheOtherWay()
		{
				// all the others come in: the last one goes out
			PidFlow::Solver solver;
			const int a = solver.addNode(), b = solver.addNode(), c = solver.addNode();
			solver.addJunction({a, b, c}, 9);
			solver.fix(a, true, 1);
			solver.fix(b, true, 2);
			solver.solve();
			QCOMPARE(solver.state(c), PidFlow::Solver::State::Out);
			QCOMPARE(solver.sources(c), QList<int>{9});
			QVERIFY(!solver.junctionInConflict(0));

				// all go out: it comes in
			PidFlow::Solver outs;
			const int d = outs.addNode(), e = outs.addNode(), f = outs.addNode();
			outs.addJunction({d, e, f}, 9);
			outs.fix(d, false, 1);
			outs.fix(f, false, 2);
			outs.solve();
			QCOMPARE(outs.state(e), PidFlow::Solver::State::In);

				// one in, one out: nothing found
			PidFlow::Solver mixed;
			const int g = mixed.addNode(), h = mixed.addNode(), i = mixed.addNode();
			mixed.addJunction({g, h, i}, 9);
			mixed.fix(g, true, 1);
			mixed.fix(h, false, 2);
			mixed.solve();
			QCOMPARE(mixed.state(i), PidFlow::Solver::State::Unknown);

				// two not known: nothing found
			PidFlow::Solver two;
			const int j = two.addNode(), k = two.addNode(), l = two.addNode();
			two.addJunction({j, k, l}, 9);
			two.fix(j, true, 1);
			two.solve();
			QCOMPARE(two.state(k), PidFlow::Solver::State::Unknown);
			QCOMPARE(two.state(l), PidFlow::Solver::State::Unknown);
		}

		void whatATeeGivesGoesOnToTheNextOne()
		{
			PidFlow::Solver solver;
			const int a = solver.addNode(), b = solver.addNode(), c = solver.addNode();
			const int d = solver.addNode(), e = solver.addNode(), f = solver.addNode();
				// the second tee is given last: found all the same
			solver.addJunction({d, e, f}, 20);
			solver.addJunction({a, b, c}, 10);
			solver.link(c, d);
			solver.fix(a, true, 1);
			solver.fix(b, true, 2);
			solver.fix(e, true, 3);
			solver.solve();
			QCOMPARE(solver.state(c), PidFlow::Solver::State::Out);
			QCOMPARE(solver.state(d), PidFlow::Solver::State::In);
			QCOMPARE(solver.state(f), PidFlow::Solver::State::Out);
		}

		void aTeeIntoWhichEverythingComesIsADisagreement()
		{
			PidFlow::Solver solver;
			const int a = solver.addNode(), b = solver.addNode(), c = solver.addNode();
			solver.addJunction({a, b, c}, 9);
			solver.fix(a, true, 1);
			solver.fix(b, true, 2);
			solver.fix(c, true, 3);
			solver.solve();
			QVERIFY(solver.junctionInConflict(0));
			QVERIFY(!solver.junctionInConflict(1));
		}

			// ---- the role of a nozzle ----
		void theSymbolGivesTheRoleOfItsNozzles()
		{
			const QString check = symbols().at(1).second;
			QDomDocument doc;
			doc.setContent(check);
			QDomElement first = doc.documentElement().firstChildElement(QStringLiteral("description"))
								.firstChildElement(QStringLiteral("terminal"));
			TerminalData data;
			QVERIFY(data.fromXml(first));
			QCOMPARE(data.m_flow_role, TerminalData::FlowIn);

				// a signal terminal has none, whatever it says
			QDomElement signal = first.cloneNode().toElement();
			signal.setAttribute(QStringLiteral("medium"), QStringLiteral("signal"));
			TerminalData signal_data;
			QVERIFY(signal_data.fromXml(signal));
			QCOMPARE(signal_data.m_flow_role, TerminalData::NoFlowRole);

				// the element editor reads it and writes it; none is not written
			ElementScene scene(nullptr);
			scene.fromXml(doc, QPointF(), true);
			const QDomDocument written = scene.toXml();
			QStringList roles;
			for (QDomElement t = written.documentElement().firstChildElement(QStringLiteral("description"))
								 .firstChildElement(QStringLiteral("terminal"));
				 !t.isNull() ; t = t.nextSiblingElement(QStringLiteral("terminal"))) {
				roles << t.attribute(QStringLiteral("pid_flow_role"), QStringLiteral("-"));
			}
			roles.sort();
			QCOMPARE(roles, (QStringList{QStringLiteral("in"), QStringLiteral("out")}));

			QDomDocument valve;
			valve.setContent(symbols().at(0).second);
			ElementScene plain(nullptr);
			plain.fromXml(valve, QPointF(), true);
			QVERIFY(!plain.toXml().toString().contains(QLatin1String("pid_flow_role")));

				// a placed symbol knows it, and its nozzle says it
			Scene drawn;
			Element *element = drawn.add("check.elmt");
			QVERIFY(element);
			QCOMPARE(nozzle(element, "N1")->flowRole(), TerminalData::FlowIn);
			QCOMPARE(nozzle(element, "N2")->flowRole(), TerminalData::FlowOut);
			QVERIFY(nozzle(element, "N1")->toolTip().contains(QLatin1String("Inlet")));
			QVERIFY(nozzle(element, "N2")->toolTip().contains(QLatin1String("Outlet")));
			Element *gate = drawn.add("valve.elmt");
			QCOMPARE(nozzle(gate, "N1")->flowRole(), TerminalData::NoFlowRole);
		}

			// ---- on a folio ----
		void aCheckValveGivesTheDirectionToItsLine()
		{
			Scene scene;
			Element *tank = scene.add("tank.elmt");
			Element *valve = scene.add("valve.elmt");
			Element *check = scene.add("check.elmt");
			Element *reducer = scene.add("reducer.elmt");
			Element *other = scene.add("tank.elmt");
			Conductor *a = scene.pipe(tank, "N2", valve, "N1");
				// drawn the other way: the flow goes from its last terminal
			Conductor *b = scene.pipe(check, "N1", valve, "N2");
			Conductor *c = scene.pipe(check, "N2", reducer, "N1");
			Conductor *d = scene.pipe(reducer, "N2", other, "N1");
			QVERIFY(a && b && c && d);
			QCOMPARE(PidFlow::direction(a), PidFlow::Direction::Forward);
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Backward);
			QCOMPARE(PidFlow::direction(c), PidFlow::Direction::Forward);
			QCOMPARE(PidFlow::direction(d), PidFlow::Direction::Forward);

				// beyond equipment, nothing
			Element *last = scene.add("valve.elmt");
			Conductor *e = scene.pipe(other, "N2", last, "N1");
			QCOMPARE(PidFlow::direction(e), PidFlow::Direction::Unknown);
		}

		void aPumpGivesTheDirectionOnBothSides()
		{
			Scene scene;
			Element *tank = scene.add("tank.elmt");
			Element *pump = scene.add("pump.elmt");
			Element *valve = scene.add("valve.elmt");
			Conductor *suction = scene.pipe(pump, "N1", tank, "N2");
			Conductor *discharge = scene.pipe(pump, "N2", valve, "N1");
			QCOMPARE(PidFlow::direction(suction), PidFlow::Direction::Backward);
			QCOMPARE(PidFlow::direction(discharge), PidFlow::Direction::Forward);
		}

		void twoArrowsFacingEachOtherDisagree()
		{
			Scene scene;
			Element *tank = scene.add("tank.elmt");
			Element *first = scene.add("arrow.elmt");
			Element *valve = scene.add("valve.elmt");
			Element *second = scene.add("arrow.elmt");
			Conductor *a = scene.pipe(tank, "N2", first, "N1");
			Conductor *b = scene.pipe(first, "N2", valve, "N1");
				// the second arrow points back
			Conductor *c = scene.pipe(valve, "N2", second, "N2");
			QCOMPARE(PidFlow::direction(a), PidFlow::Direction::Conflict);
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Conflict);
			QCOMPARE(PidFlow::direction(c), PidFlow::Direction::Conflict);
			QCOMPARE(PidFlow::text(b), QStringLiteral("disagreement"));
		}

		void theLastBranchOfATeeIsFound()
		{
			Scene scene;
			Element *pump = scene.add("pump.elmt");
			Element *tee = scene.add("tee.elmt");
			Element *arrow = scene.add("arrow.elmt");
			Element *tank = scene.place_below(tee);
			Conductor *in_run = scene.pipe(pump, "N2", tee, "N1");
				// the run goes on out, and so the branch is not known
			Conductor *out_run = scene.pipe(tee, "N2", arrow, "N1");
			Conductor *branch = scene.pipe(tee, "N3", tank, "N1");
			QCOMPARE(PidFlow::direction(in_run), PidFlow::Direction::Forward);
			QCOMPARE(PidFlow::direction(out_run), PidFlow::Direction::Forward);
			QCOMPARE(PidFlow::direction(branch), PidFlow::Direction::Unknown);

				// the run going out turned back: both come in, the branch goes out
			scene.folio->removeItem(out_run);
			delete out_run;
			Conductor *back = scene.pipe(arrow, "N2", tee, "N2");
			QCOMPARE(PidFlow::direction(back), PidFlow::Direction::Forward);
			QCOMPARE(PidFlow::direction(branch), PidFlow::Direction::Forward);
		}

		void aSignalLineHasNoDirection()
		{
			Scene scene;
			Element *tank = scene.add("tank.elmt");
			Element *check = scene.add("check.elmt");
			Conductor *pipe = scene.pipe(tank, "N2", check, "N1");
			QVERIFY(pipe);
			QCOMPARE(PidFlow::direction(pipe), PidFlow::Direction::Forward);
			QCOMPARE(PidFlow::direction(nullptr), PidFlow::Direction::Unknown);
			QVERIFY(PidFlow::text(nullptr).isEmpty());
		}

			// ---- shown ----
		void thePageOfAPipeShowsIt()
		{
			Scene scene;
			Element *tank = scene.add("tank.elmt");
			Element *check = scene.add("check.elmt");
			Element *valve = scene.add("valve.elmt");
			Conductor *a = scene.pipe(tank, "N2", check, "N1");
			Conductor *b = scene.pipe(valve, "N1", check, "N2");
			const QString expected = QStringLiteral("from %1 to %2")
					.arg(PidFlow::nozzleName(nozzle(check, "N2")), PidFlow::nozzleName(nozzle(valve, "N1")));
			QCOMPARE(PidFlow::text(b), expected);
			QVERIFY(expected.contains(QLatin1String("(N2)")));

			PipePropertiesWidget page({b});
			auto *label = page.findChild<QLabel *>(QStringLiteral("flow_direction"));
			QVERIFY(label);
			QCOMPARE(label->text(), expected);
			page.setPipes({a, b});
			QVERIFY(label->text() != expected);

				// no source: unknown
			Element *more = scene.add("valve.elmt");
			Element *end = scene.add("tank.elmt");
			Conductor *c = scene.pipe(more, "N2", end, "N1");
			page.setPipes({c});
			QCOMPARE(label->text(), QStringLiteral("unknown"));
		}
};

QET_APP_TEST_MAIN(tst_pipeflow)
#include "tst_pipeflow.moc"
