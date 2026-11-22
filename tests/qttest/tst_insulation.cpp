// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidinsulation.h"
#include "pidlinedesignation.h"
#include "qetgraphicsitem/conductortextitem.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "ui/pipepropertieswidget.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QUndoStack>

#include <QDomDocument>
#include <QImage>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

// Step 7d: the insulation of a pipe. A hatched stub (insulation), dashed
// (heat tracing) or both is drawn where the insulated part of a line
// begins and ends: at equipment, at a free end, at an insulation limit,
// beside a pipe of another kind of insulation; nothing at a valve or a tee
// between two pipes insulated alike. A pipe may ask for a mark in its
// middle, its insulation written beside it. The length of the stubs and
// their colours are settings of the project.
class tst_insulation : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40: N1 (west) and N2 (east) at 10 from its middle,
		// N3 (south) when @p tee; nozzles of pipes; @p extra on <definition>
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &extra,
								  bool tee = false, const QString &medium = QStringLiteral("pipe"))
	{
		const QString attr = QStringLiteral(" medium=\"%1\"").arg(medium);
		const QString branch = tee
				? QStringLiteral("<terminal name=\"N3\" x=\"0\" y=\"10\" orientation=\"s\" "
								 "type=\"Generic\"%1/>").arg(attr)
				: QString();
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" %3"
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%2/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%2/>"
			"%4</description></definition>").arg(uuid, attr, extra, branch));
		return doc.documentElement();
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_valve, m_tee, m_equipment, m_limit, m_signals, m_arrow;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/insulation"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/insulation/") + file, &m_project);
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

		// A row of its own, the symbols 200 apart
	QList<Element *> row(const QList<ElementsLocation> &symbols)
	{
		m_next_y += 200;
		QList<Element *> placed;
		qreal x = 0;
		for (const ElementsLocation &symbol : symbols) {
			placed << place(symbol, QPointF(x, m_next_y));
			x += 200;
		}
		return placed;
	}

	static Terminal *nozzle(Element *element, const char *name)
	{
		for (Terminal *t : element->terminals())
			if (t->name() == QLatin1String(name)) return t;
		return nullptr;
	}

	Conductor *draw(Terminal *from, Terminal *to,
					PipeProperties::InsulationKind kind = PipeProperties::NoInsulation)
	{
		auto *pipe = new Conductor(from, to);
		m_folio->addItem(pipe);
		insulate(pipe, kind);
		return pipe;
	}

	static void insulate(Conductor *pipe, PipeProperties::InsulationKind kind,
						 bool mark = false, const QString &text = QString())
	{
		ConductorProperties data = pipe->properties();
		data.m_pipe.insulation_kind = kind;
		data.m_pipe.insulation_mark = mark;
		data.m_pipe.insulation = text;
		pipe->setProperties(data);
	}

		// The lines a path is made of: each a moveTo and a lineTo
	static int lines(const QPainterPath &path)
	{
		int count = 0;
		for (int i = 0 ; i < path.elementCount() ; ++i) {
			if (path.elementAt(i).isMoveTo()) ++count;
		}
		return count;
	}

	void setStub(qreal mm)
	{
		PidCodification codes = m_project.pidCodification();
		codes.insulation_stub = mm;
		m_project.setPidCodification(codes);
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("insulation"), NamesList());
			QDomDocument a, b, c, d, e, f;
			m_valve = add("valve.elmt", definition(a, "91111111-1111-1111-1111-111111111111",
												   "pid_inline=\"true\" pid_family=\"valve\" "));
			m_tee = add("tee.elmt", definition(b, "92222222-2222-2222-2222-222222222222",
											   "pid_run=\"N1 N2 N3\" pid_family=\"pipe\" ", true));
			m_equipment = add("equipment.elmt", definition(c, "93333333-3333-3333-3333-333333333333",
														   "pid_family=\"equipment\" "));
			m_limit = add("limit.elmt", definition(d, "94444444-4444-4444-4444-444444444444",
												   "pid_inline=\"true\" pid_family=\"pipe\" "
												   "pid_role=\"insulation_limit\" "));
			m_signals = add("signals.elmt", definition(e, "95555555-5555-5555-5555-555555555555",
													   QString(), false, QStringLiteral("signal")));
			m_arrow = add("arrow.elmt", definition(f, "96666666-6666-6666-6666-666666666666",
												   "pid_inline=\"true\" pid_family=\"pipe\" "
												   "pid_role=\"flow_arrow\" "));
			QVERIFY(m_valve.exist() && m_tee.exist() && m_equipment.exist() && m_limit.exist()
					&& m_signals.exist() && m_arrow.exist());
			m_folio = m_project.addNewDiagram();
		}

			// The geometry: a part of a path, a path moved aside, a band of
			// hatches none of which sticks out of its ends, the line of a
			// heat tracing beside the pipe or inside the band
		void theGeometryOfAStub()
		{
			const QList<QPointF> corner{QPointF(0, 0), QPointF(40, 0), QPointF(40, 40)};
			QCOMPARE(PidInsulation::length(corner), 80.0);
			QCOMPARE(PidInsulation::stretch(corner, 30, 50),
					 QList<QPointF>({QPointF(30, 0), QPointF(40, 0), QPointF(40, 10)}));
			QVERIFY(PidInsulation::stretch(corner, 50, 30).isEmpty());
				// to the left of a path going east then south: above, then
				// on its east side
			QCOMPARE(PidInsulation::offset(corner, 6),
					 QList<QPointF>({QPointF(0, -6), QPointF(46, -6), QPointF(46, 40)}));

			const QList<QPointF> straight{QPointF(0, 0), QPointF(32, 0)};
			const QPainterPath band = PidInsulation::band(straight);
				// two sides and five hatches, 5 apart, centred, each 6 along
				// as across
			QCOMPARE(lines(band), 7);
			QVERIFY(band.boundingRect().left() >= -0.01 && band.boundingRect().right() <= 32.01);
			QCOMPARE(band.boundingRect().top(), -PidInsulation::half_width);
				// a point in line inside the stub, as a pipe often has
				// (going south, as at a nozzle on top of a tank): one straight
				// run, five hatches still, not three on its longer step
			const QList<QPointF> in_line{QPointF(0, 0), QPointF(0, 25), QPointF(0, 32)};
			QCOMPARE(lines(PidInsulation::band(in_line)), 7);
			QCOMPARE(PidInsulation::stretch(in_line, 0, 32),
					 QList<QPointF>({QPointF(0, 0), QPointF(0, 32)}));
				// a turn back is no straight run: its point is kept
			QCOMPARE(PidInsulation::offset({QPointF(0, 0), QPointF(20, 0), QPointF(10, 0)}, 6).size(), 3);
				// too short for a hatch: its sides only
			QCOMPARE(lines(PidInsulation::band({QPointF(0, 0), QPointF(10, 0)})), 2);

				// the heat tracing on the same side of the folio, whichever
				// way the pipe was drawn (step 7d-bis): below a horizontal
				// step, right of a vertical one, after a corner too
			QCOMPARE(PidInsulation::tracing(straight, false).elementAt(0).y, PidInsulation::half_width);
			QCOMPARE(PidInsulation::tracing(straight, true).elementAt(0).y, PidInsulation::half_width / 2);
			QCOMPARE(PidInsulation::beside({QPointF(32, 0), QPointF(0, 0)}, 6),
					 QList<QPointF>({QPointF(32, 6), QPointF(0, 6)}));
			QCOMPARE(PidInsulation::beside({QPointF(0, 0), QPointF(0, 32)}, 6),
					 QList<QPointF>({QPointF(6, 0), QPointF(6, 32)}));
			QCOMPARE(PidInsulation::beside({QPointF(0, 32), QPointF(0, 0)}, 6),
					 QList<QPointF>({QPointF(6, 32), QPointF(6, 0)}));
				// a U: below both of its horizontal steps, right of the
				// vertical one; the same path drawn back, the same line
			const QList<QPointF> u{QPointF(0, 0), QPointF(40, 0), QPointF(40, 40), QPointF(0, 40)};
			const QList<QPointF> u_line{QPointF(0, 6), QPointF(46, 6), QPointF(46, 46), QPointF(0, 46)};
			QCOMPARE(PidInsulation::beside(u, 6), u_line);
			QList<QPointF> back_u = u, back_line = u_line;
			std::reverse(back_u.begin(), back_u.end());
			std::reverse(back_line.begin(), back_line.end());
			QCOMPARE(PidInsulation::beside(back_u, 6), back_line);
		}

			// The settings of the project: 8 mm, not written; another length
			// in its file and in the CSV of its presentation, read back; a
			// length out of bounds left out; the colours as those of objects
		void theStubsAreSettingsOfTheProject()
		{
			PidCodification codes;
			QCOMPARE(codes.insulation_stub, 8.0);
			QVERIFY(codes.isEmpty());
			codes.insulation_stub = 10;
			QVERIFY(codes.hasTextSettings());
			QDomDocument doc;
			QDomElement parent = doc.createElement(QStringLiteral("project"));
			parent.appendChild(codes.toXml(doc));
			QCOMPARE(parent.firstChildElement().attribute(QStringLiteral("insulation_stub")),
					 QStringLiteral("10"));
			PidCodification read;
			read.fromXml(parent);
			QCOMPARE(read.insulation_stub, 10.0);
			QVERIFY(read == codes);
			QVERIFY(codes.presentationToCsv().contains(QStringLiteral("text_option;insulation_stub;10\n")));

			int skipped = 0;
			const PidCodification csv = PidCodification::fromCsv(QStringLiteral(
				"text_option;insulation_stub;12,5\nobject_color;insulation;#ff0000\n"
				"object_color;tracing;#0000ff\n"), &skipped);
			QCOMPARE(skipped, 0);
			QCOMPARE(csv.insulation_stub, 12.5);
			QCOMPARE(csv.object_colors.value(PidCodification::insulationColorKey()), QColor(Qt::red));
			QCOMPARE(csv.object_colors.value(PidCodification::tracingColorKey()), QColor(Qt::blue));
			const PidCodification wrong = PidCodification::fromCsv(
						QStringLiteral("text_option;insulation_stub;0\n"), &skipped);
			QCOMPARE(skipped, 1);
			QCOMPARE(wrong.insulation_stub, 8.0);
				// a project without presentation takes that of a file
			QCOMPARE(PidCodification().completedWith(csv).insulation_stub, 12.5);
		}

			// equipment ── P1 ── valve ── P2 ── equipment: a stub at each
			// piece of equipment, none at the valve while both sides are
			// insulated alike; one there when the other side is not, or
			// otherwise; always at an insulation limit
		void theStubsAreWhereTheInsulationEnds()
		{
			const auto e = row({m_equipment, m_valve, m_equipment});
			Conductor *p1 = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"), PipeProperties::Insulation);
			Conductor *p2 = draw(nozzle(e[1], "N2"), nozzle(e[2], "N1"), PipeProperties::Insulation);
			QVERIFY(p1->insulationEndsAt(nozzle(e[0], "N2")));
			QVERIFY(!p1->insulationEndsAt(nozzle(e[1], "N1")));
			QVERIFY(!p2->insulationEndsAt(nozzle(e[1], "N2")));
			QVERIFY(p2->insulationEndsAt(nozzle(e[2], "N1")));
			QCOMPARE(p1->pipesBeyond(nozzle(e[1], "N1")), QList<Conductor *>({p2}));
			QVERIFY(p1->pipesBeyond(nozzle(e[0], "N2")).isEmpty());

			insulate(p2, PipeProperties::NoInsulation);
			QVERIFY(p1->insulationEndsAt(nozzle(e[1], "N1")));
			QVERIFY(!p2->insulationEndsAt(nozzle(e[2], "N1")));
			insulate(p2, PipeProperties::InsulationAndTracing);
			QVERIFY(p1->insulationEndsAt(nozzle(e[1], "N1")));
			QVERIFY(p2->insulationEndsAt(nozzle(e[1], "N2")));

				// an insulation limit: both sides, insulated alike
			const auto l = row({m_equipment, m_limit, m_equipment});
			Conductor *q1 = draw(nozzle(l[0], "N2"), nozzle(l[1], "N1"), PipeProperties::Insulation);
			Conductor *q2 = draw(nozzle(l[1], "N2"), nozzle(l[2], "N1"), PipeProperties::Insulation);
			QVERIFY(l[1]->isInsulationLimit() && !e[1]->isInsulationLimit());
			QVERIFY(q1->insulationEndsAt(nozzle(l[1], "N1")));
			QVERIFY(q2->insulationEndsAt(nozzle(l[1], "N2")));
			bool limit = false;
			QVERIFY(q1->pipesBeyond(nozzle(l[1], "N1"), &limit).isEmpty());
			QVERIFY(limit);

				// drawn as a spec break: a mark on the line, no piece, each
				// pipe going on into its middle; what a line carries goes
				// through it (its spec, unlike a spec break)
			QVERIFY(l[1]->isLineMark() && !e[1]->isLineMark());
			QVERIFY(!nozzle(l[1], "N1")->drawsConnectionMark());
			QVERIFY(!q1->lineIntoSpecBreak(nozzle(l[1], "N1")).isNull());
			QVERIFY(q1->lineIntoSpecBreak(nozzle(l[0], "N2")).isNull());
			QVERIFY(q1->inlineRun(Conductor::LineReach::Spec).contains(q2));

				// the stubs start in its middle, at its stroke, where the
				// pipes are drawn to, not at its nozzles (step 7d-bis; gap
				// of 2.5 mm seen at the hand test of 22/11), as long
			const qreal middle = l[1]->pos().x();
			QPainterPath hatches, dashes;
			q1->insulationPaths(&hatches, &dashes);
			QVERIFY(qAbs(q1->mapRectToScene(hatches.boundingRect()).right() - middle) < 0.01);
			QCOMPARE(lines(hatches), 2 * 7);
			q2->insulationPaths(&hatches, &dashes);
			QVERIFY(qAbs(q2->mapRectToScene(hatches.boundingRect()).left() - middle) < 0.01);
			QCOMPARE(lines(hatches), 2 * 7);
				// the heat tracing too
			insulate(q2, PipeProperties::Tracing);
			q2->insulationPaths(&hatches, &dashes);
			QVERIFY(qAbs(q2->mapRectToScene(dashes.boundingRect()).left() - middle) < 0.01);
			insulate(q2, PipeProperties::Insulation);
		}

			// A flow arrow stops nothing: no stub between two pipes
			// insulated alike; where only one is, its stub starts at the
			// nozzle of the arrow, not in its middle, which it would hide
			// (choice of the user, 22/11, step 7d-bis)
		void aFlowArrowKeepsItsStubAtItsNozzle()
		{
			const auto e = row({m_equipment, m_arrow, m_equipment});
			Conductor *left = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"), PipeProperties::Insulation);
			Conductor *right = draw(nozzle(e[1], "N2"), nozzle(e[2], "N1"), PipeProperties::Insulation);
			QVERIFY(e[1]->isFlowArrow() && e[1]->isLineMark());
			QVERIFY(!left->insulationEndsAt(nozzle(e[1], "N1")));
			QVERIFY(!right->insulationEndsAt(nozzle(e[1], "N2")));
			insulate(left, PipeProperties::NoInsulation);
			QVERIFY(right->insulationEndsAt(nozzle(e[1], "N2")));
				// the pipe still goes into its middle
			QVERIFY(!right->lineIntoSpecBreak(nozzle(e[1], "N2")).isNull());
			QPainterPath hatches, dashes;
			right->insulationPaths(&hatches, &dashes);
			QCOMPARE(lines(hatches), 2 * 7);
			const qreal from = right->mapRectToScene(hatches.boundingRect()).left();
			QVERIFY(qAbs(from - nozzle(e[1], "N2")->dockConductor().x()) < 0.01);
			QVERIFY(from > e[1]->pos().x() + 1);
		}

			// The insulation given on the page of a pipe goes to the whole
			// line, through a valve, a flow arrow, a tee and its branch, up
			// to an insulation limit or equipment: its kind, its text and
			// its mark, one undo step each (step 7d-bis, choice of the user
			// of 22/11, instead of the pipe selected only)
		void theInsulationGoesToTheWholeLine()
		{
				// equipment ─p1─ valve ─p2─ arrow ─p3─ limit ─p4─ equipment
			const auto e = row({m_equipment, m_valve, m_arrow, m_limit, m_equipment});
			Conductor *p1 = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"));
			Conductor *p2 = draw(nozzle(e[1], "N2"), nozzle(e[2], "N1"));
			Conductor *p3 = draw(nozzle(e[2], "N2"), nozzle(e[3], "N1"));
			Conductor *p4 = draw(nozzle(e[3], "N2"), nozzle(e[4], "N1"));
			QList<Conductor *> run = p2->inlineRun(Conductor::LineReach::Insulation);
			std::sort(run.begin(), run.end());
			QList<Conductor *> expected{p1, p2, p3};
			std::sort(expected.begin(), expected.end());
			QCOMPARE(run, expected);
				// a limit stops it, on both sides
			QCOMPARE(p4->inlineRun(Conductor::LineReach::Insulation), QList<Conductor *>({p4}));

			PipePropertiesWidget page({p2});
			auto *kind = page.findChild<QComboBox *>(QStringLiteral("insulation_kind"));
			auto *text = page.findChild<QLineEdit *>(QStringLiteral("insulation"));
			auto *mark = page.findChild<QCheckBox *>(QStringLiteral("insulation_mark"));
			QVERIFY(kind && text && mark);
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();
			const int traced = kind->findData(int(PipeProperties::InsulationAndTracing));
			kind->setCurrentIndex(traced);
			emit kind->activated(traced);
			text->setText(QStringLiteral("Q 80"));
			text->setModified(true);
			emit text->editingFinished();
			mark->click();
			QCOMPARE(stack.count(), steps + 3);
			for (Conductor *pipe : {p1, p2, p3}) {
				const PipeProperties &p = pipe->properties().m_pipe;
				QCOMPARE(p.insulation_kind, PipeProperties::InsulationAndTracing);
				QCOMPARE(p.insulation, QStringLiteral("Q 80"));
				QVERIFY(p.insulation_mark);
			}
			QCOMPARE(p4->properties().m_pipe, PipeProperties());
				// stubs at the equipment and the limit only
			QVERIFY(p1->insulationEndsAt(nozzle(e[0], "N2")));
			QVERIFY(!p1->insulationEndsAt(nozzle(e[1], "N1")));
			QVERIFY(!p2->insulationEndsAt(nozzle(e[2], "N1")));
			QVERIFY(p3->insulationEndsAt(nozzle(e[3], "N1")));
				// back to none, on the whole line again
			const int none = kind->findData(int(PipeProperties::NoInsulation));
			kind->setCurrentIndex(none);
			emit kind->activated(none);
			for (Conductor *pipe : {p1, p2, p3}) {
				QCOMPARE(pipe->properties().m_pipe.insulation_kind, PipeProperties::NoInsulation);
			}
			stack.undo();
			QCOMPARE(p1->properties().m_pipe.insulation_kind, PipeProperties::InsulationAndTracing);

				// a tee: on through it, the branch included
			const auto t = row({m_equipment, m_tee, m_equipment});
			Element *below = place(m_equipment, t[1]->pos() + QPointF(0, 100));
			Conductor *left = draw(nozzle(t[0], "N2"), nozzle(t[1], "N1"));
			Conductor *right = draw(nozzle(t[1], "N2"), nozzle(t[2], "N1"));
			Conductor *branch = draw(nozzle(t[1], "N3"), nozzle(below, "N1"));
			page.setPipes({left});
			const int insulated = kind->findData(int(PipeProperties::Insulation));
			kind->setCurrentIndex(insulated);
			emit kind->activated(insulated);
			for (Conductor *pipe : {left, right, branch}) {
				QCOMPARE(pipe->properties().m_pipe.insulation_kind, PipeProperties::Insulation);
			}
		}

			// A tee: nothing where the run goes on insulated, the branch
			// included; a stub on the branch when nothing beyond is insulated
		void aTeeBetweenInsulatedPipesHasNoStub()
		{
			const auto e = row({m_equipment, m_tee, m_equipment});
			Element *below = place(m_equipment, e[1]->pos() + QPointF(0, 100));
			Conductor *left = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"), PipeProperties::Insulation);
			Conductor *right = draw(nozzle(e[1], "N2"), nozzle(e[2], "N1"));
			Conductor *branch = draw(nozzle(e[1], "N3"), nozzle(below, "N1"), PipeProperties::Insulation);
			QVERIFY(!left->insulationEndsAt(nozzle(e[1], "N1")));
			QVERIFY(!branch->insulationEndsAt(nozzle(e[1], "N3")));
			QVERIFY(!right->insulationEndsAt(nozzle(e[1], "N2")));
			insulate(left, PipeProperties::NoInsulation);
			QVERIFY(branch->insulationEndsAt(nozzle(e[1], "N3")));
		}

			// What is drawn: a band at each end, of the length of the project
			// (8 mm, 32 units: five hatches), less on a short pipe; dashes
			// for a heat tracing; a third band in the middle with the mark,
			// its text above it; nothing for a pipe not insulated, a signal
			// line or a wire
		void theStubsDrawnFollowTheProject()
		{
			const auto e = row({m_equipment, m_equipment});
			Conductor *pipe = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"), PipeProperties::Insulation);
			QPainterPath hatches, dashes;
			pipe->insulationPaths(&hatches, &dashes);
			QCOMPARE(lines(hatches), 2 * 7);
			QVERIFY(dashes.isEmpty());
				// none in the middle of the pipe, 180 long
			const QRectF middle = pipe->mapRectFromScene(QRectF(nozzle(e[0], "N2")->dockConductor()
																 + QPointF(40, -10), QSizeF(100, 20)));
			QVERIFY(!hatches.intersects(middle));

			setStub(12);
			pipe->insulationPaths(&hatches, &dashes);
				// 48 units: eight hatches each
			QCOMPARE(lines(hatches), 2 * 10);
			setStub(8);

			insulate(pipe, PipeProperties::Tracing);
			pipe->insulationPaths(&hatches, &dashes);
			QVERIFY(hatches.isEmpty());
			QCOMPARE(lines(dashes), 2);
				// below the pipe, drawn either way (step 7d-bis: the dashes
				// were above it when drawn from right to left)
			const qreal axis = nozzle(e[0], "N2")->dockConductor().y();
			QVERIFY(pipe->mapRectToScene(dashes.boundingRect()).top() > axis);
			const auto r = row({m_equipment, m_equipment});
			Conductor *back = draw(nozzle(r[1], "N1"), nozzle(r[0], "N2"), PipeProperties::Tracing);
			QPainterPath back_hatches, back_dashes;
			back->insulationPaths(&back_hatches, &back_dashes);
			QCOMPARE(lines(back_dashes), 2);
			QVERIFY(back->mapRectToScene(back_dashes.boundingRect()).top()
					> nozzle(r[0], "N2")->dockConductor().y());

			insulate(pipe, PipeProperties::Insulation, true, QStringLiteral("Q 80"));
			pipe->insulationPaths(&hatches, &dashes);
			QCOMPARE(lines(hatches), 3 * 7);
			QRectF text;
			const QList<QPointF> mark = pipe->insulationMark(&text);
			QCOMPARE(mark.size(), 2);
			const QPointF centre = pipe->mapToScene((mark.first() + mark.last()) / 2);
			QCOMPARE(centre.x(), 100.0);
			QVERIFY(!text.isNull());
			QVERIFY(text.bottom() < (mark.first().y() - PidInsulation::half_width));
			QVERIFY(pipe->boundingRect().contains(text));
				// no text, no rectangle; no mark asked, none
			insulate(pipe, PipeProperties::Insulation, true);
			pipe->insulationMark(&text);
			QVERIFY(text.isNull());
			insulate(pipe, PipeProperties::Insulation, false, QStringLiteral("Q 80"));
			QVERIFY(pipe->insulationMark().isEmpty());

			insulate(pipe, PipeProperties::NoInsulation, true, QStringLiteral("Q 80"));
			pipe->insulationPaths(&hatches, &dashes);
			QVERIFY(hatches.isEmpty() && dashes.isEmpty());
			QVERIFY(pipe->insulationMark().isEmpty());

				// a signal line has none, whatever its data say
			const auto s = row({m_signals, m_signals});
			auto *line = new Conductor(nozzle(s[0], "N2"), nozzle(s[1], "N1"));
			m_folio->addItem(line);
			QCOMPARE(line->medium(), TerminalData::Signal);
			ConductorProperties data = line->properties();
			data.m_pipe.insulation_kind = PipeProperties::Insulation;
			line->setProperties(data);
			line->insulationPaths(&hatches, &dashes);
			QVERIFY(hatches.isEmpty());
			QVERIFY(!line->insulationEndsAt(nozzle(s[0], "N2")));
		}

			// The bounding rectangle of a pipe with a mark asks nothing of the
			// folio: a nozzle which draws its mark looks in the folio for a
			// nozzle against it, and the folio asked the rectangle again while
			// it indexed the pipe, without end (crash of the hand test of the
			// 21/11). It holds the text of the mark, whatever the side.
		void theBoundsOfAMarkAskNothingOfTheFolio()
		{
			const auto e = row({m_equipment, m_equipment});
			e[0]->setNozzleMarks({{QStringLiteral("N2"), PipeConnection::MarkChoice::Always}});
			e[1]->setNozzleMarks({{QStringLiteral("N1"), PipeConnection::MarkChoice::Always}});
			Conductor *pipe = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"), PipeProperties::Insulation);
			insulate(pipe, PipeProperties::Insulation, true, QStringLiteral("Q 80"));
				// the folio indexes the pipe: no recursion
			QVERIFY(!m_folio->items(nozzle(e[0], "N2")->dockConductor()).isEmpty());
			QRectF text;
			const QList<QPointF> mark = pipe->insulationMark(&text);
			QCOMPARE(mark.size(), 2);
			QVERIFY(m_folio->items(pipe->mapToScene((mark.first() + mark.last()) / 2)).contains(pipe));
			QVERIFY(!text.isNull());
			QVERIFY(pipe->boundingRect().contains(text));
		}

			// The mark in the middle and the designation, both on the middle
			// of the longest segment, touched (hand test of 21/11): the mark
			// goes past the designation, its text on the other side
		void theMarkGoesPastTheDesignation()
		{
			PidCodification codes = m_project.pidCodification();
			const PidCodification kept = codes;
			codes.tag_models.insert(PidLineDesignation::modelKey(), QStringLiteral("{insulation}"));
			m_project.setPidCodification(codes);
			const auto e = row({m_equipment, m_equipment});
			Conductor *pipe = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"));
			ConductorProperties data = pipe->properties();
			data.m_show_text = true;
			data.m_pipe.insulation_kind = PipeProperties::Insulation;
			data.m_pipe.insulation_mark = true;
			data.m_pipe.insulation = QStringLiteral("Q 80");
			pipe->setProperties(data);
			QVERIFY(pipe->textItem()->isVisible());
			const QRectF designation = pipe->mapRectFromItem(pipe->textItem(),
															 pipe->textItem()->boundingRect());
			QRectF text;
			const QList<QPointF> mark = pipe->insulationMark(&text);
			QCOMPARE(mark.size(), 2);
				// past it along the pipe
			QVERIFY(qMin(mark.first().x(), mark.last().x()) > designation.right());
				// its text on the other side of the pipe, in the bounds
			QVERIFY(!text.isNull());
			QVERIFY((text.center().y() - mark.first().y())
					* (designation.center().y() - mark.first().y()) < 0);
			QVERIFY(pipe->boundingRect().contains(text));
				// a designation moved by hand moves the mark, never the
				// bounds, of which the folio would not be told
			const QRectF bounds = pipe->boundingRect();
			pipe->textItem()->setPos(pipe->textItem()->pos() + QPointF(40, 0));
			QCOMPARE(pipe->boundingRect(), bounds);
			QVERIFY(pipe->insulationMark() != mark);
			QVERIFY(pipe->insulationMark(&text).size() == 2 && bounds.contains(text));
				// without the designation: in the middle again
			data.m_show_text = false;
			pipe->setProperties(data);
			const QList<QPointF> alone = pipe->insulationMark();
			QCOMPARE(pipe->mapToScene((alone.first() + alone.last()) / 2).x(), 100.0);
				// a pipe drawn from right to left: at the right of its
				// designation all the same, as it reads
			const auto r = row({m_equipment, m_equipment});
			Conductor *back = draw(nozzle(r[1], "N1"), nozzle(r[0], "N2"));
			data = back->properties();
			data.m_show_text = true;
			data.m_pipe.insulation_kind = PipeProperties::Insulation;
			data.m_pipe.insulation_mark = true;
			data.m_pipe.insulation = QStringLiteral("Q 80");
			back->setProperties(data);
			const QRectF its = back->mapRectFromItem(back->textItem(), back->textItem()->boundingRect());
			const QList<QPointF> back_mark = back->insulationMark();
			QCOMPARE(back_mark.size(), 2);
			QVERIFY(back->mapToScene(back_mark.first()).x() > back->mapToScene(its.topRight()).x()
					&& back->mapToScene(back_mark.last()).x() > back->mapToScene(its.topRight()).x());
			m_project.setPidCodification(kept);
		}

			// Painted in the colour the project gives the stubs, on paper as
			// on the screen; in that of the pipe without one
		void theStubsArePaintedInTheirColour()
		{
			const auto e = row({m_equipment, m_equipment});
			Conductor *pipe = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"), PipeProperties::Insulation);
			auto redPixels = [pipe]() {
				const QRectF area = pipe->boundingRect();
				QImage image(area.size().toSize(), QImage::Format_RGB32);
				image.fill(Qt::white);
				QPainter painter(&image);
				painter.translate(-area.topLeft());
				QStyleOptionGraphicsItem option;
				pipe->paint(&painter, &option, nullptr);
				painter.end();
				int count = 0;
				for (int y = 0 ; y < image.height() ; ++y) {
					for (int x = 0 ; x < image.width() ; ++x) {
						const QColor c(image.pixel(x, y));
						if (c.red() > 200 && c.green() < 80 && c.blue() < 80) ++count;
					}
				}
				return count;
			};
			QCOMPARE(redPixels(), 0);
			PidCodification codes = m_project.pidCodification();
			codes.object_colors.insert(PidCodification::insulationColorKey(), QColor(Qt::red));
			m_project.setPidCodification(codes);
			QVERIFY(redPixels() > 20);
			insulate(pipe, PipeProperties::NoInsulation);
			QCOMPARE(redPixels(), 0);
			codes.object_colors.remove(PidCodification::insulationColorKey());
			m_project.setPidCodification(codes);
		}
};

QET_APP_TEST_MAIN(tst_insulation)
#include "tst_insulation.moc"
