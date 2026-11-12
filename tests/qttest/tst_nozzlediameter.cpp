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
#include "editor/ui/pipingblockwidget.h"
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
#include "ui/configpage/pidconfigpage.h"
#include "ui/elementnozzleswidget.h"
#include "ui/pipepropertieswidget.h"
#include "undocommand/changeelementlocationcommand.h"
#include "undocommand/changenozzlediameterscommand.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSignalSpy>
#include <QPushButton>
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
	can change (<pid_nozzles> block of the element). Nothing goes from a nozzle to
	the pipe connected there, nor back.
	The nozzles a line runs through (a valve, a tee) and a free end have no
	size of their own: they have the one of their line (step 5l-1j).
	The nozzles of the symbols here are plain ends, which take a DN and an
	outside diameter alike (see tst_nozzleconnection for the others).
*/
class tst_nozzlediameter : public QObject
{
	Q_OBJECT

		// A plastic, named by its outside diameter, of the sizes @p sizes:
		// outside diameter in tenths of a millimetre, then DN (0 for none)
	static PidCodification::Entry plasticOf(const QString &code,
											const QList<PidCodification::Size> &sizes)
	{
		PidCodification::Entry plastic;
		plastic.code = code;
		plastic.outside_diameter = true;
		for (const PidCodification::Size &size : sizes) {
			plastic.addSize(size.outside, size.nominal);
		}
		return plastic;
	}

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
			"orientation=\"w\" type=\"Generic\" medium=\"pipe\" pid_connection=\"plain_end\" pid_dn=\"%2\" "
			"show_name=\"true\"/>"
			"<terminal uuid=\"{7b000000-0000-0000-0000-000000000002}\" name=\"N2\" x=\"20\" y=\"0\" "
			"orientation=\"e\" type=\"Generic\" medium=\"pipe\" pid_connection=\"plain_end\" show_name=\"true\"/>"
			"<terminal uuid=\"{7b000000-0000-0000-0000-000000000003}\" name=\"E1\" x=\"0\" y=\"-10\" "
			"orientation=\"n\" type=\"Generic\" pid_dn=\"50\" show_name=\"true\"/>"
			"%4</description></definition>").arg(uuid).arg(n1_dn).arg(extra)
			.arg(extra.contains(QStringLiteral("three"))
				 ? QStringLiteral("<terminal uuid=\"{7b000000-0000-0000-0000-000000000004}\" "
								  "name=\"N3\" x=\"0\" y=\"10\" orientation=\"s\" "
								  "type=\"Generic\" medium=\"pipe\" pid_connection=\"plain_end\"/>")
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
						 QStringLiteral("pid_inline=\"true\" "));
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
		return qRound(pipe->properties().m_pipe.nominal.value());
	}

	static void setDn(Conductor *pipe, int value)
	{
		setDiameter(pipe, PipeDiameter::nominal(value));
	}

		// One size alone: a DN, or an outside diameter without a DN
	static void setDiameter(Conductor *pipe, const PipeDiameter &diameter)
	{
		setSizes(pipe, diameter.isOutside() ? PipeDiameter() : diameter,
				 diameter.isOutside() ? diameter : PipeDiameter());
	}

		// The DN of the line and the outside diameter of a plastic pipe
	static void setSizes(Conductor *pipe, const PipeDiameter &nominal, const PipeDiameter &outside)
	{
		ConductorProperties properties = pipe->properties();
		properties.m_pipe.nominal = nominal;
		properties.m_pipe.outside = outside;
		pipe->setProperties(properties);
	}

		// DN by name of nozzle
	static NozzleDiameters dns(std::initializer_list<std::pair<QString, int>> values)
	{
		NozzleDiameters diameters;
		for (const auto &value : values) {
			diameters.insert(value.first, PipeDiameter::nominal(value.second));
		}
		return diameters;
	}

	private slots:
			// ---- the symbol ----
		void aNozzleKeepsItsDiameterInTheSymbol()
		{
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"0\" name=\"N1\" orientation=\"e\" type=\"Generic\" "
				"medium=\"pipe\" pid_dn=\"100\"/>"));
			PartTerminal part(nullptr);
			part.fromXml(doc.documentElement());
			QCOMPARE(part.diameter(), PipeDiameter::nominal(100));

			QDomDocument out;
			QCOMPARE(part.toXml(out).attribute(QStringLiteral("pid_dn")), QStringLiteral("100"));
			part.setDiameter(PipeDiameter());
			QDomDocument out2;
			QVERIFY(!part.toXml(out2).hasAttribute(QStringLiteral("pid_dn")));

				// an outside diameter, for a socket: od, never dn
			part.setDiameterText(QStringLiteral("Ø 110"));
			QCOMPARE(part.diameter(), PipeDiameter::outside(110));
			QDomDocument out3;
			const QDomElement written = part.toXml(out3);
			QCOMPARE(written.attribute(QStringLiteral("pid_od")), QStringLiteral("110"));
			QVERIFY(!written.hasAttribute(QStringLiteral("pid_dn")));
			PartTerminal back(nullptr);
			back.fromXml(written);
			QCOMPARE(back.diameter(), PipeDiameter::outside(110));
			QCOMPARE(back.labelText(), QStringLiteral("N1\nØ 110"));
		}

		void onlyANozzleHasADiameter()
		{
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"0\" name=\"T1\" orientation=\"e\" type=\"Generic\" pid_dn=\"100\"/>"));
			PartTerminal part(nullptr);
			part.fromXml(doc.documentElement());
			QVERIFY(!part.diameter().isSet());
			QDomDocument out;
			QVERIFY(!part.toXml(out).hasAttribute(QStringLiteral("pid_dn")));

				// kept aside while the terminal is not a nozzle
			part.setMedium(TerminalData::Pipe);
			part.setDiameter(PipeDiameter::nominal(100));
			QCOMPARE(part.labelText(), QStringLiteral("T1\nDN 100"));
			part.setMedium(TerminalData::Electrical);
			QCOMPARE(part.labelText(), QStringLiteral("T1"));
			QDomDocument out2;
			QVERIFY(!part.toXml(out2).hasAttribute(QStringLiteral("pid_dn")));
		}

		void aDiameterIsAPositiveWholeNumber_data()
		{
			QTest::addColumn<QString>("written");
			QTest::addColumn<int>("read");
			QTest::newRow("whole")    << "80"   << 800;
			QTest::newRow("tenth")    << "88,9" << 889;
			QTest::newRow("empty")    << ""     << 0;
			QTest::newRow("zero")     << "0"    << 0;
			QTest::newRow("negative") << "-80"  << 0;
			QTest::newRow("text")     << "DN"   << 0;
		}

			// in tenths of a millimetre
		void aDiameterIsAPositiveWholeNumber()
		{
			QFETCH(QString, written);
			QFETCH(int, read);
			QCOMPARE(PipeDiameter::tenthsOf(written), read);
		}

		void theLabelShowsTheDiameterUnderTheName()
		{
			QCOMPARE(TerminalData::labelText(QStringLiteral("N1"), PipeDiameter::nominal(80)),
					 QStringLiteral("N1\nDN 80"));
			QCOMPARE(TerminalData::labelText(QStringLiteral("N1"), PipeDiameter()), QStringLiteral("N1"));
			QCOMPARE(TerminalData::labelText(QString(), PipeDiameter::nominal(80)), QStringLiteral("DN 80"));
				// an outside diameter keeps its sign: 110 alone would be a DN
			QCOMPARE(TerminalData::labelText(QStringLiteral("N1"), PipeDiameter::outside(110)),
					 QStringLiteral("N1\nØ 110"));

				// two lines take the height of two lines, one the height of one
			const QFontMetrics metrics{QFont()};
			const QSizeF one = TerminalData::labelSize(metrics, QStringLiteral("N1"));
			const QSizeF two = TerminalData::labelSize(metrics, QStringLiteral("N1\nDN 80"));
			QCOMPARE(one, QSizeF(metrics.size(Qt::TextSingleLine, QStringLiteral("N1"))));
			QVERIFY(two.height() > one.height() * 1.5);
		}

			// ---- a placed element ----
		void aPlacedNozzleShowsTheDiameterOfItsSymbol()
		{
			QETProject project;
			Element *tank = place(project.addNewDiagram(), addTank(project));
			QVERIFY(tank);
			QCOMPARE(terminal(tank, QStringLiteral("N1"))->diameter().value(), 80.0);
			QCOMPARE(terminal(tank, QStringLiteral("N1"))->labelText(), QStringLiteral("N1\nDN 80"));
			QCOMPARE(terminal(tank, QStringLiteral("N2"))->diameter().value(), 0.0);
			QCOMPARE(terminal(tank, QStringLiteral("N2"))->labelText(), QStringLiteral("N2"));
				// an electrical terminal shows its name, as it always did
			QCOMPARE(terminal(tank, QStringLiteral("E1"))->diameter().value(), 0.0);
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

			a->setNozzleDiameters(dns({{QStringLiteral("N1"), 100}, {QStringLiteral("N2"), 50}}));
			QCOMPARE(n1->diameter().value(), 100.0);
			QCOMPARE(n2->labelText(), QStringLiteral("N2\nDN 50"));
				// the room for a second line was there before it came
			QCOMPARE(n2->boundingRect(), room);
				//measured in the font the label is drawn with, not the
				//default font of the application
			const QFontMetrics metrics{n2->labelFont()};
			QVERIFY(room.height() >= TerminalData::labelSize(metrics, n2->labelText()).height());
				// each element its own, from the same symbol
			QCOMPARE(terminal(b, QStringLiteral("N1"))->diameter().value(), 80.0);
				// back to the one of the symbol
			a->setNozzleDiameters({});
			QCOMPARE(n1->diameter().value(), 80.0);
		}

		void aDiameterIsChangedWithUndo()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *tank = place(folio, addTank(project));
			QVERIFY(tank);
			const NozzleDiameters diameters = dns({{QStringLiteral("N1"), 150}});
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
			a->setNozzleDiameters(dns({{QStringLiteral("N1"), 100}}));
			Conductor *pipe = connect(folio, a, QStringLiteral("N1"), b, QStringLiteral("N1"));
			QCOMPARE(dn(pipe), 100);
				// the other nozzle keeps its 80, and says it differs
			QCOMPARE(terminal(b, QStringLiteral("N1"))->diameter().value(), 80.0);
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
			QCOMPARE(n2->labelText(), QStringLiteral("N2\nDN 150"));
			QVERIFY(!n2->hasDiameterGap());
			setDn(pipe, 200);
			QCOMPARE(n2->diameter().value(), 200.0);
			QVERIFY(!n2->hasDiameterGap());
			QVERIFY(a->nozzleDiameters().isEmpty());

				// the Nozzles tab shows it in grey, and once a value of its own
				// is emptied the nozzle is back on its pipe, still in grey
			ElementNozzlesWidget tab(a);
			tab.setLiveEdit(true);
			QLineEdit *field = tab.fieldFor(QStringLiteral("N2"));
			QVERIFY(field->text().isEmpty());
			QCOMPARE(field->placeholderText(), QStringLiteral("DN 200"));
			field->setText(QStringLiteral("100"));
			emit field->editingFinished();
			QCOMPARE(n2->diameter().value(), 100.0);
			field->clear();
			emit field->editingFinished();
			QVERIFY(a->nozzleDiameters().isEmpty());
			QCOMPARE(n2->diameter().value(), 200.0);
			QCOMPARE(field->placeholderText(), QStringLiteral("DN 200"));

				// and goes back to nothing without its pipe
			folio->removeItem(pipe);
			QCOMPARE(n2->diameter().value(), 0.0);
			folio->addItem(pipe);
			QCOMPARE(n2->diameter().value(), 200.0);
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
			auto *field = page.findChild<QLineEdit *>(QStringLiteral("diameter"));
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
			QCOMPARE(terminal(first, QStringLiteral("N2"))->diameter().value(), 150.0);
			QVERIFY(first->nozzleDiameters().isEmpty());

				// one step, undone at once
			QCOMPARE(folio->undoStack().count(), steps + 1);
			folio->undoStack().undo();
			QCOMPARE(dn(a), 0);
			QCOMPARE(dn(c), 0);
			folio->undoStack().redo();
			QCOMPARE(dn(c), 150);
		}

			// The DN goes through the valves whatever the material: each pipe
			// reached takes it as its material has it, with the outside
			// diameter of its material, asked once for a material and a DN;
			// a pipe of the material of the one edited takes its sizes as
			// they are (step 5l-1j, j5c)
		void aChangeOfDiameterGoesThroughAnyMaterial()
		{
			QETProject project;
			PidCodification codification;
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.addSize(0, 65);
			steel.addSize(0, 80);
			steel.addSize(0, 100);
				// DN 100 is a Ø 110 or a Ø 125, none by default
			const PidCodification::Entry plastic = plasticOf(
						QStringLiteral("M01"), {{900, 80}, {1100, 100}, {1250, 100}});
				// the same, Ø 125 by default
			PidCodification::Entry other = plasticOf(
						QStringLiteral("M03"), {{900, 80}, {1100, 100}});
			other.addSize(1250, 100, true);
				// DN 60 where the others have DN 65
			PidCodification::Entry iron;
			iron.code = QStringLiteral("M04");
			iron.addSize(0, 60);
			iron.addSize(0, 80);
			iron.addSize(0, 100);
			codification.materials = {steel, plastic, other, iron};
			codification.addEqualNominals(60, 65);
			project.setPidCodification(codification);

				// steel, plastic, other plastic, cast iron, plastic, through
				// four valves
			const ElementsLocation plain = addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0);
			const ElementsLocation valve_symbol = addValve(project);
			Diagram *folio = project.addNewDiagram();
			QList<Element *> symbols{place(folio, plain)};
			for (int i = 0 ; i < 4 ; ++i) {
				symbols << place(folio, valve_symbol);
			}
			symbols << place(folio, plain);
			QList<Conductor *> line;
			const QStringList materials{QStringLiteral("M02"), QStringLiteral("M01"),
										QStringLiteral("M03"), QStringLiteral("M04"),
										QStringLiteral("M01")};
			for (int i = 0 ; i < symbols.size() ; ++i) {
				QVERIFY(symbols.at(i));
				symbols.at(i)->setPos(200 + 200 * i, 200);
				if (i) {
					Conductor *pipe = connect(folio, symbols.at(i - 1), QStringLiteral("N2"),
											  symbols.at(i), QStringLiteral("N1"));
					QVERIFY(pipe);
					ConductorProperties properties = pipe->properties();
					properties.m_pipe.material = materials.at(i - 1);
					pipe->setProperties(properties);
					line << pipe;
				}
			}
			Conductor *steel_pipe = line.at(0);
			Conductor *plastic_pipe = line.at(1);
			Conductor *other_pipe = line.at(2);
			Conductor *iron_pipe = line.at(3);
			Conductor *last_pipe = line.at(4);
			QCOMPARE(steel_pipe->inlineRun().size(), 5);
			auto od = [](const Conductor *pipe) { return pipe->properties().m_pipe.outside; };

			PipePropertiesWidget page({steel_pipe});
			QLineEdit *field = page.findChild<QLineEdit *>(QStringLiteral("diameter"));
			QVERIFY(field);
			QStringList questions;
			page.setChooser([&questions](const QString &question, const QStringList &) {
				questions << question;
				return 1;
			});
			auto type = [field](const QString &text) {
				field->setText(text);
				field->setModified(true);
				emit field->editingFinished();
			};

				// DN 100 on the steel pipe
			const int steps = folio->undoStack().count();
			type(QStringLiteral("100"));
			for (Conductor *pipe : std::as_const(line)) {
				QCOMPARE(dn(pipe), 100);
			}
			QVERIFY(!od(steel_pipe).isSet());
			QVERIFY(!od(iron_pipe).isSet());
				// one question for the two pipes of the first plastic
			QCOMPARE(questions.size(), 1);
			QVERIFY(questions.first().contains(QStringLiteral("M01")));
			QCOMPARE(od(plastic_pipe), PipeDiameter::outside(125));
			QCOMPARE(od(last_pipe), PipeDiameter::outside(125));
				// the default one of the other, without a question, and said
			QCOMPARE(od(other_pipe), PipeDiameter::outside(125));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("M03")));
				// in one step
			QCOMPARE(folio->undoStack().count(), steps + 1);
			folio->undoStack().undo();
			for (Conductor *pipe : std::as_const(line)) {
				QVERIFY(!pipe->properties().m_pipe.hasSize());
			}
			folio->undoStack().redo();
			QCOMPARE(od(last_pipe), PipeDiameter::outside(125));
				// all of their material: nothing marked
			for (Conductor *pipe : std::as_const(line)) {
				QVERIFY(pipe->offSeriesNote().isEmpty());
			}

				// DN 65: DN 60 on the cast iron, which has that one, and said;
				// the plastics have no DN 65: kept all the same, without an
				// outside diameter, and said
			type(QStringLiteral("65"));
			QCOMPARE(dn(steel_pipe), 65);
			QCOMPARE(dn(iron_pipe), 60);
			QCOMPARE(dn(plastic_pipe), 65);
			QVERIFY(!od(plastic_pipe).isSet());
			QCOMPARE(questions.size(), 1);
			QVERIFY(page.diameterMessage().contains(QStringLiteral("M04")));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("M01")));
				// and marked on the folio: the plastics, not the steel nor
				// the cast iron, whose DN 60 is said equal
			QVERIFY(plastic_pipe->offSeriesNote().contains(QStringLiteral("M01")));
			QVERIFY(last_pipe->offSeriesNote().contains(QStringLiteral("M01")));
			QVERIFY(other_pipe->offSeriesNote().contains(QStringLiteral("M03")));
			QVERIFY(steel_pipe->offSeriesNote().isEmpty());
			QVERIFY(iron_pipe->offSeriesNote().isEmpty());

				// from a plastic pipe: the other pipe of its material takes
				// the outside diameter typed; the other plastic keeps the one
				// it has of this DN
			type(QStringLiteral("100"));
			QCOMPARE(questions.size(), 2);
			QCOMPARE(od(other_pipe), PipeDiameter::outside(125));
			PipePropertiesWidget plastic_page({plastic_pipe});
			plastic_page.setChooser([&questions](const QString &question, const QStringList &) {
				questions << question;
				return 1;
			});
			QLineEdit *plastic_field = plastic_page.findChild<QLineEdit *>(QStringLiteral("diameter"));
			plastic_field->setText(QStringLiteral("110"));
			plastic_field->setModified(true);
			emit plastic_field->editingFinished();
			QCOMPARE(od(plastic_pipe), PipeDiameter::outside(110));
			QCOMPARE(od(last_pipe), PipeDiameter::outside(110));
			QCOMPARE(od(other_pipe), PipeDiameter::outside(125));
			QCOMPARE(dn(steel_pipe), 100);
			QCOMPARE(dn(iron_pipe), 100);
			QCOMPARE(questions.size(), 2);

				// emptied: the whole line has no size
			type(QString());
			for (Conductor *pipe : std::as_const(line)) {
				QVERIFY(!pipe->properties().m_pipe.hasSize());
				QVERIFY(pipe->offSeriesNote().isEmpty());
			}

				// sizes given another way than through the panel: DN 65,
				// said equal to the DN 60 of the cast iron, is of its sizes,
				// DN 50 is not; an outside diameter the plastic has not
			auto give = [](Conductor *pipe, const PipeDiameter &nominal,
						   const PipeDiameter &outside) {
				ConductorProperties properties = pipe->properties();
				properties.m_pipe.nominal = nominal;
				properties.m_pipe.outside = outside;
				pipe->setProperties(properties);
			};
			give(iron_pipe, PipeDiameter::nominal(65), PipeDiameter());
			QVERIFY(iron_pipe->offSeriesNote().isEmpty());
			give(iron_pipe, PipeDiameter::nominal(50), PipeDiameter());
			QVERIFY(iron_pipe->offSeriesNote().contains(QStringLiteral("M04")));
			give(plastic_pipe, PipeDiameter::nominal(100), PipeDiameter::outside(112));
			QVERIFY(plastic_pipe->offSeriesNote().contains(QStringLiteral("112")));
				// none when the material leaves the project
			ConductorProperties properties = iron_pipe->properties();
			properties.m_pipe.material = QStringLiteral("M09");
			iron_pipe->setProperties(properties);
			QVERIFY(iron_pipe->offSeriesNote().isEmpty());
		}

			// there is no flange of DN 60: a flanged nozzle without a DN of
			// its own, on a cast iron line DN 60, is written DN 65
		void aFlangeIsWrittenWithTheUsualDn()
		{
			QETProject project;
			PidCodification codification;
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.addSize(0, 65);
			steel.addSize(0, 80);
			PidCodification::Entry iron;
			iron.code = QStringLiteral("M04");
			iron.addSize(0, 60);
			iron.addSize(0, 80);
			codification.materials = {steel, iron};
			codification.addEqualNominals(60, 65);
			project.setPidCodification(codification);

			const ElementsLocation plain = addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0);
			Diagram *folio = project.addNewDiagram();
			Element *pump = place(folio, plain);
			Element *other = place(folio, plain);
			QVERIFY(pump && other);
			other->setPos(400, 200);
			Conductor *pipe = connect(folio, pump, QStringLiteral("N2"), other, QStringLiteral("N1"));
			QVERIFY(pipe);
			ConductorProperties properties = pipe->properties();
			properties.m_pipe.material = QStringLiteral("M04");
			properties.m_pipe.nominal = PipeDiameter::nominal(60);
			pipe->setProperties(properties);
			Terminal *outlet = terminal(pump, QStringLiteral("N2"));
			Terminal *inlet = terminal(other, QStringLiteral("N1"));

				// plain ends: as the cast iron has it
			QCOMPARE(outlet->diameterText(), QStringLiteral("DN 60"));
				// a flange: the usual one, and the other end stays
			pump->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::Flange}});
			QCOMPARE(outlet->diameterText(), QStringLiteral("DN 65"));
			QCOMPARE(inlet->diameterText(), QStringLiteral("DN 60"));
				// nothing differs, nothing marked
			QVERIFY(!outlet->hasDiameterGap());
			QVERIFY(pipe->offSeriesNote().isEmpty());
				// a DN said equal to no other: as it is
			properties.m_pipe.nominal = PipeDiameter::nominal(80);
			pipe->setProperties(properties);
			QCOMPARE(outlet->diameterText(), QStringLiteral("DN 80"));
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
						QStringLiteral("pid_inline=\"true\" three=\"true\" "));
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

			// the size lives on the line (step 5l-1j): a valve keeps none of
			// its own, so that none goes out of date; a change of DN always
			// goes through it, and a valve smaller than its line takes a
			// reducer on each side
		void aValveKeepsNoSizeOfItsOwn()
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
			Terminal *in = terminal(valve, QStringLiteral("N1"));
			Terminal *out = terminal(valve, QStringLiteral("N2"));
			QVERIFY(in->followsPipe());
			QVERIFY(out->followsPipe());
				// the nozzles of equipment may have a size of their own
			QVERIFY(!terminal(left, QStringLiteral("N2"))->followsPipe());
				// nor does an electrical terminal follow anything
			QVERIFY(!terminal(left, QStringLiteral("E1"))->followsPipe());
			Conductor *before = connect(folio, left, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			Conductor *after = connect(folio, valve, QStringLiteral("N2"), right, QStringLiteral("N2"));
			QCOMPARE(before->inlineRun().size(), 2);

				// two sizes given to it (an old file, a script): not kept,
				// not written, and the line still runs through
			valve->setNozzleDiameters(dns({{QStringLiteral("N1"), 100}, {QStringLiteral("N2"), 80}}));
			QVERIFY(valve->nozzleDiameters().isEmpty());
			QVERIFY(!in->ownDiameter().isSet());
			QVERIFY(!in->diameter().isSet());
			QCOMPARE(before->inlineRun().size(), 2);
			QCOMPARE(after->inlineRun().size(), 2);
			QDomDocument doc;
			QHash<Terminal *, int> table;
			QVERIFY(valve->toXml(doc, table).firstChildElement(QStringLiteral("pid_nozzles")).isNull());

				// it has the size of its line, on both sides
			setDn(before, 100);
			QCOMPARE(in->diameter().value(), 100.0);
			QCOMPARE(out->diameter().value(), 100.0);
			QVERIFY(!in->hasDiameterGap());
				// two sizes on its pipes: marked, as nothing reduces there
			setDn(after, 80);
			QVERIFY(in->hasDiameterGap());
			QVERIFY(out->hasDiameterGap());
		}

			// the size its symbol gives a nozzle of a valve is ignored too
		void theSizeOfTheSymbolIsIgnoredOnAValve()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			const ElementsLocation sized = addSymbol(
						project, QStringLiteral("sized.elmt"),
						QStringLiteral("{7b888888-8888-8888-8888-888888888888}"), 100,
						QStringLiteral("pid_inline=\"true\" "));
			Element *valve = place(folio, sized);
			QVERIFY(valve);
			QVERIFY(valve->isPipeInline());
			Terminal *n1 = terminal(valve, QStringLiteral("N1"));
			QCOMPARE(n1->symbolDiameter(), PipeDiameter::nominal(100));
			QVERIFY(!n1->diameter().isSet());
			QCOMPARE(n1->labelText(), QStringLiteral("N1"));

				// a new pipe takes nothing from it
			Element *tank = place(folio, addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0));
			QVERIFY(tank);
			tank->setPos(400, 200);
			Conductor *pipe = connect(folio, valve, QStringLiteral("N1"), tank, QStringLiteral("N1"));
			QCOMPARE(dn(pipe), 0);
		}

			// the only nozzle of a piece of pipe (a free end, a vent) has
			// the size of its pipe; a piece of pipe of two nozzles out of a
			// run (a reducer) may have sizes of its own
		void aFreeEndHasTheSizeOfItsPipe()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("nozzles"), NamesList());
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" pid_family=\"pipe\" "
				"width=\"20\" height=\"20\" hotspot_x=\"10\" hotspot_y=\"10\">"
				"<uuid uuid=\"{7b999999-9999-9999-9999-999999999999}\"/>"
				"<names><name lang=\"en\">Free end</name></names>"
				"<description>"
				"<line x1=\"0\" y1=\"-5\" x2=\"0\" y2=\"5\" end1=\"none\" end2=\"none\" "
				"length1=\"1.5\" length2=\"1.5\" antialias=\"false\" "
				"style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
				"<terminal uuid=\"{7b000000-0000-0000-0000-000000000091}\" name=\"N1\" x=\"0\" y=\"0\" "
				"orientation=\"w\" type=\"Generic\" medium=\"pipe\" pid_connection=\"plain_end\" pid_dn=\"50\"/>"
				"</description></definition>"));
			project.embeddedElementCollection()->addElementDefinition(
						QStringLiteral("import/nozzles"), QStringLiteral("end.elmt"), doc.documentElement());
			Element *tank = place(folio, addTank(project));
			Element *end = place(folio, ElementsLocation(
									 QStringLiteral("embed://import/nozzles/end.elmt"), &project));
			QVERIFY(tank && end);
			end->setPos(0, 200);
			Terminal *n1 = terminal(end, QStringLiteral("N1"));
			QVERIFY(n1->followsPipe());
				// neither the DN 50 of its symbol nor one given to it
			QVERIFY(!n1->diameter().isSet());
			end->setNozzleDiameters(dns({{QStringLiteral("N1"), 65}}));
			QVERIFY(end->nozzleDiameters().isEmpty());
				// the pipe takes the DN 80 of the tank, and the end has it
			Conductor *pipe = connect(folio, end, QStringLiteral("N1"), tank, QStringLiteral("N1"));
			QVERIFY(pipe);
			QCOMPARE(dn(pipe), 80);
			QCOMPARE(n1->diameter().value(), 80.0);
			QVERIFY(!n1->hasDiameterGap());

				// its tab: one greyed field showing it, and no box
			ElementNozzlesWidget tab(end);
			QVERIFY(!tab.sameConnectionBox());
			QVERIFY(!tab.sameSizeBox());
			QVERIFY(!tab.fieldFor(QStringLiteral("N1"))->parentWidget()->isEnabled());
			QCOMPARE(tab.fieldFor(QStringLiteral("N1"))->placeholderText(), QStringLiteral("DN 80"));
			QVERIFY(tab.connectionFor(QStringLiteral("N1"))->isEnabled());

				// two nozzles and no run: each a size of its own
			Element *reducer = place(folio, addSymbol(
						project, QStringLiteral("reducer.elmt"),
						QStringLiteral("{7baaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}"), 100,
						QStringLiteral("pid_family=\"pipe\" ")));
			QVERIFY(reducer);
			QVERIFY(!terminal(reducer, QStringLiteral("N1"))->followsPipe());
			QCOMPARE(terminal(reducer, QStringLiteral("N1"))->diameter().value(), 100.0);
			reducer->setNozzleDiameters(dns({{QStringLiteral("N2"), 80}}));
			QCOMPARE(terminal(reducer, QStringLiteral("N2"))->diameter().value(), 80.0);
		}

			// the Nozzles tab of a valve: its sizes greyed, showing the size of
			// the line; its connections and their drawing stay free
		void theNozzlesTabGreysTheSizesOfAValve()
		{
			QETProject project;
			const ElementsLocation tank = addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0);
			Diagram *folio = project.addNewDiagram();
			Element *left = place(folio, tank);
			Element *valve = place(folio, addValve(project));
			QVERIFY(left && valve);
			valve->setPos(400, 200);
			Conductor *before = connect(folio, left, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			setDn(before, 100);
			const QString n1 = QStringLiteral("N1");
			const QString n2 = QStringLiteral("N2");
			{
				ElementNozzlesWidget tab(valve);
				tab.setLiveEdit(true);
					// one connection for all, but no "one size for all"
				QVERIFY(tab.sameConnectionBox());
				QVERIFY(!tab.sameSizeBox());
				QVERIFY(!tab.fieldFor(n1)->parentWidget()->isEnabled());
				QVERIFY(!tab.fieldFor(n2)->parentWidget()->isEnabled());
				QVERIFY(tab.fieldFor(n1)->text().isEmpty());
				QCOMPARE(tab.fieldFor(n1)->placeholderText(), QStringLiteral("DN 100"));
					// the free side has the size of the line too
				QCOMPARE(tab.fieldFor(n2)->placeholderText(), QStringLiteral("DN 100"));
				QVERIFY(tab.connectionFor(n1)->isEnabled());
				QVERIFY(tab.markFor(n1)->isEnabled());

					// a size typed anyway is neither kept nor an undo step
				const int steps = folio->undoStack().count();
				tab.fieldFor(n1)->setText(QStringLiteral("150"));
				emit tab.fieldFor(n1)->editingFinished();
				QVERIFY(valve->nozzleDiameters().isEmpty());
				QVERIFY(tab.chosen().isEmpty());
				QCOMPARE(folio->undoStack().count(), steps);

					// its connection is still chosen there, with undo
				QComboBox *connection = tab.connectionFor(n1);
				const int flange = connection->findData(int(PipeConnection::Flange));
				QVERIFY(flange >= 0);
				tab.sameConnectionBox()->setChecked(false);
				connection->setCurrentIndex(flange);
				emit connection->activated(flange);
				QCOMPARE(terminal(valve, n1)->connection(), PipeConnection::Flange);
				QCOMPARE(terminal(valve, n2)->connection(), PipeConnection::PlainEnd);
				QVERIFY(valve->nozzleDiameters().isEmpty());
				QVERIFY(!tab.fieldFor(n1)->parentWidget()->isEnabled());
			}
				// the line given another size: so shows the tab opened again
			setDn(before, 150);
			ElementNozzlesWidget again(valve);
			QCOMPARE(again.fieldFor(n2)->placeholderText(), QStringLiteral("DN 150"));
		}

			// another type for an element: the nozzles which follow their pipe
			// in the new symbol keep no size, and get theirs back on undo
		void aChangeOfTypeDropsTheSizesOfTheNozzlesWhichFollow()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *element = place(folio, addTank(project));
			QVERIFY(element);
			const NozzleDiameters given = dns({{QStringLiteral("N1"), 100}, {QStringLiteral("N2"), 50}});
			element->setNozzleDiameters(given);
			folio->undoStack().push(new ChangeElementLocationCommand(element, addValve(project)));
			QVERIFY(element->isPipeInline());
			QVERIFY(element->nozzleDiameters().isEmpty());
			QVERIFY(!terminal(element, QStringLiteral("N1"))->diameter().isSet());
			folio->undoStack().undo();
			QVERIFY(!element->isPipeInline());
			QCOMPARE(element->nozzleDiameters(), given);
			QCOMPARE(terminal(element, QStringLiteral("N1"))->diameter().value(), 100.0);
		}

			// the run a symbol names (pid_run): a tee takes its branch in;
			// a symbol may leave a nozzle out (the drain of a valve)
		void aTeeHasTheRunItsSymbolNames()
		{
			QETProject project;
			const ElementsLocation tank = addTank(project);
			const ElementsLocation equal = addSymbol(
						project, QStringLiteral("equal.elmt"),
						QStringLiteral("{7b666666-6666-6666-6666-666666666666}"), 0,
						QStringLiteral("pid_run=\"N1 N2 N3\" three=\"true\" "));
			const ElementsLocation reducing = addSymbol(
						project, QStringLiteral("reducing.elmt"),
						QStringLiteral("{7b777777-7777-7777-7777-777777777777}"), 0,
						QStringLiteral("pid_run=\"N1 N2\" three=\"true\" "));
			for (const ElementsLocation &tee_symbol : {equal, reducing})
			{
				Diagram *folio = project.addNewDiagram();
				Element *left = place(folio, tank);
				Element *tee = place(folio, tee_symbol);
				Element *right = place(folio, tank);
				Element *below = place(folio, tank);
				QVERIFY(left && tee && right && below);
				tee->setPos(400, 200);
				right->setPos(600, 200);
				below->setPos(400, 400);
				const bool is_equal = tee_symbol == equal;
				QCOMPARE(tee->pipeRun().size(), is_equal ? 3 : 2);
				QVERIFY(!tee->isPipeInline());
				Conductor *before = connect(folio, left, QStringLiteral("N2"), tee, QStringLiteral("N1"));
				Conductor *after = connect(folio, tee, QStringLiteral("N2"), right, QStringLiteral("N2"));
				Conductor *branch = connect(folio, tee, QStringLiteral("N3"), below, QStringLiteral("N1"));
				QVERIFY(before && after && branch);
				const QList<Conductor *> run = before->inlineRun();
				QVERIFY(run.contains(after));
				QCOMPARE(run.contains(branch), is_equal);
					// the nozzles of the run keep no size; the one left out
					// of it keeps its own
				tee->setNozzleDiameters(dns({{QStringLiteral("N1"), 100}, {QStringLiteral("N3"), 50}}));
				QVERIFY(!terminal(tee, QStringLiteral("N1"))->ownDiameter().isSet());
				QCOMPARE(terminal(tee, QStringLiteral("N3"))->followsPipe(), is_equal);
				QCOMPARE(terminal(tee, QStringLiteral("N3"))->ownDiameter().isSet(), !is_equal);
				QCOMPARE(tee->nozzleDiameters().size(), is_equal ? 0 : 1);
					// the size of the line on the free side of the run; the
					// branch has the DN 80 its pipe took from the tank, or its
					// own, and is marked: no reducer there
				setDn(before, 100);
				QCOMPARE(terminal(tee, QStringLiteral("N2"))->diameter().value(), 100.0);
				QCOMPARE(dn(branch), 80);
				QCOMPARE(terminal(tee, QStringLiteral("N3"))->diameter().value(), is_equal ? 80.0 : 50.0);
				QVERIFY(terminal(tee, QStringLiteral("N3"))->hasDiameterGap());
			}
		}

			// a symbol that is not a valve keeps a size per nozzle
		void equipmentKeepsASizePerNozzle()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *tank = place(folio, addTank(project));
			QVERIFY(tank);
			QVERIFY(!tank->isPipeInline());
			tank->setNozzleDiameters(dns({{QStringLiteral("N1"), 200}}));
			QCOMPARE(terminal(tank, QStringLiteral("N1"))->diameter().value(), 200.0);
			QCOMPARE(terminal(tank, QStringLiteral("N2"))->diameter().value(), 0.0);
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
			QCOMPARE(terminal(valve, QStringLiteral("N2"))->diameter().value(), 100.0);
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
			QCOMPARE(in->toolTip(), QStringLiteral("Plain end"));
		}

			// ---- two symbols placed against each other, without a pipe ----
			// For the sizes, two nozzles on the same point are joined by a
			// pipe of no length that nobody drew (step 5l-1j, j3): a valve
			// against a tank has the size of the nozzle of the tank
		void aValveAgainstATankHasItsSize()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *tank = place(folio, addTank(project));
			Element *valve = place(folio, addValve(project));
			Element *far = place(folio, addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0));
			QVERIFY(tank && valve && far);
			far->setPos(0, 200);
			Terminal *nozzle = terminal(tank, QStringLiteral("N1"));
			Terminal *in = terminal(valve, QStringLiteral("N1"));
			Terminal *out = terminal(valve, QStringLiteral("N2"));

				// apart: nothing
			valve->setPos(100, 200);
			QVERIFY(!out->contactNozzle());
			QVERIFY(!out->diameter().isSet());

				// N2 of the valve on N1 of the tank, which has DN 80
			valve->setPos(160, 200);
			QCOMPARE(out->dockConductor(), nozzle->dockConductor());
			QCOMPARE(out->contactNozzle(), nozzle);
			QCOMPARE(nozzle->contactNozzle(), out);
			QCOMPARE(out->diameter().value(), 80.0);
			QCOMPARE(in->diameter().value(), 80.0);
			QVERIFY(in->labelText().contains(QStringLiteral("80")));
			QVERIFY(!out->hasDiameterGap());
			QVERIFY(!nozzle->hasDiameterGap());
				// nothing is kept for it, and an electrical terminal has none
			QVERIFY(valve->nozzleDiameters().isEmpty());
			QVERIFY(!terminal(tank, QStringLiteral("E1"))->contactNozzle());

				// a new pipe on the other side of the valve continues the line
			Conductor *pipe = connect(folio, far, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			QVERIFY(pipe);
			QCOMPARE(dn(pipe), 80);
				// which stops at the tank
			QCOMPARE(pipe->inlineRun().size(), 1);

				// another size on that pipe: the size lives on the line, so
				// the valve has it on both sides; the nozzle of the tank
				// marks the difference, at the contact, where no pipe
				// draws the mark
			setDn(pipe, 100);
			QCOMPARE(out->diameter().value(), 100.0);
			QVERIFY(!in->hasDiameterGap());
			QVERIFY(!out->hasDiameterGap());
			QVERIFY(nozzle->hasDiameterGap());
			QVERIFY(nozzle->toolTip().contains(QStringLiteral("100")));
			QVERIFY(nozzle->toolTip().contains(QStringLiteral("without a pipe")));
			QImage image(400, 300, QImage::Format_ARGB32);
			image.fill(Qt::white);
			QPainter painter(&image);
			painter.setTransform(nozzle->sceneTransform());
			QStyleOptionGraphicsItem options;
			nozzle->paint(&painter, &options, nullptr);
			painter.end();
			QCOMPARE(image.pixelColor(nozzle->dockConductor().toPoint()), QColor(255, 140, 0));

				// the valve taken away: the contact ends, and its mark
			valve->setPos(100, 200);
			QVERIFY(!nozzle->contactNozzle());
			QVERIFY(!nozzle->hasDiameterGap());
			QCOMPARE(nozzle->toolTip(), QStringLiteral("Plain end"));
			QCOMPARE(out->diameter().value(), 100.0);
			QVERIFY(!out->hasDiameterGap());

				// and back, with the pipe at the size of the tank
			valve->setPos(160, 200);
			setDn(pipe, 80);
			QVERIFY(!nozzle->hasDiameterGap());
			QVERIFY(!in->hasDiameterGap());
			QCOMPARE(nozzle->toolTip(), QStringLiteral("Plain end"));
		}

			// a change of DN goes on through two valves placed against each
			// other, in one step
		void aChangeOfDiameterGoesThroughAContact()
		{
			QETProject project;
			const ElementsLocation plain = addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0);
			const ElementsLocation valve_symbol = addValve(project);
			Diagram *folio = project.addNewDiagram();
			Element *start = place(folio, plain);
			Element *first = place(folio, valve_symbol);
			Element *second = place(folio, valve_symbol);
			Element *end = place(folio, plain);
			QVERIFY(start && first && second && end);
			first->setPos(400, 200);
			second->setPos(440, 200);
			end->setPos(700, 200);
			Terminal *left = terminal(first, QStringLiteral("N2"));
			Terminal *right = terminal(second, QStringLiteral("N1"));
			QCOMPARE(left->contactNozzle(), right);
			Conductor *a = connect(folio, start, QStringLiteral("N2"), first, QStringLiteral("N1"));
			Conductor *b = connect(folio, second, QStringLiteral("N2"), end, QStringLiteral("N1"));
			QVERIFY(a && b);
			QCOMPARE(a->inlineRun().size(), 2);
			QCOMPARE(b->inlineRun().size(), 2);

				// two sizes reached anyway: marked on the valves, at the
				// contact too
			setDn(a, 100);
			QCOMPARE(left->diameter().value(), 100.0);
			QCOMPARE(right->diameter().value(), 100.0);
			setDn(b, 80);
			QVERIFY(left->hasDiameterGap());
			QVERIFY(right->hasDiameterGap());
			QVERIFY(terminal(first, QStringLiteral("N1"))->hasDiameterGap());

			PipePropertiesWidget page({a});
			auto *field = page.findChild<QLineEdit *>(QStringLiteral("diameter"));
			QVERIFY(field);
			const int steps = folio->undoStack().count();
			field->setText(QStringLiteral("150"));
			field->setModified(true);
			emit field->editingFinished();
			QCOMPARE(dn(a), 150);
			QCOMPARE(dn(b), 150);
			QCOMPARE(folio->undoStack().count(), steps + 1);
			QVERIFY(!left->hasDiameterGap());
			QVERIFY(!right->hasDiameterGap());
			QCOMPARE(right->diameter().value(), 150.0);
			folio->undoStack().undo();
			QCOMPARE(dn(a), 100);
			QCOMPARE(dn(b), 80);

				// a pipe drawn between the two nozzles takes the place of the
				// contact
			second->setPos(600, 200);
			QVERIFY(!left->contactNozzle());
			QCOMPARE(a->inlineRun().size(), 1);
		}

			// a reducer against a valve: the change stops there, and each
			// empty side of the reducer has the size of its line
		void aReducerAgainstAValveStopsAChange()
		{
			QETProject project;
			const ElementsLocation plain = addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0);
			Diagram *folio = project.addNewDiagram();
			Element *start = place(folio, plain);
			Element *valve = place(folio, addValve(project));
			Element *reducer = place(folio, addSymbol(
						project, QStringLiteral("reducer.elmt"),
						QStringLiteral("{7baaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}"), 0,
						QStringLiteral("pid_family=\"pipe\" ")));
			Element *end = place(folio, plain);
			QVERIFY(start && valve && reducer && end);
			valve->setPos(400, 200);
			reducer->setPos(440, 200);
			end->setPos(700, 200);
			Terminal *large = terminal(reducer, QStringLiteral("N1"));
			Terminal *small = terminal(reducer, QStringLiteral("N2"));
			QCOMPARE(terminal(valve, QStringLiteral("N2"))->contactNozzle(), large);
			Conductor *a = connect(folio, start, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			Conductor *b = connect(folio, reducer, QStringLiteral("N2"), end, QStringLiteral("N1"));
			QVERIFY(a && b);
			QCOMPARE(a->inlineRun().size(), 1);
			QCOMPARE(b->inlineRun().size(), 1);

			setDn(a, 100);
			setDn(b, 80);
			QCOMPARE(large->diameter().value(), 100.0);
			QCOMPARE(small->diameter().value(), 80.0);
			QVERIFY(!large->hasDiameterGap());
			QVERIFY(!terminal(valve, QStringLiteral("N2"))->hasDiameterGap());
			QVERIFY(!terminal(valve, QStringLiteral("N1"))->hasDiameterGap());
		}

			// two nozzles with a size of their own, one against the other
		void twoSizesAtAContactAreMarked()
		{
			QETProject project;
			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			Terminal *n2 = terminal(a, QStringLiteral("N2"));
			Terminal *n1 = terminal(b, QStringLiteral("N1"));
				// N2 has no size: it takes the DN 80 of the nozzle against it
			b->setPos(240, 200);
			QCOMPARE(n2->contactNozzle(), n1);
			QCOMPARE(n2->diameter().value(), 80.0);
			QVERIFY(!n2->hasDiameterGap());
			QVERIFY(!n1->hasDiameterGap());

			a->setNozzleDiameters(dns({{QStringLiteral("N2"), 100}}));
			QVERIFY(n2->hasDiameterGap());
			QVERIFY(n1->hasDiameterGap());
			QVERIFY(n1->toolTip().contains(QStringLiteral("100")));
			QVERIFY(n1->toolTip().contains(QStringLiteral("without a pipe")));

			a->setNozzleDiameters(dns({{QStringLiteral("N2"), 80}}));
			QVERIFY(!n2->hasDiameterGap());
			QCOMPARE(n1->toolTip(), QStringLiteral("Plain end"));

				// the other symbol removed from the folio: no contact left
			a->setNozzleDiameters(dns({{QStringLiteral("N2"), 100}}));
			QVERIFY(n2->hasDiameterGap());
			folio->removeItem(b);
			QVERIFY(!n2->contactNozzle());
			QVERIFY(!n2->hasDiameterGap());
			QCOMPARE(n2->toolTip(), QStringLiteral("Plain end"));
		}

			// The automatic connection joins two symbols placed against
			// each other by a pipe of no length, neither drawn nor clicked:
			// it is the same contact. Without a size, it has that of its
			// line and stops nothing.
		void aPipeOfNoLengthIsAContact()
		{
			QETProject project;
			const ElementsLocation plain = addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0);
			const ElementsLocation valve_symbol = addValve(project);
			Diagram *folio = project.addNewDiagram();
			Element *start = place(folio, plain);
			Element *first = place(folio, valve_symbol);
			Element *second = place(folio, valve_symbol);
			Element *end = place(folio, plain);
			QVERIFY(start && first && second && end);
			first->setPos(400, 200);
			second->setPos(440, 200);
			end->setPos(700, 200);
			Conductor *hidden = connect(folio, first, QStringLiteral("N2"), second, QStringLiteral("N1"));
			QVERIFY(hidden);
			QVERIFY(hidden->endsTouch());
			QCOMPARE(dn(hidden), 0);
			Terminal *left = terminal(first, QStringLiteral("N2"));
			Terminal *right = terminal(second, QStringLiteral("N1"));
			QCOMPARE(left->contactNozzle(), right);
			QCOMPARE(right->contactNozzle(), left);

				// the line gets a size on the left: seen on the right, and a
				// pipe drawn there continues the line
			Conductor *a = connect(folio, start, QStringLiteral("N2"), first, QStringLiteral("N1"));
			setDn(a, 100);
			QCOMPARE(terminal(second, QStringLiteral("N2"))->diameter().value(), 100.0);
			Conductor *b = connect(folio, second, QStringLiteral("N2"), end, QStringLiteral("N1"));
			QVERIFY(b);
			QCOMPARE(dn(b), 100);
			QCOMPARE(a->inlineRun().size(), 3);
			QVERIFY(!left->hasDiameterGap());
			QVERIFY(!right->hasDiameterGap());

				// two sizes on the two sides: marked, the pipe of no length
				// has none of its own
			setDn(b, 80);
			QVERIFY(left->hasDiameterGap());
			QVERIFY(right->hasDiameterGap());

				// a change in the panel gives it the size of the line too
			PipePropertiesWidget page({a});
			auto *field = page.findChild<QLineEdit *>(QStringLiteral("diameter"));
			QVERIFY(field);
			field->setText(QStringLiteral("150"));
			field->setModified(true);
			emit field->editingFinished();
			QCOMPARE(dn(b), 150);
			QCOMPARE(dn(hidden), 150);
			QVERIFY(!left->hasDiameterGap());
		}

			// such a pipe between a valve and equipment: its ends draw the
			// mark of two sizes, since it draws nothing itself
		void aPipeOfNoLengthMarksTwoSizes()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *tank = place(folio, addTank(project));
			Element *valve = place(folio, addValve(project));
			Element *far = place(folio, addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0));
			QVERIFY(tank && valve && far);
			far->setPos(0, 200);
			valve->setPos(160, 200);
			Terminal *nozzle = terminal(tank, QStringLiteral("N1"));
			Terminal *in = terminal(valve, QStringLiteral("N1"));
			Terminal *out = terminal(valve, QStringLiteral("N2"));
				// from the nozzle of the tank, DN 80
			Conductor *hidden = connect(folio, tank, QStringLiteral("N1"), valve, QStringLiteral("N2"));
			QVERIFY(hidden && hidden->endsTouch());
			QCOMPARE(dn(hidden), 80);
			Conductor *pipe = connect(folio, far, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			QCOMPARE(dn(pipe), 80);

				// the line is given another size: the valve follows, the
				// nozzle of the tank marks it
			PipePropertiesWidget page({pipe});
			auto *field = page.findChild<QLineEdit *>(QStringLiteral("diameter"));
			QVERIFY(field);
			field->setText(QStringLiteral("100"));
			field->setModified(true);
			emit field->editingFinished();
			QCOMPARE(dn(hidden), 100);
			QCOMPARE(out->diameter().value(), 100.0);
			QVERIFY(!in->hasDiameterGap());
			QVERIFY(!out->hasDiameterGap());
			QVERIFY(nozzle->hasDiameterGap());
			QVERIFY(nozzle->toolTip().contains(QStringLiteral("100")));
			QImage image(400, 300, QImage::Format_ARGB32);
			image.fill(Qt::white);
			QPainter painter(&image);
			painter.setTransform(nozzle->sceneTransform());
			QStyleOptionGraphicsItem options;
			nozzle->paint(&painter, &options, nullptr);
			painter.end();
			QCOMPARE(image.pixelColor(nozzle->dockConductor().toPoint()), QColor(255, 140, 0));

				// a pipe of no length made on a line which has a size takes
				// it, whichever end it starts from: the size lives on the line
			folio->removeItem(hidden);
			Conductor *again = connect(folio, tank, QStringLiteral("N1"), valve, QStringLiteral("N2"));
			QVERIFY(again && again->endsTouch());
			QCOMPARE(dn(again), 100);
			QVERIFY(nozzle->hasDiameterGap());
		}

		void theEditorKeepsThatALineRunsThrough()
		{
			QDomDocument doc;
			doc.setContent(symbol(QStringLiteral("{7b666666-6666-6666-6666-666666666666}"), 0,
								  QStringLiteral("pid_inline=\"true\" ")));
			ElementData data;
			data.fromXml(doc.documentElement());
			QVERIFY(data.m_pipe_inline);
			ElementScene scene(nullptr);
			scene.fromXml(doc, QPointF(), true);
			QCOMPARE(scene.toXml().documentElement().attribute(QStringLiteral("pid_inline")),
					 QStringLiteral("true"));

				// and writes nothing for any other symbol
			doc.setContent(symbol(QStringLiteral("{7b666666-6666-6666-6666-666666666666}"), 0));
			ElementData other;
			other.fromXml(doc.documentElement());
			QVERIFY(!other.m_pipe_inline);
			ElementScene other_scene(nullptr);
			other_scene.fromXml(doc, QPointF(), true);
			QVERIFY(!other_scene.toXml().documentElement().hasAttribute(QStringLiteral("pid_inline")));
		}

		void theEditorGreysTheSizeOfANozzleWhichFollows()
		{
			auto sizesWritten = [](const QString &extra, PartTerminal **first) {
				QDomDocument doc;
				doc.setContent(symbol(QStringLiteral("{7b666666-6666-6666-6666-666666666667}"), 100, extra));
				auto *scene = new ElementScene(nullptr);
				scene->fromXml(doc, QPointF(), true);
				*first = nullptr;
				for (QGraphicsItem *item : scene->items()) {
					auto *part = qgraphicsitem_cast<PartTerminal *>(item);
					if (part && part->terminalName() == QLatin1String("N1")) {
						*first = part;
					}
				}
				return scene;
			};
			PartTerminal *n1 = nullptr;
				// inline symbol: N1 follows its pipe, no size shown nor written
			ElementScene *inl = sizesWritten(QStringLiteral("pid_inline=\"true\" "), &n1);
			QVERIFY(n1);
			QVERIFY(n1->followsPipe());
			QVERIFY(!n1->labelText().contains(QStringLiteral("100")));
			QDomDocument out;
			QVERIFY(!n1->toXml(out).hasAttribute(QStringLiteral("pid_dn")));
			delete inl;
				// another symbol: N1 keeps its own size
			ElementScene *plain = sizesWritten(QString(), &n1);
			QVERIFY(n1);
			QVERIFY(!n1->followsPipe());
			QDomDocument out2;
			QCOMPARE(n1->toXml(out2).attribute(QStringLiteral("pid_dn")), QStringLiteral("100"));
			delete plain;
		}

		void thePipingBlockGivesTheRunOfTheLine()
		{
			QDomDocument doc;
			doc.setContent(symbol(QStringLiteral("{7b666666-6666-6666-6666-666666666668}"), 100,
								  QStringLiteral("pid_inline=\"true\" ")));
			ElementScene scene(nullptr);
			scene.fromXml(doc, QPointF(), true);
			const ElementData data = scene.elementData();
			QVERIFY(data.m_pipe_inline);
			QCOMPARE(scene.pipeNozzleNames(), (QStringList{QStringLiteral("N1"), QStringLiteral("N2")}));

			auto boxOf = [](PipingBlockWidget &block, const QString &name) {
				for (QCheckBox *cb : block.findChildren<QCheckBox *>()) {
					if (cb->text() == name) {
						return cb;
					}
				}
				return static_cast<QCheckBox *>(nullptr);
			};

				// shown with the data, and nothing changes without a touch
			PipingBlockWidget block;
			block.setData(data, scene.pipeNozzleNames());
			QVERIFY(!block.isHidden());
			QVERIFY(boxOf(block, QStringLiteral("N1"))->isChecked());
			QVERIFY(boxOf(block, QStringLiteral("N2"))->isChecked());
			QVERIFY(block.applyTo(data) == data);

				// N2 taken out of the run
			QSignalSpy edited(&block, &PipingBlockWidget::edited);
			boxOf(block, QStringLiteral("N2"))->setChecked(false);
			QCOMPARE(edited.count(), 1);
			const ElementData changed = block.applyTo(data);
			QCOMPARE(changed.m_pipe_run, QStringList{QStringLiteral("N1")});
			QVERIFY(changed != data);

				// the scene follows, and the nozzles with it
			PartTerminal *n1 = nullptr, *n2 = nullptr;
			for (QGraphicsItem *item : scene.items()) {
				auto *part = qgraphicsitem_cast<PartTerminal *>(item);
				if (part && part->terminalName() == QLatin1String("N1")) n1 = part;
				if (part && part->terminalName() == QLatin1String("N2")) n2 = part;
			}
			QVERIFY(n1 && n2);
			QVERIFY(n1->followsPipe());
			scene.setElementData(changed);
			QVERIFY(!n1->followsPipe());
			QVERIFY(!n2->followsPipe());

				// setData does not emit edited()
			block.setData(changed, scene.pipeNozzleNames());
			QCOMPARE(edited.count(), 1);

				// the family comes from the list
			ElementData other = block.applyTo(changed);
			QCOMPARE(other.m_pid_family, QString());
		}

			// pid_role="reducer" is read, written back by the editor, given by
			// the list "Role" of the block; nothing is written without it
		void theEditorKeepsTheRoleOfAReducer()
		{
			QDomDocument doc;
			doc.setContent(symbol(QStringLiteral("{7b666666-6666-6666-6666-666666666669}"), 0,
								  QStringLiteral("pid_family=\"pipe\" pid_role=\"reducer\" ")));
			ElementData data;
			data.fromXml(doc.documentElement());
			QCOMPARE(data.m_pid_role, QStringLiteral("reducer"));
			ElementScene scene(nullptr);
			scene.fromXml(doc, QPointF(), true);
			QCOMPARE(scene.toXml().documentElement().attribute(QStringLiteral("pid_role")),
					 QStringLiteral("reducer"));

			PipingBlockWidget block;
			block.setData(scene.elementData(), scene.pipeNozzleNames());
			QVERIFY(block.applyTo(scene.elementData()) == scene.elementData());
			QComboBox *role = nullptr;
			for (QComboBox *list : block.findChildren<QComboBox *>()) {
				if (list->findData(QStringLiteral("reducer")) >= 0) {
					role = list;
				}
			}
			QVERIFY(role);
			QCOMPARE(role->currentData().toString(), QStringLiteral("reducer"));
			role->setCurrentIndex(role->findData(QString()));
			const ElementData none = block.applyTo(scene.elementData());
			QVERIFY(none.m_pid_role.isEmpty());
			QVERIFY(none != scene.elementData());
			scene.setElementData(none);
			QVERIFY(!scene.toXml().documentElement().hasAttribute(QStringLiteral("pid_role")));

				// another symbol has none
			doc.setContent(symbol(QStringLiteral("{7b666666-6666-6666-6666-666666666670}"), 0));
			ElementData other;
			other.fromXml(doc.documentElement());
			QVERIFY(other.m_pid_role.isEmpty());
		}

			// A reducer stops a change of DN, whatever its sizes; one whose two
			// sides have the same size, theirs or their lines', is marked, and
			// the side whose line changes is drawn again
		void aReducerWhichKeepsTheSizeIsMarked()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			const ElementsLocation tank = addTank(project);
			const ElementsLocation reducer_symbol = addSymbol(
						project, QStringLiteral("reducer.elmt"),
						QStringLiteral("{7b555555-5555-5555-5555-555555555556}"), 0,
						QStringLiteral("pid_family=\"pipe\" pid_role=\"reducer\" "));
			Element *reducer = place(folio, reducer_symbol);
			Element *a = place(folio, tank);
			Element *b = place(folio, tank);
			QVERIFY(reducer && a && b);
			QVERIFY(reducer->isReducer());
			QVERIFY(!a->isReducer());
			a->setPos(0, 200);
			b->setPos(400, 200);
			Conductor *big = connect(folio, a, QStringLiteral("N2"), reducer, QStringLiteral("N1"));
			Conductor *small = connect(folio, reducer, QStringLiteral("N2"), b, QStringLiteral("N1"));
			QVERIFY(big && small);
			Terminal *n1 = terminal(reducer, QStringLiteral("N1"));
			Terminal *n2 = terminal(reducer, QStringLiteral("N2"));
			QCOMPARE(n1->reducerOtherSide(), n2);
			QCOMPARE(n2->reducerOtherSide(), n1);
				// its nozzles follow their pipes, each its own
			QVERIFY(!n1->followsPipe());
			setDn(big, 100);
			setDn(small, 80);
			QCOMPARE(n1->diameter(), PipeDiameter::nominal(100));
			QCOMPARE(n2->diameter(), PipeDiameter::nominal(80));
			QVERIFY(!n1->reducesNothing() && !n1->hasDiameterGap() && !n2->hasDiameterGap());
				// a change of DN stops there
			QCOMPARE(big->inlineRun(), QList<Conductor *>{big});

				// the small line given the size of the big one: both sides marked,
				// and said, on the side which did not change too
			setDn(small, 100);
			QVERIFY(n1->reducesNothing() && n2->reducesNothing());
			QVERIFY(n1->hasDiameterGap() && n2->hasDiameterGap());
			QVERIFY(n1->toolTip().contains(QStringLiteral("Reducer without a change of size")));
			setDn(small, 80);
			QVERIFY(!n1->hasDiameterGap());
			QVERIFY(!n1->toolTip().contains(QStringLiteral("Reducer without a change of size")));

				// sizes of its own, the same on both sides: marked too
			reducer->setNozzleDiameters(dns({{QStringLiteral("N1"), 80}, {QStringLiteral("N2"), 80}}));
			QVERIFY(n2->reducesNothing() && n2->hasDiameterGap());
			reducer->setNozzleDiameters(dns({{QStringLiteral("N1"), 100}, {QStringLiteral("N2"), 80}}));
			QVERIFY(!n1->hasDiameterGap() && !n2->hasDiameterGap());

				// the same symbol without the role: no mark for equal sizes
			const ElementsLocation plain_symbol = addSymbol(
						project, QStringLiteral("plain_piece.elmt"),
						QStringLiteral("{7b555555-5555-5555-5555-555555555557}"), 0,
						QStringLiteral("pid_family=\"pipe\" "));
			Element *plain = place(folio, plain_symbol);
			Element *c = place(folio, tank);
			Element *d = place(folio, tank);
			QVERIFY(plain && c && d);
			plain->setPos(200, 600);
			c->setPos(0, 600);
			d->setPos(400, 600);
			Conductor *left = connect(folio, c, QStringLiteral("N2"), plain, QStringLiteral("N1"));
			Conductor *right = connect(folio, plain, QStringLiteral("N2"), d, QStringLiteral("N1"));
			QVERIFY(left && right);
			setDn(left, 100);
			setDn(right, 100);
			QVERIFY(!terminal(plain, QStringLiteral("N1"))->reducerOtherSide());
			QVERIFY(!terminal(plain, QStringLiteral("N1"))->hasDiameterGap());
		}

			// A size typed on a side of a reducer is given to the line of that
			// side, through the valves, in one step with the size of the
			// nozzle; the other side, a line changed later and a size emptied
			// change nothing else (step 5l-1j, j4b)
		void aSizeTypedOnAReducerResizesItsLine()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			const ElementsLocation plain = addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0);
			const ElementsLocation reducer_symbol = addSymbol(
						project, QStringLiteral("reducer.elmt"),
						QStringLiteral("{7b555555-5555-5555-5555-555555555556}"), 0,
						QStringLiteral("pid_family=\"pipe\" pid_role=\"reducer\" "));
			Element *start = place(folio, plain);
			Element *reducer = place(folio, reducer_symbol);
			Element *valve = place(folio, addValve(project));
			Element *end = place(folio, plain);
			QVERIFY(start && reducer && valve && end);
			start->setPos(0, 200);
			valve->setPos(400, 200);
			end->setPos(600, 200);
			Conductor *a = connect(folio, start, QStringLiteral("N2"), reducer, QStringLiteral("N1"));
			Conductor *b = connect(folio, reducer, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			Conductor *c = connect(folio, valve, QStringLiteral("N2"), end, QStringLiteral("N1"));
			QVERIFY(a && b && c);
			setDn(a, 100);
			setDn(b, 100);
			setDn(c, 100);
			Terminal *n2 = terminal(reducer, QStringLiteral("N2"));
			QCOMPARE(n2->nearestPipes(), QList<Conductor *>{b});

			ElementNozzlesWidget tab(reducer);
			tab.setLiveEdit(true);
				// the sides of a reducer differ by nature: no box for one size
			QVERIFY(!tab.sameSizeBox());
			QLineEdit *field = tab.fieldFor(QStringLiteral("N2"));
			QVERIFY(field);
			const int steps = folio->undoStack().count();
			field->setText(QStringLiteral("80"));
			emit field->editingFinished();
			QCOMPARE(reducer->nozzleDiameters().value(QStringLiteral("N2")), PipeDiameter::nominal(80));
			QCOMPARE(dn(b), 80);
			QCOMPARE(dn(c), 80);
				// the other side keeps its line
			QCOMPARE(dn(a), 100);
			QVERIFY(!n2->hasDiameterGap());
			QCOMPARE(terminal(valve, QStringLiteral("N1"))->diameter().value(), 80.0);

				// one step, undone at once
			QCOMPARE(folio->undoStack().count(), steps + 1);
			folio->undoStack().undo();
			QVERIFY(reducer->nozzleDiameters().isEmpty());
			QCOMPARE(dn(b), 100);
			QCOMPARE(dn(c), 100);
			folio->undoStack().redo();
			QCOMPARE(dn(b), 80);
			QCOMPARE(dn(c), 80);

				// the line changed afterwards: the reducer keeps its size, marked
			setDn(b, 65);
			QCOMPARE(reducer->nozzleDiameters().value(QStringLiteral("N2")), PipeDiameter::nominal(80));
			QVERIFY(n2->hasDiameterGap());

				// emptied: the side follows its line again, no pipe changed
			field->clear();
			emit field->editingFinished();
			QVERIFY(reducer->nozzleDiameters().isEmpty());
			QCOMPARE(dn(b), 65);
			QCOMPARE(n2->diameter().value(), 65.0);
			QVERIFY(!n2->hasDiameterGap());

				// a reducer without pipes: its size alone, in one step
			Element *loose = place(folio, reducer_symbol);
			QVERIFY(loose);
			loose->setPos(200, 1000);
			ElementNozzlesWidget loose_tab(loose);
			loose_tab.setLiveEdit(true);
			const int loose_steps = folio->undoStack().count();
			loose_tab.fieldFor(QStringLiteral("N1"))->setText(QStringLiteral("150"));
			emit loose_tab.fieldFor(QStringLiteral("N1"))->editingFinished();
			QCOMPARE(loose->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::nominal(150));
			QCOMPARE(folio->undoStack().count(), loose_steps + 1);
		}

			// A side of a reducer against a valve, without a pipe: the line
			// beyond the valve takes the size; a plastic line takes the DN of
			// an outside diameter typed, with this outside diameter
		void aSizeTypedOnAReducerGoesBeyondAValveAgainstIt()
		{
			QETProject project;
			PidCodification codification;
			codification.materials = {plasticOf(QStringLiteral("M01"),
												{{900, 80}, {1100, 100}, {1250, 100}})};
			project.setPidCodification(codification);
			Diagram *folio = project.addNewDiagram();
			const ElementsLocation plain = addSymbol(
						project, QStringLiteral("plain.elmt"),
						QStringLiteral("{7b444444-4444-4444-4444-444444444444}"), 0);
			Element *reducer = place(folio, addSymbol(
						project, QStringLiteral("reducer.elmt"),
						QStringLiteral("{7b555555-5555-5555-5555-555555555556}"), 0,
						QStringLiteral("pid_family=\"pipe\" pid_role=\"reducer\" ")));
			Element *valve = place(folio, addValve(project));
			Element *end = place(folio, plain);
			QVERIFY(reducer && valve && end);
				// the N2 of the reducer on the N1 of the valve
			reducer->setPos(200, 200);
			valve->setPos(240, 200);
			end->setPos(600, 200);
			Terminal *n2 = terminal(reducer, QStringLiteral("N2"));
			QCOMPARE(terminal(valve, QStringLiteral("N1"))->contactNozzle(), n2);
			Conductor *pipe = connect(folio, valve, QStringLiteral("N2"), end, QStringLiteral("N1"));
			QVERIFY(pipe);
			ConductorProperties properties = pipe->properties();
			properties.m_pipe.material = QStringLiteral("M01");
			pipe->setProperties(properties);
			setSizes(pipe, PipeDiameter::nominal(100), PipeDiameter::outside(110));
			QCOMPARE(n2->nearestPipes(), QList<Conductor *>{pipe});

			ElementNozzlesWidget tab(reducer);
			tab.setLiveEdit(true);
			const int steps = folio->undoStack().count();
			tab.fieldFor(QStringLiteral("N2"))->setText(QStringLiteral("Ø 90"));
			emit tab.fieldFor(QStringLiteral("N2"))->editingFinished();
			QCOMPARE(reducer->nozzleDiameters().value(QStringLiteral("N2")), PipeDiameter::outside(90));
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(80));
			QCOMPARE(pipe->properties().m_pipe.outside, PipeDiameter::outside(90));
			QCOMPARE(folio->undoStack().count(), steps + 1);
			folio->undoStack().undo();
			QCOMPARE(pipe->properties().m_pipe.outside, PipeDiameter::outside(110));
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
			a->setNozzleDiameters(dns({{QStringLiteral("N2"), 100}}));
			QCOMPARE(dn(pipe), 80);
			QVERIFY(n2->hasDiameterGap());

				// a pipe changed afterwards changes nothing on the nozzles
			setDn(pipe, 200);
			QCOMPARE(n2->diameter().value(), 100.0);
			QCOMPARE(n1->diameter().value(), 80.0);
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
			QCOMPARE(n2->toolTip(), QStringLiteral("Plain end"));

				// the same value again: no difference left
			setDn(pipe, 100);
			QVERIFY(!n2->hasDiameterGap());
			QVERIFY(n1->hasDiameterGap());
		}

			// An outside diameter and a DN: the sizes of the materials of the
			// project say whether they are the same, nothing is converted
		void anOutsideDiameterIsComparedByTheSizesOfTheMaterials()
		{
			QETProject project;
			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 200);
				// N1 of b has the DN 80 of its symbol
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N1"));
			Terminal *n1 = terminal(b, QStringLiteral("N1"));
			setDiameter(pipe, PipeDiameter::outside(90));

				// a project without material: not known, so marked
			QVERIFY(n1->hasDiameterGap());
			QVERIFY(n1->toolTip().contains(QStringLiteral("Ø 90")));
			QVERIFY(n1->toolTip().contains(QStringLiteral("DN 80")));

				// a material has it for this DN: the same
			PidCodification codification;
			codification.materials = {plasticOf(QStringLiteral("M01"), {{900, 80}})};
			project.setPidCodification(codification);
			QVERIFY(!n1->hasDiameterGap());
			QCOMPARE(n1->toolTip(), QStringLiteral("Plain end"));

				// for another DN: different
			codification.materials = {plasticOf(QStringLiteral("M01"), {{900, 65}})};
			project.setPidCodification(codification);
			QVERIFY(n1->hasDiameterGap());
				// unless the project says both DN the same size of line
			codification.addEqualNominals(65, 80);
			project.setPidCodification(codification);
			QVERIFY(!n1->hasDiameterGap());
			codification.equal_nominals.clear();
				// two materials which do not agree: not known, so marked
			codification.materials << plasticOf(QStringLiteral("M02"), {{900, 80}});
			project.setPidCodification(codification);
			QVERIFY(n1->hasDiameterGap());
			QVERIFY(n1->toolTip().contains(QStringLiteral("Ø 90")));

				// two outside diameters: equal or not, no size needed
			a->setNozzleDiameters({{QStringLiteral("N2"), PipeDiameter::outside(90)}});
			QVERIFY(!terminal(a, QStringLiteral("N2"))->hasDiameterGap());
			a->setNozzleDiameters({{QStringLiteral("N2"), PipeDiameter::outside(110)}});
			QVERIFY(terminal(a, QStringLiteral("N2"))->hasDiameterGap());
				// the pipe keeps its outside diameter, and is given no DN
			QCOMPARE(pipe->properties().m_pipe.outside, PipeDiameter::outside(90));
			QVERIFY(!pipe->properties().m_pipe.nominal.isSet());
		}

			// The DN is the size of the line, of a plastic pipe too: it is
			// what a nozzle is compared with, and what a nozzle without a
			// size shows; its outside diameter is a second value, which a
			// nozzle given as an outside diameter is compared with
		void aPipeWithBothSizesIsComparedByItsDn()
		{
			QETProject project;
			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 200);
				// N1 of b has the DN 80 of its symbol, N2 of a no size
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N1"));
			Terminal *n1 = terminal(b, QStringLiteral("N1"));
			Terminal *n2 = terminal(a, QStringLiteral("N2"));
			setSizes(pipe, PipeDiameter::nominal(80), PipeDiameter::outside(90));

				// no table needed: a DN against a DN
			QVERIFY(!n1->hasDiameterGap());
			QCOMPARE(n1->toolTip(), QStringLiteral("Plain end"));
			QCOMPARE(n2->labelText(), QStringLiteral("N2\nDN 80"));
			setSizes(pipe, PipeDiameter::nominal(100), PipeDiameter::outside(110));
			QVERIFY(n1->hasDiameterGap());
			QVERIFY(n1->toolTip().contains(QStringLiteral("DN 80")));
			QVERIFY(n1->toolTip().contains(QStringLiteral("DN 100")));
			QVERIFY(!n1->toolTip().contains(QStringLiteral("110")));
			QCOMPARE(n2->labelText(), QStringLiteral("N2\nDN 100"));

				// a nozzle given as an outside diameter: against that of the
				// pipe, though both are of the same DN
			PidCodification codification;
			codification.materials = {plasticOf(QStringLiteral("M01"), {{1100, 100}, {1250, 100}})};
			project.setPidCodification(codification);
			a->setNozzleDiameters({{QStringLiteral("N2"), PipeDiameter::outside(110)}});
			QVERIFY(!n2->hasDiameterGap());
			a->setNozzleDiameters({{QStringLiteral("N2"), PipeDiameter::outside(125)}});
			QVERIFY(n2->hasDiameterGap());
			QVERIFY(n2->toolTip().contains(QStringLiteral("Ø 125")));
			QVERIFY(n2->toolTip().contains(QStringLiteral("Ø 110")));
				// a pipe without an outside diameter: against its DN, by the
				// sizes of the materials
			setSizes(pipe, PipeDiameter::nominal(100), PipeDiameter());
			QVERIFY(!n2->hasDiameterGap());
			setSizes(pipe, PipeDiameter::nominal(80), PipeDiameter());
			QVERIFY(n2->hasDiameterGap());
		}

			// a new pipe takes an outside diameter as its second value, and
			// the DN the materials of the project have for it
		void aNewPipeTakesTheOutsideDiameterOfANozzle()
		{
			QETProject project;
			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 200);
			a->setNozzleDiameters({{QStringLiteral("N2"), PipeDiameter::outside(110)}});
				// no material: the outside diameter alone
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N2"));
			QCOMPARE(pipe->properties().m_pipe.outside, PipeDiameter::outside(110));
			QVERIFY(!pipe->properties().m_pipe.nominal.isSet());
			QCOMPARE(terminal(b, QStringLiteral("N2"))->labelText(), QStringLiteral("N2\nØ 110"));

				// one DN in the materials: both, and the DN is the size of the
				// line
			PidCodification codification;
			codification.materials = {plasticOf(QStringLiteral("M01"), {{1100, 100}, {1250, 100}}),
									  plasticOf(QStringLiteral("M02"), {{1250, 125}})};
			project.setPidCodification(codification);
			setSizes(pipe, PipeDiameter(), PipeDiameter());
			pipe->takeNozzleDiameter();
			QCOMPARE(pipe->properties().m_pipe.outside, PipeDiameter::outside(110));
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(100));
			QCOMPARE(terminal(b, QStringLiteral("N2"))->labelText(), QStringLiteral("N2\nDN 100"));
			QVERIFY(!terminal(a, QStringLiteral("N2"))->hasDiameterGap());

				// two materials which do not agree: no DN is guessed
			a->setNozzleDiameters({{QStringLiteral("N2"), PipeDiameter::outside(125)}});
			setSizes(pipe, PipeDiameter(), PipeDiameter());
			pipe->takeNozzleDiameter();
			QCOMPARE(pipe->properties().m_pipe.outside, PipeDiameter::outside(125));
			QVERIFY(!pipe->properties().m_pipe.nominal.isSet());

				// a pipe which has a size takes none
			setSizes(pipe, PipeDiameter::nominal(65), PipeDiameter());
			pipe->takeNozzleDiameter();
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(65));
			QVERIFY(!pipe->properties().m_pipe.outside.isSet());
		}

			// DN 60 and DN 65, which the project says the same size of line,
			// are written together on a nozzle which follows a pipe without
			// material; the material of the pipe chooses
		void equalDnAreWrittenTogetherWithoutMaterial()
		{
			QETProject project;
			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 200);
				// N2 has no size of its own: that of its pipe
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N2"));
			QVERIFY(pipe);
			setSizes(pipe, PipeDiameter::nominal(60), PipeDiameter());
			Terminal *n2 = terminal(b, QStringLiteral("N2"));
			QCOMPARE(n2->labelText(), QStringLiteral("N2\nDN 60"));

			PidCodification codification;
			PidCodification::Entry iron;
			iron.code = QStringLiteral("M06");
			iron.addSize(0, 60);
			iron.addSize(0, 80);
			codification.materials = {iron};
			codification.addEqualNominals(60, 65);
			project.setPidCodification(codification);
			QCOMPARE(n2->labelText(), QStringLiteral("N2\nDN 60/65"));
			QCOMPARE(terminal(a, QStringLiteral("N2"))->labelText(), QStringLiteral("N2\nDN 60/65"));
				// and so in the tab of the nozzles, in grey
			{
				ElementNozzlesWidget tab(b);
				QCOMPARE(tab.fieldFor(QStringLiteral("N2"))->placeholderText(), QStringLiteral("DN 60/65"));
			}
				// a size said equal to no other is written alone
			setSizes(pipe, PipeDiameter::nominal(80), PipeDiameter());
			QCOMPARE(n2->labelText(), QStringLiteral("N2\nDN 80"));
			setSizes(pipe, PipeDiameter::nominal(60), PipeDiameter());

				// the material of the pipe chooses
			ConductorProperties properties = pipe->properties();
			properties.m_pipe.material = QStringLiteral("M06");
			pipe->setProperties(properties);
			QCOMPARE(n2->labelText(), QStringLiteral("N2\nDN 60"));
				// a nozzle with a size of its own is written with it alone
			a->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::nominal(60)}});
			QCOMPARE(terminal(a, QStringLiteral("N1"))->labelText(), QStringLiteral("N1\nDN 60"));
				// the size of a pipe without material, which differs from it
			Conductor *other = connect(folio, a, QStringLiteral("N1"), b, QStringLiteral("N1"));
			QVERIFY(other);
			setSizes(other, PipeDiameter::nominal(80), PipeDiameter());
			setSizes(other, PipeDiameter::nominal(60), PipeDiameter());
			QVERIFY(!terminal(a, QStringLiteral("N1"))->hasDiameterGap());
			QVERIFY(terminal(b, QStringLiteral("N1"))->hasDiameterGap());
			QVERIFY(terminal(b, QStringLiteral("N1"))->toolTip().contains(QStringLiteral("DN 60/65")));
		}

			// The material of the pipe, when it has one already (a symbol
			// dropped on a pipe without a size): with a DN goes the outside
			// diameter of a plastic, if it has one only for this DN;
			// a material named by its DN keeps no outside diameter
		void aNewPipeTakesTheSizesItsMaterialNames()
		{
			QETProject project;
			PidCodification codification;
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.addSize(0, 80);
			steel.addSize(0, 100);
			codification.materials = {plasticOf(QStringLiteral("M01"),
												{{900, 80}, {1100, 100}, {1250, 100}}), steel};
			project.setPidCodification(codification);
			QCOMPARE(codification.nominalOf(PipeDiameter::outside(90)), PipeDiameter::nominal(80));
			QVERIFY(!codification.nominalOf(PipeDiameter::outside(63)).isSet());
			QVERIFY(!codification.nominalOf(PipeDiameter::nominal(80)).isSet());
			QCOMPARE(codification.outsidesOf(PipeDiameter::nominal(100), QStringLiteral("M01")),
					 QList<PipeDiameter>({PipeDiameter::outside(110), PipeDiameter::outside(125)}));
			QVERIFY(codification.outsidesOf(PipeDiameter::nominal(100), QStringLiteral("M02")).isEmpty());
			QVERIFY(codification.outsidesOf(PipeDiameter::nominal(100), QString()).isEmpty());

			const ElementsLocation location = addTank(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 200);
				// N1 of a has the DN 80 of its symbol: without a material,
				// the DN alone
			Conductor *pipe = connect(folio, a, QStringLiteral("N1"), b, QStringLiteral("N2"));
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(80));
			QVERIFY(!pipe->properties().m_pipe.outside.isSet());
			auto takeAgain = [pipe](const QString &material) {
				ConductorProperties properties = pipe->properties();
				properties.m_pipe.material = material;
				properties.m_pipe.nominal = PipeDiameter();
				properties.m_pipe.outside = PipeDiameter();
				pipe->setProperties(properties);
				pipe->takeNozzleDiameter();
				return pipe->properties().m_pipe;
			};
				// the plastic has one outside diameter of DN 80
			PipeProperties taken = takeAgain(QStringLiteral("M01"));
			QCOMPARE(taken.nominal, PipeDiameter::nominal(80));
			QCOMPARE(taken.outside, PipeDiameter::outside(90));
				// and two of DN 100: the DN alone, until one is chosen
			a->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::nominal(100)}});
			taken = takeAgain(QStringLiteral("M01"));
			QCOMPARE(taken.nominal, PipeDiameter::nominal(100));
			QVERIFY(!taken.outside.isSet());
				// a nozzle given as an outside diameter, a steel pipe: its DN
			a->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::outside(90)}});
			taken = takeAgain(QStringLiteral("M02"));
			QCOMPARE(taken.nominal, PipeDiameter::nominal(80));
			QVERIFY(!taken.outside.isSet());
			taken = takeAgain(QStringLiteral("M01"));
			QCOMPARE(taken.nominal, PipeDiameter::nominal(80));
			QCOMPARE(taken.outside, PipeDiameter::outside(90));
				// an outside diameter no material has: kept, rather than
				// nothing
			a->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::outside(63)}});
			taken = takeAgain(QStringLiteral("M02"));
			QVERIFY(!taken.nominal.isSet());
			QCOMPARE(taken.outside, PipeDiameter::outside(63));
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
					// and an outside diameter, kept as one
				b->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::outside(110)}});
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
			QCOMPARE(n2->diameter().value(), 150.0);
			Element *b = nullptr;
			for (QGraphicsItem *item : folio->items()) {
				if (auto *element = qgraphicsitem_cast<Element *>(item)) {
					if (element->uuid() != tank_uuid) b = element;
				}
			}
			QVERIFY(b);
			QCOMPARE(b->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::outside(110));
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
			given->setNozzleDiameters(dns({{QStringLiteral("N1"), 100}}));

			QVERIFY(given->changeLocation(other));
			QVERIFY(plain->changeLocation(other));
			QCOMPARE(terminal(given, QStringLiteral("N1"))->diameter().value(), 100.0);
				// the other one follows the new symbol
			QCOMPARE(terminal(plain, QStringLiteral("N1"))->diameter().value(), 65.0);
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
				given->setNozzleDiameters(dns({{QStringLiteral("N1"), 100}}));
				given_uuid = given->uuid();
				plain_uuid = plain->uuid();
					// an element that changed nothing writes no <pid_nozzles>
				QDomDocument doc;
				QHash<Terminal *, int> table;
				QVERIFY(plain->toXml(doc, table).firstChildElement(QStringLiteral("pid_nozzles")).isNull());
				const QDomElement nozzle = given->toXml(doc, table)
						.firstChildElement(QStringLiteral("pid_nozzles"))
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
			QCOMPARE(terminal(given, QStringLiteral("N1"))->diameter().value(), 100.0);
			QCOMPARE(terminal(plain, QStringLiteral("N1"))->diameter().value(), 80.0);
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
				tank->setNozzleDiameters(dns({{QStringLiteral("N1"), 100}}));
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
				// no DN, then the preferred values (a project without
				// outside diameters offers none)
			QCOMPARE(list->count(), PipeProperties::preferredDiameters().size() + 1);
			QVERIFY(list->itemText(0).isEmpty());

				// picked in the list
			const int index = list->findText(QStringLiteral("DN 150"));
			QVERIFY(index > 0);
			list->setCurrentIndex(index);
			emit list->activated(index);
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::nominal(150));

				// a DN out of the lists of the project: the nearest one, and
				// said; no completion turns 12 into 125 either
			n1->setText(QStringLiteral("12"));
			QVERIFY(n1->hasAcceptableInput());
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::nominal(10));
			QCOMPARE(n1->text(), QStringLiteral("DN 10"));
			QVERIFY(tab.message().contains(QStringLiteral("12")));
			QVERIFY(!list->completer());
				// what is no size is refused
			n1->setText(QStringLiteral("abc"));
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::nominal(10));

				// a DN, an outside diameter, but no other text nor a zero in front
			int position = 0;
			QString text = QStringLiteral("DN80");
			QCOMPARE(list->validator()->validate(text, position), QValidator::Acceptable);
			text = QStringLiteral("Ø 110");
			QCOMPARE(list->validator()->validate(text, position), QValidator::Acceptable);
			text = QStringLiteral("Ø 88,9");
			QCOMPARE(list->validator()->validate(text, position), QValidator::Acceptable);
			text = QStringLiteral("Ø 88,95");
			QCOMPARE(list->validator()->validate(text, position), QValidator::Invalid);
			text = QStringLiteral("PN16");
			QCOMPARE(list->validator()->validate(text, position), QValidator::Invalid);
			text = QStringLiteral("0");
			QCOMPARE(list->validator()->validate(text, position), QValidator::Invalid);
		}

			// a nozzle takes an outside diameter of the project, which offers
			// its DN then its outside diameters
		void aNozzleTakesAnOutsideDiameterOfTheProject()
		{
			QETProject project;
			PidCodification codification;
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.addSize(1100, 0);
			plastic.addSize(1250, 0);
			PidCodification::Entry metal;
			metal.code = QStringLiteral("M02");
			metal.addSize(0, 80);
			metal.addSize(0, 100);
			codification.materials = {plastic, metal};
			codification.decimal = PidCodification::Decimal::Point;
			project.setPidCodification(codification);
			Diagram *folio = project.addNewDiagram();
			Element *tank = place(folio, addTank(project));
			QVERIFY(tank);
			ElementNozzlesWidget tab(tank);
			tab.setLiveEdit(true);
			QLineEdit *n1 = tab.fieldFor(QStringLiteral("N1"));
			auto *list = qobject_cast<QComboBox *>(n1->parentWidget());
				// no material: the sizes of all of them, DN first
			QCOMPARE(list->count(), 5);
			QCOMPARE(list->itemText(1), QStringLiteral("DN 80"));
			QCOMPARE(list->itemText(list->count() - 1), QStringLiteral("Ø 125"));

			n1->setText(QStringLiteral("ø110"));
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::outside(110));
			QCOMPARE(n1->text(), QStringLiteral("Ø 110"));
			QCOMPARE(terminal(tank, QStringLiteral("N1"))->labelText(), QStringLiteral("N1\nØ 110"));
				// one the project has not
			n1->setText(QStringLiteral("Ø 90"));
			emit n1->editingFinished();
			QCOMPARE(tank->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::outside(110));
		}

			// The project says how its sizes are written, and whether a nozzle
			// shows its size: whatever the computer which opens it
		void theProjectSaysHowASizeIsShown()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *tank = place(folio, addTank(project));
			QVERIFY(tank);
			tank->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::outside(88.9)}});
			Terminal *n1 = terminal(tank, QStringLiteral("N1"));
			PidCodification codification;
			codification.decimal = PidCodification::Decimal::Comma;
			project.setPidCodification(codification);
			QCOMPARE(n1->labelText(), QStringLiteral("N1\nØ 88,9"));
			codification.decimal = PidCodification::Decimal::Point;
			project.setPidCodification(codification);
			QCOMPARE(n1->labelText(), QStringLiteral("N1\nØ 88.9"));
			codification.show_nozzle_sizes = false;
			project.setPidCodification(codification);
			QCOMPARE(n1->labelText(), QStringLiteral("N1"));
				// a file always has a point
			QDomDocument doc;
			QHash<Terminal *, int> table;
			const QDomElement nozzle = tank->toXml(doc, table)
					.firstChildElement(QStringLiteral("pid_nozzles")).firstChildElement(QStringLiteral("nozzle"));
			QCOMPARE(nozzle.attribute(QStringLiteral("od")), QStringLiteral("88.9"));
		}

			// Step 5l-1t: the project says whether a DN under a nozzle is
			// written "DN 100" or "100"; Ø keeps its sign
		void theProjectSaysWhetherADnIsWrittenWithDn()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *tank = place(folio, addTank(project));
			QVERIFY(tank);
			tank->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::nominal(100)}});
			Terminal *n1 = terminal(tank, QStringLiteral("N1"));
				// by default, "DN": nothing written
			PidCodification codification;
			QVERIFY(codification.nozzle_dn_prefix);
			QCOMPARE(n1->labelText(), QStringLiteral("N1\nDN 100"));
			QVERIFY(!codification.hasTextSettings());
			QDomDocument doc;
			QDomElement holder = doc.createElement(QStringLiteral("project"));
			codification.decimal = PidCodification::Decimal::Point;
			holder.appendChild(codification.toXml(doc));
			QVERIFY(!holder.firstChildElement().hasAttribute(QStringLiteral("nozzle_dn_prefix")));

			codification.nozzle_dn_prefix = false;
			project.setPidCodification(codification);
			QCOMPARE(n1->labelText(), QStringLiteral("N1\n100"));
				// the tool tip and the lists keep it
			QVERIFY(n1->diameterText().startsWith(QStringLiteral("DN ")));
				// an outside diameter keeps its sign
			tank->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::outside(110)}});
			QCOMPARE(n1->labelText(), QStringLiteral("N1\nØ 110"));

				// the file and the palette keep it
			QDomDocument doc2;
			QDomElement holder2 = doc2.createElement(QStringLiteral("project"));
			holder2.appendChild(codification.toXml(doc2));
			QCOMPARE(holder2.firstChildElement().attribute(QStringLiteral("nozzle_dn_prefix")),
					 QStringLiteral("false"));
			PidCodification read;
			read.fromXml(holder2);
			QVERIFY(!read.nozzle_dn_prefix);
			QVERIFY(codification.presentationToCsv().contains(QStringLiteral("text_option;nozzle_dn_prefix;false")));
			QVERIFY(!PidCodification::fromCsv(codification.presentationToCsv()).nozzle_dn_prefix);
			QVERIFY(PidCodification().fromCsv(QString()).nozzle_dn_prefix);
		}

			// Step 5l-1t: the box of the page, greyed when the sizes are not
			// shown, given to the project with undo
		void thePageSetsTheDnBeforeTheSizes()
		{
			QETProject project;
			PidConfigPage page(&project);
			auto *prefix = page.findChild<QCheckBox *>(QStringLiteral("nozzle_dn_prefix"));
			auto *sizes = page.findChild<QCheckBox *>(QStringLiteral("show_nozzle_sizes"));
			QVERIFY(prefix && sizes);
			QVERIFY(prefix->isChecked());
			QVERIFY(prefix->isEnabled());
			sizes->setChecked(false);
			QVERIFY(!prefix->isEnabled());
			sizes->setChecked(true);
			QVERIFY(prefix->isEnabled());
			prefix->setChecked(false);
			page.applyProjectConf();
			QVERIFY(!project.pidCodification().nozzle_dn_prefix);
			project.undoStack()->undo();
			QVERIFY(project.pidCodification().nozzle_dn_prefix);
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
			QCOMPARE(n1->placeholderText(), QStringLiteral("DN 80"));
			QVERIFY(n2->placeholderText().isEmpty());

			const int steps = folio->undoStack().count();
			n1->setText(QStringLiteral("100"));
			emit n1->editingFinished();
			const NozzleDiameters given = dns({{QStringLiteral("N1"), 100}});
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
			QCOMPARE(n1->text(), QStringLiteral("DN 100"));

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
