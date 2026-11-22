// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "undocommand/mirrorselectioncommand.h"
#include "undocommand/pidrunreattachcommand.h"
#include "undocommand/rotateselectioncommand.h"

#include <QDomDocument>

// A P&ID symbol turned or mirrored on its line (5l-1h, first part): a pipe
// the turn leaves running back through its symbol goes over to the nozzle
// which now faces its other end, in the same undo step. A quarter turn of a
// straight line changes nothing; two of them do as a half turn. The branch
// of a tee keeps its pipe; a pump, with two nozzles face to face, swaps
// them as a valve, an equipment with more does not; a pipe turned with both
// its ends, or which already ran back, stays; wires are left alone.
class tst_pidrunreattach : public QObject
{
	Q_OBJECT

		// A symbol with its hotspot on the line, a nozzle N1 west and N2
		// east of it, of the medium @p medium ("" for none), N3 south of it
		// for a tee (@p more = 1), and N4 north of it too for a heat
		// exchanger (@p more = 2); @p attributes go on the definition
		// (pid_inline, pid_run)
	static QDomElement symbol(QDomDocument &doc, const QString &uuid, const QString &medium,
							  int more, const QString &attributes)
	{
		const QString attr = medium.isEmpty()
				? QString()
				: QStringLiteral(" medium=\"%1\"").arg(medium);
		QString branch;
		if (more >= 1)
			branch += QStringLiteral("<terminal name=\"N3\" x=\"0\" y=\"10\" orientation=\"s\" type=\"Generic\"%1/>").arg(attr);
		if (more >= 2)
			branch += QStringLiteral("<terminal name=\"N4\" x=\"0\" y=\"-10\" orientation=\"n\" type=\"Generic\"%1/>").arg(attr);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\" %1>"
			"<uuid uuid=\"{%2}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%3/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%3/>"
			"%4"
			"</description></definition>").arg(attributes, uuid, attr, branch));
		return doc.documentElement();
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_valve, m_tee, m_electrical, m_pump, m_exchanger;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/reattach"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/reattach/") + file, &m_project);
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

	static Terminal *nozzle(const Element *element, const QString &name)
	{
		for (Terminal *terminal : element->terminals()) {
			if (terminal->baseName() == name) {
				return terminal;
			}
		}
		return nullptr;
	}

	Conductor *join(Terminal *a, Terminal *b)
	{
		auto *conductor = new Conductor(a, b);
		m_folio->addItem(conductor);
		return conductor;
	}

	static bool on(const Conductor *conductor, const Terminal *terminal)
	{
		return conductor->terminal1 == terminal || conductor->terminal2 == terminal;
	}

		// Three valves on a line, west to east: the pipes in and out of the
		// middle one
	struct Line { Element *left, *middle, *right; Conductor *in, *out; };
	Line line(const ElementsLocation &middle)
	{
		m_next_y += 200;
		Line l;
		l.left = place(m_valve, QPointF(0, m_next_y));
		l.middle = place(middle, QPointF(200, m_next_y));
		l.right = place(m_valve, QPointF(400, m_next_y));
		l.in = join(nozzle(l.left, "N2"), nozzle(l.middle, "N1"));
		l.out = join(nozzle(l.middle, "N2"), nozzle(l.right, "N1"));
		return l;
	}

	void select(const QList<Element *> &elements)
	{
		m_folio->clearSelection();
		for (Element *element : elements) {
			element->setSelected(true);
		}
	}

		// A quarter turn of the selection, as Space does; kept for undo
	RotateSelectionCommand *turn()
	{
		auto *command = new RotateSelectionCommand(m_folio, 90);
		command->redo();
		return command;
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("reattach"), NamesList());
			QDomDocument a, b, c, d, e;
			m_valve = add("valve.elmt", symbol(a, "e1111111-1111-1111-1111-111111111111", "pipe",
											   0, "pid_inline=\"true\""));
			m_tee = add("tee.elmt", symbol(b, "e2222222-2222-2222-2222-222222222222", "pipe",
										   1, "pid_run=\"N1 N2 N3\""));
			m_electrical = add("electrical.elmt", symbol(c, "e3333333-3333-3333-3333-333333333333", "",
														 0, QString()));
			m_pump = add("pump.elmt", symbol(d, "e4444444-4444-4444-4444-444444444444", "pipe",
											 0, QString()));
			m_exchanger = add("exchanger.elmt", symbol(e, "e5555555-5555-5555-5555-555555555555", "pipe",
													   2, QString()));
			QVERIFY(m_valve.exist() && m_tee.exist() && m_electrical.exist()
					&& m_pump.exist() && m_exchanger.exist());
			m_folio = m_project.addNewDiagram();
		}

			// The straight pairs: the two nozzles of a valve, the run of a
			// tee; none for an electrical symbol
		void straightPairs()
		{
			m_next_y += 200;
			Element *valve = place(m_valve, QPointF(0, m_next_y));
			Element *tee = place(m_tee, QPointF(200, m_next_y));
			Element *electrical = place(m_electrical, QPointF(400, m_next_y));
			QVERIFY(valve && tee && electrical);
			QCOMPARE(PidRunReattachCommand::straightPairs(valve).size(), 1);
			const auto tee_pairs = PidRunReattachCommand::straightPairs(tee);
			QCOMPARE(tee_pairs.size(), 1);
			QCOMPARE(tee_pairs.first().first->baseName(), QStringLiteral("N1"));
			QCOMPARE(tee_pairs.first().second->baseName(), QStringLiteral("N2"));
			QVERIFY(PidRunReattachCommand::straightPairs(electrical).isEmpty());
			QVERIFY(!PidRunReattachCommand::concerns({electrical}));
		}

			// A pump has no run (no change of size goes through it), but its
			// two nozzles face each other: mirrored, its pipes swap as those
			// of a valve
		void aPumpMirroredSwapsItsPipes()
		{
			Line l = line(m_pump);
			QVERIFY(l.middle->pipeRun().isEmpty());
			QCOMPARE(PidRunReattachCommand::straightPairs(l.middle).size(), 1);
			MirrorSelectionCommand mirror({l.middle}, Qt::Horizontal);
			mirror.redo();
			QVERIFY(on(l.in, nozzle(l.middle, "N2")) && on(l.out, nozzle(l.middle, "N1")));
		}

			// An equipment with more than two nozzles keeps its pipes: the
			// two circuits of a heat exchanger are not mixed
		void anExchangerKeepsItsPipes()
		{
			Line l = line(m_exchanger);
			QVERIFY(PidRunReattachCommand::straightPairs(l.middle).isEmpty());
			MirrorSelectionCommand mirror({l.middle}, Qt::Horizontal);
			mirror.redo();
			QVERIFY(on(l.in, nozzle(l.middle, "N1")) && on(l.out, nozzle(l.middle, "N2")));
		}

			// Mirrored, a valve keeps its pipes on their sides: they swap
			// nozzles; undone and done again in the same step
		void aMirrorSwapsThePipes()
		{
			Line l = line(m_valve);
			QVERIFY(l.left && l.middle && l.right);
			QCOMPARE(l.in->medium(), TerminalData::Pipe);
			Terminal *n1 = nozzle(l.middle, "N1"), *n2 = nozzle(l.middle, "N2");

			MirrorSelectionCommand mirror({l.middle}, Qt::Horizontal);
			mirror.redo();
			QVERIFY(on(l.in, n2) && on(l.out, n1));
			QVERIFY(!PidRunReattachCommand::runsBack(n2, l.in));
			QVERIFY(!PidRunReattachCommand::runsBack(n1, l.out));
			QCOMPARE(n1->conductors().size(), 1);
			QCOMPARE(n2->conductors().size(), 1);

			mirror.undo();
			QVERIFY(on(l.in, n1) && on(l.out, n2));
			mirror.redo();
			QVERIFY(on(l.in, n2) && on(l.out, n1));
		}

			// A mirror across the line changes nothing
		void aMirrorAcrossTheLineChangesNothing()
		{
			Line l = line(m_valve);
			MirrorSelectionCommand mirror({l.middle}, Qt::Vertical);
			mirror.redo();
			QVERIFY(on(l.in, nozzle(l.middle, "N1")) && on(l.out, nozzle(l.middle, "N2")));
		}

			// One pipe only: it goes over to the free nozzle facing it
		void aSinglePipeGoesToTheFreeNozzle()
		{
			m_next_y += 200;
			Element *left = place(m_valve, QPointF(0, m_next_y));
			Element *valve = place(m_valve, QPointF(200, m_next_y));
			Conductor *in = join(nozzle(left, "N2"), nozzle(valve, "N1"));
			MirrorSelectionCommand mirror({valve}, Qt::Horizontal);
			mirror.redo();
			QVERIFY(on(in, nozzle(valve, "N2")));
			QVERIFY(nozzle(valve, "N1")->conductors().isEmpty());
		}

			// A quarter turn of a straight line changes nothing; a second one
			// makes a half turn, which swaps the pipes; undone, the pipes are
			// back as after the first
		void twoQuarterTurnsSwapThePipes()
		{
			Line l = line(m_valve);
			Terminal *n1 = nozzle(l.middle, "N1"), *n2 = nozzle(l.middle, "N2");
			select({l.middle});
			RotateSelectionCommand *first = turn();
			QVERIFY(on(l.in, n1) && on(l.out, n2));
			RotateSelectionCommand *second = turn();
			QVERIFY(on(l.in, n2) && on(l.out, n1));
			second->undo();
			QVERIFY(on(l.in, n1) && on(l.out, n2));
			delete second;
			delete first;
		}

			// A line in L, which goes on below the valve: turned by a quarter,
			// the valve has it on the nozzle now facing down
		void aQuarterTurnMovesALineInL()
		{
			m_next_y += 200;
			Element *valve = place(m_valve, QPointF(200, m_next_y));
			Element *below = place(m_valve, QPointF(0, m_next_y + 100));
			Conductor *pipe = join(nozzle(below, "N2"), nozzle(valve, "N1"));
			QVERIFY(!PidRunReattachCommand::runsBack(nozzle(valve, "N1"), pipe));
			select({valve});
			RotateSelectionCommand *first = turn();
			QVERIFY(on(pipe, nozzle(valve, "N2")));
			first->undo();
			QVERIFY(on(pipe, nozzle(valve, "N1")));
			delete first;
		}

			// A tee turned by half: its run swaps, its branch keeps its pipe
		void theBranchOfATeeKeepsItsPipe()
		{
			Line l = line(m_tee);
			Element *below = place(m_valve, QPointF(200, m_next_y + 100));
			Conductor *branch = join(nozzle(l.middle, "N3"), nozzle(below, "N1"));
			select({l.middle});
			RotateSelectionCommand *first = turn();
			RotateSelectionCommand *second = turn();
			QVERIFY(on(l.in, nozzle(l.middle, "N2")) && on(l.out, nozzle(l.middle, "N1")));
			QVERIFY(on(branch, nozzle(l.middle, "N3")));
			delete second;
			delete first;
		}

			// The trial of the user (22/11): a tee with its run upright, a line
			// in L up to a valve on the left, one down to a valve below, the
			// branch to a valve on the right. A quarter turn already puts the
			// line in L on the nozzle facing west; a half turn gives a clean
			// run again, upright; the branch keeps N3 all along
		void aTeeWithLinesInLTurnedByHalf()
		{
			m_next_y += 400;
			Element *tee = place(m_tee, QPointF(300, m_next_y));
			Element *up_left = place(m_valve, QPointF(100, m_next_y - 250));
			Element *below = place(m_valve, QPointF(310, m_next_y + 200));
			Element *right = place(m_valve, QPointF(500, m_next_y));
			QVERIFY(tee && up_left && below && right);
			tee->setRotation(270);
			Terminal *n1 = nozzle(tee, "N1"), *n2 = nozzle(tee, "N2"), *n3 = nozzle(tee, "N3");
			QCOMPARE(n1->orientation(), Qet::South);
			QCOMPARE(n2->orientation(), Qet::North);
			QCOMPARE(n3->orientation(), Qet::East);
			Conductor *up = join(nozzle(up_left, "N2"), n2);
			Conductor *down = join(n1, nozzle(below, "N1"));
			Conductor *branch = join(n3, nozzle(right, "N1"));
			select({tee});

			RotateSelectionCommand *first = turn();
			QCOMPARE(n1->orientation(), Qet::West);
			QVERIFY(on(up, n1) && on(down, n2) && on(branch, n3));

			RotateSelectionCommand *second = turn();
			QCOMPARE(n1->orientation(), Qet::North);
			QVERIFY(on(up, n1) && on(down, n2) && on(branch, n3));
			QVERIFY(!PidRunReattachCommand::runsBack(n1, up));
			QVERIFY(!PidRunReattachCommand::runsBack(n2, down));
			QVERIFY(PidRunReattachCommand::runsBack(n3, branch));

			second->undo();
			first->undo();
			QVERIFY(on(up, n2) && on(down, n1) && on(branch, n3));
			delete second;
			delete first;
		}

			// Two valves turned together by half, as a group: the pipe
			// between them still joins the nozzles facing each other
		void aPipeTurnedWithBothEndsStays()
		{
			m_next_y += 200;
			Element *a = place(m_valve, QPointF(0, m_next_y));
			Element *b = place(m_valve, QPointF(200, m_next_y));
			Conductor *between = join(nozzle(a, "N2"), nozzle(b, "N1"));
			select({a, b});
			auto *first = new RotateSelectionCommand(m_folio, 90, nullptr, true);
			first->redo();
			auto *second = new RotateSelectionCommand(m_folio, 90, nullptr, true);
			second->redo();
			QVERIFY(on(between, nozzle(a, "N2")) && on(between, nozzle(b, "N1")));
			QVERIFY(!PidRunReattachCommand::runsBack(nozzle(a, "N2"), between));
			delete second;
			delete first;
		}

			// A pipe which already ran back through its symbol stays where
			// it is
		void aPipeWhichRanBackStays()
		{
			m_next_y += 200;
			Element *valve = place(m_valve, QPointF(0, m_next_y));
			Element *away = place(m_valve, QPointF(200, m_next_y + 100));
			Conductor *pipe = join(nozzle(valve, "N1"), nozzle(away, "N1"));
			QVERIFY(PidRunReattachCommand::runsBack(nozzle(valve, "N1"), pipe));
			select({valve});
			RotateSelectionCommand *first = turn();
			QVERIFY(on(pipe, nozzle(valve, "N1")));
			delete first;
		}

			// Wires of an electrical symbol are left alone
		void wiresAreLeftAlone()
		{
			m_next_y += 200;
			Element *left = place(m_electrical, QPointF(0, m_next_y));
			Element *middle = place(m_electrical, QPointF(200, m_next_y));
			Conductor *wire = join(nozzle(left, "N2"), nozzle(middle, "N1"));
			MirrorSelectionCommand mirror({middle}, Qt::Horizontal);
			mirror.redo();
			QVERIFY(on(wire, nozzle(middle, "N1")));
		}
};

QET_APP_TEST_MAIN(tst_pidrunreattach)
#include "tst_pidrunreattach.moc"
