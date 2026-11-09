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
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pipeconnection.h"
#include "properties/terminaldata.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "ui/configpage/pidconfigpage.h"
#include "ui/elementnozzleswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDomDocument>
#include <QImage>
#include <QPainter>
#include <QUndoStack>

/**
	The mark of the connection drawn at the end of a nozzle (step 5l): a
	flange is a line across, a socket a bracket, a weld a dot... The project
	says which connections are drawn (none by default: a project drawn before
	keeps its look), a placed element may say otherwise for one nozzle. A pipe
	arriving on a flange draws the counter-flange. Two symbols placed against
	each other draw nothing between them.
*/
class tst_connectionmark : public QObject
{
	Q_OBJECT

		// N1, west, a flange of DN 100; N2, east, a plain end; E1, an
		// electrical terminal
	static QString symbol()
	{
		return QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"60\" height=\"40\" hotspot_x=\"30\" hotspot_y=\"20\">"
			"<uuid uuid=\"{7d111111-1111-1111-1111-111111111111}\"/>"
			"<names><name lang=\"en\">Valve</name></names>"
			"<description>"
			"<line x1=\"-20\" y1=\"0\" x2=\"20\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
			"<terminal uuid=\"{7d000000-0000-0000-0000-000000000001}\" name=\"N1\" x=\"-20\" y=\"0\" "
			"orientation=\"w\" type=\"Generic\" medium=\"pipe\" dn=\"100\"/>"
			"<terminal uuid=\"{7d000000-0000-0000-0000-000000000002}\" name=\"N2\" x=\"20\" y=\"0\" "
			"orientation=\"e\" type=\"Generic\" medium=\"pipe\" connection=\"plain_end\"/>"
			"<terminal uuid=\"{7d000000-0000-0000-0000-000000000003}\" name=\"E1\" x=\"0\" y=\"-20\" "
			"orientation=\"n\" type=\"Generic\"/>"
			"</description></definition>");
	}

	static Element *place(QETProject &project, Diagram *folio, const QPointF &pos)
	{
		const QString dir = QStringLiteral("import/marks");
		if (!project.embeddedElementCollection()->exist(dir)) {
			project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("marks"), NamesList());
			QDomDocument doc;
			doc.setContent(symbol());
			project.embeddedElementCollection()->addElementDefinition(
						dir, QStringLiteral("valve.elmt"), doc.documentElement());
		}
		const ElementsLocation location(QStringLiteral("embed://import/marks/valve.elmt"), &project);
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		folio->addItem(element);
		element->setPos(pos);
		return element;
	}

	static Terminal *terminal(const Element *element, const QString &name)
	{
		for (Terminal *t : element->terminals()) {
			if (t->baseName() == name) return t;
		}
		return nullptr;
	}

	static void drawFlanges(QETProject &project)
	{
		PidCodification codification = project.pidCodification();
		codification.drawn_connections = {PipeConnection::Flange};
		project.setPidCodification(codification);
	}

		/// A folio drawn as on paper, around @p center
	static QImage render(Diagram *folio, const QPointF &center)
	{
		QImage image(80, 80, QImage::Format_RGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		folio->render(&painter, QRectF(0, 0, 80, 80),
					  QRectF(center - QPointF(40, 40), QSizeF(80, 80)));
		painter.end();
		return image;
	}

		/// Something is drawn at @p x, @p y of @p image, or at a pixel beside:
		/// a line of one unit drawn smooth spreads on two pixels
	static bool dark(const QImage &image, int x, int y)
	{
		for (int dx = -1 ; dx <= 1 ; ++dx) {
			if (qGray(image.pixel(x + dx, y)) < 192) return true;
		}
		return false;
	}

	private slots:
		void eachConnectionHasItsMark()
		{
			for (PipeConnection::Type type : PipeConnection::all()) {
				QPainterPath strokes, fills;
				PipeConnection::markShapes(type, &strokes, &fills);
				const QRectF rect = strokes.boundingRect().united(fills.boundingRect());
				if (type == PipeConnection::PlainEnd) {
					QVERIFY(strokes.isEmpty() && fills.isEmpty());
					QVERIFY(!PipeConnection::hasMark(type));
					continue;
				}
				QVERIFY(PipeConnection::hasMark(type));
					// on the side of the pipe, within the room of the nozzle
				QVERIFY2(rect.left() >= 0, qPrintable(PipeConnection::toString(type)));
				QVERIFY(rect.right() <= 16);
				QVERIFY(rect.top() >= -12 && rect.bottom() <= 12);
			}
				// a flange: a line of 20 across, 1 mm from the nozzle
			QPainterPath flange, none;
			PipeConnection::markShapes(PipeConnection::Flange, &flange, &none);
			QCOMPARE(flange.boundingRect(), QRectF(4, -10, 0, 20));
			QVERIFY(none.isEmpty());
				// a thread: a short line, half as long
			QPainterPath thread;
			PipeConnection::markShapes(PipeConnection::Thread, &thread, &none);
			QCOMPARE(thread.boundingRect(), QRectF(4, -5, 0, 10));
				// a socket opens towards the pipe, its arms up to 14
			QPainterPath socket;
			PipeConnection::markShapes(PipeConnection::SolventSocket, &socket, &none);
			QCOMPARE(socket.boundingRect(), QRectF(4, -10, 10, 20));
				// a socket to weld has two dots at the ends of its arms
			QPainterPath fusion, dots;
			PipeConnection::markShapes(PipeConnection::FusionSocket, &fusion, &dots);
			QCOMPARE(dots.boundingRect(), QRectF(12, -12, 4, 24));
				// a butt weld, a dot of radius 4 at 4
			QPainterPath weld_lines, weld;
			PipeConnection::markShapes(PipeConnection::ButtWeld, &weld_lines, &weld);
			QVERIFY(weld_lines.isEmpty());
			QCOMPARE(weld.boundingRect(), QRectF(0, -4, 8, 8));
				// a grooved end, a collar of 10 x 20 from 2 to 12
			QPainterPath collar;
			PipeConnection::markShapes(PipeConnection::Grooved, &collar, &none);
			QCOMPARE(collar.boundingRect(), QRectF(2, -10, 10, 20));
		}

		void eachChoiceOfANozzleHasItsWord()
		{
			using Choice = PipeConnection::MarkChoice;
			for (Choice choice : {Choice::Always, Choice::Never}) {
				QCOMPARE(PipeConnection::markChoiceFromString(PipeConnection::markChoiceToString(choice)),
						 choice);
			}
			QCOMPARE(PipeConnection::markChoiceFromString(QString()), Choice::Project);
			QCOMPARE(PipeConnection::markChoiceFromString(QStringLiteral("sometimes")), Choice::Project);
		}

			// ---- the setting of the project ----
		void theProjectDrawsNoConnectionByDefault()
		{
			const PidCodification codification;
			QVERIFY(codification.drawn_connections.isEmpty());
			for (PipeConnection::Type type : PipeConnection::all()) {
				QVERIFY(!codification.drawsConnection(type));
			}
			QVERIFY(codification.isEmpty());
		}

		void theProjectKeepsTheConnectionsItDraws()
		{
			PidCodification codification;
			codification.drawn_connections = {PipeConnection::ButtWeld, PipeConnection::Flange};
			QVERIFY(codification.drawsConnection(PipeConnection::Flange));
			QVERIFY(!codification.drawsConnection(PipeConnection::Thread));
			QVERIFY(codification.hasTextSettings());

				// in the file: an attribute of the block, words in the order
				// of the lists
			QDomDocument doc;
			QDomElement project = doc.createElement(QStringLiteral("project"));
			project.appendChild(codification.toXml(doc));
			QCOMPARE(project.firstChildElement(QStringLiteral("pid_codification"))
					 .attribute(QStringLiteral("drawn_connections")),
					 QStringLiteral("flange butt_weld"));
			PidCodification read;
			read.fromXml(project);
			QCOMPARE(read.drawn_connections,
					 QList<PipeConnection::Type>({PipeConnection::Flange, PipeConnection::ButtWeld}));
			PidCodification sorted = codification;
			sorted.drawn_connections = QList<PipeConnection::Type>(
						{PipeConnection::Flange, PipeConnection::ButtWeld});
			QVERIFY(read == sorted);

				// with the palette, for new projects
			const PidCodification palette = PidCodification::fromCsv(read.paletteToCsv());
			QCOMPARE(palette.drawn_connections, read.drawn_connections);
			QVERIFY(read.codesToCsv().indexOf(QStringLiteral("drawn_connections")) < 0);
				// a word not known is left out, the others are read
			int skipped = 0;
			const PidCodification odd = PidCodification::fromCsv(
						QStringLiteral("text_option;drawn_connections;thread bayonet\n"), &skipped);
			QCOMPARE(odd.drawn_connections, QList<PipeConnection::Type>({PipeConnection::Thread}));
				// none drawn: nothing written
			QDomDocument doc2;
			QDomElement project2 = doc2.createElement(QStringLiteral("project"));
			PidCodification other;
			other.colored_labels = true;
			project2.appendChild(other.toXml(doc2));
			QVERIFY(!project2.firstChildElement(QStringLiteral("pid_codification"))
					.hasAttribute(QStringLiteral("drawn_connections")));
		}

			// ---- a nozzle ----
		void aNozzleDrawsTheMarkTheProjectAsks()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *valve = place(project, folio, QPointF(200, 200));
			QVERIFY(valve);
			Terminal *n1 = terminal(valve, QStringLiteral("N1"));
			Terminal *n2 = terminal(valve, QStringLiteral("N2"));
			Terminal *e1 = terminal(valve, QStringLiteral("E1"));

				// none by default
			QVERIFY(!n1->drawsConnectionMark());
			drawFlanges(project);
			QVERIFY(n1->drawsConnectionMark());
				// a plain end has nothing to draw, an electrical terminal never
			QVERIFY(!n2->drawsConnectionMark());
			QVERIFY(!e1->drawsConnectionMark());

				// the element says otherwise for a nozzle
			valve->setNozzleMarks({{QStringLiteral("N1"), PipeConnection::MarkChoice::Never}});
			QVERIFY(!n1->drawsConnectionMark());
			project.setPidCodification(PidCodification());
			valve->setNozzleMarks({{QStringLiteral("N1"), PipeConnection::MarkChoice::Always}});
			QVERIFY(n1->drawsConnectionMark());
				// another connection, still drawn: the choice is the nozzle's
			valve->setNozzleConnections({{QStringLiteral("N1"), PipeConnection::Thread}});
			QVERIFY(n1->drawsConnectionMark());
			valve->setNozzleConnections({{QStringLiteral("N1"), PipeConnection::PlainEnd}});
			QVERIFY(!n1->drawsConnectionMark());
				// the mark is in the room of the nozzle, its shape is not: a
				// click there does not start a pipe
			QVERIFY(n1->boundingRect().width() >= 17);
			QVERIFY(n1->shape().boundingRect().width() <= 11);
			QCOMPARE(e1->shape().boundingRect(), e1->boundingRect());
		}

		void anElementKeepsTheChoicesOfItsNozzles()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *valve = place(project, folio, QPointF(200, 200));
			QVERIFY(valve);
			QVERIFY(!folio->hasSymbolChoices());
			valve->setNozzleMarks({{QStringLiteral("N1"), PipeConnection::MarkChoice::Never}});
				// a folio with such a choice is no .qetdb
			QVERIFY(folio->hasSymbolChoices());

			QDomDocument doc;
			QHash<Terminal *, int> table;
			const QDomElement written = valve->toXml(doc, table);
			const QDomElement nozzle = written.firstChildElement(QStringLiteral("nozzles"))
									   .firstChildElement(QStringLiteral("nozzle"));
			QCOMPARE(nozzle.attribute(QStringLiteral("name")), QStringLiteral("N1"));
			QCOMPARE(nozzle.attribute(QStringLiteral("mark")), QStringLiteral("never"));
			QVERIFY(!nozzle.hasAttribute(QStringLiteral("connection")));
			QCOMPARE(Element::nozzleMarksFromXml(written), valve->nozzleMarks());

				// as the project: nothing written
			valve->setNozzleMarks({});
			QDomDocument doc2;
			QVERIFY(valve->toXml(doc2, table).firstChildElement(QStringLiteral("nozzles")).isNull());
		}

		void theNozzlesTabGivesTheChoiceWithUndo()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *valve = place(project, folio, QPointF(200, 200));
			QVERIFY(valve);
			ElementNozzlesWidget tab(valve);
			tab.setLiveEdit(true);
			QComboBox *mark = tab.markFor(QStringLiteral("N1"));
			QVERIFY(mark);
			QCOMPARE(PipeConnection::MarkChoice(mark->currentData().toInt()),
					 PipeConnection::MarkChoice::Project);
			const int index = mark->findData(int(PipeConnection::MarkChoice::Never));
			mark->setCurrentIndex(index);
			emit mark->activated(index);
			QCOMPARE(valve->nozzleMarks().value(QStringLiteral("N1")), PipeConnection::MarkChoice::Never);
			folio->undoStack().undo();
			QVERIFY(valve->nozzleMarks().isEmpty());
			QCOMPARE(PipeConnection::MarkChoice(mark->currentData().toInt()),
					 PipeConnection::MarkChoice::Project);
		}

		void thePageOfTheProjectSetsTheConnectionsDrawn()
		{
			QETProject project;
			PidConfigPage page(&project);
			auto *flange = page.findChild<QCheckBox *>(QStringLiteral("drawn_connection_flange"));
			QVERIFY(flange);
			QVERIFY(!flange->isChecked());
				// a plain end draws nothing: no box
			QVERIFY(!page.findChild<QCheckBox *>(QStringLiteral("drawn_connection_plain_end")));
			flange->setChecked(true);
			page.applyProjectConf();
			QVERIFY(project.pidCodification().drawsConnection(PipeConnection::Flange));
			project.undoStack()->undo();
			QVERIFY(!project.pidCodification().drawsConnection(PipeConnection::Flange));
		}

			// ---- what is in front ----
		void aPipeOnAFlangeDrawsTheCounterFlange()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *a = place(project, folio, QPointF(200, 200));
			Element *b = place(project, folio, QPointF(400, 200));
			QVERIFY(a && b);
			drawFlanges(project);
			a->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::Flange}});
			Terminal *a2 = terminal(a, QStringLiteral("N2"));
			Terminal *b1 = terminal(b, QStringLiteral("N1"));
			auto *pipe = new Conductor(a2, b1);
			folio->addItem(pipe);
			QCOMPARE(pipe->medium(), TerminalData::Pipe);
			QVERIFY(a2->drawsConnectionMark() && b1->drawsConnectionMark());
			QVERIFY(pipe->drawsCounterFlange(a2));
			QVERIFY(pipe->drawsCounterFlange(b1));
				// on another connection, none
			PidCodification welds = project.pidCodification();
			welds.drawn_connections << PipeConnection::ButtWeld;
			project.setPidCodification(welds);
			b->setNozzleConnections({{QStringLiteral("N1"), PipeConnection::ButtWeld}});
			QVERIFY(b1->drawsConnectionMark());
			QVERIFY(!pipe->drawsCounterFlange(b1));
				// nor on a flange not drawn
			b->setNozzleConnections({});
			b->setNozzleMarks({{QStringLiteral("N1"), PipeConnection::MarkChoice::Never}});
			QVERIFY(!pipe->drawsCounterFlange(b1));
		}

		void aPipeTooShortOrTurningAtOnceLosesTheCounterFlange()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			drawFlanges(project);
				// N2 of a at x 220, N1 of b at x 230: a straight pipe of 10
			Element *a = place(project, folio, QPointF(200, 200));
			Element *b = place(project, folio, QPointF(250, 200));
			QVERIFY(a && b);
			a->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::Flange}});
			Terminal *a2 = terminal(a, QStringLiteral("N2"));
			Terminal *b1 = terminal(b, QStringLiteral("N1"));
			auto *pipe = new Conductor(a2, b1);
			folio->addItem(pipe);
			QVERIFY(a2->drawsConnectionMark());
			QVERIFY(!pipe->drawsCounterFlange(a2));
			QVERIFY(!pipe->drawsCounterFlange(b1));
		}

		void twoSymbolsAgainstEachOtherDrawNothingBetweenThem()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			drawFlanges(project);
				// N2 of a and N1 of b on the same point, x 220
			Element *a = place(project, folio, QPointF(200, 200));
			Element *b = place(project, folio, QPointF(240, 200));
			QVERIFY(a && b);
			a->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::Flange}});
			Terminal *a2 = terminal(a, QStringLiteral("N2"));
			Terminal *b1 = terminal(b, QStringLiteral("N1"));
			QCOMPARE(a2->dockConductor(), b1->dockConductor());
				// without a pipe
			QVERIFY(!a2->drawsConnectionMark());
			QVERIFY(!b1->drawsConnectionMark());
				// with a pipe of no length
			auto *pipe = new Conductor(a2, b1);
			folio->addItem(pipe);
			QVERIFY(pipe->endsTouch());
			QVERIFY(!a2->drawsConnectionMark());
			QVERIFY(!b1->drawsConnectionMark());
			QVERIFY(!pipe->drawsCounterFlange(a2));
				// moved apart: each draws its mark again
			b->setPos(400, 200);
			QVERIFY(a2->drawsConnectionMark());
			QVERIFY(b1->drawsConnectionMark());
		}

			// ---- on paper ----
		void theMarkIsPrintedInTheInkOfTheSymbol()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *valve = place(project, folio, QPointF(200, 200));
			QVERIFY(valve);
			folio->setDrawTerminals(false);
			folio->setDisplayGrid(false);
				// N1 at x 180: its flange at x 176, from y 190 to 210
			const QPointF center(180, 200);
			QVERIFY(!dark(render(folio, center), 36, 33));
			drawFlanges(project);
			QVERIFY(dark(render(folio, center), 36, 33));
			QVERIFY(!dark(render(folio, center), 28, 33));
		}
};

QET_APP_TEST_MAIN(tst_connectionmark)
#include "tst_connectionmark.moc"
