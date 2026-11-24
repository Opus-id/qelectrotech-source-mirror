// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "pidoffpage.h"
#include "pipeproperties.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "undocommand/linkelementcommand.h"

#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

// Step 8c-3: the off-page connectors of a pipe. Each side of a pair keeps
// the data of its own pipe; a connector whose pipe differs from its twin's,
// or which has no twin, is veiled in orange and says why in its tool tip.
// The twin's pipe is read from the project database while its folio is
// not built: nothing is built to compare. Battery limits and electrical
// folio reports are never marked.
class tst_pidoffpage : public QObject
{
	Q_OBJECT

		// A symbol of @p link_type with one terminal N1, of @p medium
		// (none: electrical)
	static Element *symbol(Diagram *folio, const QString &link_type, const QString &medium)
	{
		QDomDocument doc;
		const QString name = QUuid::createUuid().toString(QUuid::Id128);
		const QString medium_attribute = medium.isEmpty()
				? QString() : QStringLiteral(" medium=\"%1\"").arg(medium);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"%1\" "
			"width=\"100\" height=\"30\" hotspot_x=\"0\" hotspot_y=\"15\">"
			"<names><name lang=\"en\">S</name></names><description>"
			"<polygon x1=\"0\" y1=\"-12\" x2=\"88\" y2=\"-12\" x3=\"100\" y3=\"0\" "
			"x4=\"88\" y4=\"12\" x5=\"0\" y5=\"12\" closed=\"true\"/>"
			"<terminal name=\"N1\" x=\"0\" y=\"0\" orientation=\"w\" type=\"Generic\" uuid=\"%2\"%3/>"
			"</description></definition>")
			.arg(link_type, QUuid::createUuid().toString(), medium_attribute));
		if (!folio->project()->embeddedElementCollection()->addElementDefinition(
				QStringLiteral("import"), name, doc.documentElement()))
			return nullptr;
		int state = 0;
		auto *element = ElementFactory::Instance()->createElement(
				ElementsLocation("embed://import/" + name + ".elmt", folio->project()),
				nullptr, &state);
		if (state || !element) return nullptr;
		folio->addItem(element);
		return element;
	}

		// A pipe from an equipment to @p report, carrying @p pipe
	static Conductor *pipeTo(Diagram *folio, Element *report, const PipeProperties &pipe)
	{
		Element *equipment = symbol(folio, QStringLiteral("simple"), QStringLiteral("pipe"));
		if (!equipment) return nullptr;
		equipment->setPos(-300, 0);
		auto *wire = new Conductor(equipment->terminals().first(), report->terminals().first());
		folio->addItem(wire);
		setPipe(wire, pipe);
		return wire;
	}

	static void setPipe(Conductor *wire, const PipeProperties &pipe)
	{
		ConductorProperties properties = wire->properties();
		properties.m_pipe = pipe;
		wire->setProperties(properties);
	}

	static PipeProperties line()
	{
		PipeProperties pipe;
		pipe.nominal = PipeDiameter::nominal(100);
		pipe.material = QStringLiteral("AC");
		pipe.fluid = QStringLiteral("EB");
		pipe.line_number = QStringLiteral("L01");
		return pipe;
	}

		// The project of two folios, a pair of pipe off-page connectors
		// between them, the same pipe on both sides but its fluid on the
		// second: saved in @p directory, its file returned
	static QString savedPair(const QTemporaryDir &directory, QUuid *out_uuid)
	{
		QETProject project;
		Diagram *first = project.addNewDiagram();
		Diagram *second = project.addNewDiagram();
		Element *out = symbol(first, QStringLiteral("next_report"), QStringLiteral("pipe"));
		Element *in = symbol(second, QStringLiteral("previous_report"), QStringLiteral("pipe"));
		if (!out || !in) return QString();
		pipeTo(first, out, line());
		PipeProperties other = line();
		other.fluid = QStringLiteral("ER");
		pipeTo(second, in, other);
		out->linkToElement(in);
		*out_uuid = out->uuid();
		QFile file(directory.filePath(QStringLiteral("offpage.qet")));
		if (!file.open(QIODevice::WriteOnly)) return QString();
		file.write(project.toXml().toByteArray());
		file.close();
		return file.fileName();
	}

	static Element *find(Diagram *folio, const QUuid &uuid)
	{
		for (Element *element : folio->elements())
			if (element->uuid() == uuid) return element;
		return nullptr;
	}

	private slots:
		void initTestCase() { QETProject::setBackupEnabled(false); }

			// A folio report with a nozzle; not a battery limit (a simple
			// symbol), nor a folio report of wires
		void aPipeReportHasANozzle()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *pipe = symbol(folio, QStringLiteral("next_report"), QStringLiteral("pipe"));
			Element *wire = symbol(folio, QStringLiteral("next_report"), QString());
			Element *limit = symbol(folio, QStringLiteral("simple"), QStringLiteral("pipe"));
			QVERIFY(pipe && wire && limit);
			QVERIFY(PidOffPage::isPipeReport(pipe));
			QVERIFY(!PidOffPage::isPipeReport(wire));
			QVERIFY(!PidOffPage::isPipeReport(limit));
			QVERIFY(!PidOffPage::isPipeReport(nullptr));
		}

			// Without a twin: said; a folio report of wires and a battery
			// limit never; linked, no more
		void aConnectorWithoutATwinIsMarked()
		{
			QETProject project;
			Diagram *first = project.addNewDiagram();
			Diagram *second = project.addNewDiagram();
			Element *out = symbol(first, QStringLiteral("next_report"), QStringLiteral("pipe"));
			Element *in = symbol(second, QStringLiteral("previous_report"), QStringLiteral("pipe"));
			Element *wire = symbol(first, QStringLiteral("next_report"), QString());
			Element *limit = symbol(first, QStringLiteral("simple"), QStringLiteral("pipe"));
			QVERIFY(out && in && wire && limit);
			pipeTo(first, out, line());
			pipeTo(second, in, line());

			QCOMPARE(PidOffPage::warnings(out).size(), 1);
			QVERIFY(PidOffPage::warnings(out).first().contains(QStringLiteral("without a twin")));
			QVERIFY(PidOffPage::warnings(wire).isEmpty());
			QVERIFY(PidOffPage::warnings(limit).isEmpty());

			out->linkToElement(in);
			QCOMPARE(PidOffPage::twin(out), in->uuid());
			QCOMPARE(PidOffPage::twin(in), out->uuid());
			QVERIFY(PidOffPage::warnings(out).isEmpty());
			QVERIFY(PidOffPage::warnings(in).isEmpty());

				// Unlinked again, by an undoable command: without a twin
			auto *unlink = new LinkElementCommand(out);
			unlink->unlinkAll();
			project.undoStack()->push(unlink);
			QVERIFY(PidOffPage::twin(out).isNull());
			QCOMPARE(PidOffPage::warnings(in).size(), 1);
			project.undoStack()->undo();
			QVERIFY(PidOffPage::warnings(out).isEmpty());
		}

			// The data compared, one line each; filled on one side and
			// empty on the other is a difference; the others are not
			// compared
		void theDifferencesAreListed()
		{
			const PipeProperties here = line();
			QVERIFY(PidOffPage::differences(here, here).isEmpty());

			PipeProperties there = here;
			there.nominal = PipeDiameter::nominal(150);
			there.fluid.clear();
			there.nominal_pressure = QStringLiteral("PN16");
			there.flow = QStringLiteral("120 m3/h");
			there.status = QStringLiteral("existing");
			there.line_kind = PipeProperties::Secondary;
			there.insulation_kind = PipeProperties::Insulation;
			there.insulation = QStringLiteral("Q 80");
			const QStringList lines = PidOffPage::differences(here, there);
			QCOMPARE(lines.size(), 8);
			QCOMPARE(lines.at(0), QStringLiteral("Size: DN 100 here, DN 150 on the other side"));
			QVERIFY(lines.contains(QStringLiteral("Fluid: EB here, nothing on the other side")));
			QVERIFY(lines.contains(QStringLiteral("Nominal pressure: nothing here, PN16 on the other side")));
			QVERIFY(lines.contains(QStringLiteral("Flow: nothing here, 120 m3/h on the other side")));
			QVERIFY(lines.contains(QStringLiteral(
				"Kind of line: main flow line here, secondary flow line on the other side")));
			QVERIFY(lines.contains(QStringLiteral(
				"Status of works: To be built here, Existing, kept on the other side")));

				// Not compared: spool, finish, colour, mark of the insulation
			PipeProperties other = here;
			other.spool = QStringLiteral("S1");
			other.external_finish = QStringLiteral("RAL 5015");
			other.color = Qt::red;
			other.insulation_mark = true;
			QVERIFY(PidOffPage::differences(here, other).isEmpty());

				// The outside diameter, a second size
			other = here;
			other.outside = PipeDiameter::outside(110);
			QCOMPARE(PidOffPage::differences(here, other).size(), 1);
		}

			// Both folios built: the pipes differ, then agree, as the data of
			// one side changes; no pipe on one side: nothing to compare
		void thePipesOfAPairAreCompared()
		{
			QETProject project;
			Diagram *first = project.addNewDiagram();
			Diagram *second = project.addNewDiagram();
			Element *out = symbol(first, QStringLiteral("next_report"), QStringLiteral("pipe"));
			Element *in = symbol(second, QStringLiteral("previous_report"), QStringLiteral("pipe"));
			QVERIFY(out && in);
			out->linkToElement(in);
			Conductor *pipe = pipeTo(first, out, line());
			QVERIFY(pipe);
				// No pipe on the other side
			QVERIFY(PidOffPage::warnings(out).isEmpty());

			PipeProperties other = line();
			other.line_number = QStringLiteral("L02");
			Conductor *twin_pipe = pipeTo(second, in, other);
			QVERIFY(twin_pipe);
			QStringList warnings = PidOffPage::warnings(out);
			QCOMPARE(warnings.size(), 2);
			QCOMPARE(warnings.at(1), QStringLiteral("Line number: L01 here, L02 on the other side"));
			QCOMPARE(PidOffPage::warnings(in).at(1),
					 QStringLiteral("Line number: L02 here, L01 on the other side"));

			setPipe(twin_pipe, line());
			QVERIFY(PidOffPage::warnings(out).isEmpty());
			QVERIFY(PidOffPage::warnings(in).isEmpty());
		}

			// The twin on a folio not built: its pipe from the project
			// database, the folio stays not built; linked or not yet
		void theTwinOfAFolioNotBuiltIsReadFromTheDatabase_data()
		{
			QTest::addColumn<bool>("shown");
			QTest::newRow("built") << false;
			QTest::newRow("shown, twin built alone") << true;
		}

		void theTwinOfAFolioNotBuiltIsReadFromTheDatabase()
		{
			QFETCH(bool, shown);
			QTemporaryDir directory;
			QVERIFY(directory.isValid());
			QUuid out_uuid;
			const QString file = savedPair(directory, &out_uuid);
			QVERIFY(!file.isEmpty());

			const QByteArray previous = qgetenv("QET_EAGER_FOLIOS");
			qputenv("QET_EAGER_FOLIOS", "0");
			QETProject reopened(file);
			if (previous.isNull()) {
				qunsetenv("QET_EAGER_FOLIOS");
			} else {
				qputenv("QET_EAGER_FOLIOS", previous);
			}
			QCOMPARE(reopened.state(), QETProject::Ok);
			Diagram *first = reopened.folios().first();
			Diagram *second = reopened.folios().at(1);
			if (shown) {
				reopened.buildFolioToShow(first);
			} else {
				reopened.buildFolios({first});
			}
			QVERIFY(first->isLoaded());
			QVERIFY(!second->isLoaded());
			Element *out = find(first, out_uuid);
			QVERIFY(out);
			QCOMPARE(out->linkedElements().isEmpty(), !shown);
			QVERIFY(!PidOffPage::twin(out).isNull());

			const QStringList warnings = PidOffPage::warnings(out);
			QCOMPARE(warnings.size(), 2);
			QCOMPARE(warnings.at(1), QStringLiteral("Fluid: EB here, ER on the other side"));
			QVERIFY(!second->isLoaded());
			if (shown) {
					// The twin built alone compares too, from the database
				Element *in = out->linkedElements().first();
				QCOMPARE(PidOffPage::warnings(in).at(1),
						 QStringLiteral("Fluid: ER here, EB on the other side"));
				QVERIFY(!second->isLoaded());
			}
		}

			// The veil: the body and the point of the cartouche, on its right
		void theVeilFollowsTheCartouche()
		{
			const QPainterPath veil = PidOffPage::veil(QRectF(0, -12, 100, 24));
			QVERIFY(veil.contains(QPointF(50, 0)));
			QVERIFY(veil.contains(QPointF(97, 0)));
			QVERIFY(!veil.contains(QPointF(99, -11)));
			QVERIFY(!veil.contains(QPointF(99, 11)));
		}
};

int main(int argc, char **argv)
{
	return QetAppTest::run<tst_pidoffpage>(argc, argv);
}
#include "tst_pidoffpage.moc"
