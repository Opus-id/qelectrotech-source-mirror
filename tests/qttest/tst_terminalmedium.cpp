// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "properties/terminaldata.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QGraphicsObject>

// The medium a terminal connects: electrical, as QET always did, or for a
// process symbol a pipe, a signal or a drive. The attribute "medium" of a
// <terminal> says it and is only written when it is not electrical, so an
// element that says nothing is read and saved exactly as before.
class tst_terminalmedium : public QObject
{
	Q_OBJECT

		// TerminalData::toXml() reads the position of the item it belongs to
	class Holder : public QGraphicsObject
	{
		public:
			QRectF boundingRect() const override { return {}; }
			void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override {}
	};

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

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/medium"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/medium/") + file, &m_project);
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

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("medium"), NamesList());
			QDomDocument a, b;
			m_electrical = add("electrical.elmt",
					   definition(a, "61111111-1111-1111-1111-111111111111", ""));
			m_pipe = add("pipe.elmt",
				     definition(b, "62222222-2222-2222-2222-222222222222", "pipe"));
			QVERIFY(m_electrical.exist() && m_pipe.exist());
			m_folio = m_project.addNewDiagram();
		}

			// ---- the data of the terminal ----
		void readsTheMedium_data()
		{
			QTest::addColumn<QString>("word");
			QTest::addColumn<int>("expected");
			QTest::newRow("absent")      << QString()    << int(TerminalData::Electrical);
			QTest::newRow("electrical")  << "electrical" << int(TerminalData::Electrical);
			QTest::newRow("pipe")        << "pipe"       << int(TerminalData::Pipe);
			QTest::newRow("signal")      << "signal"     << int(TerminalData::Signal);
			QTest::newRow("drive")       << "drive"      << int(TerminalData::Drive);
				// a word of a later version, or a typo: what QET always did
			QTest::newRow("unknown")     << "steam"      << int(TerminalData::Electrical);
			QTest::newRow("capitalised") << "Pipe"       << int(TerminalData::Electrical);
		}

		void readsTheMedium()
		{
			QFETCH(QString, word);
			QFETCH(int, expected);
			QDomDocument doc;
			QDomElement e = doc.createElement(QStringLiteral("terminal"));
			e.setAttribute(QStringLiteral("x"), 0);
			e.setAttribute(QStringLiteral("y"), 0);
			e.setAttribute(QStringLiteral("orientation"), QStringLiteral("n"));
			if (!word.isNull())
				e.setAttribute(QStringLiteral("medium"), word);
			TerminalData data;
			QVERIFY(data.fromXml(e));
			QCOMPARE(int(data.m_medium), expected);
		}

			// The words of the format, one by one: they are part of the
			// files and must never change
		void theWordsOfTheFormat()
		{
			QCOMPARE(TerminalData::mediumToString(TerminalData::Electrical),
				 QStringLiteral("electrical"));
			QCOMPARE(TerminalData::mediumToString(TerminalData::Pipe), QStringLiteral("pipe"));
			QCOMPARE(TerminalData::mediumToString(TerminalData::Signal), QStringLiteral("signal"));
			QCOMPARE(TerminalData::mediumToString(TerminalData::Drive), QStringLiteral("drive"));
		}

		void writesTheMediumOnlyWhenNotElectrical_data()
		{
			QTest::addColumn<int>("medium");
			QTest::addColumn<QString>("written");
			QTest::newRow("electrical") << int(TerminalData::Electrical) << QString();
			QTest::newRow("pipe")       << int(TerminalData::Pipe)       << "pipe";
			QTest::newRow("signal")     << int(TerminalData::Signal)     << "signal";
			QTest::newRow("drive")      << int(TerminalData::Drive)      << "drive";
		}

		void writesTheMediumOnlyWhenNotElectrical()
		{
			QFETCH(int, medium);
			QFETCH(QString, written);
			Holder holder;
			TerminalData data(&holder);
			data.m_medium = TerminalData::Medium(medium);
			QDomDocument doc;
			const QDomElement e = data.toXml(doc);
			QCOMPARE(e.hasAttribute(QStringLiteral("medium")), !written.isNull());
			if (!written.isNull())
				QCOMPARE(e.attribute(QStringLiteral("medium")), written);
		}

			// Read back what was written: the medium survives a save of the
			// element editor (PartTerminal delegates to TerminalData)
		void roundTrip()
		{
			for (TerminalData::Medium m : {TerminalData::Electrical, TerminalData::Pipe,
						       TerminalData::Signal, TerminalData::Drive}) {
				Holder holder;
				TerminalData out(&holder);
				out.m_medium = m;
				QDomDocument doc;
				const QDomElement e = out.toXml(doc);
				TerminalData in;
				QVERIFY(in.fromXml(e));
				QCOMPARE(in.m_medium, m);
			}
		}

			// ---- a placed element ----
		void aPlacedElementKnowsTheMediumOfItsTerminals()
		{
			Element *electrical = place(m_electrical, QPointF(0, 0));
			Element *pipe = place(m_pipe, QPointF(100, 0));
			QVERIFY(electrical && pipe);
			QCOMPARE(electrical->terminals().size(), 2);
			QCOMPARE(pipe->terminals().size(), 2);
			for (Terminal *t : electrical->terminals())
				QCOMPARE(t->medium(), TerminalData::Electrical);
			for (Terminal *t : pipe->terminals())
				QCOMPARE(t->medium(), TerminalData::Pipe);
		}

			// The project keeps the definition of the symbol as it is, the
			// medium included
		void theProjectKeepsTheMediumOfItsSymbols()
		{
			const QDomDocument doc = m_project.toXml();
				// the symbols only: a folio lists its placed terminals too
			const QDomNodeList terminals = doc.documentElement()
					.firstChildElement(QStringLiteral("collection"))
					.elementsByTagName(QStringLiteral("terminal"));
			int pipes = 0, without = 0;
			for (int i = 0; i < terminals.size(); ++i) {
				const QDomElement t = terminals.at(i).toElement();
				if (t.attribute(QStringLiteral("medium")) == QLatin1String("pipe")) ++pipes;
				else if (!t.hasAttribute(QStringLiteral("medium"))) ++without;
			}
			QCOMPARE(pipes, 2);
			QCOMPARE(without, 2);
		}
};

QET_APP_TEST_MAIN(tst_terminalmedium)
#include "tst_terminalmedium.moc"
