// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "diagramcontent.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidinsulation.h"
#include "pidlinedesignation.h"
#include "pidoffpage.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "ui/pipepropertieswidget.h"
#include "undocommand/deleteqgraphicsitemcommand.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDomDocument>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QUndoStack>

// Step 8r-1: the jacket of a double containment pipe, "PTFE 4x6 in PVC
// Ø 63". A second material and size on the same pipe, passed on along the
// line as the insulation, up to a jacket limit or equipment; drawn as two
// lines either side of the pipe where the jacket begins and ends, or along
// the whole pipe; written by {jacket} in the designation; its colour a
// setting of the project.
class tst_jacket : public QObject
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
	ElementsLocation m_valve, m_tee, m_equipment, m_limit, m_insulation_limit, m_wires;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/jacket"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/jacket/") + file, &m_project);
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

		// A pipe in PVC Ø 63 (M05), or none with @p jacketed false; the data
		// given after it is connected, as a file read gives them
	Conductor *draw(Terminal *from, Terminal *to, bool jacketed = false)
	{
		auto *pipe = new Conductor(from, to);
		m_folio->addItem(pipe);
		jacket(pipe, jacketed);
		return pipe;
	}

	static void jacket(Conductor *pipe, bool jacketed, bool full = false)
	{
		ConductorProperties data = pipe->properties();
		data.m_pipe.takeJacket(PipeProperties());
		if (jacketed) {
			data.m_pipe.jacket_material = QStringLiteral("M05");
			data.m_pipe.jacket_outside = PipeDiameter::outside(63);
			data.m_pipe.jacket_nominal = PipeDiameter::nominal(50);
		}
		data.m_pipe.jacket_full = full;
		pipe->setProperties(data);
	}

		// The lines of a path: each starts with a moveTo
	static int lines(const QPainterPath &path)
	{
		int count = 0;
		for (int i = 0 ; i < path.elementCount() ; ++i) {
			if (path.elementAt(i).isMoveTo()) ++count;
		}
		return count;
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("jacket"), NamesList());
			QDomDocument a, b, c, d, e, f;
			m_valve = add("valve.elmt", definition(a, "a1111111-1111-1111-1111-111111111111",
												   "pid_inline=\"true\" pid_family=\"valve\" "));
			m_tee = add("tee.elmt", definition(b, "a2222222-2222-2222-2222-222222222222",
											   "pid_run=\"N1 N2 N3\" pid_family=\"pipe\" ", true));
			m_equipment = add("equipment.elmt", definition(c, "a3333333-3333-3333-3333-333333333333",
														   "pid_family=\"equipment\" "));
			m_limit = add("limit.elmt", definition(d, "a4444444-4444-4444-4444-444444444444",
												   "pid_inline=\"true\" pid_family=\"pipe\" "
												   "pid_role=\"jacket_limit\" "));
			m_insulation_limit = add("insulation_limit.elmt",
									 definition(e, "a5555555-5555-5555-5555-555555555555",
												"pid_inline=\"true\" pid_family=\"pipe\" "
												"pid_role=\"insulation_limit\" "));
			m_wires = add("wires.elmt", definition(f, "a6666666-6666-6666-6666-666666666666",
												   QString(), false, QStringLiteral("electrical")));
			QVERIFY(m_valve.exist() && m_tee.exist() && m_equipment.exist() && m_limit.exist()
					&& m_insulation_limit.exist() && m_wires.exist());
				// PVC named by its outside diameter, Ø 63 a DN 50, Ø 110 a
				// DN 100; steel by its DN
			int skipped = 0;
			const PidCodification codes = PidCodification::fromCsv(QStringLiteral(
				"material;M01;Steel\nmaterial;M05;PVC;od\nsize;M05;63;50\nsize;M05;110;100\n"
				"size;M01;;50\nsize;M01;;100\nsize;M01;;150\n"), &skipped);
			QCOMPARE(skipped, 0);
			m_project.setPidCodification(codes);
			m_folio = m_project.addNewDiagram();
		}

			// The attributes of the file: only those set, read back; a pipe
			// is jacketed by its material or its size, not by the drawing
			// along its length alone
		void theJacketIsWrittenOnlyWhenSet()
		{
			PipeProperties p;
			QVERIFY(!p.jacketed());
			p.jacket_full = true;
			QVERIFY(!p.jacketed());
			auto names = [](const PipeProperties &p) {
				QStringList list;
				for (const auto &a : p.attributes()) list << a.first;
				return list;
			};
			QCOMPARE(names(p), QStringList({QStringLiteral("pid_jacket_full")}));
			p.jacket_full = false;
			QVERIFY(names(p).isEmpty());
			p.jacket_material = QStringLiteral("M05");
			p.jacket_outside = PipeDiameter::outside(63);
			p.jacket_nominal = PipeDiameter::nominal(50);
			QVERIFY(p.jacketed());
			QCOMPARE(names(p), QStringList({QStringLiteral("pid_jacket_material"), QStringLiteral("pid_jacket_dn"),
											QStringLiteral("pid_jacket_od")}));
			QDomDocument doc;
			QDomElement e = doc.createElement(QStringLiteral("conductor"));
			for (const auto &a : p.attributes()) e.setAttribute(a.first, a.second);
			QCOMPARE(e.attribute(QStringLiteral("pid_jacket_od")), QStringLiteral("63"));
			PipeProperties read;
			read.fromXml(e);
			QCOMPARE(read, p);
			QVERIFY(read.sameJacket(p));
				// a size alone makes a jacket too
			PipeProperties sized;
			sized.jacket_nominal = PipeDiameter::nominal(100);
			QVERIFY(sized.jacketed());
			QVERIFY(!sized.sameJacket(p));
		}

			// equipment ─p1─ valve ─p2─ equipment, jacketed alike: two lines at
			// each piece of equipment, none at the valve; at a jacket limit on
			// both sides; an insulation limit stops the insulation only
		void theStubsAreWhereTheJacketEnds()
		{
			const auto e = row({m_equipment, m_valve, m_equipment});
			Conductor *p1 = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"), true);
			Conductor *p2 = draw(nozzle(e[1], "N2"), nozzle(e[2], "N1"), true);
			QVERIFY(p1->jacketEndsAt(nozzle(e[0], "N2")));
			QVERIFY(!p1->jacketEndsAt(nozzle(e[1], "N1")));
			QVERIFY(!p2->jacketEndsAt(nozzle(e[1], "N2")));
			QVERIFY(p2->jacketEndsAt(nozzle(e[2], "N1")));
				// one stub of two lines
			QCOMPARE(lines(p1->jacketPath()), 2);
				// another jacket beyond: it ends there
			ConductorProperties data = p2->properties();
			data.m_pipe.jacket_outside = PipeDiameter::outside(110);
			p2->setProperties(data);
			QVERIFY(p1->jacketEndsAt(nozzle(e[1], "N1")));
			QCOMPARE(lines(p1->jacketPath()), 4);
				// along the whole pipe: two lines, from nozzle to nozzle
			jacket(p1, true, true);
			const QPainterPath full = p1->jacketPath();
			QCOMPARE(lines(full), 2);
			const QRectF scene = p1->mapRectToScene(full.boundingRect());
			QVERIFY(qAbs(scene.left() - nozzle(e[0], "N2")->dockConductor().x()) < 3);
			QVERIFY(qAbs(scene.right() - nozzle(e[1], "N1")->dockConductor().x()) < 3);
				// 1.5 mm either side of the axis
			QCOMPARE(scene.height(), 2 * PidInsulation::half_width);
				// none without a jacket
			jacket(p1, false, true);
			QVERIFY(p1->jacketPath().isEmpty());

				// a jacket limit: a stub on both sides
			const auto l = row({m_equipment, m_limit, m_equipment});
			Conductor *q1 = draw(nozzle(l[0], "N2"), nozzle(l[1], "N1"), true);
			Conductor *q2 = draw(nozzle(l[1], "N2"), nozzle(l[2], "N1"), true);
			QVERIFY(l[1]->isJacketLimit() && l[1]->isLineMark() && !l[1]->isInsulationLimit());
			QVERIFY(q1->jacketEndsAt(nozzle(l[1], "N1")));
			QVERIFY(q2->jacketEndsAt(nozzle(l[1], "N2")));
				// drawn into its middle, as the stubs of an insulation
			const qreal middle = l[1]->pos().x();
			QVERIFY(qAbs(q1->mapRectToScene(q1->jacketPath().boundingRect()).right() - middle) < 0.01);
				// it stops no insulation, which goes through it
			QVERIFY(!q1->inlineRun(Conductor::LineReach::Jacket).contains(q2));
			QVERIFY(q1->inlineRun(Conductor::LineReach::Insulation).contains(q2));

				// an insulation limit: the jacket goes through it
			const auto i = row({m_equipment, m_insulation_limit, m_equipment});
			Conductor *r1 = draw(nozzle(i[0], "N2"), nozzle(i[1], "N1"), true);
			Conductor *r2 = draw(nozzle(i[1], "N2"), nozzle(i[2], "N1"), true);
			QVERIFY(!r1->jacketEndsAt(nozzle(i[1], "N1")));
			QVERIFY(r1->inlineRun(Conductor::LineReach::Jacket).contains(r2));
			QVERIFY(!r1->inlineRun(Conductor::LineReach::Insulation).contains(r2));
		}

			// Insulated too: the jacket inside, the band of the insulation
			// around it, wider, and a heat tracing outside the jacket
		void anInsulationGoesAroundTheJacket()
		{
			const QList<QPointF> straight{QPointF(0, 0), QPointF(32, 0)};
			QCOMPARE(PidInsulation::band(straight, true).boundingRect().top(),
					 -PidInsulation::jacketed_half_width);
			QCOMPARE(PidInsulation::band(straight).boundingRect().top(), -PidInsulation::half_width);
			QCOMPARE(PidInsulation::tracing(straight, false, true).elementAt(0).y,
					 PidInsulation::jacketed_tracing);
			QCOMPARE(PidInsulation::tracing(straight, true, true).elementAt(0).y,
					 PidInsulation::jacketed_tracing);
			QVERIFY(PidInsulation::jacketed_tracing > PidInsulation::half_width
					&& PidInsulation::jacketed_tracing < PidInsulation::jacketed_half_width);
			QCOMPARE(lines(PidInsulation::jacket(straight)), 2);

			const auto e = row({m_equipment, m_equipment});
			Conductor *pipe = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"), true);
			ConductorProperties data = pipe->properties();
			data.m_pipe.insulation_kind = PipeProperties::Insulation;
			pipe->setProperties(data);
			QPainterPath hatches, dashes;
			pipe->insulationPaths(&hatches, &dashes);
			QCOMPARE(pipe->mapRectToScene(hatches.boundingRect()).height(),
					 2 * PidInsulation::jacketed_half_width);
			QVERIFY(pipe->boundingRect().contains(pipe->mapRectFromScene(
					pipe->mapRectToScene(hatches.boundingRect()))));
		}

			// The jacket given on the page of a pipe goes to the whole line,
			// through a valve and a tee, up to a jacket limit or equipment;
			// its size picked in the series of its material, the DN with it;
			// "(None)" takes it away; the drawing along the whole length is
			// the pipe's own; one undo step each
		void theJacketGoesToTheWholeLine()
		{
				// equipment ─p1─ valve ─p2─ tee ─p3─ limit ─p4─ equipment,
				// the branch of the tee to equipment
			const auto e = row({m_equipment, m_valve, m_tee, m_limit, m_equipment});
			Element *below = place(m_equipment, e[2]->pos() + QPointF(0, 100));
			Conductor *p1 = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"));
			Conductor *p2 = draw(nozzle(e[1], "N2"), nozzle(e[2], "N1"));
			Conductor *p3 = draw(nozzle(e[2], "N2"), nozzle(e[3], "N1"));
			Conductor *p4 = draw(nozzle(e[3], "N2"), nozzle(e[4], "N1"));
			Conductor *branch = draw(nozzle(e[2], "N3"), nozzle(below, "N1"));

			PipePropertiesWidget page({p2});
			auto *material = page.findChild<QComboBox *>(QStringLiteral("jacket_material"));
			auto *size = page.findChild<QComboBox *>(QStringLiteral("jacket_size_list"));
			auto *full = page.findChild<QCheckBox *>(QStringLiteral("jacket_full"));
			QVERIFY(material && size && full);
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();
			const int pvc = material->findData(QStringLiteral("M05"));
			QVERIFY(pvc > 0);
			material->setCurrentIndex(pvc);
			emit material->activated(pvc);
				// the sizes of PVC offered, after "nothing"
			QCOMPARE(size->count(), 3);
			size->lineEdit()->setText(QStringLiteral("63"));
			size->lineEdit()->setModified(true);
			emit size->lineEdit()->editingFinished();
			full->click();
			QCOMPARE(stack.count(), steps + 3);
			for (Conductor *pipe : {p1, p2, p3, branch}) {
				const PipeProperties p = pipe->properties().m_pipe;
				QCOMPARE(p.jacket_material, QStringLiteral("M05"));
				QCOMPARE(p.jacket_outside, PipeDiameter::outside(63));
				QCOMPARE(p.jacket_nominal, PipeDiameter::nominal(50));
				QCOMPARE(p.jacket_full, pipe == p2);
			}
			QVERIFY(!p4->properties().m_pipe.jacketed());

				// a size which is not one of PVC: refused, said
			size->lineEdit()->setText(QStringLiteral("75"));
			size->lineEdit()->setModified(true);
			emit size->lineEdit()->editingFinished();
			QCOMPARE(p2->properties().m_pipe.jacket_outside, PipeDiameter::outside(63));
			QVERIFY(page.jacketMessage().contains(QStringLiteral("75")));

				// (None): no jacket, its size neither, on the whole line
			const int none = material->findData(QString());
			material->setCurrentIndex(none);
			emit material->activated(none);
			for (Conductor *pipe : {p1, p2, p3, branch}) {
				QVERIFY(!pipe->properties().m_pipe.jacketed());
			}
			stack.undo();
			QCOMPARE(p1->properties().m_pipe.jacket_outside, PipeDiameter::outside(63));
		}

			// A jacket not larger than its pipe is said on its page, nothing
			// refused: by outside diameters, or by DN
		void aJacketNotLargerThanItsPipeIsSaid()
		{
			const auto e = row({m_equipment, m_equipment});
			Conductor *pipe = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"), true);
			PipePropertiesWidget page({pipe});
				// a pipe without size: nothing to say
			QVERIFY(page.jacketMessage().isEmpty());
			ConductorProperties data = pipe->properties();
			data.m_pipe.material = QStringLiteral("M05");
			data.m_pipe.outside = PipeDiameter::outside(110);
			data.m_pipe.nominal = PipeDiameter::nominal(100);
			pipe->setProperties(data);
			QVERIFY(page.jacketMessage().contains(QStringLiteral("63")));
				// a steel pipe of DN 50 in Ø 63: the same DN
			data.m_pipe.material = QStringLiteral("M01");
			data.m_pipe.outside = PipeDiameter();
			data.m_pipe.nominal = PipeDiameter::nominal(50);
			pipe->setProperties(data);
			QVERIFY(!page.jacketMessage().isEmpty());
				// a smaller pipe: nothing
			data.m_pipe.material = QStringLiteral("M05");
			data.m_pipe.outside = PipeDiameter::outside(32);
			data.m_pipe.nominal = PipeDiameter();
			pipe->setProperties(data);
			QVERIFY(page.jacketMessage().isEmpty());
		}

			// A new pipe takes the jacket of its line, not the drawing along
			// the whole length; none from beyond a jacket limit
		void aNewPipeTakesTheJacketOfItsLine()
		{
			auto connect = [this](Terminal *from, Terminal *to) {
				auto *pipe = new Conductor(from, to);
				m_folio->addItem(pipe);
				return pipe;
			};
			const auto t = row({m_equipment, m_tee, m_valve, m_limit, m_equipment});
			Conductor *left = connect(nozzle(t[0], "N2"), nozzle(t[1], "N1"));
			QVERIFY(!left->properties().m_pipe.jacketed());
			jacket(left, true, true);
			Element *below = place(m_equipment, t[1]->pos() + QPointF(0, 100));
			Conductor *branch = connect(nozzle(t[1], "N3"), nozzle(below, "N1"));
			Conductor *on = connect(nozzle(t[1], "N2"), nozzle(t[2], "N1"));
			Conductor *to_limit = connect(nozzle(t[2], "N2"), nozzle(t[3], "N1"));
			for (Conductor *pipe : {branch, on, to_limit}) {
				const PipeProperties p = pipe->properties().m_pipe;
				QVERIFY(p.sameJacket(left->properties().m_pipe));
				QVERIFY(!p.jacket_full);
			}
			Conductor *beyond = connect(nozzle(t[3], "N2"), nozzle(t[4], "N1"));
			QVERIFY(!beyond->properties().m_pipe.jacketed());
		}

			// A jacket limit deleted: the jacketed side gives its jacket to
			// the line joined; two jackets: that of the N1 side, said
		void aJacketLimitDeletedSpreadsTheJacket()
		{
			const auto e = row({m_equipment, m_valve, m_limit, m_valve, m_equipment});
			Conductor *p1 = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"));
			draw(nozzle(e[1], "N2"), nozzle(e[2], "N1"));
			Conductor *p3 = draw(nozzle(e[2], "N2"), nozzle(e[3], "N1"), true);
			Conductor *p4 = draw(nozzle(e[3], "N2"), nozzle(e[4], "N1"), true);
			auto remove = [this](Element *limit) {
				m_folio->clearSelection();
				limit->setSelected(true);
				auto *del = new DeleteQGraphicsItemCommand(m_folio, DiagramContent(m_folio, true));
				const QString note = del->note();
				m_folio->undoStack().push(del);
				return note;
			};
			QVERIFY(remove(e[2]).isEmpty());
			Conductor *line = nozzle(e[1], "N2")->conductors().value(0);
			QVERIFY(line && line != p3);
			for (Conductor *pipe : {p1, line, p4}) {
				QVERIFY(pipe->properties().m_pipe.sameJacket(p4->properties().m_pipe));
				QVERIFY(pipe->properties().m_pipe.jacketed());
			}
			m_folio->undoStack().undo();
			QVERIFY(!p1->properties().m_pipe.jacketed());

				// two jackets: that of the N1 side, said
			const auto f = row({m_equipment, m_limit, m_equipment});
			Conductor *q1 = draw(nozzle(f[0], "N2"), nozzle(f[1], "N1"), true);
			Conductor *q2 = draw(nozzle(f[1], "N2"), nozzle(f[2], "N1"));
			ConductorProperties data = q2->properties();
			data.m_pipe.jacket_material = QStringLiteral("M01");
			data.m_pipe.jacket_nominal = PipeDiameter::nominal(150);
			q2->setProperties(data);
			const PipeProperties kept = q1->properties().m_pipe;
			QVERIFY(!remove(f[1]).isEmpty());
			Conductor *joined = nozzle(f[0], "N2")->conductors().value(0);
			QVERIFY(joined && joined->properties().m_pipe.sameJacket(kept));
		}

			// The designation: {jacket} the code of the material and the
			// size, {jacket_name} its name, a word after a colon first;
			// nothing without a jacket, the word neither
		void theDesignationWritesTheJacket()
		{
			const PidCodification codes = m_project.pidCodification();
			PipeProperties p;
			p.fluid = QStringLiteral("W01");
			const QString model = QStringLiteral("{fluid} {jacket_name:in} | {jacket} | {jacket:/}");
			QCOMPARE(PidLineDesignation::text(model, p, codes), QStringLiteral("W01 | |"));
			p.jacket_material = QStringLiteral("M05");
			p.jacket_outside = PipeDiameter::outside(63);
			p.jacket_nominal = PipeDiameter::nominal(50);
			const QString o = QString(PipeDiameter::outsideSign());
			QCOMPARE(PidLineDesignation::text(model, p, codes),
					 QStringLiteral("W01 in PVC %1 63 | M05 %1 63 | / M05 %1 63").arg(o));
				// a material named by its DN writes the DN
			p.jacket_material = QStringLiteral("M01");
			p.jacket_outside = PipeDiameter();
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{jacket}"), p, codes),
					 QStringLiteral("M01 DN 50"));
				// an unknown parameter of another field stays as it is
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{fluid:x}"), p, codes),
					 QStringLiteral("{fluid:x}"));
		}

			// Two off-page connectors: a jacket which differs is a difference
		void anOffPageConnectorComparesTheJacket()
		{
			PipeProperties here, there;
			QVERIFY(PidOffPage::differences(here, there).isEmpty());
			there.jacket_material = QStringLiteral("M05");
			there.jacket_outside = PipeDiameter::outside(63);
			QCOMPARE(PidOffPage::differences(here, there).size(), 2);
		}

			// Painted in the colour the project gives the jackets; a status
			// of works comes first; nothing on a wire, whatever its data say
		void theJacketIsPaintedInItsColour()
		{
			const auto e = row({m_equipment, m_equipment});
			Conductor *pipe = draw(nozzle(e[0], "N2"), nozzle(e[1], "N1"), true);
			auto pixels = [](Conductor *line, auto test) {
				const QRectF area = line->boundingRect();
				QImage image(area.size().toSize(), QImage::Format_RGB32);
				image.fill(Qt::white);
				QPainter painter(&image);
				painter.translate(-area.topLeft());
				QStyleOptionGraphicsItem option;
				line->paint(&painter, &option, nullptr);
				painter.end();
				int count = 0;
				for (int y = 0 ; y < image.height() ; ++y) {
					for (int x = 0 ; x < image.width() ; ++x) {
						if (test(QColor(image.pixel(x, y)))) ++count;
					}
				}
				return count;
			};
			auto red = [](const QColor &c) { return c.red() > 200 && c.green() < 80 && c.blue() < 80; };
			auto blue = [](const QColor &c) { return c.blue() > 200 && c.green() < 80 && c.red() < 80; };
			QCOMPARE(pixels(pipe, red), 0);
			PidCodification codes = m_project.pidCodification();
			const PidCodification kept = codes;
			codes.object_colors.insert(PidCodification::jacketColorKey(), QColor(Qt::red));
			codes.status_colors.insert(QStringLiteral("demolished"), QColor(Qt::blue));
			m_project.setPidCodification(codes);
			QVERIFY(pixels(pipe, red) > 20);
				// a status of works: its colour, the jacket too
			ConductorProperties data = pipe->properties();
			data.m_pipe.status = QStringLiteral("demolished");
			pipe->setProperties(data);
			QCOMPARE(pixels(pipe, red), 0);
			QVERIFY(pixels(pipe, blue) > 20);
			data.m_pipe.status.clear();
			pipe->setProperties(data);
			jacket(pipe, false);
			QCOMPARE(pixels(pipe, red), 0);

				// a wire: no jacket drawn, nothing passed on
			const auto w = row({m_wires, m_wires});
			auto *wire = new Conductor(nozzle(w[0], "N2"), nozzle(w[1], "N1"));
			m_folio->addItem(wire);
			QCOMPARE(wire->medium(), TerminalData::Electrical);
			jacket(wire, true, true);
			QVERIFY(wire->jacketPath().isEmpty());
			QVERIFY(!wire->jacketEndsAt(nozzle(w[0], "N2")));
			m_project.setPidCodification(kept);
		}
};

QET_APP_TEST_MAIN(tst_jacket)
#include "tst_jacket.moc"
