// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "conductorproperties.h"
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
