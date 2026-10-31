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

#include <QDomDocument>
#include <QGraphicsObject>

// What may be connected to a terminal: an electrical wire, as QET always
// did, or for a P&ID symbol a pipe line, a signal line or a drive. The
// attribute "connection" of a <terminal> says it, and is only written for
// the P&ID kinds; a terminal that is not electrical takes no new wire.
class tst_terminalconnection : public QObject
{
	Q_OBJECT

		// TerminalData::toXml() reads the position of the item it belongs to
	class Holder : public QGraphicsObject
	{
		public:
			QRectF boundingRect() const override { return {}; }
			void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override {}
	};

	static TerminalData *dataOf(TerminalData::ConnectionKind kind)
	{
		auto *data = new TerminalData();
		data->m_connection = kind;
		return data;
	}

		// A symbol 40 x 40 with two terminals N1 (west) and N2 (east), both
		// of the kind @p connection ("" for none)
	static QDomElement definition(QDomDocument &doc, const QString &uuid,
				      const QString &connection)
	{
		const QString attr = connection.isEmpty()
				? QString()
				: QStringLiteral(" connection=\"%1\"").arg(connection);
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
	ElementsLocation m_electrical, m_pipe, m_pipe_too;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/conn"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/conn/") + file, &m_project);
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
						QStringLiteral("import"), QStringLiteral("conn"), NamesList());
			QDomDocument a, b, c;
			m_electrical = add("electrical.elmt",
					   definition(a, "51111111-1111-1111-1111-111111111111", ""));
			m_pipe = add("pipe.elmt",
				     definition(b, "52222222-2222-2222-2222-222222222222", "pipe"));
			m_pipe_too = add("pipe_too.elmt",
					 definition(c, "53333333-3333-3333-3333-333333333333", "pipe"));
			QVERIFY(m_electrical.exist() && m_pipe.exist() && m_pipe_too.exist());
			m_folio = m_project.addNewDiagram();
		}

			// ---- the data of the terminal ----
		void readsTheConnection_data()
		{
			QTest::addColumn<QString>("word");
			QTest::addColumn<int>("expected");
			QTest::newRow("absent")      << QString()  << int(TerminalData::Electrical);
			QTest::newRow("electrical")  << "electrical" << int(TerminalData::Electrical);
			QTest::newRow("pipe")        << "pipe"     << int(TerminalData::Pipe);
			QTest::newRow("signal")      << "signal"   << int(TerminalData::Signal);
			QTest::newRow("drive")       << "drive"    << int(TerminalData::Drive);
				// a word of a later version, or a typo: what QET always did
			QTest::newRow("unknown")     << "steam"    << int(TerminalData::Electrical);
			QTest::newRow("capitalised") << "Pipe"     << int(TerminalData::Electrical);
		}

		void readsTheConnection()
		{
			QFETCH(QString, word);
			QFETCH(int, expected);
			QDomDocument doc;
			QDomElement e = doc.createElement(QStringLiteral("terminal"));
			e.setAttribute(QStringLiteral("x"), 0);
			e.setAttribute(QStringLiteral("y"), 0);
			e.setAttribute(QStringLiteral("orientation"), QStringLiteral("w"));
			if (!word.isNull()) e.setAttribute(QStringLiteral("connection"), word);
			TerminalData data;
			QVERIFY(data.fromXml(e));
			QCOMPARE(int(data.m_connection), expected);
				// the other "type", for terminal blocks, is not touched
			QCOMPARE(int(data.m_type), int(TerminalData::Generic));
		}

		void writesOnlyThePidKinds_data()
		{
			QTest::addColumn<int>("kind");
			QTest::addColumn<QString>("written");
			QTest::newRow("electrical") << int(TerminalData::Electrical) << QString();
			QTest::newRow("pipe")       << int(TerminalData::Pipe)       << "pipe";
			QTest::newRow("signal")     << int(TerminalData::Signal)     << "signal";
			QTest::newRow("drive")      << int(TerminalData::Drive)      << "drive";
		}

		void writesOnlyThePidKinds()
		{
			QFETCH(int, kind);
			QFETCH(QString, written);
			Holder holder;
			TerminalData data(&holder);
			data.m_connection = TerminalData::ConnectionKind(kind);
			QDomDocument doc;
			const QDomElement e = data.toXml(doc);
			QCOMPARE(e.hasAttribute(QStringLiteral("connection")), !written.isNull());
			if (!written.isNull()) {
				QCOMPARE(e.attribute(QStringLiteral("connection")), written);
			}
				// and read back the same
			TerminalData back;
			QVERIFY(back.fromXml(e));
			QCOMPARE(int(back.m_connection), kind);
		}

			// ---- the terminal of the element editor ----
		void theEditorKeepsTheConnection()
		{
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<terminal name=\"E1\" x=\"0\" y=\"-15\" orientation=\"n\" "
				"type=\"Generic\" connection=\"drive\"/>"));
			PartTerminal part(nullptr);
			part.fromXml(doc.documentElement());
			QCOMPARE(part.connectionKind(), TerminalData::Drive);

				// through the property of the QObject, as the undo command of
				// the editor (QPropertyUndoCommand) sets it
			QObject &object = part;
			QVERIFY(object.setProperty("connection", int(TerminalData::Pipe)));
			QCOMPARE(part.connectionKind(), TerminalData::Pipe);
			QDomDocument out;
			QCOMPARE(part.toXml(out).attribute(QStringLiteral("connection")),
				 QStringLiteral("pipe"));

				// back to electrical: nothing written, as before
			QVERIFY(object.setProperty("connection", int(TerminalData::Electrical)));
			QDomDocument out2;
			QVERIFY(!part.toXml(out2).hasAttribute(QStringLiteral("connection")));
				// the name and the type are untouched
			QCOMPARE(part.terminalName(), QStringLiteral("E1"));
			QCOMPARE(part.terminalType(), TerminalData::Generic);
		}

			// ---- the name a terminal gets with its kind ----
		void suggestsAName_data()
		{
			QTest::addColumn<int>("kind");
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
			QFETCH(int, kind);
			QFETCH(QString, current);
			QFETCH(QStringList, taken);
			QFETCH(QString, expected);
			QCOMPARE(TerminalData::suggestedName(TerminalData::ConnectionKind(kind),
							     current, taken),
				 expected);
		}

			// ---- how a terminal looks ----
		void eachKindHasItsOwnStem()
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
					QVERIFY2(colors.at(i) != colors.at(j), "two kinds share a color");
					// nor the colors the hover gives while a wire is drawn,
					// nor the blue of the docking point
				QVERIFY(colors.at(i) != Terminal::allowedColor);
				QVERIFY(colors.at(i) != Terminal::warningColor);
				QVERIFY(colors.at(i) != Terminal::neutralColor);
			}
		}

			// ---- wires ----
		void onlyElectricalTerminalsTakeWires_data()
		{
			QTest::addColumn<int>("first");
			QTest::addColumn<int>("second");
			QTest::addColumn<bool>("linkable");
			const int e = TerminalData::Electrical, p = TerminalData::Pipe,
				  s = TerminalData::Signal, d = TerminalData::Drive;
			QTest::newRow("electrical-electrical") << e << e << true;
			QTest::newRow("pipe-electrical")       << p << e << false;
			QTest::newRow("electrical-pipe")       << e << p << false;
			QTest::newRow("pipe-pipe")             << p << p << false;
			QTest::newRow("signal-electrical")     << s << e << false;
			QTest::newRow("drive-electrical")      << d << e << false;
			QTest::newRow("drive-drive")           << d << d << false;
		}

		void onlyElectricalTerminalsTakeWires()
		{
			QFETCH(int, first);
			QFETCH(int, second);
			QFETCH(bool, linkable);
			Terminal a(dataOf(TerminalData::ConnectionKind(first)));
			Terminal b(dataOf(TerminalData::ConnectionKind(second)));
			QCOMPARE(a.acceptsWires(), first == TerminalData::Electrical);
			QCOMPARE(a.connectionKind(), TerminalData::ConnectionKind(first));
			QCOMPARE(a.canBeLinkedTo(&b), linkable);
			QCOMPARE(b.canBeLinkedTo(&a), linkable);
		}

			// Two symbols face to face, terminals aligned: the electrical ones
			// are wired automatically, the nozzles are not
		void autoConnectionLeavesNozzlesAlone()
		{
			Element *left = place(m_electrical, QPointF(100, 300));
			Element *right = place(m_electrical, QPointF(200, 300));
			QVERIFY(left && right);
			QVERIFY(!left->AlignedFreeTerminals().isEmpty());

			Element *pipe_left = place(m_pipe, QPointF(100, 500));
			Element *pipe_right = place(m_pipe, QPointF(200, 500));
			QVERIFY(pipe_left && pipe_right);
			QVERIFY(pipe_left->AlignedFreeTerminals().isEmpty());

				// a nozzle facing a wire terminal is not wired either
			Element *mixed_left = place(m_pipe, QPointF(100, 700));
			Element *mixed_right = place(m_electrical, QPointF(200, 700));
			QVERIFY(mixed_left && mixed_right);
			QVERIFY(mixed_left->AlignedFreeTerminals().isEmpty());
			QVERIFY(mixed_right->AlignedFreeTerminals().isEmpty());
		}

			// ---- Change type ----
		void changeTypeKeepsTheKindOfTheTerminals()
		{
			Element *pipe = place(m_pipe, QPointF(400, 300));
			QVERIFY(pipe);
			QVERIFY(pipe->canChangeLocationTo(m_pipe_too));
			QVERIFY(!pipe->canChangeLocationTo(m_electrical));

			Element *electrical = place(m_electrical, QPointF(400, 500));
			QVERIFY(electrical);
			QVERIFY(!electrical->canChangeLocationTo(m_pipe));
		}
};

QET_APP_TEST_MAIN(tst_terminalconnection)
#include "tst_terminalconnection.moc"
