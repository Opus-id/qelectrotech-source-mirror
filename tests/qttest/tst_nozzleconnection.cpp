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
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "ui/elementnozzleswidget.h"

#include <QComboBox>
#include <QDomDocument>
#include <QLineEdit>
#include <QUndoStack>

/**
	How a pipe is joined to a nozzle: a flange by default, a plain end, a
	socket, a thread... Given by the symbol (attribute connection of the
	terminal), changed by a placed element (<nozzles> block). The connection
	decides the kind of size of the nozzle: a flange a DN, a solvent socket an
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
			QVERIFY(PipeConnection::allows(PipeConnection::SolventSocket, PipeDiameter::outside(110)));
			QVERIFY(!PipeConnection::allows(PipeConnection::SolventSocket, PipeDiameter::nominal(100)));
			for (PipeConnection::Type type : {PipeConnection::PlainEnd, PipeConnection::FusionSocket,
											   PipeConnection::GasketSocket, PipeConnection::ButtWeld,
											   PipeConnection::Grooved}) {
				QVERIFY(PipeConnection::allows(type, PipeDiameter::nominal(100)));
				QVERIFY(PipeConnection::allows(type, PipeDiameter::outside(110)));
			}
				// none is always allowed
			for (PipeConnection::Type type : PipeConnection::all()) {
				QVERIFY(PipeConnection::allows(type, PipeDiameter()));
			}
				// a number typed alone: an outside diameter for a socket to
				// glue or to weld, else a DN
			QCOMPARE(PipeConnection::kinds(PipeConnection::FusionSocket).constFirst(),
					 PipeDiameter::Outside);
			QCOMPARE(PipeConnection::kinds(PipeConnection::GasketSocket).constFirst(),
					 PipeDiameter::Nominal);
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
			QCOMPARE(part.connection(), PipeConnection::SolventSocket);
			QDomDocument out;
			QCOMPARE(part.toXml(out).attribute(QStringLiteral("connection")),
					 QStringLiteral("solvent_socket"));

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

				// a DN names no socket to glue: the nozzle has no size
			tank->setNozzleConnections({{QStringLiteral("N1"), PipeConnection::SolventSocket}});
			QCOMPARE(n1->connection(), PipeConnection::SolventSocket);
			QVERIFY(!n1->diameter().isSet());
			QCOMPARE(n1->labelText(), QStringLiteral("N1"));
			QCOMPARE(n1->toolTip(), QStringLiteral("Solvent socket"));
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
			QCOMPARE(nozzle.attribute(QStringLiteral("connection")), QStringLiteral("solvent_socket"));
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
			ElementNozzlesWidget tab(tank);
			tab.setLiveEdit(true);
			QComboBox *connection = tab.connectionFor(QStringLiteral("N1"));
			QLineEdit *n1 = tab.fieldFor(QStringLiteral("N1"));
			QVERIFY(connection && n1);
			QCOMPARE(connection->currentData().toInt(), int(PipeConnection::Flange));
			auto *list = qobject_cast<QComboBox *>(n1->parentWidget());
				// a flange: the DN only
			QCOMPARE(list->count(), 3);
			QCOMPARE(n1->placeholderText(), QStringLiteral("DN 100"));

				// a socket to glue: DN 100 of the symbol becomes Ø 110, the
				// size the table joins to it, in one undo step
			const int steps = folio->undoStack().count();
			pick(connection, PipeConnection::SolventSocket);
			QCOMPARE(tank->nozzleConnections().value(QStringLiteral("N1")), PipeConnection::SolventSocket);
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::outside(110));
			QCOMPARE(n1->text(), QStringLiteral("Ø 110"));
			QVERIFY(tab.message().contains(QStringLiteral("Ø 110")));
			QCOMPARE(folio->undoStack().count(), steps + 1);
				// the outside diameters only
			QCOMPARE(list->count(), 3);
			QCOMPARE(list->itemText(1), QStringLiteral("Ø 110"));
				// a DN typed is refused
			n1->setText(QStringLiteral("DN 80"));
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::outside(110));
			QCOMPARE(n1->text(), QStringLiteral("Ø 110"));
				// a number alone is an outside diameter
			n1->setText(QStringLiteral("125"));
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::outside(125));

				// undone: the flange and the DN of the symbol again
			folio->undoStack().undo();
			folio->undoStack().undo();
			QVERIFY(tank->nozzleConnections().isEmpty());
			QVERIFY(tank->nozzleDiameters().isEmpty());
			QCOMPARE(connection->currentData().toInt(), int(PipeConnection::Flange));
			QCOMPARE(terminal(tank, QStringLiteral("N1"))->labelText(), QStringLiteral("N1\nDN 100"));

				// and back to a flange from Ø 110: DN 100, the one of the
				// symbol, so nothing is kept
			folio->undoStack().redo();
			pick(connection, PipeConnection::Flange);
			QVERIFY(tank->nozzleConnections().isEmpty());
			QVERIFY(tank->nozzleDiameters().isEmpty());
		}

		void aSizeTheTableJoinsTwiceIsRemoved()
		{
			QETProject project;
			PidCodification table = codification();
			table.outside_diameters.insert(1250, {100, 125});
			project.setPidCodification(table);
			Diagram *folio = project.addNewDiagram();
			Element *tank = placeTank(project, folio);
			QVERIFY(tank);
			ElementNozzlesWidget tab(tank);
			tab.setLiveEdit(true);
				// DN 100 is Ø 110 or Ø 125: neither is chosen
			pick(tab.connectionFor(QStringLiteral("N1")), PipeConnection::SolventSocket);
			QCOMPARE(tank->nozzleConnections().value(QStringLiteral("N1")), PipeConnection::SolventSocket);
			QVERIFY(tank->nozzleDiameters().isEmpty());
			QVERIFY(tab.fieldFor(QStringLiteral("N1"))->text().isEmpty());
			QVERIFY(!terminal(tank, QStringLiteral("N1"))->diameter().isSet());
			QVERIFY(tab.message().contains(QStringLiteral("DN 100")));
		}
};

QET_APP_TEST_MAIN(tst_nozzleconnection)
#include "tst_nozzleconnection.moc"
