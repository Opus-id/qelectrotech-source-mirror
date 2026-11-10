/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "qetapptest.h"

#include "diagram.h"
#include "editor/graphicspart/partterminal.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pipeconnection.h"
#include "properties/terminaldata.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "ui/elementnozzleswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDomDocument>
#include <QLineEdit>
#include <QUndoStack>

/**
	How a pipe is joined to a nozzle: a flange by default, a plain end, a
	socket, a thread... Given by the symbol (attribute connection of the
	terminal), changed by a placed element (<nozzles> block). The connection
	decides the kind of size of the nozzle: a flange a DN, a socket a DN or an
	outside diameter.
*/
class tst_nozzleconnection : public QObject
{
	Q_OBJECT

		// N1, a flange of DN 100; N2, a plain end without size
	static QString symbol()
	{
		return QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"60\" height=\"40\" hotspot_x=\"30\" hotspot_y=\"20\">"
			"<uuid uuid=\"{7c111111-1111-1111-1111-111111111111}\"/>"
			"<names><name lang=\"en\">Tank</name></names>"
			"<description>"
			"<line x1=\"-20\" y1=\"0\" x2=\"20\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
			"<terminal uuid=\"{7c000000-0000-0000-0000-000000000001}\" name=\"N1\" x=\"-20\" y=\"0\" "
			"orientation=\"w\" type=\"Generic\" medium=\"pipe\" dn=\"100\" show_name=\"true\"/>"
			"<terminal uuid=\"{7c000000-0000-0000-0000-000000000002}\" name=\"N2\" x=\"20\" y=\"0\" "
			"orientation=\"e\" type=\"Generic\" medium=\"pipe\" connection=\"plain_end\" "
			"show_name=\"true\"/>"
			"</description></definition>");
	}

	static Element *placeTank(QETProject &project, Diagram *folio)
	{
		project.embeddedElementCollection()->createDir(
					QStringLiteral("import"), QStringLiteral("nozzles"), NamesList());
		QDomDocument doc;
		doc.setContent(symbol());
		project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/nozzles"), QStringLiteral("tank.elmt"), doc.documentElement());
		const ElementsLocation location(QStringLiteral("embed://import/nozzles/tank.elmt"), &project);
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		folio->addItem(element);
		element->setPos(200, 200);
		return element;
	}

		// A second tank of the symbol placeTank() put in @p project
	static Element *placeAnotherTank(QETProject &project, Diagram *folio)
	{
		const ElementsLocation location(QStringLiteral("embed://import/nozzles/tank.elmt"), &project);
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		folio->addItem(element);
		element->setPos(400, 200);
		return element;
	}

	static Terminal *terminal(const Element *element, const QString &name)
	{
		for (Terminal *t : element->terminals()) {
			if (t->baseName() == name) return t;
		}
		return nullptr;
	}

		// Sizes DN 80, DN 100, Ø 110, Ø 125; Ø 110 joined to DN 100 only
	static PidCodification codification()
	{
		PidCodification codification;
		PidCodification::Entry plastic;
		plastic.code = QStringLiteral("M01");
		plastic.outside_diameter = true;
		plastic.series = {1100, 1250};
		PidCodification::Entry metal;
		metal.code = QStringLiteral("M02");
		metal.series = {800, 1000};
		codification.materials = {plastic, metal};
		codification.outside_diameters.insert(1100, {100});
		codification.decimal = PidCodification::Decimal::Point;
		return codification;
	}

		// Pick @p connection in @p list, as the user does
	static void pick(QComboBox *list, PipeConnection::Type connection)
	{
		const int index = list->findData(int(connection));
		list->setCurrentIndex(index);
		emit list->activated(index);
	}

	private slots:
		void eachConnectionTakesItsKindsOfSize()
		{
			QVERIFY(PipeConnection::allows(PipeConnection::Flange, PipeDiameter::nominal(100)));
			QVERIFY(!PipeConnection::allows(PipeConnection::Flange, PipeDiameter::outside(110)));
			QVERIFY(PipeConnection::allows(PipeConnection::QuickCoupling, PipeDiameter::nominal(65)));
			QVERIFY(!PipeConnection::allows(PipeConnection::QuickCoupling, PipeDiameter::outside(75)));
				// a socket as the plain end it receives
			for (PipeConnection::Type type : {PipeConnection::PlainEnd, PipeConnection::Socket,
											   PipeConnection::ButtWeld, PipeConnection::Grooved}) {
				QVERIFY(PipeConnection::allows(type, PipeDiameter::nominal(100)));
				QVERIFY(PipeConnection::allows(type, PipeDiameter::outside(110)));
			}
				// none is always allowed
			for (PipeConnection::Type type : PipeConnection::all()) {
				QVERIFY(PipeConnection::allows(type, PipeDiameter()));
			}
				// a number typed alone: a DN, but inches for a thread
			for (PipeConnection::Type type : PipeConnection::all()) {
				QCOMPARE(PipeConnection::kinds(type).constFirst(),
						 type == PipeConnection::Thread ? PipeDiameter::Thread : PipeDiameter::Nominal);
			}
		}

		void eachConnectionHasItsWord()
		{
			for (PipeConnection::Type type : PipeConnection::all()) {
				bool ok = false;
				QCOMPARE(PipeConnection::fromString(PipeConnection::toString(type), &ok), type);
				QVERIFY(ok);
			}
				// a word a later version wrote, or none: a flange
			bool ok = true;
			QCOMPARE(PipeConnection::fromString(QStringLiteral("bayonet"), &ok), PipeConnection::Flange);
			QVERIFY(!ok);
			QCOMPARE(PipeConnection::fromString(QString()), PipeConnection::Flange);
				// the three sockets of the first trials: the socket they became
			for (const QString &word : {QStringLiteral("solvent_socket"), QStringLiteral("fusion_socket"),
										QStringLiteral("gasket_socket")}) {
				ok = false;
				QCOMPARE(PipeConnection::fromString(word, &ok), PipeConnection::Socket);
				QVERIFY(ok);
			}
		}

			// ---- the symbol ----
		void theSymbolKeepsTheConnectionOfANozzle()
		{
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"0\" name=\"N1\" orientation=\"e\" type=\"Generic\" "
				"medium=\"pipe\" connection=\"solvent_socket\" od=\"110\"/>"));
			PartTerminal part(nullptr);
			part.fromXml(doc.documentElement());
				// a socket of the first trials, written as the socket it became
			QCOMPARE(part.connection(), PipeConnection::Socket);
			QDomDocument out;
			QCOMPARE(part.toXml(out).attribute(QStringLiteral("connection")),
					 QStringLiteral("socket"));

				// a flange, the default, is not written
			part.setConnectionWord(QStringLiteral("flange"));
			QCOMPARE(part.connection(), PipeConnection::Flange);
			QDomDocument out2;
			QVERIFY(!part.toXml(out2).hasAttribute(QStringLiteral("connection")));

				// a terminal of another medium has none
			doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"0\" name=\"T1\" orientation=\"e\" type=\"Generic\" "
				"connection=\"thread\"/>"));
			PartTerminal electrical(nullptr);
			electrical.fromXml(doc.documentElement());
			QCOMPARE(electrical.connection(), PipeConnection::Flange);
			QDomDocument out3;
			QVERIFY(!electrical.toXml(out3).hasAttribute(QStringLiteral("connection")));
		}

			// ---- a placed element ----
		void aPlacedElementGivesItsNozzleAnotherConnection()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *tank = placeTank(project, folio);
			QVERIFY(tank);
			Terminal *n1 = terminal(tank, QStringLiteral("N1"));
			Terminal *n2 = terminal(tank, QStringLiteral("N2"));
			QCOMPARE(n1->connection(), PipeConnection::Flange);
			QCOMPARE(n2->connection(), PipeConnection::PlainEnd);
			QCOMPARE(n1->labelText(), QStringLiteral("N1\nDN 100"));
			QCOMPARE(n1->toolTip(), QStringLiteral("Flange"));
			QVERIFY(!folio->hasSymbolChoices());

				// a socket is named by a DN too: DN 100 kept
			tank->setNozzleConnections({{QStringLiteral("N1"), PipeConnection::Socket}});
			QCOMPARE(n1->connection(), PipeConnection::Socket);
			QCOMPARE(n1->diameter(), PipeDiameter::nominal(100));
			QCOMPARE(n1->labelText(), QStringLiteral("N1\nDN 100"));
			QCOMPARE(n1->toolTip(), QStringLiteral("Socket"));
			tank->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::outside(110)}});
			QCOMPARE(n1->labelText(), QStringLiteral("N1\nØ 110"));
				// a folio with such a choice is no .qetdb
			QVERIFY(folio->hasSymbolChoices());

				// written in the <nozzles> block, and read back
			QDomDocument doc;
			QHash<Terminal *, int> table;
			const QDomElement written = tank->toXml(doc, table);
			const QDomElement nozzle = written.firstChildElement(QStringLiteral("nozzles"))
									   .firstChildElement(QStringLiteral("nozzle"));
			QCOMPARE(nozzle.attribute(QStringLiteral("name")), QStringLiteral("N1"));
			QCOMPARE(nozzle.attribute(QStringLiteral("connection")), QStringLiteral("socket"));
			QCOMPARE(nozzle.attribute(QStringLiteral("od")), QStringLiteral("110"));
			QCOMPARE(Element::nozzleConnectionsFromXml(written), tank->nozzleConnections());
			QCOMPARE(Element::nozzleDiametersFromXml(written), tank->nozzleDiameters());

				// a connection alone is written too, without a size
			tank->setNozzleDiameters({});
			tank->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::Thread}});
			QDomDocument doc2;
			const QDomElement nozzles = tank->toXml(doc2, table).firstChildElement(QStringLiteral("nozzles"));
			QCOMPARE(nozzles.elementsByTagName(QStringLiteral("nozzle")).count(), 1);
			const QDomElement n2_written = nozzles.firstChildElement(QStringLiteral("nozzle"));
			QCOMPARE(n2_written.attribute(QStringLiteral("connection")), QStringLiteral("thread"));
			QVERIFY(!n2_written.hasAttribute(QStringLiteral("dn")));
			QVERIFY(!n2_written.hasAttribute(QStringLiteral("od")));

				// back to those of the symbol: nothing written
			tank->setNozzleConnections({});
			QDomDocument doc3;
			QVERIFY(tank->toXml(doc3, table).firstChildElement(QStringLiteral("nozzles")).isNull());
			QCOMPARE(n1->labelText(), QStringLiteral("N1\nDN 100"));
		}

			// ---- the Nozzles tab ----
		void theNozzlesTabConvertsTheSizeWithTheTable()
		{
			QETProject project;
			project.setPidCodification(codification());
			Diagram *folio = project.addNewDiagram();
			Element *tank = placeTank(project, folio);
			QVERIFY(tank);
				// N1, a flange of DN 100 in the symbol, made a socket of Ø 110
			tank->setNozzleConnections({{QStringLiteral("N1"), PipeConnection::Socket}});
			tank->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::outside(110)}});
			ElementNozzlesWidget tab(tank);
			tab.setLiveEdit(true);
			QComboBox *connection = tab.connectionFor(QStringLiteral("N1"));
			QLineEdit *n1 = tab.fieldFor(QStringLiteral("N1"));
			QVERIFY(connection && n1);
			QCOMPARE(connection->currentData().toInt(), int(PipeConnection::Socket));
			QCOMPARE(n1->text(), QStringLiteral("Ø 110"));
			auto *list = qobject_cast<QComboBox *>(n1->parentWidget());
				// a socket: the DN and the outside diameters
			QCOMPARE(list->count(), 5);
				// a number alone is a DN
			n1->setText(QStringLiteral("80"));
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::nominal(80));
			n1->setText(QStringLiteral("Ø 110"));
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::outside(110));

				// a flange: Ø 110 becomes DN 100, the size the table joins to
				// it, in one undo step; the flange and the DN of the symbol,
				// so nothing is kept
			const int steps = folio->undoStack().count();
			pick(connection, PipeConnection::Flange);
			QVERIFY(tank->nozzleConnections().isEmpty());
			QVERIFY(tank->nozzleDiameters().isEmpty());
			QVERIFY(tab.message().contains(QStringLiteral("DN 100")));
			QCOMPARE(folio->undoStack().count(), steps + 1);
			QCOMPARE(terminal(tank, QStringLiteral("N1"))->labelText(), QStringLiteral("N1\nDN 100"));
				// the DN only
			QCOMPARE(list->count(), 3);
				// an outside diameter typed is refused
			n1->setText(QStringLiteral("Ø 125"));
			emit n1->editingFinished();
			QVERIFY(tank->nozzleDiameters().isEmpty());

				// undone: the socket of Ø 110 again
			folio->undoStack().undo();
			QCOMPARE(tank->nozzleConnections().value(QStringLiteral("N1")), PipeConnection::Socket);
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::outside(110));
			QCOMPARE(connection->currentData().toInt(), int(PipeConnection::Socket));
		}

			// The dialog of the properties: nothing applied before Apply
		void theDialogAppliesAConnectionAndAnOutsideDiameter()
		{
			QETProject project;
			project.setPidCodification(codification());
			Diagram *folio = project.addNewDiagram();
			Element *tank = placeTank(project, folio);
			QVERIFY(tank);
			tank->setNozzleDiameters({});
			ElementNozzlesWidget tab(tank);
			tab.setLiveEdit(false);
			pick(tab.connectionFor(QStringLiteral("N1")), PipeConnection::PlainEnd);
			QLineEdit *n1 = tab.fieldFor(QStringLiteral("N1"));
			n1->setText(QStringLiteral("d110"));
			emit n1->editingFinished();
				// written as the lists write it at once, applied later
			QCOMPARE(n1->text(), QStringLiteral("Ø 110"));
			QVERIFY(tank->nozzleConnections().isEmpty());
			QVERIFY(tank->nozzleDiameters().isEmpty());
			QUndoCommand *undo = tab.associatedUndo();
			QVERIFY(undo);
			folio->undoStack().push(undo);
			QCOMPARE(tank->nozzleConnections().value(QStringLiteral("N1")), PipeConnection::PlainEnd);
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::outside(110));
			QCOMPARE(n1->text(), QStringLiteral("Ø 110"));
		}

			// One connection, one size for all the nozzles (step 5l-1b)
		void theNozzlesTabGivesAllTheNozzlesOneConnectionAndOneSize()
		{
			QETProject project;
			project.setPidCodification(codification());
			Diagram *folio = project.addNewDiagram();
			Element *tank = placeTank(project, folio);
			QVERIFY(tank);
			const QString n1 = QStringLiteral("N1");
			const QString n2 = QStringLiteral("N2");
			{
					// a flange of DN 100 and a plain end without size: unlike,
					// both boxes unchecked, every list free
				ElementNozzlesWidget tab(tank);
				QVERIFY(tab.sameConnectionBox() && tab.sameSizeBox());
				QVERIFY(!tab.sameConnectionBox()->isChecked());
				QVERIFY(!tab.sameSizeBox()->isChecked());
				QVERIFY(tab.connectionFor(n2)->isEnabled());
			}
				// alike: both checked, the lists of N2 greyed
			tank->setNozzleConnections({{n2, PipeConnection::Flange}});
			tank->setNozzleDiameters({{n2, PipeDiameter::nominal(100)}});
			ElementNozzlesWidget tab(tank);
			tab.setLiveEdit(true);
			QVERIFY(tab.sameConnectionBox()->isChecked());
			QVERIFY(tab.sameSizeBox()->isChecked());
			QVERIFY(!tab.connectionFor(n2)->isEnabled());
			QVERIFY(!tab.markFor(n2)->isEnabled());
			QVERIFY(!tab.fieldFor(n2)->parentWidget()->isEnabled());

				// the connection of N1 goes to N2, in one undo step
			const int steps = folio->undoStack().count();
			pick(tab.connectionFor(n1), PipeConnection::Socket);
			QCOMPARE(folio->undoStack().count(), steps + 1);
			QCOMPARE(terminal(tank, n1)->connection(), PipeConnection::Socket);
			QCOMPARE(terminal(tank, n2)->connection(), PipeConnection::Socket);
				// so does its drawing
			QComboBox *mark = tab.markFor(n1);
			const int never = mark->findData(int(PipeConnection::MarkChoice::Never));
			mark->setCurrentIndex(never);
			emit mark->activated(never);
			QCOMPARE(terminal(tank, n2)->markChoice(), PipeConnection::MarkChoice::Never);
				// and its size
			tab.fieldFor(n1)->setText(QStringLiteral("DN 80"));
			emit tab.fieldFor(n1)->editingFinished();
			QCOMPARE(terminal(tank, n2)->diameter(), PipeDiameter::nominal(80));
			QVERIFY(tab.sameSizeBox()->isChecked());

				// unchecked: N2 has a connection of its own
			tab.sameConnectionBox()->setChecked(false);
			QVERIFY(tab.connectionFor(n2)->isEnabled());
			pick(tab.connectionFor(n2), PipeConnection::Thread);
			QCOMPARE(terminal(tank, n1)->connection(), PipeConnection::Socket);
			QCOMPARE(terminal(tank, n2)->connection(), PipeConnection::Thread);
				// checked again: N2 takes the one of N1, in one undo step...
			const int before = folio->undoStack().count();
			tab.sameConnectionBox()->setChecked(true);
			QCOMPARE(folio->undoStack().count(), before + 1);
			QCOMPARE(terminal(tank, n2)->connection(), PipeConnection::Socket);
				// ...undone: unlike again, the box unchecks itself
			folio->undoStack().undo();
			QCOMPARE(terminal(tank, n2)->connection(), PipeConnection::Thread);
			QVERIFY(!tab.sameConnectionBox()->isChecked());
			QVERIFY(tab.connectionFor(n2)->isEnabled());
		}

			// A valve has the size of its line (step 5l-1j): no box "one size
			// for all", no size kept, the DN of its symbol ignored
		void aValveHasNoBoxForOneSize()
		{
			QETProject project;
			project.setPidCodification(codification());
			Diagram *folio = project.addNewDiagram();
			project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("nozzles"), NamesList());
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" pipe_inline=\"true\" "
				"width=\"60\" height=\"40\" hotspot_x=\"30\" hotspot_y=\"20\">"
				"<uuid uuid=\"{7c222222-2222-2222-2222-222222222222}\"/>"
				"<names><name lang=\"en\">Valve</name></names>"
				"<description>"
				"<terminal uuid=\"{7c000000-0000-0000-0000-000000000011}\" name=\"N1\" x=\"-20\" y=\"0\" "
				"orientation=\"w\" type=\"Generic\" medium=\"pipe\" dn=\"100\"/>"
				"<terminal uuid=\"{7c000000-0000-0000-0000-000000000012}\" name=\"N2\" x=\"20\" y=\"0\" "
				"orientation=\"e\" type=\"Generic\" medium=\"pipe\"/>"
				"</description></definition>"));
			project.embeddedElementCollection()->addElementDefinition(
						QStringLiteral("import/nozzles"), QStringLiteral("valve.elmt"), doc.documentElement());
			int state = 0;
			Element *valve = ElementFactory::Instance()->createElement(
						ElementsLocation(QStringLiteral("embed://import/nozzles/valve.elmt"), &project),
						nullptr, &state);
			QVERIFY(valve && !state);
			folio->addItem(valve);
			ElementNozzlesWidget tab(valve);
			tab.setLiveEdit(true);
			QVERIFY(tab.sameConnectionBox());
			QVERIFY(!tab.sameSizeBox());
			QVERIFY(!tab.fieldFor(QStringLiteral("N1"))->parentWidget()->isEnabled());
			tab.fieldFor(QStringLiteral("N1"))->setText(QStringLiteral("DN 80"));
			emit tab.fieldFor(QStringLiteral("N1"))->editingFinished();
			QVERIFY(!terminal(valve, QStringLiteral("N1"))->diameter().isSet());
			QVERIFY(!terminal(valve, QStringLiteral("N2"))->diameter().isSet());
			QVERIFY(valve->nozzleDiameters().isEmpty());
				// one connection for all still goes from N1 to N2
			QVERIFY(tab.sameConnectionBox()->isChecked());
			pick(tab.connectionFor(QStringLiteral("N1")), PipeConnection::Socket);
			QCOMPARE(terminal(valve, QStringLiteral("N2"))->connection(), PipeConnection::Socket);
			QVERIFY(valve->nozzleDiameters().isEmpty());
		}

		void aSizeTheTableJoinsTwiceIsRemoved()
		{
			QETProject project;
			PidCodification table = codification();
			table.materials[1].series << 1250;
			table.outside_diameters.insert(1100, {100, 125});
			project.setPidCodification(table);
			Diagram *folio = project.addNewDiagram();
			Element *tank = placeTank(project, folio);
			QVERIFY(tank);
			tank->setNozzleConnections({{QStringLiteral("N1"), PipeConnection::Socket}});
			tank->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::outside(110)}});
			ElementNozzlesWidget tab(tank);
			tab.setLiveEdit(true);
				// Ø 110 is DN 100 or DN 125: neither is chosen, the nozzle has
				// the DN of its symbol again
			pick(tab.connectionFor(QStringLiteral("N1")), PipeConnection::Flange);
			QVERIFY(tank->nozzleConnections().isEmpty());
			QVERIFY(tank->nozzleDiameters().isEmpty());
			QVERIFY(tab.fieldFor(QStringLiteral("N1"))->text().isEmpty());
			QCOMPARE(terminal(tank, QStringLiteral("N1"))->diameter(), PipeDiameter::nominal(100));
			QVERIFY(tab.message().contains(QStringLiteral("Ø 110")));
		}

			// ---- pipe threads (5k-2) ----
		void aThreadIsNamedInInches()
		{
				// shown with G, written in a file without it
			QCOMPARE(PipeDiameter::thread(12).text(), QStringLiteral("G 1\"1/2"));
			QCOMPARE(PipeDiameter::thread(4).text(), QStringLiteral("G 1/2\""));
			QCOMPARE(PipeDiameter::thread(3).text(), QStringLiteral("G 3/8\""));
			QCOMPARE(PipeDiameter::thread(16).text(), QStringLiteral("G 2\""));
			QCOMPARE(PipeDiameter::thread(20).text(), QStringLiteral("G 2\"1/2"));
			QCOMPARE(PipeDiameter::thread(12).fileText(), QStringLiteral("1 1/2"));
			QCOMPARE(PipeDiameter::thread(4).fileText(), QStringLiteral("1/2"));
			QCOMPARE(PipeDiameter::thread(16).fileText(), QStringLiteral("2"));

				// typed in several ways
			bool ok = false;
			QCOMPARE(PipeDiameter::fromText(QStringLiteral("G 1\"1/2"), PipeDiameter::Nominal, &ok),
					 PipeDiameter::thread(12));
			QVERIFY(ok);
			QCOMPARE(PipeDiameter::fromText(QStringLiteral("1\"1/2")), PipeDiameter::thread(12));
			QCOMPARE(PipeDiameter::fromText(QStringLiteral("g3/4")), PipeDiameter::thread(6));
			QCOMPARE(PipeDiameter::fromText(QStringLiteral("3/4\"")), PipeDiameter::thread(6));
			QCOMPARE(PipeDiameter::fromText(QStringLiteral("1-1/4"), PipeDiameter::Thread),
					 PipeDiameter::thread(10));
			QCOMPARE(PipeDiameter::fromText(QStringLiteral("1 1/2"), PipeDiameter::Thread),
					 PipeDiameter::thread(12));
				// a number alone is of the kind given
			QCOMPARE(PipeDiameter::fromText(QStringLiteral("2"), PipeDiameter::Thread),
					 PipeDiameter::thread(16));
			QCOMPARE(PipeDiameter::fromText(QStringLiteral("2")), PipeDiameter::nominal(2));
				// G is no D: never an outside diameter
			QVERIFY(PipeDiameter::fromText(QStringLiteral("G 2")).isThread());
				// not inches
			for (const QString &wrong : {QStringLiteral("G"), QStringLiteral("G 3/2"),
										 QStringLiteral("G 1/3"), QStringLiteral("G 1,5"),
										 QStringLiteral("1/2/4"), QStringLiteral("G 0")}) {
				PipeDiameter::fromText(wrong, PipeDiameter::Thread, &ok);
				QVERIFY2(!ok, qPrintable(wrong));
			}
				// each one of the series is read back as shown
			const QList<PipeDiameter> series = PipeDiameter::threadSeries();
			QCOMPARE(series.size(), 14);
			QCOMPARE(series.constFirst(), PipeDiameter::thread(1));
			QCOMPARE(series.constLast(), PipeDiameter::thread(48));
			for (const PipeDiameter &size : series) {
				QCOMPARE(PipeDiameter::fromText(size.text()), size);
				QDomDocument doc;
				QDomElement e = doc.createElement(QStringLiteral("nozzle"));
				size.toXml(e, QStringLiteral("dn"), QStringLiteral("od"), QStringLiteral("thread"));
				QCOMPARE(PipeDiameter::fromXml(e, QStringLiteral("dn"), QStringLiteral("od"),
											   QStringLiteral("thread")), size);
			}
				// a pipe never writes one: no name given, nothing written
			QDomDocument doc;
			QDomElement pipe = doc.createElement(QStringLiteral("conductor"));
			PipeDiameter::thread(12).toXml(pipe, QStringLiteral("pipe_dn"), QStringLiteral("pipe_od"));
			QVERIFY(pipe.attributes().isEmpty());
		}

		void aThreadIsJoinedToItsDnByTheFixedTable()
		{
			QCOMPARE(PipeDiameter::thread(4).threadNominal(), PipeDiameter::nominal(15));
			QCOMPARE(PipeDiameter::thread(12).threadNominal(), PipeDiameter::nominal(40));
			QCOMPARE(PipeDiameter::thread(48).threadNominal(), PipeDiameter::nominal(150));
			QCOMPARE(PipeDiameter::threadOfNominal(PipeDiameter::nominal(25)), PipeDiameter::thread(8));
			QVERIFY(!PipeDiameter::threadOfNominal(PipeDiameter::nominal(300)).isSet());
			QVERIFY(!PipeDiameter::thread(64).threadNominal().isSet());

				// a thread takes inches only
			QVERIFY(PipeConnection::kinds(PipeConnection::Thread)
					== QList<PipeDiameter::Kind>{PipeDiameter::Thread});
			QVERIFY(PipeConnection::allows(PipeConnection::Thread, PipeDiameter::thread(12)));
			QVERIFY(!PipeConnection::allows(PipeConnection::Thread, PipeDiameter::nominal(40)));
			QVERIFY(!PipeConnection::allows(PipeConnection::Flange, PipeDiameter::thread(12)));
				// fitted: the table between a DN and a thread, nothing else
			QCOMPARE(PipeConnection::fitted(PipeConnection::Thread, PipeDiameter::nominal(40)),
					 PipeDiameter::thread(12));
			QCOMPARE(PipeConnection::fitted(PipeConnection::Flange, PipeDiameter::thread(12)),
					 PipeDiameter::nominal(40));
			QCOMPARE(PipeConnection::fitted(PipeConnection::PlainEnd, PipeDiameter::thread(32)),
					 PipeDiameter::nominal(100));
			QVERIFY(!PipeConnection::fitted(PipeConnection::Thread, PipeDiameter::outside(48.3)).isSet());
			QVERIFY(!PipeConnection::fitted(PipeConnection::Thread, PipeDiameter::nominal(300)).isSet());
			QVERIFY(!PipeConnection::fitted(PipeConnection::Flange, PipeDiameter::outside(110)).isSet());

				// the mark compares a thread by its DN
			const PidCodification table = codification();
			using Match = PidCodification::DiameterMatch;
			QCOMPARE(table.compare(PipeDiameter::thread(32), PipeDiameter::nominal(100)), Match::Same);
			QCOMPARE(table.compare(PipeDiameter::nominal(80), PipeDiameter::thread(32)), Match::Different);
			QCOMPARE(table.compare(PipeDiameter::thread(32), PipeDiameter::outside(110)), Match::Same);
			QCOMPARE(table.compare(PipeDiameter::thread(12), PipeDiameter::outside(125)), Match::Unknown);
			QCOMPARE(table.compare(PipeDiameter::thread(12), PipeDiameter::thread(12)), Match::Same);
			QCOMPARE(table.compare(PipeDiameter::thread(12), PipeDiameter::thread(16)), Match::Different);

				// the lists of a nozzle always offer them, after the others
			const QList<PipeDiameter> sizes = table.nozzleSizes();
			QCOMPARE(sizes.mid(sizes.size() - 14), PipeDiameter::threadSeries());
			QVERIFY(table.acceptsForNozzle(PipeDiameter::thread(12)));
			QVERIFY(!table.acceptsForNozzle(PipeDiameter::thread(64)));
			QVERIFY(PidCodification().acceptsForNozzle(PipeDiameter::thread(1)));
		}

		void theSymbolWritesAThread()
		{
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"0\" name=\"N1\" orientation=\"e\" type=\"Generic\" "
				"medium=\"pipe\" connection=\"thread\" thread=\"1 1/2\"/>"));
			PartTerminal part(nullptr);
			part.fromXml(doc.documentElement());
			QCOMPARE(part.diameter(), PipeDiameter::thread(12));
			QCOMPARE(part.diameterText(), QStringLiteral("G 1\"1/2"));
			QDomDocument out;
			const QDomElement written = part.toXml(out);
			QCOMPARE(written.attribute(QStringLiteral("thread")), QStringLiteral("1 1/2"));
			QVERIFY(!written.hasAttribute(QStringLiteral("dn")));
			QVERIFY(!written.hasAttribute(QStringLiteral("od")));

				// the property the editor's undo uses
			part.setDiameterText(QStringLiteral("G 3/4\""));
			QCOMPARE(part.diameter(), PipeDiameter::thread(6));

				// a DN given to a thread before the threads existed: in inches
			doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"0\" name=\"N1\" orientation=\"e\" type=\"Generic\" "
				"medium=\"pipe\" connection=\"thread\" dn=\"40\"/>"));
			PartTerminal old(nullptr);
			old.fromXml(doc.documentElement());
			QCOMPARE(old.diameter(), PipeDiameter::thread(12));
			QDomDocument out2;
			QCOMPARE(old.toXml(out2).attribute(QStringLiteral("thread")), QStringLiteral("1 1/2"));
				// one the table has not is kept as written
			doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"0\" name=\"N1\" orientation=\"e\" type=\"Generic\" "
				"medium=\"pipe\" connection=\"thread\" dn=\"300\"/>"));
			PartTerminal kept(nullptr);
			kept.fromXml(doc.documentElement());
			QCOMPARE(kept.diameter(), PipeDiameter::nominal(300));
		}

		void aPlacedThreadedNozzleAndItsPipe()
		{
			QETProject project;
			project.setPidCodification(codification());
			Diagram *folio = project.addNewDiagram();
			Element *tank = placeTank(project, folio);
			QVERIFY(tank);
			Terminal *n1 = terminal(tank, QStringLiteral("N1"));
			Terminal *n2 = terminal(tank, QStringLiteral("N2"));

				// N1, a flange of DN 100, threaded: G 4" by the table
			tank->setNozzleConnections({{QStringLiteral("N1"), PipeConnection::Thread},
										{QStringLiteral("N2"), PipeConnection::Thread}});
			QCOMPARE(n1->diameter(), PipeDiameter::thread(32));
			QCOMPARE(n1->labelText(), QStringLiteral("N1\nG 4\""));
			QCOMPARE(n1->toolTip(), QStringLiteral("Pipe thread"));
				// a DN of an older file too
			tank->setNozzleDiameters({{QStringLiteral("N2"), PipeDiameter::nominal(40)}});
			QCOMPARE(n2->diameter(), PipeDiameter::thread(12));
			tank->setNozzleDiameters({{QStringLiteral("N2"), PipeDiameter::thread(12)}});
			QCOMPARE(n2->labelText(), QStringLiteral("N2\nG 1\"1/2"));

				// written in the <nozzles> block, and read back
			QDomDocument doc;
			QHash<Terminal *, int> table;
			const QDomElement written = tank->toXml(doc, table);
			QDomElement nozzle = written.firstChildElement(QStringLiteral("nozzles"))
								 .firstChildElement(QStringLiteral("nozzle"));
			while (!nozzle.isNull() && nozzle.attribute(QStringLiteral("name")) != QLatin1String("N2")) {
				nozzle = nozzle.nextSiblingElement(QStringLiteral("nozzle"));
			}
			QCOMPARE(nozzle.attribute(QStringLiteral("thread")), QStringLiteral("1 1/2"));
			QVERIFY(!nozzle.hasAttribute(QStringLiteral("dn")));
			QCOMPARE(Element::nozzleDiametersFromXml(written), tank->nozzleDiameters());

				// a new pipe takes the DN of the table, no thread
			Element *other = placeAnotherTank(project, folio);
			QVERIFY(other);
			Terminal *start = n2;
			Terminal *end = terminal(other, QStringLiteral("N1"));
			QVERIFY(start->canBeLinkedTo(end));
			auto *pipe = new Conductor(start, end);
			folio->addItem(pipe);
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(40));
			QVERIFY(!pipe->properties().m_pipe.outside.isSet());
			QVERIFY(!n2->hasDiameterGap());
				// DN 100 at the other end: marked
			QVERIFY(end->hasDiameterGap());
				// and the pipe is written as a DN
			bool has_dn = false;
			for (const auto &attribute : pipe->properties().m_pipe.attributes()) {
				if (attribute.first == QLatin1String("pipe_dn")) {
					has_dn = attribute.second == QLatin1String("40");
				}
			}
			QVERIFY(has_dn);
				// a pipe of another DN: marked at the thread
			ConductorProperties properties = pipe->properties();
			properties.m_pipe.nominal = PipeDiameter::nominal(50);
			pipe->setProperties(properties);
			QVERIFY(n2->hasDiameterGap());
		}

		void theNozzlesTabOffersTheThreads()
		{
			QETProject project;
			project.setPidCodification(codification());
			Diagram *folio = project.addNewDiagram();
			Element *tank = placeTank(project, folio);
			QVERIFY(tank);
			ElementNozzlesWidget tab(tank);
			tab.setLiveEdit(true);
			QComboBox *connection = tab.connectionFor(QStringLiteral("N1"));
			QLineEdit *n1 = tab.fieldFor(QStringLiteral("N1"));
			auto *list = qobject_cast<QComboBox *>(n1->parentWidget());

				// DN 100 of the symbol becomes G 4", in one undo step
			const int steps = folio->undoStack().count();
			pick(connection, PipeConnection::Thread);
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::thread(32));
			QCOMPARE(n1->text(), QStringLiteral("G 4\""));
			QCOMPARE(folio->undoStack().count(), steps + 1);
				// the threads only, from 1/8"
			QCOMPARE(list->count(), 15);
			QCOMPARE(list->itemText(1), QStringLiteral("G 1/8\""));
				// a number alone is in inches
			n1->setText(QStringLiteral("1 1/2"));
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::thread(12));
			QCOMPARE(n1->text(), QStringLiteral("G 1\"1/2"));
				// one the table has not: the nearest
			n1->setText(QStringLiteral("G 8"));
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::thread(48));
				// a DN typed is refused
			n1->setText(QStringLiteral("DN 40"));
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::thread(48));

				// back to a flange from G 6": DN 150 is not in the lists of
				// the project, so the nozzle has no size of its own
			pick(connection, PipeConnection::Flange);
			QVERIFY(n1->text().isEmpty());
			QVERIFY(tab.message().contains(QStringLiteral("G 6\"")));
				// from G 4": DN 100, the one of the symbol, so nothing is kept
			n1->setText(QString());
			pick(connection, PipeConnection::Thread);
			n1->setText(QStringLiteral("G 4\""));
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::thread(32));
			pick(connection, PipeConnection::Flange);
			QVERIFY(tank->nozzleConnections().isEmpty());
			QVERIFY(tank->nozzleDiameters().isEmpty());
			QCOMPARE(terminal(tank, QStringLiteral("N1"))->labelText(), QStringLiteral("N1\nDN 100"));
		}
};

QET_APP_TEST_MAIN(tst_nozzleconnection)
#include "tst_nozzleconnection.moc"
