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
#include <QUndoStack>

/**
	The direction of flow of the pipes (step 8a-bis), found from the
	symbols, never written: the role of a nozzle (pid_flow_role="in" / "out")
	in its symbol; the direction going through the symbols a line runs
	through, a reducer, a chamber of two nozzles (pid_chamber, a heat
	exchanger), a tee or a cross by its rule; never through a vessel; a
	line used both ways in turn (pid_flow_role="both"); a disagreement said;
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

	static QString north(const QString &attributes = QString())
	{
		return QStringLiteral(
			"<terminal uuid=\"{7f000000-0000-0000-0000-000000000004}\" name=\"N4\" x=\"0\" y=\"-20\" "
			"orientation=\"n\" type=\"Generic\" medium=\"pipe\" %1/>").arg(attributes);
	}

	static QString chamber(const char *name) { return QStringLiteral("pid_chamber=\"%1\"").arg(QLatin1String(name)); }

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
				//A heat exchanger: N1 and N3 one chamber, N2 and N4 the other
			{QStringLiteral("exchanger.elmt"), symbol(QStringLiteral("7f111118-1111-1111-1111-111111111111"),
					QStringLiteral("pid_family=\"equipment\""), chamber("1"), chamber("2"),
					south().replace(QLatin1String("medium=\"pipe\""),
									QLatin1String("medium=\"pipe\" pid_chamber=\"1\""))
					+ north(chamber("2")))},
				//The arrow of both ways
				//its points beyond its nozzles, as the arrow of the collection
			{QStringLiteral("both.elmt"), symbol(QStringLiteral("7f11111a-1111-1111-1111-111111111111"),
					QStringLiteral("pid_family=\"pipe\" pid_inline=\"true\" pid_role=\"flow_arrow\""),
					QStringLiteral("pid_flow_role=\"both\""), QStringLiteral("pid_flow_role=\"both\""),
					QStringLiteral(
				"<polygon x1=\"-10\" y1=\"-10\" x2=\"-30\" y2=\"0\" x3=\"-10\" y3=\"10\" closed=\"false\" "
				"antialias=\"false\" style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
				"<polygon x1=\"10\" y1=\"-10\" x2=\"30\" y2=\"0\" x3=\"10\" y3=\"10\" closed=\"false\" "
				"antialias=\"false\" style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"))},
				//A vessel: its three nozzles one chamber
			{QStringLiteral("vessel.elmt"), symbol(QStringLiteral("7f111119-1111-1111-1111-111111111111"),
					QStringLiteral("pid_family=\"equipment\""), chamber("1"), chamber("1"),
					south().replace(QLatin1String("medium=\"pipe\""),
									QLatin1String("medium=\"pipe\" pid_chamber=\"1\"")))},
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

		void bothWaysIsNoDisagreement()
		{
			PidFlow::Solver solver;
			const int a = solver.addNode(), b = solver.addNode(), c = solver.addNode();
			solver.link(a, b);
			solver.fixBoth(a, 1);
			solver.fixBoth(b, 2);
			solver.solve();
			QCOMPARE(solver.state(a), PidFlow::Solver::State::Both);
			QCOMPARE(solver.state(b), PidFlow::Solver::State::Both);
			QCOMPARE(solver.state(c), PidFlow::Solver::State::Unknown);

				// but one way only besides: a disagreement
			PidFlow::Solver one_way;
			const int d = one_way.addNode(), e = one_way.addNode();
			one_way.link(d, e);
			one_way.fixBoth(d, 1);
			one_way.fix(e, true, 2);
			one_way.solve();
			QCOMPARE(one_way.state(d), PidFlow::Solver::State::Conflict);

				// a tee: a nozzle used both ways is never the last one
			PidFlow::Solver tee;
			const int f = tee.addNode(), g = tee.addNode(), h = tee.addNode();
			tee.addJunction({f, g, h}, 9);
			tee.fix(f, true, 1);
			tee.fixBoth(g, 2);
			tee.solve();
			QCOMPARE(tee.state(h), PidFlow::Solver::State::Unknown);
			QCOMPARE(tee.state(g), PidFlow::Solver::State::Both);
			QVERIFY(!tee.junctionInConflict(0));
		}

			// Trial of the user (8a-bis-2): two come in, the third is used
			// both ways; when it comes in too, everything comes in
		void aTeeWithOneLineOfBothWaysMayDisagree()
		{
			auto conflict = [](QList<int> ways) {
					// 1 in, 0 out, 2 both
				PidFlow::Solver solver;
				QList<int> nodes;
				for (int i = 0 ; i < ways.size() ; ++i) {
					nodes << solver.addNode();
				}
				solver.addJunction(nodes, 9);
				for (int i = 0 ; i < ways.size() ; ++i) {
					if (ways.at(i) == 2) {
						solver.fixBoth(nodes.at(i), i + 1);
					} else {
						solver.fix(nodes.at(i), ways.at(i) == 1, i + 1);
					}
				}
				solver.solve();
				return solver.junctionInConflict(0);
			};
			QVERIFY(conflict({1, 1, 2}));
			QVERIFY(conflict({0, 2, 0}));
				// one in, one out: the line of both ways may go either way
			QVERIFY(!conflict({1, 0, 2}));
				// two lines of both ways may take turns
			QVERIFY(!conflict({1, 2, 2}));
			QVERIFY(!conflict({2, 2, 2}));
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
				// the symbols in cause named (8a-bis-2)
			QCOMPARE(PidFlow::text(b), QStringLiteral("disagreement (Piece, Piece)"));

				// each arrow marked, naming the other one; not the valve
			QCOMPARE(PidFlow::disagreements(first),
					 QStringList{QStringLiteral("Direction of flow opposite to that of Piece.")});
			QCOMPARE(PidFlow::disagreements(second), PidFlow::disagreements(first));
			QCOMPARE(first->flowDisagreements(), PidFlow::disagreements(first));
			QVERIFY(PidFlow::disagreements(valve).isEmpty());
			QVERIFY(PidFlow::disagreements(tank).isEmpty());

				// the second arrow gone: no mark any more
			scene.folio->removeItem(c);
			delete c;
			QVERIFY(PidFlow::disagreements(first).isEmpty());
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Forward);
		}

			// ---- the marks of a disagreement (8a-bis-2) ----
		void theSourcesOpposedAreFound()
		{
				// two against one: each of the two names the one, the one both
			PidFlow::Solver solver;
			const int a = solver.addNode(), b = solver.addNode(), c = solver.addNode();
			solver.link(a, b);
			solver.link(b, c);
			solver.fix(a, false, 1);
			solver.fix(b, true, 2);
			solver.fix(c, true, 3);
			solver.solve();
			QCOMPARE(solver.opposed(1), QList<int>{3});
			QCOMPARE(solver.opposed(2), QList<int>{3});
			QCOMPARE(solver.opposed(3), (QList<int>{1, 2}));
			QVERIFY(solver.opposed(4).isEmpty());

				// sources which agree oppose nothing
			PidFlow::Solver agreed;
			const int d = agreed.addNode(), e = agreed.addNode();
			agreed.link(d, e);
			agreed.fix(d, false, 1);
			agreed.fix(e, true, 2);
			agreed.solve();
			QVERIFY(agreed.opposed(1).isEmpty());
			QVERIFY(agreed.opposed(2).isEmpty());

				// both ways against one way
			PidFlow::Solver both;
			const int f = both.addNode(), g = both.addNode();
			both.link(f, g);
			both.fixBoth(f, 1);
			both.fix(g, true, 2);
			both.solve();
			QCOMPARE(both.opposed(1), QList<int>{2});
			QCOMPARE(both.opposed(2), QList<int>{1});
			QVERIFY(both.saysBoth(1));
			QVERIFY(!both.saysBoth(2));

				// a junction gives its nozzles
			PidFlow::Solver tee;
			const int h = tee.addNode(), i = tee.addNode(), j = tee.addNode();
			tee.addJunction({h, i, j}, 9);
			tee.solve();
			QCOMPARE(tee.junctionNodes(0), (QList<int>{h, i, j}));
			QVERIFY(tee.junctionNodes(1).isEmpty());
		}

		void aTeeIntoWhichEverythingComesIsMarked()
		{
			Scene scene;
			Element *left = scene.add("pump.elmt");
			Element *tee = scene.add("tee.elmt");
			Element *right = scene.add("pump.elmt");
			Element *below = place(scene.project, scene.folio, tee->pos() + QPointF(0, 200),
								   QStringLiteral("check.elmt"));
			QVERIFY(left && tee && right && below);
			QVERIFY(scene.pipe(left, "N2", tee, "N1"));
			QVERIFY(scene.pipe(right, "N2", tee, "N2"));
			QVERIFY(PidFlow::disagreements(tee).isEmpty());
				// the third one comes in too
			QVERIFY(scene.pipe(below, "N2", tee, "N3"));
			const QStringList notes = PidFlow::disagreements(tee);
			QCOMPARE(notes.size(), 1);
			QVERIFY(notes.first().startsWith(QLatin1String("Everything comes in here: directions given by ")));
				// the pumps by their discharge, the check valve by its name,
				// its two nozzles being on that branch
			QCOMPARE(notes.first().count(QLatin1String("Piece (N2)")), 2);
			QCOMPARE(notes.first().count(QLatin1String("Piece")), 3);
				// only the tee: each source agrees with its own stretch
			QVERIFY(PidFlow::disagreements(left).isEmpty());
			QVERIFY(PidFlow::disagreements(below).isEmpty());
		}

		void theTeeOfTheTrialIsMarked()
		{
				// two arrows come into a tee, an arrow of both ways on the third
			Scene scene;
			Element *left = scene.add("arrow.elmt");
			Element *tee = scene.add("tee.elmt");
			Element *both = scene.add("both.elmt");
			Element *below = place(scene.project, scene.folio, tee->pos() + QPointF(0, 200),
								   QStringLiteral("arrow.elmt"));
			QVERIFY(left && tee && both && below);
			QVERIFY(scene.pipe(left, "N2", tee, "N1"));
			QVERIFY(scene.pipe(tee, "N2", both, "N1"));
			QVERIFY(PidFlow::disagreements(tee).isEmpty());
			QVERIFY(scene.pipe(below, "N2", tee, "N3"));
			const QStringList notes = PidFlow::disagreements(tee);
			QCOMPARE(notes.size(), 1);
			QVERIFY(notes.first().startsWith(
						QLatin1String("Everything comes in here when the line of both ways comes in: "
									  "directions given by ")));
				// the tee only
			QVERIFY(PidFlow::disagreements(both).isEmpty());
			QVERIFY(PidFlow::disagreements(left).isEmpty());
		}

		void bothWaysAgainstOneWayIsMarked()
		{
			Scene scene;
			Element *both = scene.add("both.elmt");
			Element *valve = scene.add("valve.elmt");
			Element *check = scene.add("check.elmt");
			QVERIFY(scene.pipe(both, "N2", valve, "N1"));
			QVERIFY(scene.pipe(valve, "N2", check, "N1"));
			QCOMPARE(PidFlow::disagreements(both),
					 QStringList{QStringLiteral("Both ways here, one way only at Piece.")});
			QCOMPARE(PidFlow::disagreements(check),
					 QStringList{QStringLiteral("One way only here, both ways at Piece.")});
		}

		void aLineWithoutDisagreementIsNotMarked()
		{
			Scene scene;
			Element *pump = scene.add("pump.elmt");
			Element *check = scene.add("check.elmt");
			Element *arrow = scene.add("arrow.elmt");
			QVERIFY(scene.pipe(pump, "N2", check, "N1"));
			QVERIFY(scene.pipe(check, "N2", arrow, "N1"));
			for (const Element *element : {pump, check, arrow}) {
				QVERIFY(PidFlow::disagreements(element).isEmpty());
			}
				// a symbol off any folio, none
			QVERIFY(PidFlow::disagreements(nullptr).isEmpty());
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

		void theDirectionGoesThroughAChamberOfTwoNozzles()
		{
				// read and written, as the role; a placed nozzle knows its partner
			QDomDocument doc;
			doc.setContent(symbols().at(7).second);
			ElementScene scene(nullptr);
			scene.fromXml(doc, QPointF(), true);
			QCOMPARE(scene.toXml().toString().count(QLatin1String("pid_chamber=")), 4);
			QDomDocument valve;
			valve.setContent(symbols().at(0).second);
			ElementScene plain(nullptr);
			plain.fromXml(valve, QPointF(), true);
			QVERIFY(!plain.toXml().toString().contains(QLatin1String("pid_chamber")));

			Scene folio;
			Element *pump = folio.add("pump.elmt");
			Element *exchanger = folio.add("exchanger.elmt");
			Element *valve_out = folio.add("valve.elmt");
			Element *other = folio.add("valve.elmt");
			QCOMPARE(nozzle(exchanger, "N1")->chamber(), QStringLiteral("1"));
			QCOMPARE(nozzle(exchanger, "N1")->chamberPartner(), nozzle(exchanger, "N3"));
			QCOMPARE(nozzle(exchanger, "N4")->chamberPartner(), nozzle(exchanger, "N2"));
			QVERIFY(!nozzle(pump, "N1")->chamberPartner());

			Conductor *in_pipe = folio.pipe(pump, "N2", exchanger, "N1");
			Conductor *out_pipe = folio.pipe(exchanger, "N3", valve_out, "N1");
			Conductor *other_side = folio.pipe(exchanger, "N2", other, "N1");
			QCOMPARE(PidFlow::direction(in_pipe), PidFlow::Direction::Forward);
				// the same fluid goes out of its chamber
			QCOMPARE(PidFlow::direction(out_pipe), PidFlow::Direction::Forward);
				// the other fluid: nothing said
			QCOMPARE(PidFlow::direction(other_side), PidFlow::Direction::Unknown);
		}

		void aVesselGivesNothing()
		{
			Scene folio;
			Element *pump = folio.add("pump.elmt");
			Element *vessel = folio.add("vessel.elmt");
			Element *valve = folio.add("valve.elmt");
			QVERIFY(!nozzle(vessel, "N1")->chamberPartner());
			Conductor *in_pipe = folio.pipe(pump, "N2", vessel, "N1");
			Conductor *out_pipe = folio.pipe(vessel, "N2", valve, "N1");
			QCOMPARE(PidFlow::direction(in_pipe), PidFlow::Direction::Forward);
			QCOMPARE(PidFlow::direction(out_pipe), PidFlow::Direction::Unknown);
		}

			// A closed sand filter: the line from the tee to the filter is used
			// both ways in turn, production then backwash
		void theArrowOfBothWaysSaysSo()
		{
			Scene folio;
			Element *pump = folio.add("pump.elmt");
			Element *tee = folio.add("tee.elmt");
			Element *both = folio.add("both.elmt");
			Element *filter = folio.add("tank.elmt");
			Element *drain = folio.place_below(tee);
			Conductor *supply = folio.pipe(pump, "N2", tee, "N1");
			Conductor *to_arrow = folio.pipe(tee, "N2", both, "N1");
			Conductor *to_filter = folio.pipe(both, "N2", filter, "N1");
			Conductor *waste = folio.pipe(tee, "N3", drain, "N1");
			QCOMPARE(PidFlow::direction(supply), PidFlow::Direction::Forward);
			QCOMPARE(PidFlow::direction(to_arrow), PidFlow::Direction::Both);
			QCOMPARE(PidFlow::direction(to_filter), PidFlow::Direction::Both);
			QCOMPARE(PidFlow::text(to_filter), QStringLiteral("both ways"));
				// nothing found beyond it, never a disagreement
			QCOMPARE(PidFlow::direction(waste), PidFlow::Direction::Unknown);
			QVERIFY(nozzle(both, "N1")->toolTip().contains(QLatin1String("both ways")));
				// its points, beyond its nozzles, lie over its own pipes: no
				// traversal said
			QVERIFY(!to_arrow->traversedSymbols().contains(both));
			QVERIFY(!to_filter->traversedSymbols().contains(both));
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

			// ---- reversed by the right click (8a-bis-3) ----
		static PidFlow::Corrections on(const QGraphicsItem *item)
		{
			return PidFlow::corrections(item, item->sceneBoundingRect().center());
		}

		static void apply(Diagram *folio, const QList<Element *> &arrows)
		{
			QUndoCommand *command = PidFlow::mirrorArrows(arrows, QStringLiteral("Reverse"));
			QVERIFY(command);
			folio->undoStack().push(command);
		}

		void aLineIsReversedByItsArrows()
		{
			Scene scene;
			Element *tank = scene.add("tank.elmt");
			Element *first = scene.add("arrow.elmt");
			Element *valve = scene.add("valve.elmt");
			Element *second = scene.add("arrow.elmt");
			Element *both = scene.add("both.elmt");
			Element *end = scene.add("tank.elmt");
			Conductor *a = scene.pipe(tank, "N2", first, "N1");
			Conductor *b = scene.pipe(first, "N2", valve, "N1");
			Conductor *c = scene.pipe(valve, "N2", second, "N1");
			Conductor *d = scene.pipe(second, "N2", both, "N1");
			Conductor *e = scene.pipe(both, "N2", end, "N1");
			QVERIFY(a && b && c && d && e);
				// the arrow of both ways is in another stretch: the sources
				// of one way and of both ways meet at it
			scene.folio->removeItem(d);
			delete d;
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Forward);

				// from a pipe, from an arrow: its two arrows, no disagreement
			const PidFlow::Corrections from_pipe = on(b);
			QVERIFY(!from_pipe.disagreement);
			QCOMPARE(from_pipe.reverse.size(), 2);
			QVERIFY(from_pipe.reverse.contains(first) && from_pipe.reverse.contains(second));
			QVERIFY(from_pipe.align.isEmpty());
			QCOMPARE(on(second).reverse.size(), 2);
				// a valve in no disagreement offers nothing, nor the arrow of
				// both ways, alone on its stretch
			QVERIFY(on(valve).reverse.isEmpty());
			QVERIFY(on(both).reverse.isEmpty());
			QVERIFY(on(e).reverse.isEmpty());

			const int steps = scene.folio->undoStack().count();
			apply(scene.folio, from_pipe.reverse);
			QCOMPARE(scene.folio->undoStack().count(), steps + 1);
			QCOMPARE(PidFlow::direction(a), PidFlow::Direction::Backward);
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Backward);
			QCOMPARE(PidFlow::direction(c), PidFlow::Direction::Backward);
			QVERIFY(PidFlow::disagreements(first).isEmpty());
				// the pipes went over to the other nozzles
			QCOMPARE(a->terminal2, nozzle(first, "N2"));
			QCOMPARE(PidFlow::direction(e), PidFlow::Direction::Both);

			scene.folio->undoStack().undo();
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Forward);
			QCOMPARE(a->terminal2, nozzle(first, "N1"));
			scene.folio->undoStack().redo();
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Backward);
		}

		void aCheckValveIsNeverReversed()
		{
				// the arrow agrees with the check valve: nothing offered
			Scene scene;
			Element *tank = scene.add("tank.elmt");
			Element *check = scene.add("check.elmt");
			Element *arrow = scene.add("arrow.elmt");
			Element *end = scene.add("tank.elmt");
			Conductor *a = scene.pipe(tank, "N2", check, "N1");
			Conductor *b = scene.pipe(check, "N2", arrow, "N1");
			Conductor *c = scene.pipe(arrow, "N2", end, "N1");
			QVERIFY(a && b && c);
			QVERIFY(on(b).reverse.isEmpty());
			QVERIFY(on(arrow).reverse.isEmpty());

				// against it, drawn pointing back: under Correct, from the
				// pipe, the arrow and the check valve marked; the arrow alone
				// mirrored
			scene.folio->removeItem(c);
			delete c;
			scene.folio->removeItem(b);
			delete b;
			arrow->setRotation(180);
			b = scene.pipe(check, "N2", arrow, "N2");
			c = scene.pipe(arrow, "N1", end, "N1");
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Conflict);
			for (const QGraphicsItem *item : {static_cast<const QGraphicsItem *>(b),
											  static_cast<const QGraphicsItem *>(arrow),
											  static_cast<const QGraphicsItem *>(check)}) {
				const PidFlow::Corrections found = on(item);
				QVERIFY(found.disagreement);
				QCOMPARE(found.reverse, QList<Element *>{arrow});
			}
				// the tank, not marked: nothing
			QVERIFY(!on(tank).disagreement && on(tank).reverse.isEmpty());
			apply(scene.folio, on(b).reverse);
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Forward);
			QCOMPARE(PidFlow::direction(c), PidFlow::Direction::Forward);
			QVERIFY(PidFlow::disagreements(check).isEmpty());
			QVERIFY(arrow->hasHorizontalMirror());
			QVERIFY(!check->hasHorizontalMirror() && !check->hasVerticalMirror());
		}

		void twoArrowsAreAlignedOnOne()
		{
			Scene scene;
			Element *tank = scene.add("tank.elmt");
			Element *first = scene.add("arrow.elmt");
			Element *valve = scene.add("valve.elmt");
			Element *second = scene.add("arrow.elmt");
			Element *end = scene.add("tank.elmt");
			QVERIFY(scene.pipe(tank, "N2", first, "N1"));
			Conductor *b = scene.pipe(first, "N2", valve, "N1");
				// the second arrow points back
			second->setRotation(180);
			QVERIFY(scene.pipe(valve, "N2", second, "N2"));
			QVERIFY(scene.pipe(second, "N1", end, "N1"));
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Conflict);

				// reversing both would put nothing right
			const PidFlow::Corrections from_pipe = on(b);
			QVERIFY(from_pipe.disagreement);
			QVERIFY(from_pipe.reverse.isEmpty());
			QVERIFY(from_pipe.align.isEmpty());
			QCOMPARE(on(first).align, QList<Element *>{second});
			QCOMPARE(on(second).align, QList<Element *>{first});

			apply(scene.folio, on(second).align);
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Backward);
			QVERIFY(PidFlow::disagreements(first).isEmpty());
			QVERIFY(PidFlow::disagreements(second).isEmpty());
		}

		void aTurnedArrowIsMirroredAlongItsPipes()
		{
			Scene scene;
			Element *tank = scene.add("tank.elmt");
			Element *arrow = place(scene.project, scene.folio, tank->pos() + QPointF(0, 200),
								   QStringLiteral("arrow.elmt"));
			Element *valve = place(scene.project, scene.folio, tank->pos() + QPointF(0, 400),
								   QStringLiteral("valve.elmt"));
			QVERIFY(arrow && valve);
			arrow->setRotation(90);
			valve->setRotation(90);
			Conductor *a = scene.pipe(tank, "N3", arrow, "N1");
			Conductor *b = scene.pipe(arrow, "N2", valve, "N1");
			QVERIFY(a && b);
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Forward);
			apply(scene.folio, on(a).reverse);
			QCOMPARE(PidFlow::direction(a), PidFlow::Direction::Backward);
			QCOMPARE(PidFlow::direction(b), PidFlow::Direction::Backward);
				// mirrored along the pipes, its own horizontal axis
			QVERIFY(arrow->hasHorizontalMirror() && !arrow->hasVerticalMirror());
			QCOMPARE(arrow->rotation(), 90.0);
		}
};

QET_APP_TEST_MAIN(tst_pipeflow)
#include "tst_pipeflow.moc"
