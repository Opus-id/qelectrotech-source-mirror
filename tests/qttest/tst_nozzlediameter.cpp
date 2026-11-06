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
#include "diagramview.h"
#include "editor/elementscene.h"
#include "editor/graphicspart/partterminal.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "properties/terminaldata.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "qetresult.h"
#include "properties/elementdata.h"
#include "ui/elementnozzleswidget.h"
#include "ui/pipepropertieswidget.h"
#include "undocommand/changenozzlediameterscommand.h"

#include <QComboBox>
#include <QDomDocument>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QTemporaryDir>
#include <QUndoStack>

/**
	The nominal diameter of a nozzle: a default in the symbol (attribute dn of
	the terminal), shown under the name of the nozzle, that a placed element
	can change (<nozzles> block of the element). Nothing goes from a nozzle to
	the pipe connected there, nor back.
*/
class tst_nozzlediameter : public QObject
{
	Q_OBJECT

		// N1 has a usual size, N2 none, E1 is electrical: a dn means nothing
	static QString symbol(const QString &uuid, int n1_dn, const QString &extra = QString())
	{
		return QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" %3"
			"width=\"60\" height=\"40\" hotspot_x=\"30\" hotspot_y=\"20\">"
			"<uuid uuid=\"%1\"/>"
			"<names><name lang=\"en\">Tank</name></names>"
			"<description>"
			"<line x1=\"-20\" y1=\"0\" x2=\"20\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
			"<terminal uuid=\"{7b000000-0000-0000-0000-000000000001}\" name=\"N1\" x=\"-20\" y=\"0\" "
			"orientation=\"w\" type=\"Generic\" medium=\"pipe\" dn=\"%2\" show_name=\"true\"/>"
			"<terminal uuid=\"{7b000000-0000-0000-0000-000000000002}\" name=\"N2\" x=\"20\" y=\"0\" "
			"orientation=\"e\" type=\"Generic\" medium=\"pipe\" show_name=\"true\"/>"
			"<terminal uuid=\"{7b000000-0000-0000-0000-000000000003}\" name=\"E1\" x=\"0\" y=\"-10\" "
			"orientation=\"n\" type=\"Generic\" dn=\"50\" show_name=\"true\"/>"
			"%4</description></definition>").arg(uuid).arg(n1_dn).arg(extra)
			.arg(extra.contains(QStringLiteral("three"))
				 ? QStringLiteral("<terminal uuid=\"{7b000000-0000-0000-0000-000000000004}\" "
								  "name=\"N3\" x=\"0\" y=\"10\" orientation=\"s\" "
								  "type=\"Generic\" medium=\"pipe\"/>")
				 : QString());
	}

	static ElementsLocation addSymbol(QETProject &project, const QString &file,
									  const QString &uuid, int n1_dn,
									  const QString &extra = QString())
	{
		project.embeddedElementCollection()->createDir(
					QStringLiteral("import"), QStringLiteral("nozzles"), NamesList());
		QDomDocument doc;
		doc.setContent(symbol(uuid, n1_dn, extra));
		project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/nozzles"), file, doc.documentElement());
		return ElementsLocation(QStringLiteral("embed://import/nozzles/") + file, &project);
	}

	static ElementsLocation addTank(QETProject &project)
	{
		return addSymbol(project, QStringLiteral("tank.elmt"),
						 QStringLiteral("{7b111111-1111-1111-1111-111111111111}"), 80);
	}

		// A valve: a line runs through it, and it has no DN of its own
	static ElementsLocation addValve(QETProject &project)
	{
		return addSymbol(project, QStringLiteral("valve.elmt"),
						 QStringLiteral("{7b333333-3333-3333-3333-333333333333}"), 0,
						 QStringLiteral("pipe_inline=\"true\" "));
	}

	static Element *place(Diagram *folio, const ElementsLocation &location)
	{
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

		// A pipe drawn between two nozzles; nullptr if they refuse it
	static Conductor *connect(Diagram *folio, const Element *a, const QString &from,
							  const Element *b, const QString &to)
	{
		Terminal *start = terminal(a, from);
		Terminal *end = terminal(b, to);
		if (!start->canBeLinkedTo(end)) return nullptr;
		auto *pipe = new Conductor(start, end);
		folio->addItem(pipe);
		return pipe;
	}

	static int dn(const Conductor *pipe)
	{
		return pipe->properties().m_pipe.nominal_diameter;
	}

	static void setDn(Conductor *pipe, int value)
	{
		ConductorProperties properties = pipe->properties();
		properties.m_pipe.nominal_diameter = value;
		pipe->setProperties(properties);
	}

	private slots:
			// ---- the symbol ----
		void aNozzleKeepsItsDiameterInTheSymbol()
		{
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"0\" name=\"N1\" orientation=\"e\" type=\"Generic\" "
				"medium=\"pipe\" dn=\"100\"/>"));
			PartTerminal part(nullptr);
			part.fromXml(doc.documentElement());
			QCOMPARE(part.dn(), 100);

			QDomDocument out;
			QCOMPARE(part.toXml(out).attribute(QStringLiteral("dn")), QStringLiteral("100"));
			part.setDn(0);
			QDomDocument out2;
			QVERIFY(!part.toXml(out2).hasAttribute(QStringLiteral("dn")));
		}

		void onlyANozzleHasADiameter()
		{
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"0\" name=\"T1\" orientation=\"e\" type=\"Generic\" dn=\"100\"/>"));
			PartTerminal part(nullptr);
			part.fromXml(doc.documentElement());
			QCOMPARE(part.dn(), 0);
			QDomDocument out;
			QVERIFY(!part.toXml(out).hasAttribute(QStringLiteral("dn")));

				// kept aside while the terminal is not a nozzle
			part.setMedium(TerminalData::Pipe);
			part.setDn(100);
			QCOMPARE(part.labelText(), QStringLiteral("T1\n100"));
			part.setMedium(TerminalData::Electrical);
			QCOMPARE(part.labelText(), QStringLiteral("T1"));
			QDomDocument out2;
			QVERIFY(!part.toXml(out2).hasAttribute(QStringLiteral("dn")));
		}

		void aDiameterIsAPositiveWholeNumber_data()
		{
			QTest::addColumn<QString>("written");
			QTest::addColumn<int>("read");
			QTest::newRow("whole")    << "80"  << 80;
			QTest::newRow("empty")    << ""    << 0;
			QTest::newRow("zero")     << "0"   << 0;
			QTest::newRow("negative") << "-80" << 0;
			QTest::newRow("text")     << "DN"  << 0;
		}

		void aDiameterIsAPositiveWholeNumber()
		{
			QFETCH(QString, written);
			QFETCH(int, read);
			QCOMPARE(TerminalData::dnFromString(written), read);
		}

		void theLabelShowsTheDiameterUnderTheName()
		{
			QCOMPARE(TerminalData::labelText(QStringLiteral("N1"), 80), QStringLiteral("N1\n80"));
			QCOMPARE(TerminalData::labelText(QStringLiteral("N1"), 0), QStringLiteral("N1"));
			QCOMPARE(TerminalData::labelText(QString(), 80), QStringLiteral("80"));

				// two lines take the height of two lines, one the height of one
			const QFontMetrics metrics{QFont()};
			const QSizeF one = TerminalData::labelSize(metrics, QStringLiteral("N1"));
			const QSizeF two = TerminalData::labelSize(metrics, QStringLiteral("N1\n80"));
			QCOMPARE(one, QSizeF(metrics.size(Qt::TextSingleLine, QStringLiteral("N1"))));
			QVERIFY(two.height() > one.height() * 1.5);
		}

			// ---- a placed element ----
		void aPlacedNozzleShowsTheDiameterOfItsSymbol()
		{
			QETProject project;
			Element *tank = place(project.addNewDiagram(), addTank(project));
			QVERIFY(tank);
			QCOMPARE(terminal(tank, QStringLiteral("N1"))->nominalDiameter(), 80);
			QCOMPARE(terminal(tank, QStringLiteral("N1"))->labelText(), QStringLiteral("N1\n80"));
			QCOMPARE(terminal(tank, QStringLiteral("N2"))->nominalDiameter(), 0);
			QCOMPARE(terminal(tank, QStringLiteral("N2"))->labelText(), QStringLiteral("N2"));
				// an electrical terminal shows its name, as it always did
			QCOMPARE(terminal(tank, QStringLiteral("E1"))->nominalDiameter(), 0);
			QCOMPARE(terminal(tank, QStringLiteral("E1"))->labelText(), QStringLiteral("E1"));
		}

		void anElementGivesItsNozzlesTheirOwnDiameter()
		{
			QETProject project;
			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			Terminal *n1 = terminal(a, QStringLiteral("N1"));
			Terminal *n2 = terminal(a, QStringLiteral("N2"));
			const QRectF room = n2->boundingRect();

			a->setNozzleDiameters({{QStringLiteral("N1"), 100}, {QStringLiteral("N2"), 50}});
			QCOMPARE(n1->nominalDiameter(), 100);
			QCOMPARE(n2->labelText(), QStringLiteral("N2\n50"));
				// the room for a second line was there before it came
			QCOMPARE(n2->boundingRect(), room);
			const QFontMetrics metrics{QFont()};
			QVERIFY(room.height() >= TerminalData::labelSize(metrics, n2->labelText()).height());
				// each element its own, from the same symbol
			QCOMPARE(terminal(b, QStringLiteral("N1"))->nominalDiameter(), 80);
				// back to the one of the symbol
			a->setNozzleDiameters({});
			QCOMPARE(n1->nominalDiameter(), 80);
		}

		void aDiameterIsChangedWithUndo()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *tank = place(folio, addTank(project));
			QVERIFY(tank);
			const QMap<QString, int> diameters{{QStringLiteral("N1"), 150}};
			folio->undoStack().push(new ChangeNozzleDiametersCommand(tank, diameters));
			QCOMPARE(tank->nozzleDiameters(), diameters);
			folio->undoStack().undo();
			QVERIFY(tank->nozzleDiameters().isEmpty());
			folio->undoStack().redo();
			QCOMPARE(tank->nozzleDiameters(), diameters);
		}

			// ---- a nozzle and its pipe ----
		void aNewPipeTakesTheDiameterOfANozzle()
		{
			QETProject project;
			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 200);
				// N2 has none, N1 has the 80 of its symbol: whichever end it is
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N1"));
			QCOMPARE(pipe->medium(), TerminalData::Pipe);
			QCOMPARE(dn(pipe), 80);
			QVERIFY(!terminal(b, QStringLiteral("N1"))->hasDiameterGap());
		}

		void aNewPipeTakesTheDiameterOfTheNozzleItStartsFrom()
		{
			QETProject project;
			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 200);
			a->setNozzleDiameters({{QStringLiteral("N1"), 100}});
			Conductor *pipe = connect(folio, a, QStringLiteral("N1"), b, QStringLiteral("N1"));
			QCOMPARE(dn(pipe), 100);
				// the other nozzle keeps its 80, and says it differs
			QCOMPARE(terminal(b, QStringLiteral("N1"))->nominalDiameter(), 80);
			QVERIFY(!terminal(a, QStringLiteral("N1"))->hasDiameterGap());
			QVERIFY(terminal(b, QStringLiteral("N1"))->hasDiameterGap());
			QVERIFY(terminal(b, QStringLiteral("N1"))->toolTip().contains(QStringLiteral("100")));
		}

			// no value to go out of date: a nozzle without a DN of its own
			// shows the one of its pipe, and writes nothing
		void aNozzleWithoutADiameterFollowsItsPipe()
		{
			QETProject project;
			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 200);
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N2"));
			Terminal *n2 = terminal(a, QStringLiteral("N2"));
			QCOMPARE(dn(pipe), 0);
			QCOMPARE(n2->labelText(), QStringLiteral("N2"));

			setDn(pipe, 150);
			QCOMPARE(n2->labelText(), QStringLiteral("N2\n150"));
			QVERIFY(!n2->hasDiameterGap());
			setDn(pipe, 200);
			QCOMPARE(n2->nominalDiameter(), 200);
			QVERIFY(!n2->hasDiameterGap());
			QVERIFY(a->nozzleDiameters().isEmpty());

				// the Nozzles tab shows it in grey, and once a value of its own
				// is emptied the nozzle is back on its pipe, still in grey
			ElementNozzlesWidget tab(a);
			tab.setLiveEdit(true);
			QLineEdit *field = tab.fieldFor(QStringLiteral("N2"));
			QVERIFY(field->text().isEmpty());
			QCOMPARE(field->placeholderText(), QStringLiteral("200"));
			field->setText(QStringLiteral("100"));
			emit field->editingFinished();
			QCOMPARE(n2->nominalDiameter(), 100);
			field->clear();
			emit field->editingFinished();
			QVERIFY(a->nozzleDiameters().isEmpty());
			QCOMPARE(n2->nominalDiameter(), 200);
			QCOMPARE(field->placeholderText(), QStringLiteral("200"));

				// and goes back to nothing without its pipe
			folio->removeItem(pipe);
			QCOMPARE(n2->nominalDiameter(), 0);
			folio->addItem(pipe);
			QCOMPARE(n2->nominalDiameter(), 200);
		}

			// ---- a line through valves ----
			// A valve has one DN: a change of DN made by the user goes through
			// it, in one step; equipment, tees and three-way valves stop it
		void aChangeOfDiameterGoesThroughTheValves()
		{
			QETProject project;
			const ElementsLocation tank = addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0);
			const ElementsLocation valve_symbol = addValve(project);
			Diagram *folio = project.addNewDiagram();
			Element *start = place(folio, tank);
			Element *first = place(folio, valve_symbol);
			Element *second = place(folio, valve_symbol);
			Element *equipment = place(folio, tank);
			Element *end = place(folio, tank);
			QVERIFY(start && first && second && equipment && end);
			first->setPos(400, 200);
			second->setPos(600, 200);
			equipment->setPos(800, 200);
			end->setPos(1000, 200);
			Conductor *a = connect(folio, start, QStringLiteral("N2"), first, QStringLiteral("N1"));
			Conductor *b = connect(folio, first, QStringLiteral("N2"), second, QStringLiteral("N1"));
			Conductor *c = connect(folio, second, QStringLiteral("N2"), equipment, QStringLiteral("N1"));
			Conductor *d = connect(folio, equipment, QStringLiteral("N2"), end, QStringLiteral("N1"));
			QVERIFY(a && b && c && d);
			setDn(d, 65);
			QCOMPARE(a->inlineRun().size(), 3);

			PipePropertiesWidget page({a});
			auto *field = page.findChild<QLineEdit *>(QStringLiteral("nominal_diameter"));
			QVERIFY(field);
			const int steps = folio->undoStack().count();
			field->setText(QStringLiteral("150"));
			field->setModified(true);
			emit field->editingFinished();

			QCOMPARE(dn(a), 150);
			QCOMPARE(dn(b), 150);
			QCOMPARE(dn(c), 150);
				// stopped by the equipment
			QCOMPARE(dn(d), 65);
			QCOMPARE(terminal(first, QStringLiteral("N2"))->nominalDiameter(), 150);
			QVERIFY(first->nozzleDiameters().isEmpty());

				// one step, undone at once
			QCOMPARE(folio->undoStack().count(), steps + 1);
			folio->undoStack().undo();
			QCOMPARE(dn(a), 0);
			QCOMPARE(dn(c), 0);
			folio->undoStack().redo();
			QCOMPARE(dn(c), 150);
		}

			// the same pipe from any side
		void aChangeGoesBothWays()
		{
			QETProject project;
			const ElementsLocation tank = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *left = place(folio, tank);
			Element *valve = place(folio, addValve(project));
			Element *right = place(folio, tank);
			QVERIFY(left && valve && right);
			valve->setPos(400, 200);
			right->setPos(600, 200);
			Conductor *before = connect(folio, left, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			Conductor *after = connect(folio, valve, QStringLiteral("N2"), right, QStringLiteral("N2"));
			QCOMPARE(before->inlineRun().size(), 2);
			QCOMPARE(after->inlineRun().size(), 2);
		}

			// a three-way valve joins lines, as a tee
		void aThreeWayValveStopsAChange()
		{
			QETProject project;
			const ElementsLocation tank = addTank(project);
			const ElementsLocation three = addSymbol(
						project, QStringLiteral("three.elmt"),
						QStringLiteral("{7b555555-5555-5555-5555-555555555555}"), 0,
						QStringLiteral("pipe_inline=\"true\" three=\"true\" "));
			Diagram *folio = project.addNewDiagram();
			Element *left = place(folio, tank);
			Element *valve = place(folio, three);
			Element *right = place(folio, tank);
			QVERIFY(left && valve && right);
			valve->setPos(400, 200);
			right->setPos(600, 200);
			QVERIFY(!valve->isPipeInline());
			Conductor *before = connect(folio, left, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			connect(folio, valve, QStringLiteral("N2"), right, QStringLiteral("N2"));
			QCOMPARE(before->inlineRun().size(), 1);
		}

			// a valve has one DN: given to one nozzle, seen on both
		void aValveHasOneDiameter()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *valve = place(folio, addValve(project));
			QVERIFY(valve);
			QVERIFY(valve->isPipeInline());
			valve->setNozzleDiameters({{QStringLiteral("N1"), 80}});
			QCOMPARE(terminal(valve, QStringLiteral("N2"))->nominalDiameter(), 80);

				// and a symbol that is not a valve keeps two
			Element *tank = place(folio, addTank(project));
			QVERIFY(!tank->isPipeInline());
			tank->setNozzleDiameters({{QStringLiteral("N1"), 200}});
			QCOMPARE(terminal(tank, QStringLiteral("N2"))->nominalDiameter(), 0);
		}

			// a new pipe on the free side of a valve takes the DN of the line
		void aNewPipeOnAValveContinuesTheLine()
		{
			QETProject project;
			const ElementsLocation tank = addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0);
			Diagram *folio = project.addNewDiagram();
			Element *left = place(folio, tank);
			Element *valve = place(folio, addValve(project));
			Element *right = place(folio, tank);
			QVERIFY(left && valve && right);
			valve->setPos(400, 200);
			right->setPos(600, 200);
			Conductor *before = connect(folio, left, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			setDn(before, 100);
			QCOMPARE(terminal(valve, QStringLiteral("N2"))->nominalDiameter(), 100);
			Conductor *after = connect(folio, valve, QStringLiteral("N2"), right, QStringLiteral("N1"));
			QCOMPARE(dn(after), 100);
		}

			// reached anyway (one pipe changed by a script, a paste...): said
			// on both sides, nothing corrected
		void twoDiametersOnAValveAreMarked()
		{
			QETProject project;
			const ElementsLocation tank = addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0);
			Diagram *folio = project.addNewDiagram();
			Element *left = place(folio, tank);
			Element *valve = place(folio, addValve(project));
			Element *right = place(folio, tank);
			QVERIFY(left && valve && right);
			valve->setPos(400, 200);
			right->setPos(600, 200);
			Conductor *before = connect(folio, left, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			Conductor *after = connect(folio, valve, QStringLiteral("N2"), right, QStringLiteral("N1"));
			setDn(before, 100);
			setDn(after, 150);
			Terminal *in = terminal(valve, QStringLiteral("N1"));
			Terminal *out = terminal(valve, QStringLiteral("N2"));
			QVERIFY(in->hasDiameterGap());
			QVERIFY(out->hasDiameterGap());
			QVERIFY(in->toolTip().contains(QStringLiteral("150")));
			QVERIFY(out->toolTip().contains(QStringLiteral("100")));
			QCOMPARE(dn(before), 100);

				// the same again: no mark left on either side
			setDn(after, 100);
			QVERIFY(!in->hasDiameterGap());
			QVERIFY(!out->hasDiameterGap());
			QVERIFY(in->toolTip().isEmpty());
		}

		void theEditorKeepsThatALineRunsThrough()
		{
			QDomDocument doc;
			doc.setContent(symbol(QStringLiteral("{7b666666-6666-6666-6666-666666666666}"), 0,
								  QStringLiteral("pipe_inline=\"true\" ")));
			ElementData data;
			data.fromXml(doc.documentElement());
			QVERIFY(data.m_pipe_inline);
			ElementScene scene(nullptr);
			scene.fromXml(doc, QPointF(), true);
			QCOMPARE(scene.toXml().documentElement().attribute(QStringLiteral("pipe_inline")),
					 QStringLiteral("true"));

				// and writes nothing for any other symbol
			doc.setContent(symbol(QStringLiteral("{7b666666-6666-6666-6666-666666666666}"), 0));
			ElementData other;
			other.fromXml(doc.documentElement());
			QVERIFY(!other.m_pipe_inline);
			ElementScene other_scene(nullptr);
			other_scene.fromXml(doc, QPointF(), true);
			QVERIFY(!other_scene.toXml().documentElement().hasAttribute(QStringLiteral("pipe_inline")));
		}

		void aDifferenceIsMarkedNotCorrected()
		{
			QETProject project;
			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 200);
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N1"));
			Terminal *n2 = terminal(a, QStringLiteral("N2"));
			Terminal *n1 = terminal(b, QStringLiteral("N1"));

				// a value given afterwards changes nothing on the pipe
			a->setNozzleDiameters({{QStringLiteral("N2"), 100}});
			QCOMPARE(dn(pipe), 80);
			QVERIFY(n2->hasDiameterGap());

				// a pipe changed afterwards changes nothing on the nozzles
			setDn(pipe, 200);
			QCOMPARE(n2->nominalDiameter(), 100);
			QCOMPARE(n1->nominalDiameter(), 80);
			QVERIFY(n2->hasDiameterGap());
			QVERIFY(n1->hasDiameterGap());
			QVERIFY(n1->toolTip().contains(QStringLiteral("200")));

				// the mark is over the pipe, which would hide a mark of the element
			QImage image(700, 300, QImage::Format_ARGB32);
			image.fill(Qt::white);
			QPainter painter(&image);
			painter.translate(pipe->pos());
			QStyleOptionGraphicsItem options;
			pipe->paint(&painter, &options, nullptr);
			painter.end();
			QCOMPARE(image.pixelColor(n2->dockConductor().toPoint()), QColor(255, 140, 0));

				// emptied by hand, a pipe stays empty: nothing to compare
			setDn(pipe, 0);
			QCOMPARE(dn(pipe), 0);
			QVERIFY(!n2->hasDiameterGap());
			QVERIFY(n2->toolTip().isEmpty());

				// the same value again: no difference left
			setDn(pipe, 100);
			QVERIFY(!n2->hasDiameterGap());
			QVERIFY(n1->hasDiameterGap());
		}

			// the DN of the pipe is that of the file, whatever its nozzles say
		void aSavedPipeKeepsItsDiameter()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("line.qet"));
			QUuid tank_uuid;
			{
				QETProject project;
				const ElementsLocation location = addTank(project);
				Diagram *folio = project.addNewDiagram();
				Element *a = place(folio, location);
				Element *b = place(folio, location);
				QVERIFY(a && b);
				b->setPos(400, 200);
				Conductor *empty = connect(folio, a, QStringLiteral("N1"), b, QStringLiteral("N1"));
				Conductor *full = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N2"));
				setDn(empty, 0);
				setDn(full, 150);
				tank_uuid = a->uuid();
				project.setFilePath(path);
				QVERIFY(project.write().isOk());
			}

			QETProject back(path);
			Diagram *folio = back.diagrams().first();
			folio->ensureLoaded();
			Element *a = nullptr;
			for (QGraphicsItem *item : folio->items()) {
				if (auto *element = qgraphicsitem_cast<Element *>(item)) {
					if (element->uuid() == tank_uuid) a = element;
				}
			}
			QVERIFY(a);
			Terminal *n1 = terminal(a, QStringLiteral("N1"));
			Terminal *n2 = terminal(a, QStringLiteral("N2"));
			QCOMPARE(n1->conductors().size(), 1);
			QCOMPARE(n2->conductors().size(), 1);
				// emptied before saving, so not filled again by its nozzle
			QCOMPARE(dn(n1->conductors().first()), 0);
			QCOMPARE(n2->nominalDiameter(), 150);
		}

		void anotherSymbolKeepsTheDiametersOfTheElement()
		{
			QETProject project;
			const ElementsLocation location = addTank(project);
			const ElementsLocation other = addSymbol(
						project, QStringLiteral("other.elmt"),
						QStringLiteral("{7b222222-2222-2222-2222-222222222222}"), 65);
			Element *given = place(project.addNewDiagram(), location);
			Element *plain = place(project.diagrams().first(), location);
			QVERIFY(given && plain);
			given->setNozzleDiameters({{QStringLiteral("N1"), 100}});

			QVERIFY(given->changeLocation(other));
			QVERIFY(plain->changeLocation(other));
			QCOMPARE(terminal(given, QStringLiteral("N1"))->nominalDiameter(), 100);
				// the other one follows the new symbol
			QCOMPARE(terminal(plain, QStringLiteral("N1"))->nominalDiameter(), 65);
		}

			// ---- saved with the project ----
		void theDiametersAreSavedWithTheProject_data()
		{
			QTest::addColumn<QString>("suffix");
			QTest::newRow(".qet") << QStringLiteral(".qet");
			QTest::newRow(".qetz") << QStringLiteral(".qetz");
		}

		void theDiametersAreSavedWithTheProject()
		{
			QFETCH(QString, suffix);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("tanks") + suffix);
			QUuid given_uuid, plain_uuid;
			{
				QETProject project;
				const ElementsLocation location = addTank(project);
				Diagram *folio = project.addNewDiagram();
				Element *given = place(folio, location);
				Element *plain = place(folio, location);
				QVERIFY(given && plain);
				plain->setPos(400, 200);
				given->setNozzleDiameters({{QStringLiteral("N1"), 100}});
				given_uuid = given->uuid();
				plain_uuid = plain->uuid();
					// an element that changed nothing writes no <nozzles>
				QDomDocument doc;
				QHash<Terminal *, int> table;
				QVERIFY(plain->toXml(doc, table).firstChildElement(QStringLiteral("nozzles")).isNull());
				const QDomElement nozzle = given->toXml(doc, table)
						.firstChildElement(QStringLiteral("nozzles"))
						.firstChildElement(QStringLiteral("nozzle"));
				QCOMPARE(nozzle.attribute(QStringLiteral("name")), QStringLiteral("N1"));
				QCOMPARE(nozzle.attribute(QStringLiteral("dn")), QStringLiteral("100"));
				project.setFilePath(path);
				QVERIFY(project.write().isOk());
			}

			QETProject back(path);
			QCOMPARE(back.diagrams().size(), 1);
			Diagram *folio = back.diagrams().first();
			folio->ensureLoaded();
			Element *given = nullptr;
			Element *plain = nullptr;
			for (QGraphicsItem *item : folio->items()) {
				if (auto *element = qgraphicsitem_cast<Element *>(item)) {
					if (element->uuid() == given_uuid) given = element;
					if (element->uuid() == plain_uuid) plain = element;
				}
			}
			QVERIFY(given && plain);
			QCOMPARE(terminal(given, QStringLiteral("N1"))->nominalDiameter(), 100);
			QCOMPARE(terminal(plain, QStringLiteral("N1"))->nominalDiameter(), 80);
			QVERIFY(plain->nozzleDiameters().isEmpty());
		}

			// a .qetdb has no place for them: refused rather than lost
		void aQetdbFileIsRefusedWhileANozzleHasItsOwnDiameter()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString qet = dir.filePath(QStringLiteral("tanks.qet"));
			const QString qetdb = dir.filePath(QStringLiteral("tanks.qetdb"));
			{
				QETProject project;
				Element *tank = place(project.addNewDiagram(), addTank(project));
				QVERIFY(tank);
				tank->setNozzleDiameters({{QStringLiteral("N1"), 100}});
				project.setFilePath(qet);
				QVERIFY(project.write().isOk());
			}

			QETProject back(qet);
				// told before any folio is built
			QVERIFY(back.hasSymbolChoices());
			back.setFilePath(qetdb);
			QVERIFY(!back.write().isOk());
			QVERIFY(!QFile::exists(qetdb));

			Diagram *folio = back.diagrams().first();
			folio->ensureLoaded();
			for (QGraphicsItem *item : folio->items())
				if (auto *element = qgraphicsitem_cast<Element *>(item))
					element->setNozzleDiameters({});
			QVERIFY(!back.hasSymbolChoices());
			QVERIFY(back.write().isOk());
			QVERIFY(QFile::exists(qetdb));
		}

			// ---- below 50 % ----
			// The help lines of a nozzle stay, those of an electrical terminal
			// go as they always did. They are children of the terminal, made
			// when it is painted.
		void aNozzleKeepsItsHelpLinesBelowHalfZoom()
		{
			DiagramView::setTerminalHelpLinesShown(true);
			QETProject project;
			Element *tank = place(project.addNewDiagram(), addTank(project));
			QVERIFY(tank);
			Terminal *nozzle = terminal(tank, QStringLiteral("N1"));
			Terminal *electrical = terminal(tank, QStringLiteral("E1"));
			nozzle->drawHelpLine(true);
			electrical->drawHelpLine(true);

			QImage image(100, 100, QImage::Format_ARGB32);
			QPainter painter(&image);
			painter.scale(0.49, 0.49);
			QStyleOptionGraphicsItem options;
			nozzle->paint(&painter, &options, nullptr);
			electrical->paint(&painter, &options, nullptr);
			painter.end();

			QCOMPARE(nozzle->childItems().size(), 2);
			QVERIFY(electrical->childItems().isEmpty());
			nozzle->drawHelpLine(false);
			electrical->drawHelpLine(false);
		}

			// A pipe stays below that zoom, whatever happens to its terminals
		void aPipeStaysBelowHalfZoom_data()
		{
			QTest::addColumn<qreal>("zoom");
			QTest::newRow("49 %") << 0.49;
			QTest::newRow("20 %") << 0.2;
		}

		void aPipeStaysBelowHalfZoom()
		{
			QFETCH(qreal, zoom);
			QETProject project;
			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 200);
			auto *pipe = new Conductor(terminal(a, QStringLiteral("N2")),
									   terminal(b, QStringLiteral("N1")));
			folio->addItem(pipe);

			QImage image(400, 200, QImage::Format_ARGB32);
			image.fill(Qt::white);
			QPainter painter(&image);
			painter.scale(zoom, zoom);
			painter.translate(pipe->pos());
			QStyleOptionGraphicsItem options;
			pipe->paint(&painter, &options, nullptr);
			painter.end();

			int inked = 0;
			for (int y = 0 ; y < image.height() ; ++y) {
				for (int x = 0 ; x < image.width() ; ++x) {
					if (qGray(image.pixel(x, y)) < 128) {
						++inked;
					}
				}
			}
				// the 160 units between the two nozzles, one pixel wide at least
			QVERIFY2(inked >= int(150 * zoom), qPrintable(QString::number(inked)));
		}

			// ---- the preferred values ----
		void theListHasThePreferredValuesOfIso6708()
		{
			const QList<int> expected{
				10, 15, 20, 25, 32, 40, 50, 60, 65, 80, 100, 125, 150, 200,
				250, 300, 350, 400, 450, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1400,
				1500, 1600, 1800, 2000, 2200, 2400, 2600, 2800, 3000, 3200, 3400, 3600, 3800, 4000};
			QCOMPARE(PipeProperties::preferredDiameters(), expected);
		}

		void aDiameterIsPickedInTheListOrTyped()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *tank = place(folio, addTank(project));
			QVERIFY(tank);
			ElementNozzlesWidget tab(tank);
			tab.setLiveEdit(true);
			QLineEdit *n1 = tab.fieldFor(QStringLiteral("N1"));
			QVERIFY(n1);
			auto *list = qobject_cast<QComboBox *>(n1->parentWidget());
			QVERIFY(list);
				// no DN, then the preferred values
			QCOMPARE(list->count(), PipeProperties::preferredDiameters().size() + 1);
			QVERIFY(list->itemText(0).isEmpty());

				// picked in the list
			const int index = list->findText(QStringLiteral("150"));
			QVERIFY(index > 0);
			list->setCurrentIndex(index);
			emit list->activated(index);
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), 150);

				// another value stays possible, and is kept as typed: no
				// completion turns 12 into 125
			n1->setText(QStringLiteral("12"));
			QVERIFY(n1->hasAcceptableInput());
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), 12);
			QVERIFY(!list->completer());

				// neither text nor a zero in front
			int position = 0;
			QString text = QStringLiteral("DN80");
			QCOMPARE(list->validator()->validate(text, position), QValidator::Invalid);
			text = QStringLiteral("0");
			QCOMPARE(list->validator()->validate(text, position), QValidator::Invalid);
		}

			// ---- the Nozzles tab of the properties of an element ----
		void theNozzlesTabChangesWithUndo()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *tank = place(folio, addTank(project));
			QVERIFY(tank);
			QVERIFY(ElementNozzlesWidget::hasNozzles(tank));
			QVERIFY(!ElementNozzlesWidget::hasNozzles(nullptr));

			ElementNozzlesWidget tab(tank);
			tab.setLiveEdit(true);
			QLineEdit *n1 = tab.fieldFor(QStringLiteral("N1"));
			QLineEdit *n2 = tab.fieldFor(QStringLiteral("N2"));
			QVERIFY(n1 && n2);
			QVERIFY(!tab.fieldFor(QStringLiteral("E1")));
				// the one of the symbol is shown in grey, nothing is given
			QVERIFY(n1->text().isEmpty());
			QCOMPARE(n1->placeholderText(), QStringLiteral("80"));
			QVERIFY(n2->placeholderText().isEmpty());

			const int steps = folio->undoStack().count();
			n1->setText(QStringLiteral("100"));
			emit n1->editingFinished();
			const QMap<QString, int> given{{QStringLiteral("N1"), 100}};
			QCOMPARE(tank->nozzleDiameters(), given);
			QCOMPARE(folio->undoStack().count(), steps + 1);
				// left again without a change: no other step
			emit n1->editingFinished();
			QCOMPARE(folio->undoStack().count(), steps + 1);

				// the field follows the undo
			folio->undoStack().undo();
			QVERIFY(tank->nozzleDiameters().isEmpty());
			QVERIFY(n1->text().isEmpty());
			folio->undoStack().redo();
			QCOMPARE(n1->text(), QStringLiteral("100"));

				// emptied, or written as in the symbol: nothing of its own.
				// Empty is a finished value: else the field would not apply it
			n1->clear();
			QVERIFY(n1->hasAcceptableInput());
			emit n1->editingFinished();
			QVERIFY(tank->nozzleDiameters().isEmpty());
			n1->setText(QStringLiteral("80"));
			QVERIFY(tab.chosen().isEmpty());
			QVERIFY(!tab.associatedUndo());
		}
};

QET_APP_TEST_MAIN(tst_nozzlediameter)
#include "tst_nozzlediameter.moc"
