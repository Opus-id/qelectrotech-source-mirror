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
#include "ui/configpage/pidconfigpage.h"
#include "ui/pipepropertieswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>
#include <QDomDocument>
#include <QImage>
#include <QPainter>
#include <QtMath>

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

		// A filled square of 40, centred on the origin
	static QString filledSquare()
	{
		return QStringLiteral(
			"<polygon x1=\"-20\" y1=\"-20\" x2=\"20\" y2=\"-20\" x3=\"20\" y3=\"20\" x4=\"-20\" y4=\"20\" "
			"closed=\"true\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:none;filling:black;color:black\"/>");
	}

		// The end of a line, 40 x 40: a single nozzle N1 at its origin,
		// facing @p facing; a free end when @p role is "free_end"; the
		// drawing @p shapes
	static QString lineEnd(const QString &uuid, const QString &facing, const QString &role = QString(),
						   const QString &shapes = QString())
	{
		return QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" pid_family=\"pipe\"%3 "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/>"
			"<names><name lang=\"en\">End</name></names><description>%4"
			"<terminal uuid=\"{7e000000-0000-0000-0000-000000000005}\" name=\"N1\" x=\"0\" y=\"0\" "
			"orientation=\"%2\" type=\"Generic\" medium=\"pipe\"/>"
			"</description></definition>")
				.arg(uuid, facing,
					 role.isEmpty() ? QString() : QStringLiteral(" pid_role=\"%1\"").arg(role), shapes);
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
				{QStringLiteral("tank.elmt"), tank()},
				{QStringLiteral("freeend.elmt"),
				 lineEnd(QStringLiteral("7e111116-1111-1111-1111-111111111111"), QStringLiteral("w"),
						 QStringLiteral("free_end"))},
					// the same filled square, a fitting and an equipment
				{QStringLiteral("square_pipe.elmt"),
				 symbol(QStringLiteral("7e111119-1111-1111-1111-111111111111"),
						QStringLiteral("pid_family=\"pipe\""), filledSquare())},
				{QStringLiteral("square_equipment.elmt"),
				 symbol(QStringLiteral("7e11111a-1111-1111-1111-111111111111"),
						QStringLiteral("pid_family=\"equipment\""), filledSquare())},
					// and a valve, an instrument (step 8r-2)
				{QStringLiteral("square_valve.elmt"),
				 symbol(QStringLiteral("7e11111b-1111-1111-1111-111111111111"),
						QStringLiteral("pid_family=\"valve\""), filledSquare())},
				{QStringLiteral("square_instrument.elmt"),
				 symbol(QStringLiteral("7e11111c-1111-1111-1111-111111111111"),
						QStringLiteral("pid_family=\"instrument\""), filledSquare())},
				{QStringLiteral("inlet.elmt"),
				 lineEnd(QStringLiteral("7e111117-1111-1111-1111-111111111111"), QStringLiteral("e"))},
					// drawn 40 east of its nozzle, beyond its frame
				{QStringLiteral("long.elmt"),
				 lineEnd(QStringLiteral("7e111118-1111-1111-1111-111111111111"), QStringLiteral("w"),
						 QString(),
						 QStringLiteral("<line x1=\"0\" y1=\"0\" x2=\"40\" y2=\"0\" end1=\"none\" end2=\"none\" "
										"length1=\"1.5\" length2=\"1.5\" antialias=\"false\" "
										"style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"))}};
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

		// The pixel of the square thumbnail of a P&ID symbol of the family
		// pipe (its fittings, its ends) showing its point @p point, when what
		// it draws spans @p drawn: drawn in the middle, a margin around,
		// twice its size at most (ElementPictureFactory::pixmap())
	static QPoint pixelOf(const QRectF &drawn, const QPointF &point)
	{
		const qreal side = ElementPictureFactory::pidThumbnailSide();
		const qreal room = side * ElementPictureFactory::pidThumbnailFill(QStringLiteral("pipe"));
		const qreal zoom = qMin(room / qMax(drawn.width(), drawn.height()), 2.0);
		const QPointF at = QPointF(side / 2.0, side / 2.0) + (point - drawn.center()) * zoom;
		return QPoint(qFloor(at.x()), qFloor(at.y()));
	}

	static int alphaAt(const QImage &image, const QPoint &pixel)
	{
		return qAlpha(image.pixel(pixel));
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
				// square, framed on its three lines, main lines 4 wide
			const int side = ElementPictureFactory::pidThumbnailSide();
			QCOMPARE(thumbnail.size(), QSize(side, side));
			const QRectF lines(-22, -2, 44, 24);
				// the run, the branch, nothing above
			QVERIFY(alphaAt(thumbnail, pixelOf(lines, QPointF(-15, 0))) > 128);
			QVERIFY(alphaAt(thumbnail, pixelOf(lines, QPointF(15, 0))) > 128);
			QVERIFY(alphaAt(thumbnail, pixelOf(lines, QPointF(0, 15))) > 128);
			QVERIFY(alphaAt(thumbnail, pixelOf(lines, QPointF(0, -10))) < 128);
			QVERIFY(alphaAt(thumbnail, pixelOf(lines, QPointF(1.5, 15))) > 128);

				// an injection point: its reagent, out of the run, a secondary
				// line, 2 wide, beside which its arrow shows
			Scene injection(QStringLiteral("injection.elmt"));
			QVERIFY(injection.fitting);
			const QImage reagent = ElementPictureFactory::instance()
								   ->pixmap(injection.fitting->location()).toImage();
			QVERIFY(alphaAt(reagent, pixelOf(lines, QPointF(-15, 0))) > 128);
			QVERIFY(alphaAt(reagent, pixelOf(lines, QPointF(0, 15))) > 128);
			QVERIFY(alphaAt(reagent, pixelOf(lines, QPointF(-1.6, 15))) < 128);
			QVERIFY(alphaAt(reagent, pixelOf(lines, QPointF(2, 11))) > 128);

			Scene other(QStringLiteral("plain.elmt"));
			QVERIFY(other.fitting);
			const QImage empty = ElementPictureFactory::instance()
								 ->pixmap(other.fitting->location()).toImage();
			QVERIFY(qAlpha(empty.pixel(5, 20)) < 128);
		}

			// The end of a line shows a stub of main line, 3 mm (12) out of
			// its nozzle the way its pipe leaves it: west for a free end,
			// which draws nothing else, east for an end facing east (8b).
			// Square as every P&ID thumbnail, framed on what it draws
		void theThumbnailOfALineEndShowsItsPipe()
		{
			const int side = ElementPictureFactory::pidThumbnailSide();
			Scene free_end(QStringLiteral("freeend.elmt"));
			QVERIFY(free_end.fitting);
			const QImage west = ElementPictureFactory::instance()
								->pixmap(free_end.fitting->location()).toImage();
			QCOMPARE(west.size(), QSize(side, side));
				// the stub alone, 4 wide, twice its size at most: nothing
				// beyond it on either side
			const QRectF stub_west(-12, -2, 12, 4);
			QVERIFY(alphaAt(west, pixelOf(stub_west, QPointF(-6, 0))) > 128);
			QVERIFY(alphaAt(west, pixelOf(stub_west, QPointF(-6, 1.5))) > 128);
			QVERIFY(alphaAt(west, pixelOf(stub_west, QPointF(-16, 0))) < 128);
			QVERIFY(alphaAt(west, pixelOf(stub_west, QPointF(4, 0))) < 128);

			Scene inlet(QStringLiteral("inlet.elmt"));
			QVERIFY(inlet.fitting);
			const QImage east = ElementPictureFactory::instance()
								->pixmap(inlet.fitting->location()).toImage();
			QCOMPARE(east.size(), QSize(side, side));
			const QRectF stub_east(0, -2, 12, 4);
			QVERIFY(alphaAt(east, pixelOf(stub_east, QPointF(6, 0))) > 128);
			QVERIFY(alphaAt(east, pixelOf(stub_east, QPointF(-4, 0))) < 128);

				// Drawn beyond its frame: the thumbnail takes it all in, its
				// line and its stub
			Scene long_end(QStringLiteral("long.elmt"));
			QVERIFY(long_end.fitting);
			const QImage whole = ElementPictureFactory::instance()
								 ->pixmap(long_end.fitting->location()).toImage();
			QCOMPARE(whole.size(), QSize(side, side));
			const QRectF line_and_stub(-12, -2, 52, 4);
				// its thin line, half on each row around its axis
			const QPoint far = pixelOf(line_and_stub, QPointF(30, 0));
			int ink = 0;
			for (int y = far.y() - 2 ; y <= far.y() + 2 ; ++y) {
				ink = qMax(ink, qAlpha(whole.pixel(far.x(), y)));
			}
			QVERIFY2(ink > 64, qPrintable(QString::number(ink)));
			QVERIFY(alphaAt(whole, pixelOf(line_and_stub, QPointF(-6, 0))) > 128);
		}

			// The drawing of an equipment fills more of its thumbnail than
			// that of a fitting (a pump and its motor stay large enough, a
			// valve does not crowd the list): 87 % and 70 % of its side
		void anEquipmentFillsMoreOfItsThumbnail()
		{
			const auto inkedWidth = [](const QString &file) {
				Scene scene(file);
				if (!scene.fitting) return -1;
				const QImage image = ElementPictureFactory::instance()
									 ->pixmap(scene.fitting->location()).toImage();
				const int row = image.height() / 2;
				int width = 0;
				for (int x = 0 ; x < image.width() ; ++x) {
					if (qAlpha(image.pixel(x, row)) > 128) ++width;
				}
				return width;
			};
			const int side = ElementPictureFactory::pidThumbnailSide();
			const int fitting = inkedWidth(QStringLiteral("square_pipe.elmt"));
			const int equipment = inkedWidth(QStringLiteral("square_equipment.elmt"));
			QVERIFY2(qAbs(fitting - qRound(side * 0.7)) <= 2, qPrintable(QString::number(fitting)));
			QVERIFY2(qAbs(equipment - qRound(side * 0.87)) <= 2, qPrintable(QString::number(equipment)));
		}

			// ---- a valve, an instrument on a line (step 8r-2) ----
		void aValveTakesTheColourItsPipesAgreeOn()
		{
			Scene scene(QStringLiteral("square_valve.elmt"));
			QVERIFY(scene.fitting);
			PidCodification codification = scene.project.pidCodification();
			QVERIFY(codification.symbols_follow_pipes);
			codification.object_colors.insert(QStringLiteral("valve"), QColor(Qt::green));
			codification.status_colors.insert(QStringLiteral("demolished"), QColor(Qt::magenta));
			scene.project.setPidCodification(codification);
			Element *valve = scene.fitting;
				// no pipe: its family
			QVERIFY(valve->takesPipeInk());
			QCOMPARE(valve->symbolInk(), QColor(Qt::green));
				// one pipe is enough, two which agree too; drawn so
			QVERIFY(scene.pipe("N1", PipeProperties::Main, Qt::blue));
			QCOMPARE(valve->symbolInk(), QColor(Qt::blue));
			Conductor *east = scene.pipe("N2", PipeProperties::Main, Qt::blue);
			QVERIFY(east);
			QCOMPARE(valve->symbolInk(), QColor(Qt::blue));
			QCOMPARE(QColor(scene.render().pixel(30, 30)), QColor(Qt::blue));
				// a third one of another colour: its family again
			Conductor *south = scene.pipe("N3", PipeProperties::Main, Qt::red);
			QVERIFY(south);
			QCOMPARE(valve->symbolInk(), QColor(Qt::green));
			QCOMPARE(QColor(scene.render().pixel(30, 30)), QColor(Qt::green));
				// it changes with the pipe
			ConductorProperties data = south->properties();
			data.m_pipe.color = QColor(Qt::blue);
			south->setProperties(data);
			QCOMPARE(QColor(scene.render().pixel(30, 30)), QColor(Qt::blue));
				// and when one goes
			data.m_pipe.color = QColor(Qt::red);
			south->setProperties(data);
			QCOMPARE(valve->symbolInk(), QColor(Qt::green));
			scene.folio->removeItem(south);
			delete south;
			QCOMPARE(QColor(scene.render().pixel(30, 30)), QColor(Qt::blue));
				// a status of works comes first
			valve->setOptionChoices({{PidStatus::option(), QStringLiteral("demolished")}});
			QCOMPARE(valve->symbolInk(), QColor(Qt::magenta));
			valve->setOptionChoices({});
			QCOMPARE(valve->symbolInk(), QColor(Qt::blue));
				// a folio drawn in black
			scene.folio->setDrawColoredConductors(false);
			QCOMPARE(valve->symbolInk(), QColor(Qt::black));
			scene.folio->setDrawColoredConductors(true);
				// the project says no: its family, at once
			codification.symbols_follow_pipes = false;
			scene.project.setPidCodification(codification);
			QVERIFY(!valve->takesPipeInk());
			QCOMPARE(valve->symbolInk(), QColor(Qt::green));
			QCOMPARE(QColor(scene.render().pixel(30, 30)), QColor(Qt::green));
				// a pipe changed then does not reach it
			data = east->properties();
			data.m_pipe.color = QColor(Qt::red);
			east->setProperties(data);
			QCOMPARE(valve->symbolInk(), QColor(Qt::green));
		}

		void aPipeWithoutColourAgreesWithNone()
		{
			Scene scene(QStringLiteral("square_valve.elmt"));
			QVERIFY(scene.fitting);
			PidCodification codification = scene.project.pidCodification();
			codification.object_colors.insert(QStringLiteral("valve"), QColor(Qt::green));
			scene.project.setPidCodification(codification);
			QVERIFY(scene.pipe("N1", PipeProperties::Main, Qt::blue));
			QVERIFY(scene.pipe("N2"));
			QVERIFY(!scene.fitting->agreedPipeColor().isValid());
			QCOMPARE(scene.fitting->symbolInk(), QColor(Qt::green));
		}

		void anInstrumentOnALineFollowsItAnEquipmentNever()
		{
			Scene instrument(QStringLiteral("square_instrument.elmt"));
			QVERIFY(instrument.fitting);
			QVERIFY(instrument.fitting->takesPipeInk());
			QVERIFY(instrument.pipe("N1", PipeProperties::Main, Qt::blue));
			QCOMPARE(instrument.fitting->symbolInk(), QColor(Qt::blue));
			QCOMPARE(QColor(instrument.render().pixel(30, 30)), QColor(Qt::blue));

			Scene equipment(QStringLiteral("square_equipment.elmt"));
			QVERIFY(equipment.fitting);
			QVERIFY(!equipment.fitting->takesPipeInk());
			QVERIFY(equipment.pipe("N1", PipeProperties::Main, Qt::blue));
			QCOMPARE(equipment.fitting->symbolInk(), QColor(Qt::black));
				// the west tank, of no family, neither
			QVERIFY(!instrument.west->takesPipeInk());
		}

		void theProjectSaysWhetherSymbolsFollowTheirPipes()
		{
				// on by default, nothing written
			PidCodification codification;
			QVERIFY(codification.symbols_follow_pipes);
			QVERIFY(codification.isEmpty());
			QVERIFY(codification.followsPipes(QStringLiteral("valve")));
			QVERIFY(codification.followsPipes(QStringLiteral("instrument")));
			QVERIFY(!codification.followsPipes(QStringLiteral("equipment")));
			QVERIFY(!codification.followsPipes(QStringLiteral("pipe")));
			QVERIFY(!codification.followsPipes(QString()));
			QVERIFY(!codification.presentationToCsv().contains(QStringLiteral("symbols_follow_pipes")));

				// off: written in the file and in the presentation
			codification.symbols_follow_pipes = false;
			QVERIFY(codification.hasColors());
			QVERIFY(!codification.followsPipes(QStringLiteral("valve")));
			QDomDocument doc;
			QDomElement holder = doc.createElement(QStringLiteral("project"));
			holder.appendChild(codification.toXml(doc));
			QCOMPARE(holder.firstChildElement().attribute(QStringLiteral("symbols_follow_pipes")),
					 QStringLiteral("false"));
			PidCodification read;
			read.fromXml(holder);
			QVERIFY(!read.symbols_follow_pipes);
			QVERIFY(read == codification);
			QVERIFY(codification.presentationToCsv().contains(
						QStringLiteral("text_option;symbols_follow_pipes;false")));
			QVERIFY(!PidCodification::fromCsv(codification.presentationToCsv()).symbols_follow_pipes);
			QVERIFY(!PidCodification::fromCsv(QStringLiteral("option_texte;symbols_follow_pipes;false"))
					 .symbols_follow_pipes);
				// a file of codes does not take it from the project
			PidCodification codes;
			PidCodification::Entry water;
			water.code = QStringLiteral("W01");
			water.name = QStringLiteral("Water");
			codes.fluids.append(water);
			QVERIFY(!codes.completedWith(codification).symbols_follow_pipes);
		}

		void thePageSetsWhetherSymbolsFollowTheirPipes()
		{
			QETProject project;
			PidConfigPage page(&project);
			auto *box = page.findChild<QCheckBox *>(QStringLiteral("symbols_follow_pipes"));
			QVERIFY(box);
			QVERIFY(box->isChecked());
			QVERIFY(box->isEnabled());
			box->setChecked(false);
			page.applyProjectConf();
			QVERIFY(!project.pidCodification().symbols_follow_pipes);
			project.undoStack()->undo();
			QVERIFY(project.pidCodification().symbols_follow_pipes);
		}

			// ---- the kinds of line of the project (step 8r-6) ----
		void theProjectKeepsItsKindsOfLine()
		{
			PidCodification codification;
				// those of ISO 10628-1, nothing written
			QCOMPARE(codification.line_kinds, PidCodification::isoLineKinds());
			QVERIFY(codification.isEmpty());
			QCOMPARE(codification.lineKind(PipeProperties::Main).width, 1.0);
			QCOMPARE(codification.lineKind(PipeProperties::Secondary).width, 0.5);
			QCOMPARE(codification.lineKind(PipeProperties::Auxiliary).pattern,
					 PidCodification::LinePattern::Dashes);
			QCOMPARE(codification.lineKind(QString()).code, PipeProperties::Main);
				// a code it does not know: a main line, its code kept
			QCOMPARE(codification.lineKind(QStringLiteral("steam")).code, QStringLiteral("steam"));
			QCOMPARE(codification.lineKind(QStringLiteral("steam")).width, 1.0);
			QCOMPARE(codification.lineKindName(QStringLiteral("steam")), QStringLiteral("steam"));
			QCOMPARE(codification.lineKindName(PipeProperties::Main), QStringLiteral("Main flow line"));

				// what cannot be
			QVERIFY(!codification.setLineKind({QStringLiteral("a b"), QString(), 1.0,
											   PidCodification::LinePattern::Solid}));
			QVERIFY(!codification.setLineKind({QStringLiteral("L4"), QString(), 9.0,
											   PidCodification::LinePattern::Solid}));
			QVERIFY(!codification.removeLineKind(PipeProperties::Main));
				// a kind added, one of ISO changed: written
			QVERIFY(codification.setLineKind({QStringLiteral("L4"), QStringLiteral("Buried"), 0.7,
											  PidCodification::LinePattern::DashDot}));
			QVERIFY(codification.setLineKind({PipeProperties::Secondary, QStringLiteral("no name"), 0.7,
											  PidCodification::LinePattern::Solid}));
			QCOMPARE(codification.line_kinds.size(), 4);
			QVERIFY(codification.lineKind(PipeProperties::Secondary).name.isEmpty());
			QCOMPARE(codification.lineKindName(QStringLiteral("L4")), QStringLiteral("Buried"));
			QVERIFY(codification.hasTextSettings());

			QDomDocument doc;
			QDomElement holder = doc.createElement(QStringLiteral("project"));
			holder.appendChild(codification.toXml(doc));
			int written = 0;
			for (QDomElement e = holder.firstChildElement().firstChildElement(QStringLiteral("line_kind"));
				 !e.isNull() ; e = e.nextSiblingElement(QStringLiteral("line_kind"))) {
				++written;
			}
			QCOMPARE(written, 2);
			PidCodification read;
			read.fromXml(holder);
			QCOMPARE(read.line_kinds, codification.line_kinds);
			QVERIFY(read == codification);

			const QString csv = codification.presentationToCsv();
			QVERIFY2(csv.contains(QStringLiteral("line_kind;L4;Buried;0.7;dash_dot")), qPrintable(csv));
			QVERIFY2(csv.contains(QStringLiteral("line_kind;secondary;;0.7;solid")), qPrintable(csv));
			QCOMPARE(PidCodification::fromCsv(csv).line_kinds, codification.line_kinds);
				// the French word, a comma, no pattern: solid
			int skipped = 0;
			const PidCodification french = PidCodification::fromCsv(
						QStringLiteral("genre_ligne;L5;Enterrée;0,7\ngenre_ligne;L6;;abc;solid\n"), &skipped);
			QCOMPARE(french.lineKind(QStringLiteral("L5")).width, 0.7);
			QCOMPARE(french.lineKind(QStringLiteral("L5")).pattern, PidCodification::LinePattern::Solid);
			QCOMPARE(french.lineKindName(QStringLiteral("L5")), QStringLiteral("Enterrée"));
			QCOMPARE(skipped, 1);

			QVERIFY(codification.removeLineKind(QStringLiteral("L4")));
			QCOMPARE(codification.line_kinds.size(), 3);
		}

		void aPipeIsDrawnAsItsProjectDrawsItsKind()
		{
			Scene scene(QStringLiteral("tee.elmt"));
			Conductor *pipe = scene.pipe("N1", PipeProperties::Secondary);
			QVERIFY(pipe);
				// ISO 10628-1 by default
			QCOMPARE(pipe->lineWidth(), 2.0);
			QVERIFY(pipe->pipeDashes().isEmpty());
			PidCodification codification = scene.project.pidCodification();
			QVERIFY(codification.setLineKind({PipeProperties::Secondary, QString(), 0.75,
											  PidCodification::LinePattern::ShortDashes}));
			QVERIFY(codification.setLineKind({QStringLiteral("L4"), QStringLiteral("Buried"), 1.5,
											  PidCodification::LinePattern::DashDot}));
			scene.project.setPidCodification(codification);
			QCOMPARE(pipe->lineWidth(), 3.0);
			QCOMPARE(pipe->pipeDashes(), (QList<qreal>{2, 2}));
			QCOMPARE(pipe->plainPen(1.0).widthF(), 3.0);
			QCOMPARE(pipe->plainPen(1.0).dashPattern(), (QList<qreal>{2, 2}));
				// a kind added
			ConductorProperties data = pipe->properties();
			data.m_pipe.line_kind = QStringLiteral("L4");
			pipe->setProperties(data);
			QCOMPARE(pipe->lineWidth(), 6.0);
			QCOMPARE(pipe->plainPen(1.0).dashPattern(), (QList<qreal>{6, 4, 1, 4}));
				// one the project does not know: a main line
			data.m_pipe.line_kind = QStringLiteral("steam");
			pipe->setProperties(data);
			QCOMPARE(pipe->lineWidth(), 4.0);
			QVERIFY(pipe->pipeDashes().isEmpty());

				// the panel offers the kinds of the project, and the unknown one
			PipePropertiesWidget page({pipe});
			auto *list = page.findChild<QComboBox *>(QStringLiteral("line_kind"));
			QVERIFY(list);
			QCOMPARE(list->count(), 5);
			QCOMPARE(list->itemData(3).toString(), QStringLiteral("L4"));
			QCOMPARE(list->itemText(3), QStringLiteral("Buried"));
			QCOMPARE(list->currentData().toString(), QStringLiteral("steam"));
			list->setCurrentIndex(3);
			emit list->activated(3);
			QCOMPARE(pipe->properties().m_pipe.line_kind, QStringLiteral("L4"));
			QCOMPARE(list->count(), 4);
		}

		void theFreeNozzleOfAJunctionIsAMainLineOfTheProject()
		{
			Scene scene(QStringLiteral("tee.elmt"));
			QVERIFY(scene.fitting);
				// no pipe: its lines are main lines, 4 wide by default
			QCOMPARE(widthAcrossColumn(scene.render(), 32), 4);
			PidCodification codification = scene.project.pidCodification();
			QVERIFY(codification.setLineKind({PipeProperties::Main, QString(), 2.0,
											  PidCodification::LinePattern::Solid}));
			scene.project.setPidCodification(codification);
				// 2 mm: 8 wide, to N1 as to N3
			const QImage image = scene.render();
			QCOMPARE(widthAcrossColumn(image, 32), 8);
			QCOMPARE(widthAcrossRow(image, 50), 8);
		}

		void thePageSetsTheKindsOfLine()
		{
			QETProject project;
			PidConfigPage page(&project);
			auto *table = page.findChild<QTableWidget *>(QStringLiteral("line_kind_table"));
			QVERIFY(table);
			QCOMPARE(table->rowCount(), 3);
				// the code and name of a kind of ISO cannot be edited
			QVERIFY(!(table->item(0, 0)->flags() & Qt::ItemIsEditable));
				// its width can
			table->item(1, 2)->setText(QStringLiteral("0,7"));
			QCOMPARE(page.findChild<QComboBox *>(QStringLiteral("line_pattern_2"))->currentData().toInt(),
					 int(PidCodification::LinePattern::Dashes));
				// a kind added
			page.addLineKind();
			QCOMPARE(table->rowCount(), 4);
			table->item(3, 0)->setText(QStringLiteral("L4"));
			table->item(3, 1)->setText(QStringLiteral("Buried"));
			auto *pattern = qobject_cast<QComboBox *>(table->cellWidget(3, 3));
			QVERIFY(pattern);
			pattern->setCurrentIndex(pattern->findData(int(PidCodification::LinePattern::DashDot)));
			emit pattern->activated(pattern->currentIndex());
				// a code given twice: in orange, left out
			page.addLineKind();
			table->item(4, 0)->setText(QStringLiteral("L4"));
			QVERIFY(!table->item(4, 0)->toolTip().isEmpty());
			page.applyProjectConf();
			const PidCodification &applied = project.pidCodification();
			QCOMPARE(applied.line_kinds.size(), 4);
			QCOMPARE(applied.lineKind(PipeProperties::Secondary).width, 0.7);
			QCOMPARE(applied.lineKind(QStringLiteral("L4")).pattern, PidCodification::LinePattern::DashDot);
			QCOMPARE(applied.lineKindName(QStringLiteral("L4")), QStringLiteral("Buried"));
				// the page shown again from the project: the line left out gone
			QCOMPARE(table->rowCount(), 4);
				// a kind of ISO is never removed; one added is
			table->clearSelection();
			table->selectRow(0);
			page.removeSelectedLineKinds();
			QCOMPARE(table->rowCount(), 4);
			table->clearSelection();
			table->selectRow(3);
			page.removeSelectedLineKinds();
			QCOMPARE(table->rowCount(), 3);
				// back to ISO
			page.resetLineKinds();
			page.applyProjectConf();
			QCOMPARE(project.pidCodification().line_kinds, PidCodification::isoLineKinds());
			project.undoStack()->undo();
			QCOMPARE(project.pidCodification().lineKind(PipeProperties::Secondary).width, 0.7);
			QCOMPARE(project.pidCodification().lineKindName(QStringLiteral("L4")), QStringLiteral("Buried"));
		}
};

QET_APP_TEST_MAIN(tst_pipejunction)
#include "tst_pipejunction.moc"
