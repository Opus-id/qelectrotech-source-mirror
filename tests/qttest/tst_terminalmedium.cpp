// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "editor/graphicspart/partterminal.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "properties/terminaldata.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "wiringrules.h"

#include <QDomDocument>
#include <QGraphicsObject>
#include <QSignalSpy>

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

			// ---- the terminal of the element editor ----
		void theEditorKeepsTheMedium()
		{
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<terminal name=\"E1\" x=\"0\" y=\"-15\" orientation=\"n\" "
				"type=\"Generic\" medium=\"drive\"/>"));
			PartTerminal part(nullptr);
			part.fromXml(doc.documentElement());
			QCOMPARE(part.medium(), TerminalData::Drive);

				// through the property of the QObject, as the undo command of
				// the editor (QPropertyUndoCommand) sets it
			QObject &object = part;
			QSignalSpy changed(&part, &PartTerminal::mediumChanged);
			QVERIFY(object.setProperty("medium", int(TerminalData::Pipe)));
			QCOMPARE(part.medium(), TerminalData::Pipe);
			QCOMPARE(changed.count(), 1);
			QDomDocument out;
			QCOMPARE(part.toXml(out).attribute(QStringLiteral("medium")),
				 QStringLiteral("pipe"));

				// back to electrical: nothing written, as before
			QVERIFY(object.setProperty("medium", int(TerminalData::Electrical)));
			QDomDocument out2;
			QVERIFY(!part.toXml(out2).hasAttribute(QStringLiteral("medium")));
				// the name and the type are untouched
			QCOMPARE(part.terminalName(), QStringLiteral("E1"));
			QCOMPARE(part.terminalType(), TerminalData::Generic);
		}

			// ---- the name a terminal gets with its medium ----
		void suggestsAName_data()
		{
			QTest::addColumn<int>("medium");
			QTest::addColumn<QString>("current");
			QTest::addColumn<QStringList>("taken");
			QTest::addColumn<QString>("expected");
			const int e = TerminalData::Electrical, p = TerminalData::Pipe,
				  s = TerminalData::Signal, d = TerminalData::Drive;
			QTest::newRow("drive E1 becomes a nozzle")
					<< p << "E1" << QStringList{"N1", "N2"} << "N3";
			QTest::newRow("no name, nozzle")       << p << "" << QStringList{} << "N1";
			QTest::newRow("no name, drive")        << d << "" << QStringList{"N1"} << "E1";
			QTest::newRow("no name, signal")       << s << "" << QStringList{} << "S1";
			QTest::newRow("nozzle N2 becomes a drive")
					<< d << "N2" << QStringList{"N1", "E1"} << "E2";
			QTest::newRow("nozzle becomes a signal")
					<< s << "N1" << QStringList{"N2"} << "S1";
				// already following the new convention: kept
			QTest::newRow("N1 stays a nozzle")     << p << "N1" << QStringList{"N2"} << "N1";
				// chosen by the author: kept
			QTest::newRow("free name kept")        << p << "Suction" << QStringList{} << "Suction";
			QTest::newRow("number kept")           << p << "1" << QStringList{} << "1";
			QTest::newRow("lower case kept")       << p << "e1" << QStringList{} << "e1";
				// electrical names follow no convention: nothing is renamed
			QTest::newRow("back to electrical")    << e << "N1" << QStringList{} << "N1";
			QTest::newRow("no name, electrical")   << e << "" << QStringList{} << "";
		}

		void suggestsAName()
		{
			QFETCH(int, medium);
			QFETCH(QString, current);
			QFETCH(QStringList, taken);
			QFETCH(QString, expected);
			QCOMPARE(TerminalData::suggestedName(TerminalData::Medium(medium),
							     current, taken),
				 expected);
		}

			// ---- how a terminal looks ----
		void eachMediumHasItsOwnStem()
		{
				// an electrical terminal looks as it always did
			QCOMPARE(Terminal::stemColor(TerminalData::Electrical), QColor(Qt::red));
			const QList<QColor> colors{
				Terminal::stemColor(TerminalData::Electrical),
				Terminal::stemColor(TerminalData::Pipe),
				Terminal::stemColor(TerminalData::Signal),
				Terminal::stemColor(TerminalData::Drive)};
			for (int i = 0; i < colors.size(); ++i) {
				for (int j = i + 1; j < colors.size(); ++j)
					QVERIFY2(colors.at(i) != colors.at(j), "two media share a color");
					// nor the colors the hover gives while a wire is drawn,
					// nor the blue of the docking point
				QVERIFY(colors.at(i) != Terminal::allowedColor);
				QVERIFY(colors.at(i) != Terminal::warningColor);
				QVERIFY(colors.at(i) != Terminal::neutralColor);
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

			// ---- joining terminals of different media ----
			// Joined, with a warning while drawing, unless the project's
			// wiring rules refuse it; the master switch turns the rule off
		void differentMediaAreJoinedUnlessTheProjectRefuses()
		{
			Element *electrical = place(m_electrical, QPointF(0, 200));
			Element *pipe = place(m_pipe, QPointF(100, 200));
			Element *pipe_too = place(m_pipe, QPointF(200, 200));
			QVERIFY(electrical && pipe && pipe_too);
			Terminal *wire_end = electrical->terminals().at(0);
			Terminal *nozzle = pipe->terminals().at(0);
			Terminal *other_nozzle = pipe_too->terminals().at(0);
			QVERIFY(!wire_end->sameMediumAs(nozzle));
			QVERIFY(nozzle->sameMediumAs(other_nozzle));

				// by default: no rule, everything is joined as before
			QVERIFY(m_project.wiringRules().isDefault());
			QVERIFY(!wire_end->refusesMediumOf(nozzle));
			QVERIFY(wire_end->canBeLinkedTo(nozzle));

			WiringRules::Settings refuse;
			refuse.own = true;
			refuse.refuse_mixed_media = true;
			m_project.setWiringRules(refuse);
			QVERIFY(wire_end->refusesMediumOf(nozzle));
			QVERIFY(nozzle->refusesMediumOf(wire_end));
			QVERIFY(!wire_end->canBeLinkedTo(nozzle));
				// the same medium is never refused
			QVERIFY(!nozzle->refusesMediumOf(other_nozzle));
			QVERIFY(nozzle->canBeLinkedTo(other_nozzle));

				// the master switch turns every rule off
			WiringRules::setMasterEnabled(false);
			QVERIFY(wire_end->canBeLinkedTo(nozzle));
			WiringRules::setMasterEnabled(true);

			m_project.setWiringRules(WiringRules::Settings());
			QVERIFY(wire_end->canBeLinkedTo(nozzle));
		}

			// The automatic wiring of aligned terminals never joins two
			// media, rule or not; it still joins two terminals of the same
			// medium, two electrical ones as always
		void alignedTerminalsOfDifferentMediaAreNotWired()
		{
			Element *electrical = place(m_electrical, QPointF(0, 400));
			Element *pipe = place(m_pipe, QPointF(100, 400));
			Element *electrical_too = place(m_electrical, QPointF(0, 600));
			Element *electrical_three = place(m_electrical, QPointF(100, 600));
			QVERIFY(electrical && pipe && electrical_too && electrical_three);
				// the east terminal of the first (x 10) faces the west one
				// of the second (x 90)
			const auto pairs = [](Element *from, Element *to) {
				int n = 0;
				for (const auto &pair : from->AlignedFreeTerminals())
					if (pair.second->parentElement() == to) ++n;
				return n;
			};
			QCOMPARE(pairs(electrical, pipe), 0);
			QCOMPARE(pairs(electrical_too, electrical_three), 1);
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
