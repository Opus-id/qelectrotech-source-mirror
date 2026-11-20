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
#include "editor/elementscene.h"
#include "editor/graphicspart/partline.h"
#include "editor/graphicspart/partterminal.h"
#include "editor/graphicspart/parttext.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "factory/elementpicturefactory.h"
#include "properties/elementoptions.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "qetresult.h"
#include "ui/elementoptionswidget.h"
#include "undocommand/changeelementoptionscommand.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDomDocument>
#include <QIcon>
#include <QPalette>
#include <QUndoStack>
#include <QImage>
#include <QPainter>
#include <QPicture>
#include <QTemporaryDir>

/**
	The options of a symbol (ElementOptions): a placed element chooses a
	value of each option, and the parts of the drawing whose show_if the
	choice does not meet are not drawn. Checked on the data, on the drawing,
	on a placed element saved and read back with its project, and on what
	the element editor keeps.
*/
class tst_elementoptions : public QObject
{
	Q_OBJECT

	static constexpr int zoom = 4;

		// A pump: a line always drawn (y = -10), the motor (y = 0) for a
		// fixed or variable speed, the speed arrow (y = 10) for a variable
		// one, and a line (y = -15) whose condition names an option the
		// symbol does not have
	static QString pump()
	{
		const QString style = QStringLiteral(
			"line-style:normal;line-weight:eleve;filling:none;color:black");
		auto line = [&style](int y, const QString &show_if) {
			return QStringLiteral(
				"<line x1=\"-15\" y1=\"%1\" x2=\"15\" y2=\"%1\" end1=\"none\" end2=\"none\" "
				"length1=\"1.5\" length2=\"1.5\" style=\"%2\" antialias=\"false\"%3/>")
				.arg(y).arg(style,
					 show_if.isEmpty() ? QString()
									   : QStringLiteral(" show_if=\"%1\"").arg(show_if));
		};
		return QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{7a111111-1111-1111-1111-111111111111}\"/>"
			"<names><name lang=\"en\">Pump</name></names>"
			"<options>"
			"<option name=\"motor\" default=\"none\">"
			"<names><name lang=\"en\">Motor</name><name lang=\"fr\">Moteur</name></names>"
			"<value name=\"none\"><names><name lang=\"en\">No motor</name></names></value>"
			"<value name=\"fixed\"><names><name lang=\"en\">Fixed speed</name></names></value>"
			"<value name=\"variable\"><names><name lang=\"en\">Variable speed</name></names></value>"
			"</option>"
			"</options>"
			"<description>%1%2%3%4</description></definition>")
			.arg(line(-10, QString()),
				 line(0, QStringLiteral("motor=fixed,variable")),
				 line(10, QStringLiteral("motor=variable")),
				 line(-15, QStringLiteral("seal=double")));
	}

	static QDomElement pumpDefinition(QDomDocument &doc)
	{
		doc.setContent(pump());
		return doc.documentElement();
	}

		// Where the drawing has ink, at (x, y) of the symbol
	static bool inked(const QImage &image, qreal x, qreal y)
	{
		return qGray(image.pixel(int((x + 20) * zoom), int((y + 20) * zoom))) < 128;
	}

	static QImage render(const QPicture &picture)
	{
		QImage image(40 * zoom, 40 * zoom, QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		painter.scale(zoom, zoom);
		painter.translate(20, 20);
		painter.drawPicture(0, 0, picture);
		painter.end();
		return image;
	}

	static QImage render(Element *element)
	{
		QImage image(40 * zoom, 40 * zoom, QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		painter.scale(zoom, zoom);
		painter.translate(20, 20);
		element->paint(&painter, nullptr, nullptr);
		painter.end();
		return image;
	}

	static QString motorOf(const QImage &image)
	{
		const bool motor = inked(image, 0, 0);
		const bool arrow = inked(image, 0, 10);
		if (!inked(image, 0, -10) || !inked(image, 0, -15)) return QStringLiteral("broken");
		if (motor && arrow) return QStringLiteral("variable");
		if (motor) return QStringLiteral("fixed");
		if (arrow) return QStringLiteral("broken");
		return QStringLiteral("none");
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

	static ElementsLocation addPump(QETProject &project)
	{
		project.embeddedElementCollection()->createDir(
					QStringLiteral("import"), QStringLiteral("options"), NamesList());
		QDomDocument doc;
		project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/options"), QStringLiteral("pump.elmt"),
					pumpDefinition(doc));
		return ElementsLocation(QStringLiteral("embed://import/options/pump.elmt"), &project);
	}

		// A valve (step 6b-2): two nozzles always there, and a signal terminal
		// S1 at the top for a motor only, not for a hand lever nor for none
	static ElementsLocation addValve(QETProject &project)
	{
		project.embeddedElementCollection()->createDir(
					QStringLiteral("import"), QStringLiteral("valves"), NamesList());
		QDomDocument doc;
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{7a222222-2222-2222-2222-222222222222}\"/>"
			"<names><name lang=\"en\">Valve</name></names>"
			"<options><option name=\"command\" default=\"none\">"
			"<value name=\"none\"/><value name=\"manual\"/><value name=\"motor\"/>"
			"</option></options>"
			"<description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-weight:normal\" antialias=\"false\"/>"
			"<terminal x=\"-20\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\" name=\"N1\" "
			"uuid=\"{7a222222-0000-0000-0000-000000000001}\"/>"
			"<terminal x=\"20\" y=\"0\" orientation=\"e\" type=\"Generic\" medium=\"pipe\" name=\"N2\" "
			"uuid=\"{7a222222-0000-0000-0000-000000000002}\"/>"
			"<terminal x=\"0\" y=\"-20\" orientation=\"n\" type=\"Generic\" medium=\"signal\" name=\"S1\" "
			"show_if=\"command=motor\" uuid=\"{7a222222-0000-0000-0000-000000000003}\"/>"
			"</description></definition>"));
		project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/valves"), QStringLiteral("valve.elmt"),
					doc.documentElement());
		return ElementsLocation(QStringLiteral("embed://import/valves/valve.elmt"), &project);
	}

		// A bubble (step 6b-3a): round or oval. A signal terminal S1 at the
		// top always; S3 on the left side of the round one, S5 on the left
		// side of the oval one in its place; nozzles P3 and P5 the same way
		// at the bottom, a nozzle taking one pipe only
	static ElementsLocation addBubble(QETProject &project)
	{
		project.embeddedElementCollection()->createDir(
					QStringLiteral("import"), QStringLiteral("bubbles"), NamesList());
		QDomDocument doc;
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"80\" height=\"80\" hotspot_x=\"40\" hotspot_y=\"40\">"
			"<uuid uuid=\"{7a333333-3333-3333-3333-333333333333}\"/>"
			"<names><name lang=\"en\">Bubble</name></names>"
			"<options><option name=\"shape\" default=\"round\">"
			"<value name=\"round\"/><value name=\"oval\"/>"
			"</option></options>"
			"<description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-weight:normal\" antialias=\"false\"/>"
			"<terminal x=\"0\" y=\"-20\" orientation=\"n\" type=\"Generic\" medium=\"signal\" name=\"S1\" "
			"uuid=\"{7a333333-0000-0000-0000-000000000001}\"/>"
			"<terminal x=\"-20\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"signal\" name=\"S3\" "
			"show_if=\"shape=round\" uuid=\"{7a333333-0000-0000-0000-000000000003}\"/>"
			"<terminal x=\"-30\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"signal\" name=\"S5\" "
			"show_if=\"shape=oval\" replaces=\"S3\" uuid=\"{7a333333-0000-0000-0000-000000000005}\"/>"
			"<terminal x=\"-10\" y=\"20\" orientation=\"s\" type=\"Generic\" medium=\"pipe\" name=\"P3\" "
			"show_if=\"shape=round\" uuid=\"{7a333333-0000-0000-0000-000000000013}\"/>"
			"<terminal x=\"10\" y=\"20\" orientation=\"s\" type=\"Generic\" medium=\"pipe\" name=\"P5\" "
			"show_if=\"shape=oval\" replaces=\"P3\" uuid=\"{7a333333-0000-0000-0000-000000000015}\"/>"
			"</description></definition>"));
		project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/bubbles"), QStringLiteral("bubble.elmt"),
					doc.documentElement());
		return ElementsLocation(QStringLiteral("embed://import/bubbles/bubble.elmt"), &project);
	}

		// The end of @p conductor on @p terminal is drawn there
	static bool endsOn(const Conductor *conductor, const Terminal *terminal)
	{
		const QPainterPath path = conductor->path();
		if (path.elementCount() < 2) return false;
		const QPointF end = conductor->terminal1 == terminal
				? QPointF(path.elementAt(0))
				: QPointF(path.elementAt(path.elementCount() - 1));
		return QLineF(conductor->mapToScene(end), terminal->dockConductor()).length() < 0.01;
	}

	static Terminal *terminalNamed(const Element *element, const QString &name)
	{
		for (Terminal *terminal : element->terminals()) {
			if (terminal->name() == name) return terminal;
		}
		return nullptr;
	}

		// Whether @p terminal draws anything, on a folio which draws terminals
	static bool drawn(Terminal *terminal)
	{
		QImage image(80, 80, QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		painter.translate(40, 40);
		terminal->paint(&painter, nullptr, nullptr);
		painter.end();
		for (int y = 0 ; y < image.height() ; ++y) {
			for (int x = 0 ; x < image.width() ; ++x) {
				if (image.pixel(x, y) != qRgb(255, 255, 255)) return true;
			}
		}
		return false;
	}

	private slots:
			// ---- the data ----
		void theOptionsAreRead()
		{
			QDomDocument doc;
			ElementOptions options;
			options.fromXml(pumpDefinition(doc));
			QCOMPARE(options.options().size(), 1);
			const ElementOptions::Option *motor = options.option(QStringLiteral("motor"));
			QVERIFY(motor);
			QCOMPARE(motor->default_value, QStringLiteral("none"));
			QCOMPARE(motor->values.size(), 3);
			QCOMPARE(motor->values.at(2).name, QStringLiteral("variable"));
			QCOMPARE(motor->names.name(), QStringLiteral("Motor"));
			QVERIFY(motor->hasValue(QStringLiteral("fixed")));
			QVERIFY(!motor->hasValue(QStringLiteral("diesel")));
			QVERIFY(!options.option(QStringLiteral("seal")));
		}

		void whatCannotBeReadIsLeftOut()
		{
			QDomDocument doc;
			QVERIFY(doc.setContent(QStringLiteral(
				"<definition><options>"
					// a default that is not a value: the first value
				"<option name=\"motor\" default=\"diesel\">"
				"<value name=\"none\"/><value name=\"fixed\"/><value name=\"fixed\"/>"
				"<value name=\"bad name\"/></option>"
					// named twice, no name, a name show_if could not read
				"<option name=\"motor\"><value name=\"other\"/></option>"
				"<option><value name=\"a\"/></option>"
				"<option name=\"a=b\"><value name=\"a\"/></option>"
					// no value
				"<option name=\"empty\"/>"
				"</options></definition>")));
			ElementOptions options;
			options.fromXml(doc.documentElement());
			QCOMPARE(options.options().size(), 1);
			const ElementOptions::Option *motor = options.option(QStringLiteral("motor"));
			QVERIFY(motor);
			QCOMPARE(motor->default_value, QStringLiteral("none"));
			QCOMPARE(motor->values.size(), 2);
		}

		void aSymbolWithoutOptionsHasNone()
		{
			QDomDocument doc;
			QVERIFY(doc.setContent(QStringLiteral("<definition><description/></definition>")));
			ElementOptions options;
			options.fromXml(doc.documentElement());
			QVERIFY(options.isEmpty());
			QVERIFY(options.defaults().isEmpty());
			QVERIFY(options.resolved({{QStringLiteral("motor"), QStringLiteral("fixed")}}).isEmpty());
		}

		void theChoicesAreResolved()
		{
			QDomDocument doc;
			ElementOptions options;
			options.fromXml(pumpDefinition(doc));
			const ElementOptions::Choices defaults{{QStringLiteral("motor"), QStringLiteral("none")}};
			QCOMPARE(options.defaults(), defaults);
			QCOMPARE(options.resolved({}), defaults);
				// a value the option does not have: the default
			QCOMPARE(options.resolved({{QStringLiteral("motor"), QStringLiteral("diesel")}}), defaults);
				// an option the symbol does not have: left out
			const ElementOptions::Choices chosen{{QStringLiteral("motor"), QStringLiteral("variable")},
												 {QStringLiteral("seal"), QStringLiteral("double")}};
			const ElementOptions::Choices variable{{QStringLiteral("motor"), QStringLiteral("variable")}};
			QCOMPARE(options.resolved(chosen), variable);
		}

		void theConditions_data()
		{
			QTest::addColumn<QString>("show_if");
			QTest::addColumn<bool>("shown");
				// resolved: motor = fixed, seal = single
			QTest::newRow("none") << QString() << true;
			QTest::newRow("met") << "motor=fixed" << true;
			QTest::newRow("one of") << "motor=none,fixed" << true;
			QTest::newRow("spaces") << " motor = none , fixed " << true;
			QTest::newRow("not met") << "motor=variable" << false;
			QTest::newRow("both met") << "motor=fixed;seal=single" << true;
			QTest::newRow("one not met") << "motor=fixed;seal=double" << false;
			QTest::newRow("unknown option") << "colour=red" << true;
			QTest::newRow("unreadable") << "motor" << true;
			QTest::newRow("no name") << "=fixed" << true;
		}

		void theConditions()
		{
			QFETCH(QString, show_if);
			QFETCH(bool, shown);
			const ElementOptions::Choices resolved{{QStringLiteral("motor"), QStringLiteral("fixed")},
												   {QStringLiteral("seal"), QStringLiteral("single")}};
			QCOMPARE(ElementOptions::isShown(show_if, resolved), shown);
		}

		void theKeyTellsTheChoicesApart()
		{
			const ElementOptions::Choices a{{QStringLiteral("seal"), QStringLiteral("single")},
											{QStringLiteral("motor"), QStringLiteral("fixed")}};
			QCOMPARE(ElementOptions::key(a), QStringLiteral("motor=fixed;seal=single"));
			QVERIFY(ElementOptions::key(a)
					!= ElementOptions::key({{QStringLiteral("motor"), QStringLiteral("variable")}}));
			QCOMPARE(ElementOptions::key({}), QString());
		}

		void theDeclarationIsWrittenAsRead()
		{
			QDomDocument doc;
			ElementOptions options;
			options.fromXml(pumpDefinition(doc));
			QDomDocument out;
			QDomElement definition = out.createElement(QStringLiteral("definition"));
			out.appendChild(definition);
			definition.appendChild(options.toXml(out));
			ElementOptions again;
			again.fromXml(definition);
			QVERIFY(again == options);
			QVERIFY(!again.isEmpty());
		}

		void theChoicesOfAnElementAreWrittenAsRead()
		{
			QDomDocument doc;
			const ElementOptions::Choices chosen{{QStringLiteral("seal"), QStringLiteral("double")},
												 {QStringLiteral("motor"), QStringLiteral("variable")}};
			QDomElement element = doc.createElement(QStringLiteral("element"));
			element.appendChild(ElementOptions::choicesToXml(doc, chosen));
			QCOMPARE(ElementOptions::choicesFromXml(element), chosen);
				// in the order of the names, for a stable file
			const QDomElement first = element.firstChildElement().firstChildElement();
			QCOMPARE(first.attribute(QStringLiteral("name")), QStringLiteral("motor"));
				// nothing to write without a choice
			QVERIFY(ElementOptions::choicesToXml(doc, {}).isNull());
		}

			// ---- the drawing ----
		void theDrawingFollowsTheChoices()
		{
			QDomDocument doc;
			const QDomElement definition = pumpDefinition(doc);
			ElementPictureFactory *factory = ElementPictureFactory::instance();
			QCOMPARE(motorOf(render(factory->pictureFromDefinition(definition))),
					 QStringLiteral("none"));
			QCOMPARE(motorOf(render(factory->pictureFromDefinition(
							 definition, {{QStringLiteral("motor"), QStringLiteral("fixed")}}))),
					 QStringLiteral("fixed"));
			QCOMPARE(motorOf(render(factory->pictureFromDefinition(
							 definition, {{QStringLiteral("motor"), QStringLiteral("variable")}}))),
					 QStringLiteral("variable"));
				// a value the option does not have: the default
			QCOMPARE(motorOf(render(factory->pictureFromDefinition(
							 definition, {{QStringLiteral("motor"), QStringLiteral("diesel")}}))),
					 QStringLiteral("none"));
		}

			// ---- a placed element ----
		void aPlacedElementIsDrawnAfterItsChoices()
		{
			QETProject project;
			const ElementsLocation location = addPump(project);
			QVERIFY(location.exist());
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			QVERIFY(a->optionChoices().isEmpty());
			QCOMPARE(a->resolvedOptions().value(QStringLiteral("motor")), QStringLiteral("none"));
			QCOMPARE(motorOf(render(a)), QStringLiteral("none"));

			a->setOptionChoices({{QStringLiteral("motor"), QStringLiteral("variable")}});
			QCOMPARE(motorOf(render(a)), QStringLiteral("variable"));
				// each element its own choice, from the same symbol
			QCOMPARE(motorOf(render(b)), QStringLiteral("none"));
			b->setOptionChoices({{QStringLiteral("motor"), QStringLiteral("fixed")}});
			QCOMPARE(motorOf(render(b)), QStringLiteral("fixed"));
			QCOMPARE(motorOf(render(a)), QStringLiteral("variable"));
				// back to the default
			a->setOptionChoices({});
			QCOMPARE(motorOf(render(a)), QStringLiteral("none"));
		}

			// ---- a terminal which depends on the options ----
		void aTerminalFollowsTheChoices()
		{
			QETProject project;
			const ElementsLocation location = addValve(project);
			QVERIFY(location.exist());
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			Terminal *a_signal = terminalNamed(a, QStringLiteral("S1"));
			Terminal *b_signal = terminalNamed(b, QStringLiteral("S1"));
			Terminal *nozzle = terminalNamed(a, QStringLiteral("N1"));
			QVERIFY(a_signal && b_signal && nozzle);
			QCOMPARE(a_signal->showIf(), QStringLiteral("command=motor"));
				// the terminal is there whatever the options: Change type keeps it
			QCOMPARE(a->terminals().size(), 3);
				// no motor by default: hidden, it takes no wire
			QVERIFY(a_signal->isHiddenByOptions());
			QVERIFY(!a_signal->hasRoomForWire());
			QVERIFY(!a_signal->canBeLinkedTo(b_signal));
			QVERIFY(!b_signal->canBeLinkedTo(a_signal));
				// a terminal without condition is never hidden
			QVERIFY(nozzle->showIf().isEmpty());
			QVERIFY(!nozzle->isHiddenByOptions());

			a->setOptionChoices({{QStringLiteral("command"), QStringLiteral("motor")}});
			QVERIFY(!a_signal->isHiddenByOptions());
			QVERIFY(a_signal->hasRoomForWire());
				// the other end still hidden: still refused
			QVERIFY(!a_signal->canBeLinkedTo(b_signal));
			b->setOptionChoices({{QStringLiteral("command"), QStringLiteral("motor")}});
			QVERIFY(a_signal->canBeLinkedTo(b_signal));
				// a hand lever has no signal
			a->setOptionChoices({{QStringLiteral("command"), QStringLiteral("manual")}});
			QVERIFY(a_signal->isHiddenByOptions());
			QVERIFY(!a_signal->canBeLinkedTo(b_signal));
		}

		void aHiddenTerminalIsNotDrawnWhileFree()
		{
			QETProject project;
			const ElementsLocation location = addValve(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			QVERIFY(a);
			Terminal *signal = terminalNamed(a, QStringLiteral("S1"));
			Terminal *nozzle = terminalNamed(a, QStringLiteral("N1"));
			QVERIFY(signal && nozzle);
			QVERIFY(!drawn(signal));
			QVERIFY(drawn(nozzle));
			a->setOptionChoices({{QStringLiteral("command"), QStringLiteral("motor")}});
			QVERIFY(drawn(signal));
		}

		void aWireStaysOnATerminalHiddenAfterIt()
		{
			QETProject project;
			const ElementsLocation location = addValve(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 200);
			const ElementOptions::Choices motor{{QStringLiteral("command"), QStringLiteral("motor")}};
			a->setOptionChoices(motor);
			b->setOptionChoices(motor);
			Terminal *a_signal = terminalNamed(a, QStringLiteral("S1"));
			Terminal *b_signal = terminalNamed(b, QStringLiteral("S1"));
			auto *signal_line = new Conductor(a_signal, b_signal);
			folio->addItem(signal_line);
			QCOMPARE(a_signal->conductorsCount(), 1);
				// the motor taken off: the line and its end stay, drawn, no new one
			a->setOptionChoices({});
			QVERIFY(a_signal->isHiddenByOptions());
			QCOMPARE(a_signal->conductorsCount(), 1);
			QVERIFY(drawn(a_signal));
			QVERIFY(!a_signal->hasRoomForWire());
		}

		void theEditorKeepsTheConditionOfATerminal()
		{
			QDomDocument doc;
			QVERIFY(doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"-20\" orientation=\"n\" type=\"Generic\" medium=\"signal\" "
				"name=\"S1\" show_if=\"command=motor\"/>")));
			PartTerminal terminal(nullptr);
			terminal.fromXml(doc.documentElement());
			QDomDocument out;
			QCOMPARE(terminal.toXml(out).attribute(QStringLiteral("show_if")),
					 QStringLiteral("command=motor"));
				// none written for a terminal always there
			QVERIFY(doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"-20\" orientation=\"n\" type=\"Generic\" name=\"1\"/>")));
			PartTerminal plain(nullptr);
			plain.fromXml(doc.documentElement());
			QVERIFY(!plain.toXml(out).hasAttribute(QStringLiteral("show_if")));
		}

			// ---- a terminal in the place of another (step 6b-3a) ----
		void theEditorKeepsTheTerminalReplaced()
		{
			QDomDocument doc;
			QVERIFY(doc.setContent(QStringLiteral(
				"<terminal x=\"-30\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"signal\" "
				"name=\"S5\" show_if=\"shape=oval\" replaces=\"S3\"/>")));
			PartTerminal terminal(nullptr);
			terminal.fromXml(doc.documentElement());
			QDomDocument out;
			QCOMPARE(terminal.toXml(out).attribute(QStringLiteral("replaces")), QStringLiteral("S3"));
				// none written for a terminal in the place of none
			QVERIFY(doc.setContent(QStringLiteral(
				"<terminal x=\"0\" y=\"-20\" orientation=\"n\" type=\"Generic\" name=\"1\"/>")));
			PartTerminal plain(nullptr);
			plain.fromXml(doc.documentElement());
			QVERIFY(!plain.toXml(out).hasAttribute(QStringLiteral("replaces")));
		}

		void aWireGoesOverToTheTerminalShownInItsPlace()
		{
			QETProject project;
			const ElementsLocation location = addBubble(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			b->setPos(400, 400);
			Terminal *a_round = terminalNamed(a, QStringLiteral("S3"));
			Terminal *a_oval = terminalNamed(a, QStringLiteral("S5"));
			Terminal *b_top = terminalNamed(b, QStringLiteral("S1"));
			QVERIFY(a_round && a_oval && b_top);
			QCOMPARE(a_oval->replaces(), QStringLiteral("S3"));
				// one takes the place of the other while hidden only
			QCOMPARE(a_round->replacement(), static_cast<Terminal *>(nullptr));
			QCOMPARE(a_oval->replacement(), a_round);
			auto *line = new Conductor(a_round, b_top);
			folio->addItem(line);
			QVERIFY(endsOn(line, a_round));

			QUndoStack stack;
			stack.push(new ChangeElementOptionsCommand(
						   a, {{QStringLiteral("shape"), QStringLiteral("oval")}}));
				// the same line, now on the side of the oval, drawn there
			QCOMPARE(a_round->replacement(), a_oval);
			QCOMPARE(line->terminal1, a_oval);
			QCOMPARE(line->terminal2, b_top);
			QCOMPARE(a_round->conductorsCount(), 0);
			QCOMPARE(a_oval->conductors(), QList<Conductor *>{line});
			QCOMPARE(b_top->conductors(), QList<Conductor *>{line});
			QCOMPARE(line->diagram(), folio);
			QVERIFY(endsOn(line, a_oval));
			QVERIFY(!drawn(a_round));
				// one undo: round again, and the line back
			stack.undo();
			QVERIFY(a->optionChoices().isEmpty());
			QCOMPARE(line->terminal1, a_round);
			QCOMPARE(a_oval->conductorsCount(), 0);
			QCOMPARE(a_round->conductors(), QList<Conductor *>{line});
			QVERIFY(endsOn(line, a_round));
			stack.redo();
			QCOMPARE(line->terminal1, a_oval);
			QVERIFY(endsOn(line, a_oval));
				// and the other way: round again, the line on the round side
			stack.push(new ChangeElementOptionsCommand(a, {}));
			QCOMPARE(line->terminal1, a_round);
			QCOMPARE(a_oval->conductorsCount(), 0);
			QVERIFY(endsOn(line, a_round));
		}

		void aWireStaysWhenThePlaceIsTaken()
		{
			QETProject project;
			const ElementsLocation location = addBubble(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			Element *c = place(folio, location);
			QVERIFY(a && b && c);
			b->setPos(400, 400);
			c->setPos(0, 400);
			const ElementOptions::Choices oval{{QStringLiteral("shape"), QStringLiteral("oval")}};
			a->setOptionChoices(oval);
			b->setOptionChoices(oval);
			c->setOptionChoices(oval);
			Terminal *a_round = terminalNamed(a, QStringLiteral("P3"));
			Terminal *a_oval = terminalNamed(a, QStringLiteral("P5"));
			QVERIFY(a_round && a_oval);
			auto *first = new Conductor(a_oval, terminalNamed(b, QStringLiteral("P5")));
			folio->addItem(first);
				// round with no command: the pipe stays on the oval's nozzle
			a->setOptionChoices({});
			auto *second = new Conductor(a_round, terminalNamed(c, QStringLiteral("P5")));
			folio->addItem(second);
			QCOMPARE(a_oval->conductorsCount(), 1);
			QCOMPARE(a_round->conductorsCount(), 1);
				// oval again: its nozzle takes one pipe only, it has it
			QUndoStack stack;
			stack.push(new ChangeElementOptionsCommand(a, oval));
			QCOMPARE(a->optionChoices(), oval);
			QCOMPARE(second->terminal1, a_round);
			QCOMPARE(first->terminal1, a_oval);
			QCOMPARE(a_round->conductors(), QList<Conductor *>{second});
			QCOMPARE(a_oval->conductors(), QList<Conductor *>{first});
			stack.undo();
			QVERIFY(a->optionChoices().isEmpty());
			QCOMPARE(second->terminal1, a_round);
			QCOMPARE(first->terminal1, a_oval);
		}

		void theChoicesAreSavedWithTheProject_data()
		{
			QTest::addColumn<QString>("suffix");
			QTest::newRow(".qet") << QStringLiteral(".qet");
			QTest::newRow(".qetz") << QStringLiteral(".qetz");
		}

		void theChoicesAreSavedWithTheProject()
		{
			QFETCH(QString, suffix);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("pumps") + suffix);
			QUuid chosen_uuid, plain_uuid;
			{
				QETProject project;
				const ElementsLocation location = addPump(project);
				Diagram *folio = project.addNewDiagram();
				Element *chosen = place(folio, location);
				Element *plain = place(folio, location);
				QVERIFY(chosen && plain);
				plain->setPos(300, 200);
				chosen->setOptionChoices({{QStringLiteral("motor"), QStringLiteral("variable")}});
				chosen_uuid = chosen->uuid();
				plain_uuid = plain->uuid();
					// an element without a choice writes no <options>
				QDomDocument doc;
				QHash<Terminal *, int> table;
				QVERIFY(plain->toXml(doc, table).firstChildElement(QStringLiteral("options")).isNull());
				QVERIFY(!chosen->toXml(doc, table).firstChildElement(QStringLiteral("options")).isNull());
				project.setFilePath(path);
				QVERIFY(project.write().isOk());
			}

			QETProject back(path);
			QCOMPARE(back.diagrams().size(), 1);
			Element *chosen = nullptr;
			Element *plain = nullptr;
			for (QGraphicsItem *item : back.diagrams().first()->items()) {
				if (auto *element = qgraphicsitem_cast<Element *>(item)) {
					if (element->uuid() == chosen_uuid) chosen = element;
					if (element->uuid() == plain_uuid) plain = element;
				}
			}
			QVERIFY(chosen && plain);
			const ElementOptions::Choices variable{{QStringLiteral("motor"), QStringLiteral("variable")}};
			QCOMPARE(chosen->optionChoices(), variable);
			QVERIFY(plain->optionChoices().isEmpty());
			QCOMPARE(motorOf(render(chosen)), QStringLiteral("variable"));
			QCOMPARE(motorOf(render(plain)), QStringLiteral("none"));
		}

			// a .qetdb is written from the project database, which has no
			// place for them: refused rather than lost without a word
		void aQetdbFileIsRefusedWhileOptionsAreChosen()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString qet = dir.filePath(QStringLiteral("pumps.qet"));
			const QString qetdb = dir.filePath(QStringLiteral("pumps.qetdb"));
			{
				QETProject project;
				const ElementsLocation location = addPump(project);
				Element *pump = place(project.addNewDiagram(), location);
				QVERIFY(pump);
				pump->setOptionChoices({{QStringLiteral("motor"), QStringLiteral("fixed")}});
				project.setFilePath(qet);
				QVERIFY(project.write().isOk());
			}

			QETProject back(qet);
				// told before any folio is built
			QVERIFY(back.hasSymbolChoices());
			back.setFilePath(qetdb);
			const QETResult refused = back.write();
			QVERIFY(!refused.isOk());
			QVERIFY(refused.errorMessage().contains(QStringLiteral(".qetdb")));
			QVERIFY(!QFile::exists(qetdb));

				// back to the defaults, saved as before
			for (QGraphicsItem *item : back.diagrams().first()->items())
				if (auto *element = qgraphicsitem_cast<Element *>(item))
					element->setOptionChoices({});
			QVERIFY(!back.hasSymbolChoices());
			QVERIFY(back.write().isOk());
			QVERIFY(QFile::exists(qetdb));
		}

			// ---- the Options tab of the properties of an element ----
		void theOptionsTabChoosesWithUndo()
		{
			QETProject project;
			const ElementsLocation location = addPump(project);
			Diagram *folio = project.addNewDiagram();
			Element *pump = place(folio, location);
			QVERIFY(pump);
			QVERIFY(ElementOptionsWidget::hasOptions(pump));
			QVERIFY(!ElementOptionsWidget::hasOptions(nullptr));

			ElementOptionsWidget tab(pump);
			tab.setLiveEdit(true);
			QComboBox *motor = tab.comboFor(QStringLiteral("motor"));
			QVERIFY(motor);
			QVERIFY(!tab.comboFor(QStringLiteral("seal")));
				// the values in their order, their names shown, the default chosen
			QCOMPARE(motor->count(), 3);
			QCOMPARE(motor->itemText(1), QStringLiteral("Fixed speed"));
			QCOMPARE(motor->currentData().toString(), QStringLiteral("none"));

			const int step = folio->undoStack().index();
			motor->setCurrentIndex(motor->findData(QStringLiteral("variable")));
			const ElementOptions::Choices variable{{QStringLiteral("motor"), QStringLiteral("variable")}};
			QCOMPARE(pump->optionChoices(), variable);
			QCOMPARE(motorOf(render(pump)), QStringLiteral("variable"));
			QCOMPARE(folio->undoStack().index(), step + 1);
			QCOMPARE(folio->undoStack().undoText(), QStringLiteral("Change the options of an element"));

			folio->undoStack().undo();
			QVERIFY(pump->optionChoices().isEmpty());
			QCOMPARE(motorOf(render(pump)), QStringLiteral("none"));
				// the list follows the undo, and makes no step of its own
			QCOMPARE(motor->currentData().toString(), QStringLiteral("none"));
			QCOMPARE(folio->undoStack().index(), step);
			folio->undoStack().redo();
			QCOMPARE(pump->optionChoices(), variable);
			QCOMPARE(motor->currentData().toString(), QStringLiteral("variable"));

				// the default chosen is no choice: saved as before
			motor->setCurrentIndex(motor->findData(QStringLiteral("none")));
			QVERIFY(pump->optionChoices().isEmpty());

				// showing the element's value makes no step
			const int shown = folio->undoStack().index();
			tab.updateUi();
			QCOMPARE(folio->undoStack().index(), shown);
		}

		void theOptionsTabKeepsTheChoicesOfOtherOptions()
		{
			QETProject project;
			const ElementsLocation location = addPump(project);
			Element *pump = place(project.addNewDiagram(), location);
			QVERIFY(pump);
				// a choice for an option this symbol does not offer
			pump->setOptionChoices({{QStringLiteral("seal"), QStringLiteral("double")}});
			ElementOptionsWidget tab(pump);
			QComboBox *motor = tab.comboFor(QStringLiteral("motor"));
			QVERIFY(motor);
			motor->setCurrentIndex(motor->findData(QStringLiteral("fixed")));
			const ElementOptions::Choices both{{QStringLiteral("motor"), QStringLiteral("fixed")},
											   {QStringLiteral("seal"), QStringLiteral("double")}};
			QCOMPARE(tab.chosen(), both);
				// not live (the dialog of the element) as well: applied at
				// once, the drawing follows (5l-1m), and OK has nothing more
				// to push
			QVERIFY(!tab.isLiveEdit());
			QCOMPARE(pump->optionChoices(), both);
			QVERIFY(!tab.associatedUndo());
		}

			// The "Options" submenu of the context menu (5l-1m): a value given
			// to several symbols at once, in one undo step; a default is no
			// choice; a value already chosen or not offered makes no step
		void aValueIsGivenToSeveralSymbols()
		{
			QETProject project;
			const ElementsLocation location = addPump(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = place(folio, location);
			Element *b = place(folio, location);
			QVERIFY(a && b);
			const int steps = folio->undoStack().count();

			QUndoCommand *undo = ElementOptionsWidget::choose({a, b}, QStringLiteral("motor"),
															  QStringLiteral("fixed"));
			QVERIFY(undo);
			folio->undoStack().push(undo);
			QCOMPARE(folio->undoStack().count(), steps + 1);
			QCOMPARE(a->resolvedOptions().value(QStringLiteral("motor")), QStringLiteral("fixed"));
			QCOMPARE(b->resolvedOptions().value(QStringLiteral("motor")), QStringLiteral("fixed"));

			QVERIFY(!ElementOptionsWidget::choose({a, b}, QStringLiteral("motor"), QStringLiteral("fixed")));
			QVERIFY(!ElementOptionsWidget::choose({a}, QStringLiteral("motor"), QStringLiteral("diesel")));
			QVERIFY(!ElementOptionsWidget::choose({a}, QStringLiteral("seal"), QStringLiteral("double")));

				// back to the default: no choice left
			undo = ElementOptionsWidget::choose({a}, QStringLiteral("motor"), QStringLiteral("none"));
			QVERIFY(undo);
			folio->undoStack().push(undo);
			QVERIFY(!a->optionChoices().contains(QStringLiteral("motor")));
			QCOMPARE(b->resolvedOptions().value(QStringLiteral("motor")), QStringLiteral("fixed"));

			folio->undoStack().undo();
			folio->undoStack().undo();
			QVERIFY(a->optionChoices().isEmpty() && b->optionChoices().isEmpty());
		}

			// Each value of the list of an option shows the drawing it gives,
			// large enough to be read in the list shown (5l-1m)
		void eachValueShowsItsDrawing()
		{
			QETProject project;
			Element *pump = place(project.addNewDiagram(), addPump(project));
			QVERIFY(pump);
			ElementOptionsWidget tab(pump);
			QComboBox *motor = tab.comboFor(QStringLiteral("motor"));
			QVERIFY(motor);
			QCOMPARE(motor->view()->iconSize(), ElementOptionsWidget::listIconSize());
			const QSize size = ElementOptionsWidget::listIconSize();
			const int none = motor->findData(QStringLiteral("none"));
			const int fixed = motor->findData(QStringLiteral("fixed"));
			QVERIFY(none >= 0 && fixed >= 0);
			QVERIFY(!motor->itemIcon(none).isNull() && !motor->itemIcon(fixed).isNull());
			QVERIFY(motor->itemIcon(none).pixmap(size).toImage()
					!= motor->itemIcon(fixed).pixmap(size).toImage());
				// the type list draws the types with the options chosen
			QVERIFY(tab.typeList());
			QVERIFY(!tab.typeList()->itemIcon(tab.typeList()->currentIndex()).isNull());

				// on a dark palette the black ink of the symbol is made
				// light, as in the collections: never black on black
			QPalette dark;
			dark.setColor(QPalette::Window, QColor(30, 30, 30));
			dark.setColor(QPalette::Base, QColor(30, 30, 30));
			dark.setColor(QPalette::WindowText, QColor(230, 230, 230));
			dark.setColor(QPalette::Text, QColor(230, 230, 230));
			const auto darkestInk = [](const QImage &image) {
				int darkest = 255;
				for (int y = 0 ; y < image.height() ; ++y) {
					for (int x = 0 ; x < image.width() ; ++x) {
						const QRgb pixel = image.pixel(x, y);
						if (qAlpha(pixel) > 128) {
							darkest = qMin(darkest, qGray(pixel));
						}
					}
				}
				return darkest;
			};
			const ElementOptions::Choices fixed_motor{{QStringLiteral("motor"), QStringLiteral("fixed")}};
			const QImage on_light = ElementOptionsWidget::drawingIcon(
						pump->location(), fixed_motor, QPalette(Qt::white)).pixmap(size).toImage();
			const QImage on_dark = ElementOptionsWidget::drawingIcon(
						pump->location(), fixed_motor, dark).pixmap(size).toImage();
			QVERIFY(darkestInk(on_light) < 100);
			QVERIFY(darkestInk(on_dark) > 128);
		}

			// ---- the element editor ----
		void theEditorKeepsTheConditionOfAPart()
		{
			QDomDocument doc;
			QVERIFY(doc.setContent(QStringLiteral(
				"<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
				"length1=\"1.5\" length2=\"1.5\" style=\"line-weight:normal\" antialias=\"true\" "
				"show_if=\"motor=fixed,variable\"/>")));
			PartLine line(nullptr);
			line.fromXml(doc.documentElement());
			QCOMPARE(line.showIf(), QStringLiteral("motor=fixed,variable"));
			QDomDocument out;
			QCOMPARE(line.toXml(out).attribute(QStringLiteral("show_if")),
					 QStringLiteral("motor=fixed,variable"));
				// none written for a part drawn always
			line.setShowIf(QString());
			QVERIFY(!line.toXml(out).hasAttribute(QStringLiteral("show_if")));
		}

		void theEditorKeepsTheConditionOfAText()
		{
			QDomDocument doc;
			QVERIFY(doc.setContent(QStringLiteral(
				"<text x=\"0\" y=\"0\" text=\"M\" font=\"Sans Serif,9,-1,5,50,0,0,0,0,0\" "
				"rotation=\"0\" color=\"#000000\" show_if=\"motor=fixed,variable\"/>")));
			PartText text(nullptr);
			text.fromXml(doc.documentElement());
			QCOMPARE(text.showIf(), QStringLiteral("motor=fixed,variable"));
			QDomDocument out;
			QCOMPARE(text.toXml(out).attribute(QStringLiteral("show_if")),
					 QStringLiteral("motor=fixed,variable"));
		}

		void theEditorKeepsTheOptionsOfASymbol()
		{
			QDomDocument doc;
			QVERIFY(doc.setContent(pump()));
			ElementScene scene(nullptr);
			scene.fromXml(doc, QPointF(), true);
			const QDomDocument saved = scene.toXml();
			const QDomElement root = saved.documentElement();
			ElementOptions read, written;
			read.fromXml(doc.documentElement());
			written.fromXml(root);
			QVERIFY(written == read);
				// and the conditions of its parts
			QStringList conditions;
			const QDomElement description = root.firstChildElement(QStringLiteral("description"));
			for (QDomElement part = description.firstChildElement() ; !part.isNull() ;
				 part = part.nextSiblingElement()) {
				if (part.hasAttribute(QStringLiteral("show_if")))
					conditions << part.attribute(QStringLiteral("show_if"));
			}
			conditions.sort();
			QCOMPARE(conditions, QStringList({QStringLiteral("motor=fixed,variable"),
											  QStringLiteral("motor=variable"),
											  QStringLiteral("seal=double")}));
		}

			// Step 7m: the editor may show the parts of one case of the
			// options only; nothing saved changes
		void theEditorShowsOneCase()
		{
			QDomDocument doc;
			QVERIFY(doc.setContent(pump()));
			ElementScene scene(nullptr);
			scene.fromXml(doc, QPointF(), true);
			auto shown = [&scene]() {
				QMap<QString, bool> parts;
				for (QGraphicsItem *item : scene.items()) {
					const QString show_if = ElementScene::showIfOf(item);
					if (!show_if.isEmpty()) {
						parts.insert(show_if, item->isVisible());
					}
				}
				return parts;
			};
			QCOMPARE(shown().size(), 3);
			QVERIFY(!shown().values().contains(false));

				// a part selected, with its handles, which the case hides
				// (it crashed: unselecting deletes the handles)
			QGraphicsItem *variable = nullptr;
			for (QGraphicsItem *item : scene.items()) {
				if (ElementScene::showIfOf(item) == QLatin1String("motor=variable")) {
					variable = item;
				}
			}
			QVERIFY(variable);
			variable->setSelected(true);
			QVERIFY(variable->isSelected());
			scene.setOptionPreview({{QStringLiteral("motor"), QStringLiteral("none")}});
			QVERIFY(!variable->isSelected());
			QVERIFY(!variable->isVisible());
			QVERIFY(!shown().value(QStringLiteral("motor=fixed,variable")));
			QVERIFY(!shown().value(QStringLiteral("motor=variable")));
				// a condition on an option the symbol has not: drawn
			QVERIFY(shown().value(QStringLiteral("seal=double")));
			scene.setOptionPreview({{QStringLiteral("motor"), QStringLiteral("fixed")}});
			QVERIFY(shown().value(QStringLiteral("motor=fixed,variable")));
			QVERIFY(!shown().value(QStringLiteral("motor=variable")));

				// saved whole, the parts hidden too
			QStringList conditions;
			const QDomElement description = scene.toXml().documentElement()
					.firstChildElement(QStringLiteral("description"));
			for (QDomElement part = description.firstChildElement() ; !part.isNull() ;
				 part = part.nextSiblingElement()) {
				if (part.hasAttribute(QStringLiteral("show_if")))
					conditions << part.attribute(QStringLiteral("show_if"));
			}
			QCOMPARE(conditions.size(), 3);

			scene.setOptionPreview(ElementOptions::Choices());
			QVERIFY(!shown().values().contains(false));
		}

		void aSymbolWithoutOptionsIsSavedAsBefore()
		{
			QDomDocument doc;
			QVERIFY(doc.setContent(QStringLiteral(
				"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
				"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
				"<names><name lang=\"en\">Valve</name></names><description>"
				"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
				"length1=\"1.5\" length2=\"1.5\" style=\"line-weight:normal\" antialias=\"false\"/>"
				"</description></definition>")));
			ElementScene scene(nullptr);
			scene.fromXml(doc, QPointF(), true);
			const QString saved = scene.toXml().toString();
			QVERIFY(!saved.contains(QStringLiteral("<options")));
			QVERIFY(!saved.contains(QStringLiteral("show_if")));
		}
};

QET_APP_TEST_MAIN(tst_elementoptions)
#include "tst_elementoptions.moc"
