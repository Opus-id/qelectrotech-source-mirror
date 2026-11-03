// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "conductorautonumerotation.h"
#include "conductorproperties.h"
#include "dataBase/projectdatabase.h"
#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "utils/conductorcreator.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

// The medium a conductor carries: electrical, as every conductor was, or a
// pipe, a signal, a drive, with the words of the terminals. A conductor
// takes it from the terminal it starts from, keeps it whatever properties
// are applied to it afterwards, and saves it only when it is not
// electrical; opening a file keeps what the file says.
class tst_conductormedium : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40 with two terminals N1 (west) and N2 (east), both
		// with the medium @p medium ("" for none)
	static QDomElement definition(QDomDocument &doc, const QString &uuid,
				      const QString &medium)
	{
		const QString attr = medium.isEmpty()
				? QString()
				: QStringLiteral(" medium=\"%1\"").arg(medium);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%2/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%2/>"
			"</description></definition>").arg(uuid, attr));
		return doc.documentElement();
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_electrical, m_pipe;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/cmedium"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/cmedium/") + file, &m_project);
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

		// Two symbols side by side on a row of their own; the east terminal
		// of the first, then the west terminal of the second
	QPair<Terminal *, Terminal *> pair(const ElementsLocation &first,
					   const ElementsLocation &second)
	{
		m_next_y += 100;
		Element *a = place(first, QPointF(0, m_next_y));
		Element *b = place(second, QPointF(200, m_next_y));
		if (!a || !b) return {};
		return {a->terminals().at(1), b->terminals().at(0)};
	}

		// The real binary's --resave from @p in to @p out, in a sandbox of
		// its own (so a running QElectroTech cannot answer instead)
	static bool resave(const QTemporaryDir &dir, const QString &in, const QString &out)
	{
		const QString home = dir.filePath(QStringLiteral("home"));
		QDir().mkpath(home);
		QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		env.insert(QStringLiteral("HOME"), home);
		env.insert(QStringLiteral("XDG_CONFIG_HOME"), home + QStringLiteral("/config"));
		env.insert(QStringLiteral("XDG_DATA_HOME"), home + QStringLiteral("/data"));
		QProcess proc;
		proc.setProcessEnvironment(env);
		proc.start(QStringLiteral(QET_TEST_BINARY_PATH), {QStringLiteral("--resave"), in, out});
		return proc.waitForFinished(120000) && proc.exitCode() == 0 && QFile::exists(out);
	}

		// A conductor drawn from @p from to @p to, on the folio
	Conductor *draw(Terminal *from, Terminal *to)
	{
		auto *conductor = new Conductor(from, to);
		m_folio->addItem(conductor);
		return conductor;
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("cmedium"), NamesList());
			QDomDocument a, b;
			m_electrical = add("electrical.elmt",
					   definition(a, "71111111-1111-1111-1111-111111111111", ""));
			m_pipe = add("pipe.elmt",
				     definition(b, "72222222-2222-2222-2222-222222222222", "pipe"));
			QVERIFY(m_electrical.exist() && m_pipe.exist());
			m_folio = m_project.addNewDiagram();
		}

			// ---- the properties ----
		void thePropertiesWriteTheMediumOnlyWhenNotElectrical()
		{
			QDomDocument doc;
			ConductorProperties electrical;
			QDomElement e = doc.createElement(QStringLiteral("conductor"));
			electrical.toXml(e);
			QVERIFY(!e.hasAttribute(QStringLiteral("medium")));

			ConductorProperties pipe;
			pipe.m_medium = TerminalData::Pipe;
			QDomElement p = doc.createElement(QStringLiteral("conductor"));
			pipe.toXml(p);
			QCOMPARE(p.attribute(QStringLiteral("medium")), QStringLiteral("pipe"));

				// read back
			ConductorProperties in;
			in.fromXml(p);
			QCOMPARE(in.m_medium, TerminalData::Pipe);
				// a word this version does not know: electrical
			p.setAttribute(QStringLiteral("medium"), QStringLiteral("steam"));
			in.fromXml(p);
			QCOMPARE(in.m_medium, TerminalData::Electrical);
				// the medium makes two sets of properties differ
			QVERIFY(pipe != electrical);
		}

			// ---- a new conductor ----
		void aConductorTakesTheMediumOfTheTerminalItStartsFrom_data()
		{
			QTest::addColumn<bool>("from_pipe");
			QTest::addColumn<bool>("to_pipe");
			QTest::addColumn<int>("expected");
			QTest::newRow("two electrical terminals") << false << false << int(TerminalData::Electrical);
			QTest::newRow("two nozzles")              << true  << true  << int(TerminalData::Pipe);
			QTest::newRow("from a nozzle")            << true  << false << int(TerminalData::Pipe);
			QTest::newRow("from an electrical one")   << false << true  << int(TerminalData::Electrical);
		}

		void aConductorTakesTheMediumOfTheTerminalItStartsFrom()
		{
			QFETCH(bool, from_pipe);
			QFETCH(bool, to_pipe);
			QFETCH(int, expected);
			const auto ends = pair(from_pipe ? m_pipe : m_electrical,
					       to_pipe ? m_pipe : m_electrical);
			QVERIFY(ends.first && ends.second);
			Conductor *conductor = draw(ends.first, ends.second);
			QCOMPARE(int(conductor->medium()), expected);
		}

			// The defaults of the folio, or the properties of any other
			// wire, never change the medium; setMedium() does
		void otherPropertiesKeepTheMedium()
		{
			const auto ends = pair(m_pipe, m_pipe);
			Conductor *pipe = draw(ends.first, ends.second);
			QCOMPARE(pipe->medium(), TerminalData::Pipe);

			ConductorProperties others = m_folio->defaultConductorProperties;
			others.color = Qt::blue;
			QCOMPARE(others.m_medium, TerminalData::Electrical);
			pipe->setProperties(others);
			QCOMPARE(pipe->medium(), TerminalData::Pipe);
			QCOMPARE(pipe->properties().color, QColor(Qt::blue));

			pipe->setMedium(TerminalData::Signal);
			QCOMPARE(pipe->medium(), TerminalData::Signal);
			QCOMPARE(pipe->properties().m_medium, TerminalData::Signal);
		}

			// ---- the file ----
			// Saved and read back with its medium; a conductor saved without
			// one is electrical, even between two nozzles: opening a file
			// changes nothing
		void theFileSaysTheMedium()
		{
			const auto ends = pair(m_pipe, m_pipe);
			Conductor *pipe = draw(ends.first, ends.second);
			QHash<Terminal *, int> ids;
			QDomDocument doc;
			QDomElement saved = pipe->toXml(doc, ids, {});
			QCOMPARE(saved.attribute(QStringLiteral("medium")), QStringLiteral("pipe"));

				// read again onto a conductor between the same nozzles
			m_folio->removeItem(pipe);
			delete pipe;
			auto *again = new Conductor(ends.first, ends.second);
			again->fromXml(saved);
			QCOMPARE(again->medium(), TerminalData::Pipe);
			delete again;

				// a wire of before, between two nozzles: stays a wire
			saved.removeAttribute(QStringLiteral("medium"));
			auto *old = new Conductor(ends.first, ends.second);
			QCOMPARE(old->medium(), TerminalData::Pipe);
			old->fromXml(saved);
			QCOMPARE(old->medium(), TerminalData::Electrical);
			delete old;
		}

			// ---- the potential ----
			// A pipe has no potential: the pipes of a tee share nothing.
			// (Two pipes on one nozzle can no longer be drawn, see
			// aNozzleTakesOnePipe(), but a file may still hold them.)
		void pipesHaveNoPotential()
		{
			m_next_y += 100;
			Element *a = place(m_pipe, QPointF(0, m_next_y));
			Element *b = place(m_pipe, QPointF(200, m_next_y));
			Element *c = place(m_pipe, QPointF(400, m_next_y));
			QVERIFY(a && b && c);
				// a tee on the west nozzle of b
			Conductor *one = draw(a->terminals().at(1), b->terminals().at(0));
			Conductor *two = draw(b->terminals().at(0), c->terminals().at(0));
			QCOMPARE(one->medium(), TerminalData::Pipe);
			QCOMPARE(two->medium(), TerminalData::Pipe);
			QVERIFY(one->relatedPotentialConductors().isEmpty());
			QVERIFY(one->relatedPotentialConductors(true).isEmpty());
			QVERIFY(two->relatedPotentialConductors().isEmpty());
			QVERIFY(one->functionSeparatedConductors().isEmpty());
		}

			// The potential of a wire never goes through a pipe, even one on
			// the same terminal; it still goes from wire to wire as before
		void aPotentialNeverGoesThroughAPipe()
		{
			m_next_y += 100;
			Element *e1 = place(m_electrical, QPointF(0, m_next_y));
			Element *e2 = place(m_electrical, QPointF(200, m_next_y));
			Element *e3 = place(m_electrical, QPointF(400, m_next_y));
			Element *p = place(m_pipe, QPointF(600, m_next_y));
			Element *e4 = place(m_electrical, QPointF(800, m_next_y));
			QVERIFY(e1 && e2 && e3 && p && e4);
			Terminal *hub = e2->terminals().at(0);
				// two wires meet on hub, a pipe drawn from a nozzle ends there
				// too, and a fourth wire goes on from the far nozzle
			Conductor *w1 = draw(e1->terminals().at(1), hub);
			Conductor *w2 = draw(hub, e3->terminals().at(0));
			Conductor *pipe = draw(p->terminals().at(0), hub);
			Conductor *w3 = draw(e4->terminals().at(0), p->terminals().at(0));
			QCOMPARE(w1->medium(), TerminalData::Electrical);
			QCOMPARE(pipe->medium(), TerminalData::Pipe);
			QCOMPARE(w3->medium(), TerminalData::Electrical);

			QCOMPARE(w1->relatedPotentialConductors(true), QSet<Conductor *>{w2});
			QCOMPARE(w2->relatedPotentialConductors(true), QSet<Conductor *>{w1});
			QVERIFY(w3->relatedPotentialConductors(true).isEmpty());
			QVERIFY(pipe->relatedPotentialConductors(true).isEmpty());

				// the project database follows the potential the same way
			projectDataBase *db = m_project.dataBase();
			db->updateDB();
			const auto of_w1 = db->potentialWires(w1->uuid());
			QVERIFY(of_w1.has_value());
			QCOMPARE(*of_w1, (QSet<QUuid>{w1->uuid(), w2->uuid()}));
			const auto of_pipe = db->potentialWires(pipe->uuid());
			QVERIFY(of_pipe.has_value());
			QCOMPARE(*of_pipe, QSet<QUuid>{pipe->uuid()});
			const auto of_w3 = db->potentialWires(w3->uuid());
			QVERIFY(of_w3.has_value());
			QCOMPARE(*of_w3, QSet<QUuid>{w3->uuid()});
		}

			// ---- what is only for wires ----
			// Properties with every wire field set
		static ConductorProperties wireFields()
		{
			ConductorProperties p;
			p.type = ConductorProperties::Single;
			p.m_definition_mark = true;
			p.m_cable = QStringLiteral("W1:bn");
			p.m_cable_uuid = QUuid::createUuid();
			p.m_cable_slot = 2;
			p.m_wire_color = QStringLiteral("BN");
			p.m_wire_section = QStringLiteral("1.5");
			p.m_tension_protocol = QStringLiteral("400V");
			p.m_bus = QStringLiteral("L1");
			p.m_function = QStringLiteral("power");
			p.m_bicolor = true;
				// and fields a pipe uses too
			p.color = Qt::blue;
			p.text = QStringLiteral("W01");
			p.cond_size = 2;
			return p;
		}

		static void checkNoWireFields(const ConductorProperties &p)
		{
			QCOMPARE(p.type, ConductorProperties::Multi);
			QVERIFY(!p.m_definition_mark);
			QVERIFY(p.m_cable.isEmpty());
			QVERIFY(p.m_cable_uuid.isNull());
			QCOMPARE(p.m_cable_slot, -1);
			QVERIFY(p.m_wire_color.isEmpty());
			QVERIFY(p.m_wire_section.isEmpty());
			QVERIFY(p.m_tension_protocol.isEmpty());
			QVERIFY(p.m_bus.isEmpty());
			QVERIFY(p.m_function.isEmpty());
			QVERIFY(!p.m_bicolor);
		}

			// A pipe given the properties of a wire keeps only what a pipe
			// uses; a wire keeps them all, as before
		void aPipeTakesNoneOfTheWireFields()
		{
			const auto nozzles = pair(m_pipe, m_pipe);
			Conductor *pipe = draw(nozzles.first, nozzles.second);
			pipe->setProperties(wireFields());
			checkNoWireFields(pipe->properties());
			QCOMPARE(pipe->properties().color, QColor(Qt::blue));
			QCOMPARE(pipe->properties().text, QStringLiteral("W01"));
			QCOMPARE(pipe->properties().cond_size, 2.0);
			QCOMPARE(pipe->medium(), TerminalData::Pipe);

			const auto wires = pair(m_electrical, m_electrical);
			Conductor *wire = draw(wires.first, wires.second);
			wire->setProperties(wireFields());
			QCOMPARE(wire->properties().type, ConductorProperties::Single);
			QCOMPARE(wire->properties().m_wire_color, QStringLiteral("BN"));
			QCOMPARE(wire->properties().m_cable, QStringLiteral("W1:bn"));
			QVERIFY(wire->properties().m_definition_mark);

				// a wire turned into a pipe loses them
			wire->setMedium(TerminalData::Pipe);
			checkNoWireFields(wire->properties());
			QCOMPARE(wire->properties().text, QStringLiteral("W01"));
		}

			// A cable is made of wires: a pipe is never one of its cores
		void aPipeIsNoCoreOfACable()
		{
			const auto nozzles = pair(m_pipe, m_pipe);
			Conductor *pipe = draw(nozzles.first, nozzles.second);
			const QUuid cable = QUuid::createUuid();
			pipe->setCableReference(QStringLiteral("W2:bk"), cable, 1);
			QVERIFY(pipe->properties().m_cable_uuid.isNull());
			QVERIFY(pipe->properties().m_cable.isEmpty());

			const auto wires = pair(m_electrical, m_electrical);
			Conductor *wire = draw(wires.first, wires.second);
			wire->setCableReference(QStringLiteral("W2:bk"), cable, 1);
			QCOMPARE(wire->properties().m_cable_uuid, cable);
		}

			// ---- one pipe per nozzle ----
			// A nozzle takes one pipe, whatever the wiring rules; an
			// electrical terminal still takes as many wires as before
		void aNozzleTakesOnePipe()
		{
			m_next_y += 100;
			Element *a = place(m_pipe, QPointF(0, m_next_y));
			Element *b = place(m_pipe, QPointF(200, m_next_y));
			Element *c = place(m_pipe, QPointF(400, m_next_y));
			QVERIFY(a && b && c);
			Terminal *nozzle = b->terminals().at(0);
			QCOMPARE(nozzle->wireLimit(), 1);
			QVERIFY(nozzle->hasRoomForWire());
			QVERIFY(a->terminals().at(1)->canBeLinkedTo(nozzle));
			draw(a->terminals().at(1), nozzle);
			QVERIFY(!nozzle->hasRoomForWire());
			QVERIFY(!c->terminals().at(0)->canBeLinkedTo(nozzle));
			QVERIFY(!nozzle->canBeLinkedTo(c->terminals().at(0)));
				// nor is it wired automatically
			for (const auto &pair : c->AlignedFreeTerminals())
				QVERIFY(pair.second != nozzle);

			m_next_y += 100;
			Element *e1 = place(m_electrical, QPointF(0, m_next_y));
			Element *e2 = place(m_electrical, QPointF(200, m_next_y));
			Element *e3 = place(m_electrical, QPointF(400, m_next_y));
			QVERIFY(e1 && e2 && e3);
			Terminal *wire_end = e2->terminals().at(0);
			QCOMPARE(wire_end->wireLimit(), 0);
			draw(e1->terminals().at(1), wire_end);
			QVERIFY(wire_end->hasRoomForWire());
			QVERIFY(e3->terminals().at(0)->canBeLinkedTo(wire_end));
		}

			// ---- the defaults and the number of a new conductor ----
			// A wire takes the defaults of its folio, as always; a pipe takes
			// none of them (no wire number, color, size...)
		void aPipeTakesNoneOfTheWireDefaults()
		{
			const ConductorProperties before = m_folio->defaultConductorProperties;
			ConductorProperties defaults = before;
			defaults.text = QStringLiteral("X1");
			defaults.color = Qt::red;
			defaults.cond_size = 2;
			m_folio->defaultConductorProperties = defaults;

			const auto wires = pair(m_electrical, m_electrical);
			Conductor *wire = draw(wires.first, wires.second);
			QCOMPARE(wire->properties().text, QStringLiteral("X1"));
			QCOMPARE(wire->properties().color, QColor(Qt::red));
			QCOMPARE(wire->properties().cond_size, 2.0);

			const auto nozzles = pair(m_pipe, m_pipe);
			Conductor *pipe = draw(nozzles.first, nozzles.second);
			QCOMPARE(pipe->properties().text, QString());
			QVERIFY(!pipe->properties().m_show_text);
			QCOMPARE(pipe->properties().color, QColor(Qt::black));
			QCOMPARE(pipe->properties().cond_size, 1.0);
			QCOMPARE(pipe->medium(), TerminalData::Pipe);

				// the numbering of a new conductor leaves a pipe alone
			const ConductorProperties drawn = pipe->properties();
			ConductorAutoNumerotation numbering(pipe, m_folio);
			numbering.numerate();
			QVERIFY(pipe->properties() == drawn);

			m_folio->defaultConductorProperties = before;
		}

			// The creation of conductors between selected terminals gives a
			// pipe the defaults of its medium, not the wire properties
		void createdConductorsBetweenNozzlesArePipes()
		{
			const ConductorProperties before = m_folio->defaultConductorProperties;
			ConductorProperties defaults = before;
			defaults.text = QStringLiteral("X2");
			m_folio->defaultConductorProperties = defaults;

			const auto nozzles = pair(m_pipe, m_pipe);
			ConductorCreator creator(m_folio, {nozzles.first, nozzles.second});
			QCOMPARE(nozzles.first->conductors().size(), 1);
			Conductor *pipe = nozzles.first->conductors().first();
			QCOMPARE(pipe->medium(), TerminalData::Pipe);
			QCOMPARE(pipe->properties().text, QString());

			m_folio->defaultConductorProperties = before;
		}

			// ---- the data of a pipe ----
		static PipeProperties someData()
		{
			PipeProperties p;
			p.nominal_diameter = 150;
			p.fluid = QStringLiteral("W01");
			p.material = QStringLiteral("M01");
			p.flow = QStringLiteral("120 m3/h");
			p.line_number = QStringLiteral("0110");
			p.piping_class = QStringLiteral("75HB13");
			p.nominal_pressure = QStringLiteral("PN16");
			p.line_kind = PipeProperties::Secondary;
			p.spool = QStringLiteral("1");
			p.external_finish = QStringLiteral("EA");
			return p;
		}

			// Only what is set is written, each attribute prefixed "pipe_";
			// the words of the format are part of the files
		void theDataOfAPipeAreItsPipeAttributes()
		{
			QVERIFY(PipeProperties().attributes().isEmpty());
			QHash<QString, QString> a;
			for (const auto &pair : someData().attributes()) a.insert(pair.first, pair.second);
			QCOMPARE(a.size(), 10);
			QCOMPARE(a.value("pipe_dn"), QStringLiteral("150"));
			QCOMPARE(a.value("pipe_fluid"), QStringLiteral("W01"));
			QCOMPARE(a.value("pipe_material"), QStringLiteral("M01"));
			QCOMPARE(a.value("pipe_flow"), QStringLiteral("120 m3/h"));
			QCOMPARE(a.value("pipe_line_number"), QStringLiteral("0110"));
			QCOMPARE(a.value("pipe_piping_class"), QStringLiteral("75HB13"));
			QCOMPARE(a.value("pipe_nominal_pressure"), QStringLiteral("PN16"));
			QCOMPARE(a.value("pipe_line_kind"), QStringLiteral("secondary"));
			QCOMPARE(a.value("pipe_spool"), QStringLiteral("1"));
			QCOMPARE(a.value("pipe_external_finish"), QStringLiteral("EA"));
			QCOMPARE(PipeProperties::lineKindToString(PipeProperties::Main), QStringLiteral("main"));
			QCOMPARE(PipeProperties::lineKindToString(PipeProperties::Auxiliary), QStringLiteral("auxiliary"));

				// a main line is not written: the default
			PipeProperties main;
			main.nominal_diameter = 80;
			QCOMPARE(main.attributes().size(), 1);

				// read back; what cannot be read is left unset
			QDomDocument doc;
			QDomElement e = doc.createElement(QStringLiteral("conductor"));
			for (const auto &pair : someData().attributes()) e.setAttribute(pair.first, pair.second);
			PipeProperties in;
			in.fromXml(e);
			QVERIFY(in == someData());
			e.setAttribute(QStringLiteral("pipe_dn"), QStringLiteral("abc"));
			e.setAttribute(QStringLiteral("pipe_line_kind"), QStringLiteral("steam"));
			in.fromXml(e);
			QCOMPARE(in.nominal_diameter, 0);
			QCOMPARE(in.line_kind, PipeProperties::Main);
		}

			// A pipe keeps its data through a file; a wire never has any
		void aPipeKeepsItsDataAWireHasNone()
		{
			const auto nozzles = pair(m_pipe, m_pipe);
			Conductor *pipe = draw(nozzles.first, nozzles.second);
			ConductorProperties p = pipe->properties();
			p.m_pipe = someData();
			pipe->setProperties(p);
			QVERIFY(pipe->properties().m_pipe == someData());

			QHash<Terminal *, int> ids;
			QDomDocument doc;
			QDomElement saved = pipe->toXml(doc, ids, {});
			QCOMPARE(saved.attribute(QStringLiteral("pipe_dn")), QStringLiteral("150"));
			QCOMPARE(saved.attribute(QStringLiteral("pipe_fluid")), QStringLiteral("W01"));
			m_folio->removeItem(pipe);
			delete pipe;
			auto *again = new Conductor(nozzles.first, nozzles.second);
			again->fromXml(saved);
			QVERIFY(again->properties().m_pipe == someData());
			delete again;

			const auto wires = pair(m_electrical, m_electrical);
			Conductor *wire = draw(wires.first, wires.second);
			ConductorProperties w = wire->properties();
			w.m_pipe = someData();
			wire->setProperties(w);
			QVERIFY(wire->properties().m_pipe.isDefault());
			QHash<Terminal *, int> ids2;
			QDomDocument doc2;
			QVERIFY(!wire->toXml(doc2, ids2, {}).hasAttribute(QStringLiteral("pipe_dn")));
		}

			// The data of a pipe go through a .qetdb file and back, with the
			// other attributes of the conductor: no code of their own
		void theDataOfAPipeGoThroughAQetdbFile()
		{
			const auto nozzles = pair(m_pipe, m_pipe);
			Conductor *pipe = draw(nozzles.first, nozzles.second);
			ConductorProperties p = pipe->properties();
			p.m_pipe = someData();
			pipe->setProperties(p);

			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString qet = dir.filePath(QStringLiteral("pipes.qet"));
			{
				QFile f(qet);
				QVERIFY(f.open(QIODevice::WriteOnly));
				f.write(m_project.toXml().toByteArray());
			}
			const QString qetdb = dir.filePath(QStringLiteral("pipes.qetdb"));
			const QString back = dir.filePath(QStringLiteral("back.qet"));
			QVERIFY(resave(dir, qet, qetdb));
			QVERIFY(resave(dir, qetdb, back));

			QFile f(back);
			QVERIFY(f.open(QIODevice::ReadOnly));
			QDomDocument doc;
			QVERIFY(doc.setContent(&f));
			bool found = false;
			const QDomNodeList conductors = doc.elementsByTagName(QStringLiteral("conductor"));
			for (int i = 0 ; i < conductors.size() ; ++i) {
				const QDomElement c = conductors.at(i).toElement();
				if (c.attribute(QStringLiteral("uuid")) != pipe->uuid().toString()) continue;
				found = true;
				QCOMPARE(c.attribute(QStringLiteral("medium")), QStringLiteral("pipe"));
				PipeProperties read;
				read.fromXml(c);
				QVERIFY(read == someData());
			}
			QVERIFY2(found, "the pipe is not in the file read back from the .qetdb");
		}

			// An electrical conductor is saved exactly as before: no medium
		void anElectricalConductorIsSavedAsBefore()
		{
			const auto ends = pair(m_electrical, m_electrical);
			Conductor *wire = draw(ends.first, ends.second);
			QHash<Terminal *, int> ids;
			QDomDocument doc;
			QVERIFY(!wire->toXml(doc, ids, {}).hasAttribute(QStringLiteral("medium")));
		}
};

QET_APP_TEST_MAIN(tst_conductormedium)
#include "tst_conductormedium.moc"
