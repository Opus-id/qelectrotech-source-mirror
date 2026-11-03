// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "conductorproperties.h"
#include "dataBase/projectdatabase.h"
#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>

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
			// A pipe has no potential: the pipes of a tee share nothing
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
