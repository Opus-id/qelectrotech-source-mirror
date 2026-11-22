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

#include "conductorsegment.h"
#include "diagram.h"
#include "editor/elementscene.h"
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
	draws the part it brings: its flange, its socket on a plain end, its
	grooved end, its half of a quick coupling. Two symbols placed against each
	other draw nothing between them.
*/
class tst_connectionmark : public QObject
{
	Q_OBJECT

		// An equipment, its nozzles at the end of short lines: N1, west, a
		// flange of DN 100; N2, east, a plain end; E1, an electrical terminal.
		// With @p in_line, a valve, its nozzles on the side of its body.
	static QString symbol(bool in_line = false)
	{
		return QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" %1"
			"width=\"60\" height=\"40\" hotspot_x=\"30\" hotspot_y=\"20\">"
			"<uuid uuid=\"{7d11111%2-1111-1111-1111-111111111111}\"/>"
			"<names><name lang=\"en\">Valve</name></names>")
				.arg(in_line ? QStringLiteral("pid_inline=\"true\" ") : QString(),
					 in_line ? QStringLiteral("2") : QStringLiteral("1"))
			+ QStringLiteral(
			"<description>"
			"<line x1=\"-20\" y1=\"0\" x2=\"20\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
			"<terminal uuid=\"{7d000000-0000-0000-0000-000000000001}\" name=\"N1\" x=\"-20\" y=\"0\" "
			"orientation=\"w\" type=\"Generic\" medium=\"pipe\" pid_dn=\"100\"/>"
			"<terminal uuid=\"{7d000000-0000-0000-0000-000000000002}\" name=\"N2\" x=\"20\" y=\"0\" "
			"orientation=\"e\" type=\"Generic\" medium=\"pipe\" pid_connection=\"plain_end\"/>"
			"<terminal uuid=\"{7d000000-0000-0000-0000-000000000003}\" name=\"E1\" x=\"0\" y=\"-20\" "
			"orientation=\"n\" type=\"Generic\"/>"
			"</description></definition>");
	}

		// A spec break (step 5l-1l): N1 west at x -10, N2 east at x +10, a
		// short line above, nothing between its nozzles
	static QString specBreakSymbol()
	{
		return QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"pid_inline=\"true\" pid_family=\"pipe\" pid_role=\"spec_break\" "
			"width=\"40\" height=\"60\" hotspot_x=\"20\" hotspot_y=\"30\">"
			"<uuid uuid=\"{7d111113-1111-1111-1111-111111111111}\"/>"
			"<names><name lang=\"en\">Spec break</name></names><description>"
			"<line x1=\"0\" y1=\"-20\" x2=\"0\" y2=\"-14\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
			"<terminal uuid=\"{7d000000-0000-0000-0000-000000000011}\" name=\"N1\" x=\"-10\" y=\"0\" "
			"orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"<terminal uuid=\"{7d000000-0000-0000-0000-000000000012}\" name=\"N2\" x=\"10\" y=\"0\" "
			"orientation=\"e\" type=\"Generic\" medium=\"pipe\"/>"
			"</description></definition>");
	}

		// The symbols of these tests in the project: an equipment, a valve,
		// a spec break
	static void ensureSymbols(QETProject &project)
	{
		const QString dir = QStringLiteral("import/marks");
		if (project.embeddedElementCollection()->exist(dir)) {
			return;
		}
		project.embeddedElementCollection()->createDir(
					QStringLiteral("import"), QStringLiteral("marks"), NamesList());
		const QList<QPair<QString, QString>> files{
			{QStringLiteral("tank.elmt"), symbol(false)},
			{QStringLiteral("valve.elmt"), symbol(true)},
			{QStringLiteral("specbreak.elmt"), specBreakSymbol()}};
		for (const auto &file : files) {
			QDomDocument doc;
			doc.setContent(file.second);
			project.embeddedElementCollection()->addElementDefinition(dir, file.first,
																	  doc.documentElement());
		}
	}

	static Element *placeFile(QETProject &project, Diagram *folio, const QPointF &pos,
							  const QString &file)
	{
		ensureSymbols(project);
		const ElementsLocation location(QStringLiteral("embed://import/marks/") + file, &project);
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		folio->addItem(element);
		element->setPos(pos);
		return element;
	}

	static Element *placeSpecBreak(QETProject &project, Diagram *folio, const QPointF &pos)
	{
		return placeFile(project, folio, pos, QStringLiteral("specbreak.elmt"));
	}

	static Element *place(QETProject &project, Diagram *folio, const QPointF &pos,
						  bool in_line = false)
	{
		return placeFile(project, folio, pos, in_line ? QStringLiteral("valve.elmt")
													  : QStringLiteral("tank.elmt"));
	}

	static Terminal *terminal(const Element *element, const QString &name)
	{
		for (Terminal *t : element->terminals()) {
			if (t->baseName() == name) return t;
		}
		return nullptr;
	}

		/// The points of the path of @p conductor, in its own frame
	static QList<QPointF> pointsOf(const Conductor *conductor)
	{
		QList<QPointF> points;
		const QPainterPath path = conductor->path();
		for (int i = 0 ; i < path.elementCount() ; ++i) {
			points << QPointF(path.elementAt(i));
		}
		return points;
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
				PipeConnection::markShapes(type, 4, &strokes, &fills);
				const QRectF rect = strokes.boundingRect().united(fills.boundingRect());
				if (type == PipeConnection::PlainEnd) {
					QVERIFY(strokes.isEmpty() && fills.isEmpty());
					QVERIFY(!PipeConnection::hasOwnMark(type));
					continue;
				}
				QVERIFY(PipeConnection::hasOwnMark(type));
					// on the side of the pipe, within the room of the nozzle
				QVERIFY2(rect.left() >= 0, qPrintable(PipeConnection::toString(type)));
				QVERIFY(rect.right() <= 16);
				QVERIFY(rect.top() >= -12 && rect.bottom() <= 12);
			}
				// a flange: a line of 20 across, 1 mm from a valve
			QPainterPath flange, none;
			PipeConnection::markShapes(PipeConnection::Flange, 4, &flange, &none);
			QCOMPARE(flange.boundingRect(), QRectF(4, -10, 0, 20));
			QVERIFY(none.isEmpty());
				// on an equipment, on the nozzle itself
			QPainterPath on_nozzle;
			PipeConnection::markShapes(PipeConnection::Flange, 0, &on_nozzle, &none);
			QCOMPARE(on_nozzle.boundingRect(), QRectF(0, -10, 0, 20));
				// a thread: two lines of 8 along the pipe, 6 either side of
				// it, which nothing crosses: no flange. After the side of a
				// valve, astride the end of the nozzle of an equipment.
			QPainterPath thread;
			PipeConnection::markShapes(PipeConnection::Thread, 4, &thread, &none);
			QCOMPARE(thread.boundingRect(), QRectF(4, -6, 8, 12));
			for (int i = 0 ; i < thread.elementCount() ; ++i) {
				QCOMPARE(qAbs(thread.elementAt(i).y), 6.0);
			}
			QPainterPath thread_on_nozzle;
			PipeConnection::markShapes(PipeConnection::Thread, 0, &thread_on_nozzle, &none);
			QCOMPARE(thread_on_nozzle.boundingRect(), QRectF(-4, -6, 8, 12));
				// a socket opens towards the pipe, its arms 10 long
			QPainterPath socket;
			PipeConnection::markShapes(PipeConnection::Socket, 4, &socket, &none);
			QCOMPARE(socket.boundingRect(), QRectF(4, -10, 10, 20));
				// a butt weld, a dot of radius 4 at the mark
			QPainterPath weld_lines, weld;
			PipeConnection::markShapes(PipeConnection::ButtWeld, 4, &weld_lines, &weld);
			QVERIFY(weld_lines.isEmpty());
			QCOMPARE(weld.boundingRect(), QRectF(0, -4, 8, 8));
				// a grooved end, a line of 14 in a collar of 8 x 20 with hooks
			QPainterPath collar;
			PipeConnection::markShapes(PipeConnection::Grooved, 4, &collar, &none);
			QCOMPARE(collar.boundingRect(), QRectF(2, -10, 8, 20));
			QPainterPath collar_on_nozzle;
			PipeConnection::markShapes(PipeConnection::Grooved, 0, &collar_on_nozzle, &none);
			QCOMPARE(collar_on_nozzle.boundingRect(), QRectF(-2, -10, 8, 20));
				// a quick coupling, its half: a hook of 20 x 6
			QPainterPath half;
			PipeConnection::markShapes(PipeConnection::QuickCoupling, 4, &half, &none);
			QCOMPARE(half.boundingRect(), QRectF(4, -10, 6, 20));
				// the plain end is drawn with the sockets
			QCOMPARE(PipeConnection::drawnWith(PipeConnection::PlainEnd), PipeConnection::Socket);
			QCOMPARE(PipeConnection::drawnWith(PipeConnection::Thread), PipeConnection::Thread);
		}

		void aPipeBringsThePartOfSomeConnections()
		{
			const QList<PipeConnection::Type> bringing{PipeConnection::Flange, PipeConnection::PlainEnd,
													   PipeConnection::Grooved,
													   PipeConnection::QuickCoupling};
			for (PipeConnection::Type type : PipeConnection::all()) {
				QCOMPARE(PipeConnection::bringsPart(type), bringing.contains(type));
				QPainterPath part;
				PipeConnection::pipePartShapes(type, 4, &part);
				QCOMPARE(part.isEmpty(), !bringing.contains(type));
			}
				// its flange, 1 mm from the nozzle or from the side of a valve
			QPainterPath flange, flange_on_nozzle;
			PipeConnection::pipePartShapes(PipeConnection::Flange, 4, &flange);
			PipeConnection::pipePartShapes(PipeConnection::Flange, 0, &flange_on_nozzle);
			QCOMPARE(flange.boundingRect(), QRectF(4, -10, 0, 20));
			QCOMPARE(flange_on_nozzle, flange);
				// its socket, open towards the nozzle, which enters it
			QPainterPath socket;
			PipeConnection::pipePartShapes(PipeConnection::PlainEnd, 0, &socket);
			QCOMPARE(socket.boundingRect(), QRectF(0, -10, 10, 20));
			QCOMPARE(socket.elementAt(0).x, 0.0);
				// its grooved end, 1 mm after that of the nozzle
			QPainterPath grooved;
			PipeConnection::pipePartShapes(PipeConnection::Grooved, 4, &grooved);
			QCOMPARE(grooved.boundingRect(), QRectF(8, -7, 0, 14));
				// its half of a quick coupling, the same hook turned over: 1 mm
				// between each arm and the line in front
			QPainterPath half, other_half, none;
			PipeConnection::markShapes(PipeConnection::QuickCoupling, 4, &half, &none);
			PipeConnection::pipePartShapes(PipeConnection::QuickCoupling, 4, &other_half);
			QCOMPARE(other_half.boundingRect(), QRectF(8, -10, 6, 20));
			QCOMPARE(other_half.elementAt(2).y, -half.elementAt(2).y);
			QCOMPARE(other_half.boundingRect().right() - half.boundingRect().right(), 4.0);
			QCOMPARE(other_half.boundingRect().left() - half.boundingRect().left(), 4.0);

				// where the pipe begins: after the part it brings; without it,
				// at the mark of a valve, else at the nozzle
			QCOMPARE(PipeConnection::pipeStart(PipeConnection::Flange, 0, true), 4.0);
			QCOMPARE(PipeConnection::pipeStart(PipeConnection::Flange, 4, true), 4.0);
			QCOMPARE(PipeConnection::pipeStart(PipeConnection::Flange, 4, false), 0.0);
			QCOMPARE(PipeConnection::pipeStart(PipeConnection::PlainEnd, 4, true), 14.0);
			QCOMPARE(PipeConnection::pipeStart(PipeConnection::PlainEnd, 4, false), 0.0);
			QCOMPARE(PipeConnection::pipeStart(PipeConnection::Grooved, 0, true), 4.0);
			QCOMPARE(PipeConnection::pipeStart(PipeConnection::Grooved, 4, false), 4.0);
			QCOMPARE(PipeConnection::pipeStart(PipeConnection::QuickCoupling, 4, true), 14.0);
			QCOMPARE(PipeConnection::pipeStart(PipeConnection::Socket, 4, true), 4.0);
			QCOMPARE(PipeConnection::pipeStart(PipeConnection::Socket, 0, true), 0.0);
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
			const PidCodification palette = PidCodification::fromCsv(read.presentationToCsv());
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
				// a plain end is drawn with the sockets, an electrical
				// terminal never
			QVERIFY(!n2->drawsConnectionMark());
			QVERIFY(!e1->drawsConnectionMark());
			PidCodification sockets = project.pidCodification();
			sockets.drawn_connections << PipeConnection::Socket;
			project.setPidCodification(sockets);
			QVERIFY(n2->drawsConnectionMark());
				// the socket is its pipe's: no mark of its own
			QVERIFY(!n2->drawsOwnMark());
			drawFlanges(project);

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
			QVERIFY(n1->drawsConnectionMark());
			QVERIFY(!n1->drawsOwnMark());
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
			const QDomElement nozzle = written.firstChildElement(QStringLiteral("pid_nozzles"))
									   .firstChildElement(QStringLiteral("nozzle"));
			QCOMPARE(nozzle.attribute(QStringLiteral("name")), QStringLiteral("N1"));
			QCOMPARE(nozzle.attribute(QStringLiteral("mark")), QStringLiteral("never"));
			QVERIFY(!nozzle.hasAttribute(QStringLiteral("connection")));
			QCOMPARE(Element::nozzleMarksFromXml(written), valve->nozzleMarks());

				// as the project: nothing written
			valve->setNozzleMarks({});
			QDomDocument doc2;
			QVERIFY(valve->toXml(doc2, table).firstChildElement(QStringLiteral("pid_nozzles")).isNull());
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
				// a plain end is drawn with the sockets: no box of its own
			QVERIFY(!page.findChild<QCheckBox *>(QStringLiteral("drawn_connection_plain_end")));
			QVERIFY(page.findChild<QCheckBox *>(QStringLiteral("drawn_connection_socket")));
			QVERIFY(page.findChild<QCheckBox *>(QStringLiteral("drawn_connection_quick_coupling")));
			flange->setChecked(true);
			page.applyProjectConf();
			QVERIFY(project.pidCodification().drawsConnection(PipeConnection::Flange));
			project.undoStack()->undo();
			QVERIFY(!project.pidCodification().drawsConnection(PipeConnection::Flange));
		}

			// Step 5l-1u: the families of symbols the connections are drawn on
		void theFamiliesOfSymbolsAreChosen()
		{
			PidCodification codification;
				// all by default, nothing written
			QVERIFY(codification.drawsOnAllFamilies());
			QVERIFY(!codification.hasTextSettings());
			QVERIFY(codification.drawsOnFamily(QStringLiteral("valve")));
			QCOMPARE(PidCodification::connectionFamily(QString()), QStringLiteral("other"));
			QCOMPARE(PidCodification::connectionFamily(QStringLiteral("signal")), QStringLiteral("other"));
			QCOMPARE(PidCodification::connectionFamily(QStringLiteral("pipe")), QStringLiteral("pipe"));

			codification.drawn_families = {QStringLiteral("equipment")};
			QVERIFY(codification.hasTextSettings());
			QVERIFY(codification.drawsOnFamily(QStringLiteral("equipment")));
			QVERIFY(!codification.drawsOnFamily(QStringLiteral("valve")));
			QVERIFY(!codification.drawsOnFamily(QString()));
				// the file
			QDomDocument doc;
			QDomElement holder = doc.createElement(QStringLiteral("project"));
			holder.appendChild(codification.toXml(doc));
			QCOMPARE(holder.firstChildElement().attribute(QStringLiteral("drawn_families")),
					 QStringLiteral("equipment"));
			PidCodification read;
			read.fromXml(holder);
			QCOMPARE(read.drawn_families, QStringList{QStringLiteral("equipment")});
				// none chosen is kept as none, not as all
			codification.drawn_families.clear();
			QDomDocument doc2;
			QDomElement holder2 = doc2.createElement(QStringLiteral("project"));
			holder2.appendChild(codification.toXml(doc2));
			read.fromXml(holder2);
			QVERIFY(read.drawn_families.isEmpty());
				// the default presentation, an unknown word left out
			codification.drawn_families = {QStringLiteral("valve"), QStringLiteral("pipe")};
			QVERIFY(codification.presentationToCsv().contains(QStringLiteral("text_option;drawn_families;valve pipe")));
			int skipped = 0;
			const PidCodification csv = PidCodification::fromCsv(
						QStringLiteral("text_option;drawn_families;pipe tank valve\n"), &skipped);
			QCOMPARE(csv.drawn_families, (QStringList{QStringLiteral("valve"), QStringLiteral("pipe")}));
			QCOMPARE(skipped, 1);
		}

			// Step 5l-1u: a symbol of a family not chosen draws no connection,
			// unless its nozzle says always
		void aFamilyNotChosenDrawsNothing()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
				// a symbol of no family: "other"
			Element *tank = place(project, folio, QPointF(200, 200));
			QVERIFY(tank);
			drawFlanges(project);
			Terminal *n1 = terminal(tank, QStringLiteral("N1"));
			QVERIFY(n1->drawsConnectionMark());

			PidCodification codification = project.pidCodification();
			codification.drawn_families = {QStringLiteral("equipment")};
			project.setPidCodification(codification);
			QVERIFY(!n1->drawsConnectionMark());
				// its nozzle says always
			tank->setNozzleMarks({{QStringLiteral("N1"), PipeConnection::MarkChoice::Always}});
			QVERIFY(n1->drawsConnectionMark());
			tank->setNozzleMarks({});
				// "other" chosen again
			codification.drawn_families << QStringLiteral("other");
			project.setPidCodification(codification);
			QVERIFY(n1->drawsConnectionMark());
		}

			// Step 5l-1u: the boxes of the families in the page, all checked,
			// given to the project with undo
		void thePageOfTheProjectSetsTheFamilies()
		{
			QETProject project;
			PidConfigPage page(&project);
			for (const QString &family : PidCodification::connectionFamilies()) {
				auto *box = page.findChild<QCheckBox *>(QStringLiteral("drawn_family_") + family);
				QVERIFY2(box, qPrintable(family));
				QVERIFY(box->isChecked());
			}
			QVERIFY(!page.findChild<QCheckBox *>(QStringLiteral("drawn_family_signal")));
			page.findChild<QCheckBox *>(QStringLiteral("drawn_family_valve"))->setChecked(false);
			page.applyProjectConf();
			QVERIFY(!project.pidCodification().drawsOnFamily(QStringLiteral("valve")));
			QVERIFY(project.pidCodification().drawsOnFamily(QStringLiteral("equipment")));
			project.undoStack()->undo();
			QVERIFY(project.pidCodification().drawsOnAllFamilies());
		}

			// ---- what is in front ----
		void aPipeOnAFlangeBringsItsOwn()
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
				// an equipment: its flange on the nozzle, the counter-flange
				// of the pipe 1 mm beyond, where the pipe begins
			QVERIFY(a2->drawsConnectionMark() && b1->drawsConnectionMark());
			QVERIFY(a2->drawsOwnMark());
			QCOMPARE(a2->markDistance(), 0.0);
			QVERIFY(pipe->drawsPipePart(a2));
			QVERIFY(pipe->drawsPipePart(b1));
			QCOMPARE(pipe->pipeStartAt(a2), 4.0);
				// on another connection, none, and the pipe begins at the
				// nozzle, where the mark of an equipment is
			PidCodification welds = project.pidCodification();
			welds.drawn_connections << PipeConnection::ButtWeld;
			project.setPidCodification(welds);
			b->setNozzleConnections({{QStringLiteral("N1"), PipeConnection::ButtWeld}});
			QVERIFY(b1->drawsOwnMark());
			QVERIFY(!pipe->drawsPipePart(b1));
			QCOMPARE(pipe->pipeStartAt(b1), 0.0);
				// nor on a flange not drawn
			b->setNozzleConnections({});
			b->setNozzleMarks({{QStringLiteral("N1"), PipeConnection::MarkChoice::Never}});
			QVERIFY(!pipe->drawsPipePart(b1));
			QCOMPARE(pipe->pipeStartAt(b1), 0.0);
		}

		void theSideOfAValveStandsForItsFlange()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			drawFlanges(project);
			Element *valve = place(project, folio, QPointF(200, 200), true);
			Element *tank = place(project, folio, QPointF(400, 200));
			QVERIFY(valve && tank);
			QVERIFY(valve->isPipeInline());
			valve->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::Flange}});
			Terminal *v1 = terminal(valve, QStringLiteral("N1"));
			Terminal *v2 = terminal(valve, QStringLiteral("N2"));
			QCOMPARE(v1->markDistance(), 4.0);
				// a free flange of a valve: its side, nothing more
			QVERIFY(v1->drawsConnectionMark());
			QVERIFY(!v1->drawsOwnMark());
				// joined: the flange the pipe brings, 1 mm from the body, where
				// the pipe begins
			auto *pipe = new Conductor(v2, terminal(tank, QStringLiteral("N1")));
			folio->addItem(pipe);
			QVERIFY(!v2->drawsOwnMark());
			QVERIFY(pipe->drawsPipePart(v2));
			QCOMPARE(pipe->pipeStartAt(v2), 4.0);
				// another connection: the valve draws it, 1 mm from its body,
				// and the pipe stops there
			PidCodification sockets = project.pidCodification();
			sockets.drawn_connections << PipeConnection::Socket;
			project.setPidCodification(sockets);
			valve->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::Socket}});
			QVERIFY(v2->drawsOwnMark());
			QVERIFY(!pipe->drawsPipePart(v2));
			QCOMPARE(pipe->pipeStartAt(v2), 4.0);
				// a plain end: the pipe brings its socket, its mouth 1 mm from
				// the body, and begins at its back
			valve->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::PlainEnd}});
			QVERIFY(!v2->drawsOwnMark());
			QVERIFY(pipe->drawsPipePart(v2));
			QCOMPARE(pipe->pipeStartAt(v2), 14.0);
				// the sockets not drawn: the pipe goes to the nozzle
			project.setPidCodification(PidCodification());
			drawFlanges(project);
			QVERIFY(!pipe->drawsPipePart(v2));
			QCOMPARE(pipe->pipeStartAt(v2), 0.0);
		}

		void aPipeBringsItsGroovedEndAndItsHalfOfAQuickCoupling()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			PidCodification drawn = project.pidCodification();
			drawn.drawn_connections = {PipeConnection::Grooved, PipeConnection::QuickCoupling};
			project.setPidCodification(drawn);
			Element *valve = place(project, folio, QPointF(200, 200), true);
			Element *tank = place(project, folio, QPointF(400, 200));
			QVERIFY(valve && tank);
			Terminal *v2 = terminal(valve, QStringLiteral("N2"));
			Terminal *t1 = terminal(tank, QStringLiteral("N1"));
			auto *pipe = new Conductor(v2, t1);
			folio->addItem(pipe);
				// a grooved end: the pipe's 1 mm after the nozzle's, in the
				// collar the nozzle draws
			valve->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::Grooved}});
			tank->setNozzleConnections({{QStringLiteral("N1"), PipeConnection::Grooved}});
			QVERIFY(v2->drawsOwnMark() && t1->drawsOwnMark());
			QVERIFY(pipe->drawsPipePart(v2) && pipe->drawsPipePart(t1));
			QCOMPARE(pipe->pipeStartAt(v2), 8.0);
			QCOMPARE(pipe->pipeStartAt(t1), 4.0);
				// a quick coupling: the other half, 2.5 mm from the nozzle's
			valve->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::QuickCoupling}});
			tank->setNozzleConnections({{QStringLiteral("N1"), PipeConnection::QuickCoupling}});
			QVERIFY(pipe->drawsPipePart(v2) && pipe->drawsPipePart(t1));
			QCOMPARE(pipe->pipeStartAt(v2), 14.0);
			QCOMPARE(pipe->pipeStartAt(t1), 10.0);
		}

		void aShortStraightPipeKeepsItsParts()
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
				// straight, never lengthened: the nozzles face each other
			QCOMPARE(pipe->path().length(), 10.0);
				// the counter-flanges are never lost
			QVERIFY(a2->drawsConnectionMark());
			QVERIFY(pipe->drawsPipePart(a2));
			QVERIFY(pipe->drawsPipePart(b1));
			QCOMPARE(pipe->pipeStartAt(a2), 4.0);
				// nor the half of a quick coupling
			PidCodification quick = project.pidCodification();
			quick.drawn_connections << PipeConnection::QuickCoupling;
			project.setPidCodification(quick);
			a->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::QuickCoupling}});
			QVERIFY(a2->drawsOwnMark());
			QVERIFY(pipe->drawsPipePart(a2));
			QCOMPARE(pipe->pipeStartAt(a2), 10.0);
		}

		void aPipeNeverTurnsTooCloseToANozzle()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
				// N2 of a at (220, 200), east; N1 of b at (280, 300), west
			Element *a = place(project, folio, QPointF(200, 200));
			Element *b = place(project, folio, QPointF(300, 300));
			QVERIFY(a && b);
			auto *pipe = new Conductor(terminal(a, QStringLiteral("N2")), terminal(b, QStringLiteral("N1")));
			folio->addItem(pipe);
			const QPointF a2 = pipe->mapFromScene(QPointF(220, 200));
			const QPointF b1 = pipe->mapFromScene(QPointF(280, 300));
				// a path turning 2.5 mm from a nozzle, as drawn before: too short
			QVERIFY(pipe->exitsTooShort({a2, a2 + QPointF(10, 0), a2 + QPointF(10, 100), b1}));
			QVERIFY(pipe->exitsTooShort({a2, a2 + QPointF(50, 0), a2 + QPointF(50, 100), b1}));
				// 7.5 mm from each: enough
			QVERIFY(!pipe->exitsTooShort({a2, a2 + QPointF(30, 0), a2 + QPointF(30, 50),
										  b1 - QPointF(30, 50), b1 - QPointF(30, 0), b1}));
				// going back into its symbol: too short
			QVERIFY(pipe->exitsTooShort({a2, a2 - QPointF(30, 0), a2 + QPointF(-30, 100), b1}));
				// an electrical wire: never
			auto *wire = new Conductor(terminal(a, QStringLiteral("E1")), terminal(b, QStringLiteral("E1")));
			folio->addItem(wire);
			const QPointF e1 = wire->mapFromScene(QPointF(200, 180));
			QVERIFY(!wire->exitsTooShort({e1, e1 + QPointF(0, -10), e1 + QPointF(100, -10),
										  wire->mapFromScene(QPointF(300, 280))}));

				// moved by hand, then its symbols come closer: the runs leaving
				// the nozzles keep their length, the others take the difference
			auto firstRun = [](const Conductor *conductor) {
				const QPainterPath path = conductor->path();
				return QLineF(QPointF(path.elementAt(0)), QPointF(path.elementAt(1))).length();
			};
			auto lastRun = [](const Conductor *conductor) {
				const QPainterPath path = conductor->path();
				const int n = path.elementCount();
				return QLineF(QPointF(path.elementAt(n - 2)), QPointF(path.elementAt(n - 1))).length();
			};
			QVERIFY(pipe->moveSegment(1, 0, 0));
			for (const QPointF &pos : {QPointF(320, 300), QPointF(280, 300), QPointF(250, 260),
									   QPointF(230, 300)}) {
				b->setPos(pos);
				QVERIFY2(firstRun(pipe) >= PipeConnection::pipeExit - 0.5, qPrintable(QString::number(pos.x())));
				QVERIFY2(lastRun(pipe) >= PipeConnection::pipeExit - 0.5, qPrintable(QString::number(pos.x())));
			}
		}

		void theExitOfAPipeHoldsEveryConnection()
		{
			for (PipeConnection::Type type : PipeConnection::all()) {
				for (qreal at : {0.0, PipeConnection::inlineMarkDistance}) {
					QPainterPath strokes, fills;
					PipeConnection::markShapes(type, at, &strokes, &fills);
					qreal reach = strokes.boundingRect().united(fills.boundingRect()).right();
					if (PipeConnection::bringsPart(type)) {
						reach = qMax(reach, PipeConnection::pipePartStart(type, at));
					}
					QVERIFY2(reach + PipeConnection::pipePartMargin <= PipeConnection::pipeExit,
							 qPrintable(PipeConnection::toString(type)));
				}
			}
		}

		void aPipeRoutedByItselfLeavesItsNozzlesFarEnough()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			drawFlanges(project);
				// N2 of a at (220, 200), east; N1 of b at (280, 300), west
			Element *a = place(project, folio, QPointF(200, 200));
			Element *b = place(project, folio, QPointF(300, 300));
			QVERIFY(a && b);
			a->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::Flange}});
			Terminal *a2 = terminal(a, QStringLiteral("N2"));
			Terminal *b1 = terminal(b, QStringLiteral("N1"));
			auto *pipe = new Conductor(a2, b1);
			folio->addItem(pipe);
			auto firstRun = [](const Conductor *conductor) {
				const QPainterPath path = conductor->path();
				return QLineF(QPointF(path.elementAt(0)), QPointF(path.elementAt(1))).length();
			};
			auto lastRun = [](const Conductor *conductor) {
				const QPainterPath path = conductor->path();
				const int n = path.elementCount();
				return QLineF(QPointF(path.elementAt(n - 2)), QPointF(path.elementAt(n - 1))).length();
			};
				// 7.5 mm straight from each nozzle: room for its flange
			QCOMPARE(firstRun(pipe), PipeConnection::pipeExit);
			QCOMPARE(lastRun(pipe), PipeConnection::pipeExit);
			QVERIFY(pipe->drawsPipePart(a2));
			QVERIFY(pipe->drawsPipePart(b1));
				// the same route whatever the connection, drawn or not
			const QPainterPath route = pipe->path();
			a->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::QuickCoupling}});
			project.setPidCodification(PidCodification());
			a->setPos(200, 210);
			a->setPos(200, 200);
			QCOMPARE(pipe->path(), route);
				// a segment moved by hand towards the nozzle stops at its exit
				// (step 7c-bis): no bend going back over the first run
			QVERIFY(pipe->moveSegment(1, -25, 0));
			QCOMPARE(firstRun(pipe), PipeConnection::pipeExit);
			QVERIFY(!pipe->foldsBack(pointsOf(pipe)));
				// an electrical wire between the same symbols: as before
			auto *wire = new Conductor(terminal(a, QStringLiteral("E1")), terminal(b, QStringLiteral("E1")));
			folio->addItem(wire);
			QCOMPARE(wire->medium(), TerminalData::Electrical);
			QCOMPARE(firstRun(wire), 10.0);
		}

			// Step 7c-bis: a segment dragged towards a nozzle stops 7.5 mm
			// from it, never behind, and goes on again when it comes back;
			// a wire keeps the stop of upstream
		void aDraggedSegmentStopsAtTheExitOfANozzle()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
				// N2 of a at (220, 200), east; N1 of b at (380, 300), west
			Element *a = place(project, folio, QPointF(200, 200));
			Element *b = place(project, folio, QPointF(400, 300));
			QVERIFY(a && b);
			auto *pipe = new Conductor(terminal(a, QStringLiteral("N2")), terminal(b, QStringLiteral("N1")));
			folio->addItem(pipe);
				// the segment of the pipe going down, and where it stands
			auto across = [](const Conductor *conductor, bool vertical) {
				const QList<ConductorSegment *> segments = conductor->segmentsList();
				for (int i = 1 ; i + 1 < segments.size() ; ++i) {
					const ConductorSegment *segment = segments.at(i);
					if ((vertical ? segment->isVertical() : segment->isHorizontal())
						&& qAbs(segment->length()) > 0.5) {
						return i;
					}
				}
				return -1;
			};
			auto xOf = [&](const Conductor *conductor) {
				return conductor->mapToScene(conductor->segmentsList().at(across(conductor, true))
											 ->firstPoint()).x();
			};
			QVERIFY(across(pipe, true) > 0);
				// far behind N2: it stops 7.5 mm out of it
			QVERIFY(pipe->moveSegment(across(pipe, true), -500, 0));
			QCOMPARE(xOf(pipe), 220 + PipeConnection::pipeExit);
			QVERIFY(!pipe->foldsBack(pointsOf(pipe)));
				// far behind N1 of b: the same
			QVERIFY(pipe->moveSegment(across(pipe, true), 500, 0));
			QCOMPARE(xOf(pipe), 380 - PipeConnection::pipeExit);
			QVERIFY(!pipe->foldsBack(pointsOf(pipe)));
				// back between them: it follows again
			QVERIFY(pipe->moveSegment(across(pipe, true), -40, 0));
			QCOMPARE(xOf(pipe), 310.0);

				// a wire between the same symbols (E1, north): the stop of
				// upstream, 12 from the end of the stub of its first terminal
			auto *wire = new Conductor(terminal(a, QStringLiteral("E1")), terminal(b, QStringLiteral("E1")));
			folio->addItem(wire);
			QCOMPARE(wire->medium(), TerminalData::Electrical);
			const int top = across(wire, false);
			QVERIFY(top > 0);
			QVERIFY(wire->moveSegment(top, 0, 500));
			QCOMPARE(wire->mapToScene(wire->segmentsList().at(across(wire, false))->firstPoint()).y(),
					 168.0);
		}

			// Trial of the user (22/11): the segment going down to a long
			// end segment (a line cut by a valve keeps its route) dragged
			// towards the valve; upstream adds a segment back to where the
			// end segment bent, which ran back over it: the end segment is
			// shortened instead
		void aDraggedSegmentShortensALongEnd()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
				// N2 of a at (220, 200), east; N1 of b at (680, 500), west
			Element *a = place(project, folio, QPointF(200, 200));
			Element *b = place(project, folio, QPointF(700, 500));
			QVERIFY(a && b);
			auto *pipe = new Conductor(terminal(a, QStringLiteral("N2")), terminal(b, QStringLiteral("N1")));
			folio->addItem(pipe);
			QVERIFY(pipe->setPathPoints({QPointF(220, 200), QPointF(250, 200), QPointF(250, 500),
										 QPointF(680, 500)}));
			QVERIFY(pipe->moveSegment(1, 300, 0));
			QList<QPointF> scene;
			for (const QPointF &point : pointsOf(pipe)) {
				scene << pipe->mapToScene(point);
			}
			QCOMPARE(scene.last(), QPointF(680, 500));
			QCOMPARE(scene.at(scene.size() - 2), QPointF(550, 500));
			QCOMPARE(scene.at(scene.size() - 3), QPointF(550, 200));
			QVERIFY(!scene.contains(QPointF(250, 500)));
			QVERIFY(!pipe->foldsBack(pointsOf(pipe)));

				// Each end segment is one straight piece up to its bend: the
				// segment upstream adds on the line of the first one is part
				// of it
			const QList<QPointF> once = pointsOf(pipe);
			QCOMPARE(once.size(), 4);
				// Dragged on, then back a little (trial of the user): the
				// segment added on the line of the end one, the same way,
				// left a short one in the corner, its handle there; none is
				// left
			QVERIFY(pipe->moveSegment(1, 20, 0));
			QVERIFY(pipe->moveSegment(1, -20, 0));
			QCOMPARE(pointsOf(pipe), once);
		}

			// Step 7c-bis: a detour drawn by hand stays on its side when the
			// ends of the pipe come level, and when they pass each other
		void aDetourDrawnByHandKeepsItsSide()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
				// N2 of a at (220, 300), east; N1 of b at (380, 200), west
			Element *a = place(project, folio, QPointF(200, 300));
			Element *b = place(project, folio, QPointF(400, 200));
			QVERIFY(a && b);
			auto *pipe = new Conductor(terminal(a, QStringLiteral("N2")), terminal(b, QStringLiteral("N1")));
			folio->addItem(pipe);
				// down 100 below a, then up to b: a U
			QVERIFY(pipe->setPathPoints({QPointF(220, 300), QPointF(250, 300), QPointF(250, 400),
										 QPointF(320, 400), QPointF(320, 200), QPointF(380, 200)}));
			auto bottom = [pipe]() {
				qreal y = -1e9;
				for (const QPointF &point : pointsOf(pipe)) {
					y = qMax(y, pipe->mapToScene(point).y());
				}
				return y;
			};
			QCOMPARE(bottom(), 400.0);
				// a comes level with b: the U is still below, its two
				// sides now as long
			a->setPos(200, 200);
			QCOMPARE(pointsOf(pipe).size(), 6);
			QCOMPARE(bottom(), 350.0);
			QVERIFY(!pipe->foldsBack(pointsOf(pipe)));
				// a above b: still below
			a->setPos(200, 100);
			QCOMPARE(pointsOf(pipe).size(), 6);
			QCOMPARE(bottom(), 300.0);
				// back where it was drawn: the U as drawn
			a->setPos(200, 300);
			QCOMPARE(bottom(), 400.0);
		}

		void theDefaultRouteIsThePathOfANewConductor()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			drawFlanges(project);
			Element *a = place(project, folio, QPointF(200, 200));
			Element *b = place(project, folio, QPointF(300, 300));
			QVERIFY(a && b);
			auto pathOf = [](const QList<QPointF> &points) {
				QPainterPath path;
				path.moveTo(points.constFirst());
				for (int i = 1; i < points.size(); ++i) path.lineTo(points.at(i));
				return path;
			};
			auto routeOf = [](const Terminal *t1, const Terminal *t2, qreal exit_room) {
				return Conductor::defaultRoute(t1->dockConductor(), t1->orientation(),
											   t2->dockConductor(), t2->orientation(), exit_room);
			};
				// a pipe, every pair of nozzles, both ways, and on one line
			for (const QPointF &pos : {QPointF(300, 300), QPointF(300, 200), QPointF(120, 260)}) {
				b->setPos(pos);
				for (const QString &n1 : {QStringLiteral("N1"), QStringLiteral("N2")}) {
					for (const QString &n2 : {QStringLiteral("N1"), QStringLiteral("N2")}) {
						Terminal *t1 = terminal(a, n1);
						Terminal *t2 = terminal(b, n2);
						auto *pipe = new Conductor(t1, t2);
						folio->addItem(pipe);
						QCOMPARE(pipe->medium(), TerminalData::Pipe);
						QCOMPARE(pipe->path(), pathOf(routeOf(t1, t2, PipeConnection::pipeExit)));
						folio->removeItem(pipe);
						delete pipe;
					}
				}
			}
				// an electrical wire: no exit room
			Terminal *e1 = terminal(a, QStringLiteral("E1"));
			Terminal *e2 = terminal(b, QStringLiteral("E1"));
			auto *wire = new Conductor(e1, e2);
			folio->addItem(wire);
			QCOMPARE(wire->path(), pathOf(routeOf(e1, e2, 0)));
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
				// of no length indeed: no exit added
			QCOMPARE(pipe->path().length(), 0.0);
			QVERIFY(!a2->drawsConnectionMark());
			QVERIFY(!b1->drawsConnectionMark());
			QVERIFY(!pipe->drawsPipePart(a2));
				// moved apart: each draws its mark again
			b->setPos(400, 200);
			QVERIFY(a2->drawsConnectionMark());
			QVERIFY(b1->drawsConnectionMark());
		}

			// ---- a spec break (step 5l-1l, l4) ----
			// Between two pipes it draws no connection, its nozzles never
			// mark one; each pipe goes on to its middle
		void aSpecBreakBetweenTwoPipesDrawsTheLineThrough()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			drawFlanges(project);
			folio->setDrawTerminals(false);
			folio->setDisplayGrid(false);
				// N1 of the mark at x 290, N2 at x 310
			Element *mark = placeSpecBreak(project, folio, QPointF(300, 200));
			Element *left = place(project, folio, QPointF(100, 200));
			Element *right = place(project, folio, QPointF(500, 200));
			QVERIFY(mark && left && right && mark->isSpecBreak());
			Terminal *m1 = terminal(mark, QStringLiteral("N1"));
			Terminal *m2 = terminal(mark, QStringLiteral("N2"));
			auto *before = new Conductor(terminal(left, QStringLiteral("N2")), m1);
			folio->addItem(before);
				// one pipe: nothing into the mark
			QVERIFY(before->lineIntoSpecBreak(m1).isNull());
			QVERIFY(!dark(render(folio, QPointF(300, 200)), 40, 40));
			auto *after = new Conductor(m2, terminal(right, QStringLiteral("N1")));
			folio->addItem(after);
				// no connection: neither the mark's, even asked, nor a part of
				// the pipes
			mark->setNozzleMarks({{QStringLiteral("N1"), PipeConnection::MarkChoice::Always}});
			QVERIFY(!m1->drawsConnectionMark() && !m2->drawsConnectionMark());
			QVERIFY(!before->drawsPipePart(m1) && !after->drawsPipePart(m2));
			QCOMPARE(before->pipeStartAt(m1), 0.0);
				// each pipe to the middle
			const QLineF into = before->lineIntoSpecBreak(m1);
			QCOMPARE(into.length(), 10.0);
			QCOMPARE(before->mapToScene(into.p2()), QPointF(300, 200));
			QCOMPARE(after->lineIntoSpecBreak(m2).length(), 10.0);
			QVERIFY(dark(render(folio, QPointF(300, 200)), 40, 40));
		}

			// A pipe on a spec break placed against a symbol meets the
			// connection of that symbol, as if the mark were not there: its
			// counter-flange on the nozzle of the mark, no line between them;
			// the symbol keeps its own mark
		void aPipeOnASpecBreakMeetsTheSymbolBeyond()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			drawFlanges(project);
			folio->setDrawTerminals(false);
			folio->setDisplayGrid(false);
			Element *mark = placeSpecBreak(project, folio, QPointF(300, 200));
				// N1 of the valve, x 310, on N2 of the mark
			Element *valve = place(project, folio, QPointF(330, 200), true);
			Element *left = place(project, folio, QPointF(100, 200));
			QVERIFY(mark && valve && left);
			Terminal *m1 = terminal(mark, QStringLiteral("N1"));
			Terminal *v1 = terminal(valve, QStringLiteral("N1"));
			QCOMPARE(terminal(mark, QStringLiteral("N2"))->contactNozzle(), v1);
			QCOMPARE(m1->acrossSpecBreak(), v1);
			auto *pipe = new Conductor(terminal(left, QStringLiteral("N2")), m1);
			folio->addItem(pipe);
				// the side of the valve stands for its flange, the mark
				// counts for no symbol
			QVERIFY(v1->drawsConnectionMark());
			QVERIFY(!v1->drawsOwnMark());
			QVERIFY(pipe->drawsPipePart(m1));
			QCOMPARE(pipe->pipeStartAt(m1), 0.0);
			QVERIFY(pipe->lineIntoSpecBreak(m1).isNull());
				// the counter-flange on N1 of the mark, x 290, from y 190 to
				// 210; nothing between it and the valve
			const QImage drawn = render(folio, QPointF(290, 200));
			QVERIFY(dark(drawn, 40, 33));
			QVERIFY(qGray(drawn.pixel(50, 40)) >= 192);
				// the pipe stops at its counter-flange: its square end does
				// not show beyond it, from x 291 to 292 (defect of the l4)
			QVERIFY2(qGray(drawn.pixel(41, 40)) >= 192, "the pipe shows beyond its counter-flange");
				// the valve says never, or the project draws no flange: nothing
			valve->setNozzleMarks({{QStringLiteral("N1"), PipeConnection::MarkChoice::Never}});
			QVERIFY(!pipe->drawsPipePart(m1));
			valve->setNozzleMarks({});
			QVERIFY(pipe->drawsPipePart(m1));
			project.setPidCodification(PidCodification());
			QVERIFY(!pipe->drawsPipePart(m1));
				// an equipment against the mark keeps its flange
			drawFlanges(project);
			valve->setPos(1000, 1000);
			QVERIFY(!m1->acrossSpecBreak());
			Element *tank = place(project, folio, QPointF(330, 200));
			QVERIFY(tank);
			Terminal *t1 = terminal(tank, QStringLiteral("N1"));
			QCOMPARE(m1->acrossSpecBreak(), t1);
			QVERIFY(t1->drawsOwnMark());
			QVERIFY(pipe->drawsPipePart(m1));
				// a wire never goes into a mark
			auto *wire = new Conductor(terminal(left, QStringLiteral("E1")), terminal(tank, QStringLiteral("E1")));
			folio->addItem(wire);
			QVERIFY(wire->lineIntoSpecBreak(wire->terminal1).isNull());
		}

			// A pipe begins at its nozzle: where no connection is drawn, the
			// square end of its pen, half its width, does not show behind
			// the nozzle, in the symbol (lot c3 of the step 6b-1c)
		void aPipeDoesNotShowBehindItsNozzle()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			folio->setDrawTerminals(false);
			folio->setDisplayGrid(false);
				// N2 of the equipment, east, x 120; N1 of the valve, west, x 310
			Element *tank = place(project, folio, QPointF(100, 200));
			Element *valve = place(project, folio, QPointF(330, 200), true);
			QVERIFY(tank && valve);
			Terminal *t2 = terminal(tank, QStringLiteral("N2"));
			Terminal *v1 = terminal(valve, QStringLiteral("N1"));
			auto *pipe = new Conductor(t2, v1);
			folio->addItem(pipe);
				// the project draws no connection: the pipe begins at its nozzles
			QVERIFY(!t2->drawsConnectionMark() && !v1->drawsConnectionMark());
			QCOMPARE(pipe->pipeStartAt(t2), 0.0);
			QCOMPARE(pipe->pipeStartAt(v1), 0.0);
				// a pipe of 4 units, from y 198 to 202: its upper edge is the
				// row 38 of the image, clear of the line of the symbols
				// on the valve: drawn up to x 310, not from 310 to 312
			const QImage at_valve = render(folio, QPointF(310, 200));
			QVERIFY(qGray(at_valve.pixel(36, 38)) < 192);
			QVERIFY2(qGray(at_valve.pixel(40, 38)) >= 192, "the pipe shows in the valve");
			QVERIFY2(qGray(at_valve.pixel(41, 38)) >= 192, "the pipe shows in the valve");
			QVERIFY2(qGray(at_valve.pixel(41, 41)) >= 192, "the pipe shows in the valve");
				// on the equipment: drawn from x 120, not from 118 to 120
			const QImage at_tank = render(folio, QPointF(120, 200));
			QVERIFY(qGray(at_tank.pixel(44, 38)) < 192);
			QVERIFY2(qGray(at_tank.pixel(38, 38)) >= 192, "the pipe shows on the nozzle of the equipment");
			QVERIFY2(qGray(at_tank.pixel(39, 38)) >= 192, "the pipe shows on the nozzle of the equipment");
				// with its connection drawn, the pipe begins further still
			drawFlanges(project);
			QVERIFY(pipe->pipeStartAt(v1) > 0);
			const QImage flanged = render(folio, QPointF(310, 200));
			QVERIFY2(qGray(flanged.pixel(41, 38)) >= 192, "the pipe shows in the valve");
		}

			// ---- in the element editor (lot c3 of the step 6b-1c) ----
			// The editor draws on a nozzle the mark of the connection chosen
			// for it, as a placed symbol does when its project draws it. The
			// mark is no part of the symbol: its frame does not change.
		void theEditorDrawsTheMarkOfANozzle()
		{
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<terminal x=\"40\" y=\"40\" name=\"N1\" orientation=\"e\" type=\"Generic\" "
				"medium=\"pipe\"/>"));
			QGraphicsScene scene(QRectF(0, 0, 80, 80));
			PartTerminal part(nullptr);
			part.fromXml(doc.documentElement());
			scene.addItem(&part);
			const QRectF frame = part.sceneGeometricRect();
			auto drawn = [&scene]() {
				QImage image(80, 80, QImage::Format_RGB32);
				image.fill(Qt::white);
				QPainter painter(&image);
				scene.render(&painter, QRectF(0, 0, 80, 80), QRectF(0, 0, 80, 80));
				painter.end();
				return image;
			};
				// a flange, the default: a line across the end of the nozzle,
				// from y 30 to 50 at x 40
			QPainterPath dots;
			QCOMPARE(part.connectionMark(&dots).boundingRect(), QRectF(0, -10, 0, 20));
			QVERIFY(dots.isEmpty());
			QVERIFY(dark(drawn(), 40, 33));
			QVERIFY(part.boundingRect().contains(QPointF(0, -10)));
				// a socket opens towards the pipe; facing west, to the west
			part.setConnection(PipeConnection::Socket);
			QCOMPARE(part.connectionMark().boundingRect(), QRectF(0, -10, 10, 20));
			QVERIFY(dark(drawn(), 46, 30) || dark(drawn(), 46, 29));
			part.setOrientation(Qet::West);
			QCOMPARE(part.connectionMark().boundingRect(), QRectF(-10, -10, 10, 20));
			part.setOrientation(Qet::East);
				// a butt weld: a dot on the end of the nozzle
			part.setConnection(PipeConnection::ButtWeld);
			QVERIFY(part.connectionMark(&dots).isEmpty());
			QCOMPARE(dots.boundingRect(), QRectF(-4, -4, 8, 8));
				// a plain end: nothing, as before
			part.setConnection(PipeConnection::PlainEnd);
			QVERIFY(part.connectionMark(&dots).isEmpty() && dots.isEmpty());
			QVERIFY(!dark(drawn(), 40, 33));
				// the frame of the symbol never counted the mark
			QCOMPARE(part.sceneGeometricRect(), frame);
				// any other terminal: nothing
			part.setConnection(PipeConnection::Flange);
			part.setMedium(TerminalData::Electrical);
			QVERIFY(part.connectionMark(&dots).isEmpty() && dots.isEmpty());
			QVERIFY(!dark(drawn(), 40, 33));
			scene.removeItem(&part);

				// a symbol a line runs through, a valve: its side stands for
				// its flange, nothing is drawn; any other mark 1 mm beyond
				// the nozzle, on the side of the pipe
			QDomDocument valve;
			valve.setContent(symbol(true));
			ElementScene editor(nullptr);
			editor.fromXml(valve, QPointF(), true);
			PartTerminal *n1 = nullptr, *n2 = nullptr;
			for (QGraphicsItem *item : editor.items()) {
				if (auto *terminal = qgraphicsitem_cast<PartTerminal *>(item)) {
					if (terminal->terminalName() == QLatin1String("N1")) n1 = terminal;
					if (terminal->terminalName() == QLatin1String("N2")) n2 = terminal;
				}
			}
			QVERIFY(n1 && n2);
			QCOMPARE(n1->connection(), PipeConnection::Flange);
			QVERIFY(n1->connectionMark().isEmpty());
				// N2, a plain end facing east, made a socket
			n2->setConnection(PipeConnection::Socket);
			QCOMPARE(n2->connectionMark().boundingRect(),
					 QRectF(PipeConnection::inlineMarkDistance, -10, 10, 20));
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
				// N1 at x 180: the flange of an equipment on it, from y 190 to 210
			const QPointF center(180, 200);
			QVERIFY(!dark(render(folio, center), 40, 33));
			drawFlanges(project);
			QVERIFY(dark(render(folio, center), 40, 33));
			QVERIFY(!dark(render(folio, center), 34, 33));
		}

		void aPipeBeginsAtTheFlangeItBrings()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			Element *valve = place(project, folio, QPointF(200, 200), true);
			Element *tank = place(project, folio, QPointF(400, 200));
			QVERIFY(valve && tank);
			folio->setDrawTerminals(false);
			folio->setDisplayGrid(false);
			valve->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::Flange}});
			auto *pipe = new Conductor(terminal(valve, QStringLiteral("N2")),
									   terminal(tank, QStringLiteral("N1")));
			folio->addItem(pipe);
				// N2 of the valve at x 220; at x 222, between the body and the
				// flange 1 mm beyond, the pipe is drawn, then no more
			const QPointF center(220, 200);
			auto light = [](const QImage &image, int x, int y) {
				return qGray(image.pixel(x, y)) >= 192;
			};
			QVERIFY(!light(render(folio, center), 42, 40));
			drawFlanges(project);
			const QImage drawn = render(folio, center);
			QVERIFY(light(drawn, 42, 40));
				// the flange of the pipe, and the pipe beyond
			QVERIFY(dark(drawn, 44, 33));
			QVERIFY(!light(drawn, 50, 40));
		}

		void theTwoHalvesOfAQuickCouplingFaceEachOtherMirroredToo()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			PidCodification quick = project.pidCodification();
			quick.drawn_connections = {PipeConnection::QuickCoupling};
			project.setPidCodification(quick);
			Element *a = place(project, folio, QPointF(200, 200));
			Element *b = place(project, folio, QPointF(400, 200));
			QVERIFY(a && b);
			folio->setDrawTerminals(false);
			folio->setDisplayGrid(false);
			a->setNozzleConnections({{QStringLiteral("N2"), PipeConnection::QuickCoupling}});
			auto *pipe = new Conductor(terminal(a, QStringLiteral("N2")), terminal(b, QStringLiteral("N1")));
			folio->addItem(pipe);
				// N2 of a at x 220: the arm of its half from x 220 to 226,
				// that of the pipe from 224 to 230, on the other side; at
				// x 222, only the arm of the nozzle
			const QPointF center(220, 200);
			auto armAt = [](const QImage &image, int y) {
				for (int dy = -1 ; dy <= 1 ; ++dy) {
					if (qGray(image.pixel(42, y + dy)) < 192) return true;
				}
				return false;
			};
			const QImage drawn = render(folio, center);
			QVERIFY(armAt(drawn, 50));
			QVERIFY(!armAt(drawn, 30));
				// mirrored top to bottom: the half of the nozzle turns over,
				// the half of the pipe with it
			a->setVerticalMirror(true);
			const QImage mirrored = render(folio, center);
			QVERIFY(armAt(mirrored, 30));
			QVERIFY(!armAt(mirrored, 50));
				// and the arm of the pipe is on the other side
			auto pipeArmAt = [](const QImage &image, int y) {
				for (int dy = -1 ; dy <= 1 ; ++dy) {
					if (qGray(image.pixel(48, y + dy)) < 192) return true;
				}
				return false;
			};
			QVERIFY(pipeArmAt(mirrored, 50));
			QVERIFY(!pipeArmAt(mirrored, 30));
		}
};

QET_APP_TEST_MAIN(tst_connectionmark)
#include "tst_connectionmark.moc"
