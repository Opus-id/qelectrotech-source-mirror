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
#include "editor/ui/pipingblockwidget.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "factory/elementpicturefactory.h"
#include "pidcodification.h"
#include "pidstatus.h"
#include "properties/elementdata.h"
#include "properties/terminaldata.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QComboBox>
#include <QDomDocument>
#include <QImage>
#include <QPainter>

/**
	A junction of pipes (step 6b-1c, pid_role="junction"): a tee, a cross, an
	injection point. Its symbol draws none of its lines: the folio draws one
	from the origin of the symbol to each nozzle, as the pipe of that nozzle
	(width, dashes, colour), a free nozzle as the first pipe of the junction,
	else as a main line. The arrow of an injection point takes the colour of
	its reagent.
*/
class tst_pipejunction : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40: N1 west, N2 east, N3 south, 20 from its origin,
		// with the attributes @p attributes and the drawing @p shapes
	static QString symbol(const QString &uuid, const QString &attributes,
						  const QString &shapes = QString())
	{
		return QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" %2 "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/>"
			"<names><name lang=\"en\">Fitting</name></names><description>%3"
			"<terminal uuid=\"{7e000000-0000-0000-0000-000000000001}\" name=\"N1\" x=\"-20\" y=\"0\" "
			"orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"<terminal uuid=\"{7e000000-0000-0000-0000-000000000002}\" name=\"N2\" x=\"20\" y=\"0\" "
			"orientation=\"e\" type=\"Generic\" medium=\"pipe\"/>"
			"<terminal uuid=\"{7e000000-0000-0000-0000-000000000003}\" name=\"N3\" x=\"0\" y=\"20\" "
			"orientation=\"s\" type=\"Generic\" medium=\"pipe\"/>"
			"</description></definition>").arg(uuid, attributes, shapes);
	}

	static QString tee()
	{
		return symbol(QStringLiteral("7e111111-1111-1111-1111-111111111111"),
					  QStringLiteral("pid_family=\"pipe\" pid_role=\"junction\" pid_run=\"N1 N2 N3\""));
	}

		// The reagent, N3, is out of the run; an arrow head of 8 x 10 points
		// at the line, 2 from its axis
	static QString injection()
	{
		return symbol(QStringLiteral("7e111112-1111-1111-1111-111111111111"),
					  QStringLiteral("pid_family=\"pipe\" pid_role=\"junction\" pid_run=\"N1 N2\""),
					  QStringLiteral(
			"<polygon x1=\"0\" y1=\"2\" x2=\"-4\" y2=\"12\" x3=\"4\" y3=\"12\" closed=\"true\" "
			"antialias=\"false\" style=\"line-style:normal;line-weight:normal;filling:black;color:black\"/>"));
	}

		// A fourth nozzle, N4, north: two lines cross
	static QString cross()
	{
		return symbol(QStringLiteral("7e111115-1111-1111-1111-111111111111"),
					  QStringLiteral("pid_family=\"pipe\" pid_role=\"junction\" pid_run=\"N1 N2 N3 N4\""),
					  QStringLiteral(
			"<terminal uuid=\"{7e000000-0000-0000-0000-000000000004}\" name=\"N4\" x=\"0\" y=\"-20\" "
			"orientation=\"n\" type=\"Generic\" medium=\"pipe\"/>"));
	}

		// The same nozzles, no role: it draws nothing
	static QString plain()
	{
		return symbol(QStringLiteral("7e111113-1111-1111-1111-111111111111"),
					  QStringLiteral("pid_family=\"pipe\" pid_run=\"N1 N2 N3\""));
	}

		// An equipment, whose nozzles pipes come from; a fourth one, N4, north
	static QString tank()
	{
		return symbol(QStringLiteral("7e111114-1111-1111-1111-111111111111"), QString(),
					  QStringLiteral(
			"<terminal uuid=\"{7e000000-0000-0000-0000-000000000004}\" name=\"N4\" x=\"0\" y=\"-20\" "
			"orientation=\"n\" type=\"Generic\" medium=\"pipe\"/>"));
	}

	static Element *place(QETProject &project, Diagram *folio, const QPointF &pos, const QString &file)
	{
		const QString dir = QStringLiteral("import/junctions");
		if (!project.embeddedElementCollection()->exist(dir)) {
			project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("junctions"), NamesList());
			const QList<QPair<QString, QString>> files{
				{QStringLiteral("tee.elmt"), tee()},
				{QStringLiteral("injection.elmt"), injection()},
				{QStringLiteral("cross.elmt"), cross()},
				{QStringLiteral("plain.elmt"), plain()},
				{QStringLiteral("tank.elmt"), tank()}};
			for (const auto &entry : files) {
				QDomDocument doc;
				doc.setContent(entry.second);
				project.embeddedElementCollection()->addElementDefinition(dir, entry.first,
																		  doc.documentElement());
			}
		}
		const ElementsLocation location(QStringLiteral("embed://import/junctions/") + file, &project);
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		folio->addItem(element);
		element->setPos(pos);
		return element;
	}

	static Terminal *nozzle(const Element *element, const char *name)
	{
		for (Terminal *t : element->terminals()) {
			if (t->baseName() == QLatin1String(name)) return t;
		}
		return nullptr;
	}

		// A fitting at (300, 200) and three equipments around it: west of N1,
		// east of N2, south of N3, each with a nozzle facing the fitting
	struct Scene {
		QETProject project;
		Diagram *folio = nullptr;
		Element *fitting = nullptr;
		Element *west = nullptr, *east = nullptr, *south = nullptr;

		explicit Scene(const QString &file)
		{
			folio = project.addNewDiagram();
			folio->setDrawTerminals(false);
			folio->setDisplayGrid(false);
			fitting = place(project, folio, QPointF(300, 200), file);
			west = place(project, folio, QPointF(100, 200), QStringLiteral("tank.elmt"));
			east = place(project, folio, QPointF(500, 200), QStringLiteral("tank.elmt"));
			south = place(project, folio, QPointF(300, 400), QStringLiteral("tank.elmt"));
		}

			// A pipe from @p name of the fitting to the equipment on that side
		Conductor *pipe(const char *name, PipeProperties::LineKind kind = PipeProperties::Main,
						const QColor &color = QColor())
		{
			Terminal *from = nozzle(fitting, name);
			Terminal *to = name == QLatin1String("N1") ? nozzle(west, "N2")
						 : name == QLatin1String("N2") ? nozzle(east, "N1")
													   : nozzle(south, "N4");
			if (!from || !to) return nullptr;
			auto *conductor = new Conductor(from, to);
			folio->addItem(conductor);
			ConductorProperties data = conductor->properties();
			data.m_pipe.line_kind = kind;
			data.m_pipe.color = color;
			conductor->setProperties(data);
			return conductor;
		}

			/// The folio as on paper, 80 x 80 around the origin of the fitting:
			/// the pixel (40, 40) is its origin
		QImage render() const
		{
			QImage image(80, 80, QImage::Format_RGB32);
			image.fill(Qt::white);
			QPainter painter(&image);
			folio->render(&painter, QRectF(0, 0, 80, 80), QRectF(260, 160, 80, 80));
			painter.end();
			return image;
		}
	};

	static bool inked(const QImage &image, int x, int y)
	{
		return qGray(image.pixel(x, y)) < 192;
	}

		/// How wide the line crossing the row @p y is, between x 30 and 50
	static int widthAcrossRow(const QImage &image, int y)
	{
		int width = 0;
		for (int x = 30 ; x <= 50 ; ++x) {
			if (inked(image, x, y)) ++width;
		}
		return width;
	}

		/// How wide the line crossing the column @p x is, between y 30 and 50
	static int widthAcrossColumn(const QImage &image, int x)
	{
		int width = 0;
		for (int y = 30 ; y <= 50 ; ++y) {
			if (inked(image, x, y)) ++width;
		}
		return width;
	}

	private slots:
			// ---- the role ----
		void theSymbolSaysItIsAJunction()
		{
			QDomDocument doc;
			doc.setContent(tee());
			ElementData data;
			data.fromXml(doc.documentElement());
			QCOMPARE(data.m_pid_role, QStringLiteral("junction"));

				// the element editor reads it, offers it and writes it
			ElementScene scene(nullptr);
			scene.fromXml(doc, QPointF(), true);
			QCOMPARE(scene.toXml().documentElement().attribute(QStringLiteral("pid_role")),
					 QStringLiteral("junction"));
			PipingBlockWidget block;
			block.setData(scene.elementData(), scene.pipeNozzleNames());
			QVERIFY(block.applyTo(scene.elementData()) == scene.elementData());
			QComboBox *role = nullptr;
			for (QComboBox *list : block.findChildren<QComboBox *>()) {
				if (list->findData(QStringLiteral("junction")) >= 0) {
					role = list;
				}
			}
			QVERIFY(role);
			QCOMPARE(role->currentData().toString(), QStringLiteral("junction"));
			role->setCurrentIndex(role->findData(QString()));
			scene.setElementData(block.applyTo(scene.elementData()));
			QVERIFY(!scene.toXml().documentElement().hasAttribute(QStringLiteral("pid_role")));
			role->setCurrentIndex(role->findData(QStringLiteral("junction")));
			scene.setElementData(block.applyTo(scene.elementData()));
			QCOMPARE(scene.toXml().documentElement().attribute(QStringLiteral("pid_role")),
					 QStringLiteral("junction"));

				// a placed element knows it
			Scene drawn(QStringLiteral("tee.elmt"));
			QVERIFY(drawn.fitting && drawn.fitting->isJunction());
			QVERIFY(!drawn.west->isJunction());
			Scene other(QStringLiteral("plain.elmt"));
			QVERIFY(other.fitting && !other.fitting->isJunction());
		}

			// ---- the drawing ----
		void aJunctionWithoutAPipeIsDrawnWhole()
		{
			Scene scene(QStringLiteral("tee.elmt"));
			QVERIFY(scene.fitting);
			const QImage image = scene.render();
				// main lines, 4 wide, from the origin to N1, N2 and N3, on
				// paper too; nothing to the north
			QVERIFY(inked(image, 40, 40));
			QCOMPARE(widthAcrossColumn(image, 25), 4);
			QCOMPARE(widthAcrossColumn(image, 55), 4);
			QCOMPARE(widthAcrossRow(image, 55), 4);
			QVERIFY(!inked(image, 40, 30));
				// to the nozzles, no further
			QVERIFY(inked(image, 21, 40) && inked(image, 58, 40) && inked(image, 40, 58));
			QVERIFY(!inked(image, 18, 40) && !inked(image, 61, 40) && !inked(image, 40, 61));
				// all within the frame of the symbol, which is repainted
			QVERIFY(scene.fitting->boundingRect().contains(QRectF(-20, -2, 40, 4)));
			QVERIFY(scene.fitting->boundingRect().contains(QRectF(-2, 0, 4, 20)));

				// the same nozzles without the role: nothing
			Scene other(QStringLiteral("plain.elmt"));
			QVERIFY(other.fitting);
			const QImage empty = other.render();
			QVERIFY(!inked(empty, 40, 40) && !inked(empty, 25, 40) && !inked(empty, 40, 55));
		}

			// The nozzles of a junction may be on the edge of its frame: the
			// ring drawn on screen around a nozzle without a pipe goes
			// beyond it, and must be repainted with it
		void theRingOfAFreeNozzleIsRepaintedWithTheJunction()
		{
			Scene scene(QStringLiteral("tee.elmt"));
			QVERIFY(scene.fitting);
			const QRectF frame(-20, -20, 40, 40);
			const QRectF ring1(-26, -6, 12, 12), ring2(14, -6, 12, 12), ring3(-6, 14, 12, 12);
			QVERIFY(scene.fitting->boundingRect().contains(ring1));
			QVERIFY(scene.fitting->boundingRect().contains(ring2));
			QVERIFY(scene.fitting->boundingRect().contains(ring3));
				// a pipe comes: no ring there, the frame and the other rings
			QVERIFY(scene.pipe("N1"));
			QVERIFY(scene.pipe("N2"));
			QCOMPARE(scene.fitting->boundingRect().left(), frame.left());
			QCOMPARE(scene.fitting->boundingRect().right(), frame.right());
			QVERIFY(scene.fitting->boundingRect().contains(ring3));
			Conductor *branch = scene.pipe("N3");
			QVERIFY(branch);
			QCOMPARE(scene.fitting->boundingRect(), frame);
				// it goes: the ring again
			scene.folio->removeItem(branch);
			delete branch;
			QVERIFY(scene.fitting->boundingRect().contains(ring3));
				// an equipment has no ring: its frame
			QCOMPARE(scene.west->boundingRect(), frame);
		}

		void eachLineIsDrawnAsItsPipe()
		{
			Scene scene(QStringLiteral("tee.elmt"));
			QVERIFY(scene.fitting);
				// the run a main line in blue, the branch a secondary line in red
			Conductor *run = scene.pipe("N1", PipeProperties::Main, Qt::blue);
			Conductor *branch = scene.pipe("N3", PipeProperties::Secondary, Qt::red);
			QVERIFY(run && branch);
			QImage image = scene.render();
			QCOMPARE(widthAcrossColumn(image, 25), 4);
			QCOMPARE(QColor(image.pixel(25, 40)), QColor(Qt::blue));
			QCOMPARE(widthAcrossRow(image, 55), 2);
			QCOMPARE(QColor(image.pixel(40, 55)), QColor(Qt::red));
				// N2, free, as the first pipe of the junction: the run
			QCOMPARE(widthAcrossColumn(image, 55), 4);
			QCOMPARE(QColor(image.pixel(55, 40)), QColor(Qt::blue));

				// the pipe changes, the junction follows: each side its own
			ConductorProperties data = run->properties();
			data.m_pipe.line_kind = PipeProperties::Secondary;
			run->setProperties(data);
			Conductor *after = scene.pipe("N2", PipeProperties::Main, Qt::darkGreen);
			QVERIFY(after);
			image = scene.render();
			QCOMPARE(widthAcrossColumn(image, 25), 2);
			QCOMPARE(widthAcrossColumn(image, 55), 4);
			QCOMPARE(QColor(image.pixel(55, 40)), QColor(Qt::darkGreen));

				// a folio drawn in black
			scene.folio->setDrawColoredConductors(false);
			image = scene.render();
			QCOMPARE(QColor(image.pixel(55, 40)), QColor(Qt::black));
			QCOMPARE(QColor(image.pixel(40, 55)), QColor(Qt::black));
		}

		void theDashesOfAnAuxiliaryLineStartFromTheOrigin()
		{
			Scene scene(QStringLiteral("tee.elmt"));
			QVERIFY(scene.fitting);
			QVERIFY(scene.pipe("N1"));
			Conductor *branch = scene.pipe("N3", PipeProperties::Auxiliary);
			QVERIFY(branch);
				// the pen of the pipe: 2 wide, dashes of 5 and 4 widths
			const QPen pen = branch->plainPen(1.0);
			QCOMPARE(pen.widthF(), 2.0);
			QCOMPARE(pen.dashPattern(), (QList<qreal>{5, 4}));
				// along N3: inked from the origin to 10, not from 10 to 16,
				// inked again up to the nozzle at 20
			const QImage image = scene.render();
			QVERIFY(inked(image, 40, 45));
			QVERIFY(inked(image, 40, 48));
			QVERIFY(!inked(image, 40, 53));
			QVERIFY(inked(image, 40, 58));
			QCOMPARE(widthAcrossRow(image, 46), 2);
		}

		void aRightAngleLeavesNoNotch()
		{
				// N1 and N3 main lines, N2 a secondary one: the corner of the
				// two thick lines, beyond the origin, is filled
			Scene scene(QStringLiteral("tee.elmt"));
			QVERIFY(scene.fitting);
			QVERIFY(scene.pipe("N1"));
			QVERIFY(scene.pipe("N2", PipeProperties::Secondary));
			QVERIFY(scene.pipe("N3"));
			const QImage image = scene.render();
				// above the thin line, from the origin to the side of N3
			QVERIFY(inked(image, 40, 38));
			QVERIFY(inked(image, 41, 38));
			QVERIFY(!inked(image, 43, 38));
			QCOMPARE(widthAcrossColumn(image, 45), 2);
			QCOMPARE(widthAcrossColumn(image, 35), 4);
		}

		void whereTwoLinesCrossTheFirstNozzlesAreOnTop()
		{
			Scene scene(QStringLiteral("cross.elmt"));
			QVERIFY(scene.fitting && scene.fitting->isJunction());
			QVERIFY(scene.pipe("N1", PipeProperties::Main, Qt::blue));
			QVERIFY(scene.pipe("N3", PipeProperties::Secondary, Qt::red));
			const QImage image = scene.render();
				// four lines; N4, free, as the first pipe
			QCOMPARE(widthAcrossRow(image, 25), 4);
			QCOMPARE(QColor(image.pixel(40, 25)), QColor(Qt::blue));
			QCOMPARE(QColor(image.pixel(40, 55)), QColor(Qt::red));
				// the line of N1 and N2 whole, over the one of N3 and N4
			for (int y = 38 ; y <= 41 ; ++y) {
				QCOMPARE(QColor(image.pixel(40, y)), QColor(Qt::blue));
			}
			QCOMPARE(QColor(image.pixel(40, 43)), QColor(Qt::red));
		}

		void theStatusOfTheJunctionColoursItsLines()
		{
			Scene scene(QStringLiteral("tee.elmt"));
			QVERIFY(scene.fitting);
			PidCodification codification = scene.project.pidCodification();
			codification.status_colors.insert(QStringLiteral("demolished"), QColor(Qt::red));
			scene.project.setPidCodification(codification);
			QVERIFY(scene.pipe("N1", PipeProperties::Main, Qt::blue));
			QCOMPARE(QColor(scene.render().pixel(25, 40)), QColor(Qt::blue));
			scene.fitting->setOptionChoices({{PidStatus::option(), QStringLiteral("demolished")}});
			QCOMPARE(QColor(scene.render().pixel(25, 40)), QColor(Qt::red));
		}

			// ---- an injection point ----
		void theArrowOfAnInjectionPointTakesTheColourOfItsReagent()
		{
			Scene scene(QStringLiteral("injection.elmt"));
			QVERIFY(scene.fitting && scene.fitting->isJunction());
			QCOMPARE(scene.fitting->pipeRun(), (QStringList{QStringLiteral("N1"), QStringLiteral("N2")}));
				// the line alone: its colour, on the arrow and to N3
			QVERIFY(scene.pipe("N1", PipeProperties::Main, Qt::blue));
			QCOMPARE(scene.fitting->symbolInk(), QColor(Qt::blue));
			QImage image = scene.render();
				// the arrow, beside the line to N3, 11 from the origin
			QCOMPARE(QColor(image.pixel(42, 51)), QColor(Qt::blue));
			QCOMPARE(QColor(image.pixel(40, 55)), QColor(Qt::blue));
				// the reagent: a secondary line in red; the arrow follows it,
				// the line keeps its own colour
			QVERIFY(scene.pipe("N3", PipeProperties::Secondary, Qt::red));
			QCOMPARE(scene.fitting->symbolInk(), QColor(Qt::red));
			image = scene.render();
			QCOMPARE(QColor(image.pixel(42, 51)), QColor(Qt::red));
			QCOMPARE(QColor(image.pixel(40, 57)), QColor(Qt::red));
			QCOMPARE(widthAcrossRow(image, 57), 2);
			QCOMPARE(QColor(image.pixel(25, 40)), QColor(Qt::blue));
			QCOMPARE(QColor(image.pixel(55, 40)), QColor(Qt::blue));
				// the line is drawn over its reagent, whole
			QCOMPARE(QColor(image.pixel(40, 41)), QColor(Qt::blue));
			QCOMPARE(QColor(image.pixel(40, 38)), QColor(Qt::blue));

				// a tee, whose three nozzles are of the line: the first pipe
			Scene other(QStringLiteral("tee.elmt"));
			QVERIFY(other.pipe("N1", PipeProperties::Main, Qt::blue));
			QVERIFY(other.pipe("N3", PipeProperties::Secondary, Qt::red));
			QCOMPARE(other.fitting->symbolInk(), QColor(Qt::blue));
		}

			// ---- in the collection ----
		void theThumbnailShowsTheLines()
		{
			Scene scene(QStringLiteral("tee.elmt"));
			QVERIFY(scene.fitting);
			const QImage thumbnail = ElementPictureFactory::instance()
									 ->pixmap(scene.fitting->location()).toImage();
			QCOMPARE(thumbnail.size(), QSize(40, 40));
				// its origin at (20, 20): the run, the branch, nothing above
			QVERIFY(qAlpha(thumbnail.pixel(5, 20)) > 128);
			QVERIFY(qAlpha(thumbnail.pixel(35, 20)) > 128);
			QVERIFY(qAlpha(thumbnail.pixel(20, 35)) > 128);
			QVERIFY(qAlpha(thumbnail.pixel(20, 5)) < 128);
				// main lines, 4 wide
			QVERIFY(qAlpha(thumbnail.pixel(18, 35)) > 128);

				// an injection point: its reagent, out of the run, a secondary
				// line, 2 wide, beside which its arrow shows
			Scene injection(QStringLiteral("injection.elmt"));
			QVERIFY(injection.fitting);
			const QImage reagent = ElementPictureFactory::instance()
								   ->pixmap(injection.fitting->location()).toImage();
			QVERIFY(qAlpha(reagent.pixel(5, 20)) > 128);
			QVERIFY(qAlpha(reagent.pixel(20, 35)) > 128);
			QVERIFY(qAlpha(reagent.pixel(17, 35)) < 128);
			QVERIFY(qAlpha(reagent.pixel(22, 31)) > 128);

			Scene other(QStringLiteral("plain.elmt"));
			QVERIFY(other.fitting);
			const QImage empty = ElementPictureFactory::instance()
								 ->pixmap(other.fitting->location()).toImage();
			QVERIFY(qAlpha(empty.pixel(5, 20)) < 128);
		}
};

QET_APP_TEST_MAIN(tst_pipejunction)
#include "tst_pipejunction.moc"
