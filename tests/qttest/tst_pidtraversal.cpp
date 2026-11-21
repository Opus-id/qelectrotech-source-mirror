// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "conductorsegment.h"
#include "diagram.h"
#include "diagramcontext.h"
#include "factory/elementfactory.h"
#include "pidtraversal.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QImage>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

// A pipe or a signal line which runs over a symbol it is not docked to there
// (step 7c-bis): the part over the symbol is drawn orange on the screen, and
// hovering the line names the symbol. What counts is all the symbol draws:
// the inside of its closed shapes and its lines alone. A stretch touching an
// end of the line never counts: the nozzles it leaves and reaches, a basin
// it ends in. A wire is drawn and said as before.
class tst_pidtraversal : public QObject
{
	Q_OBJECT

		// A symbol drawing @p drawing, named @p name, with two terminals N1
		// (west, at -40) and N2 (east, at 40) of the medium @p medium when
		// @p terminals
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &name,
								  const QString &drawing, const QString &medium,
								  bool terminals = true)
	{
		const QString attr = medium.isEmpty()
				? QString()
				: QStringLiteral(" medium=\"%1\"").arg(medium);
		const QString nozzles = terminals
				? QStringLiteral("<terminal name=\"N1\" x=\"-40\" y=\"0\" orientation=\"w\" type=\"Generic\"%1/>"
								 "<terminal name=\"N2\" x=\"40\" y=\"0\" orientation=\"e\" type=\"Generic\"%1/>").arg(attr)
				: QString();
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"120\" height=\"120\" hotspot_x=\"60\" hotspot_y=\"60\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">%2</name></names>"
			"<description>%3%4</description></definition>").arg(uuid, name, drawing, nozzles));
		return doc.documentElement();
	}

	static QString style()
	{
		return QStringLiteral("style=\"line-style:normal;line-weight:normal;filling:none;color:black\" "
							  "antialias=\"false\"");
	}
		// A pump: a circle of 60 and two stubs out to its nozzles at 40
	static QString pumpDrawing()
	{
		return QStringLiteral("<ellipse x=\"-30\" y=\"-30\" width=\"60\" height=\"60\" %1/>"
							  "<line x1=\"-40\" y1=\"0\" x2=\"-30\" y2=\"0\" end1=\"none\" end2=\"none\" "
							  "length1=\"1.5\" length2=\"1.5\" %1/>"
							  "<line x1=\"30\" y1=\"0\" x2=\"40\" y2=\"0\" end1=\"none\" end2=\"none\" "
							  "length1=\"1.5\" length2=\"1.5\" %1/>").arg(style());
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_pump, m_signals, m_electrical, m_circle, m_stem, m_basin;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/ptrav"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/ptrav/") + file, &m_project);
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

		// A straight line, from x 40 to x 260, between two symbols of a row
		// of their own, the first at x 0
	Conductor *draw(const ElementsLocation &location, Element **from = nullptr,
					Element **to = nullptr)
	{
		m_next_y += 300;
		Element *a = place(location, QPointF(0, m_next_y));
		Element *b = place(location, QPointF(300, m_next_y));
		if (!a || !b) return nullptr;
		if (from) *from = a;
		if (to) *to = b;
		auto *conductor = new Conductor(a->terminals().at(1), b->terminals().at(0));
		m_folio->addItem(conductor);
		return conductor;
	}

		// The points of @p conductor, on the folio
	static QList<QPointF> points(const Conductor *conductor)
	{
		QList<QPointF> found;
		for (ConductorSegment *segment : conductor->segmentsList()) {
			if (found.isEmpty()) {
				found << conductor->mapToScene(segment->firstPoint());
			}
			found << conductor->mapToScene(segment->secondPoint());
		}
		return found;
	}

		// How many pixels of @p conductor are drawn orange
	static int orangePixels(Conductor *conductor)
	{
		const QRectF area = conductor->sceneBoundingRect().adjusted(-4, -4, 4, 4);
		QImage image(area.size().toSize(), QImage::Format_RGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		painter.translate(-area.topLeft() + conductor->scenePos());
		QStyleOptionGraphicsItem option;
		conductor->paint(&painter, &option, nullptr);
		painter.end();
		int count = 0;
		for (int y = 0 ; y < image.height() ; ++y) {
			for (int x = 0 ; x < image.width() ; ++x) {
				const QColor c(image.pixel(x, y));
				if (c.red() > 200 && c.green() > 90 && c.green() < 190 && c.blue() < 90) ++count;
			}
		}
		return count;
	}

	static QVector<QPointF> polyline(std::initializer_list<QPointF> list)
	{
		return QVector<QPointF>(list);
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("ptrav"), NamesList());
			QDomDocument a, b, c, d, e, f;
			m_pump = add("pump.elmt", definition(a, "b1111111-1111-1111-1111-111111111111",
												 "Pump", pumpDrawing(), "pipe"));
			m_signals = add("signals.elmt", definition(b, "b2222222-2222-2222-2222-222222222222",
													   "Bubble", pumpDrawing(), "signal"));
			m_electrical = add("electrical.elmt", definition(c, "b3333333-3333-3333-3333-333333333333",
															 "Motor", pumpDrawing(), ""));
				// A circle of 40 about its hotspot, nothing to dock to
			m_circle = add("circle.elmt", definition(d, "b4444444-4444-4444-4444-444444444444",
				"Tank", QStringLiteral("<circle x=\"-20\" y=\"-20\" diameter=\"40\" %1/>").arg(style()),
				QString(), false));
				// A line alone, under its hotspot: x 0, from y 10 to y 40
			m_stem = add("stem.elmt", definition(e, "b5555555-5555-5555-5555-555555555555",
				"Stem", QStringLiteral("<line x1=\"0\" y1=\"10\" x2=\"0\" y2=\"40\" end1=\"none\" "
									   "end2=\"none\" length1=\"1.5\" length2=\"1.5\" %1/>").arg(style()),
				QString(), false));
				// A closed basin, 100 wide about its hotspot
			m_basin = add("basin.elmt", definition(f, "b6666666-6666-6666-6666-666666666666",
				"Basin", QStringLiteral("<rect x=\"-50\" y=\"-50\" width=\"100\" height=\"100\" %1/>").arg(style()),
				QString(), false));
			QVERIFY(m_pump.exist() && m_signals.exist() && m_electrical.exist());
			QVERIFY(m_circle.exist() && m_stem.exist() && m_basin.exist());
			m_folio = m_project.addNewDiagram();
		}

			// ---- the geometry ----
		void aLineThroughAClosedShapeIsTheStretchInsideIt()
		{
			PidTraversal::Outline square;
			const QPolygonF edge(QRectF(40, -10, 20, 20));
			square.areas << edge;
			square.strokes << edge;
			square.rect = edge.boundingRect();

			const QList<PidTraversal::Span> through =
					PidTraversal::traversals(polyline({{0, 0}, {100, 0}}), square);
			QCOMPARE(through.size(), 1);
			QCOMPARE(through.first().from, 40.0);
			QCOMPARE(through.first().to, 60.0);

				// it ends inside, or begins on its edge: never
			QVERIFY(PidTraversal::traversals(polyline({{0, 0}, {50, 0}}), square).isEmpty());
			QVERIFY(PidTraversal::traversals(polyline({{60, 0}, {100, 0}}), square).isEmpty());
				// it goes by
			QVERIFY(PidTraversal::traversals(polyline({{0, 20}, {100, 20}}), square).isEmpty());
				// along its edge
			const QList<PidTraversal::Span> along =
					PidTraversal::traversals(polyline({{0, 10}, {100, 10}}), square);
			QCOMPARE(along.size(), 1);
			QCOMPARE(along.first().from, 40.0);
			QCOMPARE(along.first().to, 60.0);
				// through it on a second segment, the distance counted along
				// the line
			const QList<PidTraversal::Span> bent =
					PidTraversal::traversals(polyline({{0, -50}, {0, 0}, {100, 0}}), square);
			QCOMPARE(bent.size(), 1);
			QCOMPARE(bent.first().from, 90.0);
		}

		void aLineAcrossALineAloneIsAPointDrawnLonger()
		{
			PidTraversal::Outline stem;
			stem.strokes << QPolygonF({QPointF(50, -10), QPointF(50, 10)});
			stem.rect = stem.strokes.first().boundingRect();
			const QVector<QPointF> line = polyline({{0, 0}, {100, 0}});
			const QList<PidTraversal::Span> across = PidTraversal::traversals(line, stem);
			QCOMPARE(across.size(), 1);
			QCOMPARE(across.first().from, 50.0);
			QCOMPARE(across.first().to, 50.0);
			const QRectF drawn = PidTraversal::pathAlong(line, across, 6).boundingRect();
			QCOMPARE(drawn.left(), 47.0);
			QCOMPARE(drawn.right(), 53.0);
				// what touches the line's end is its nozzle
			QVERIFY(PidTraversal::traversals(polyline({{0, 0}, {50, 0}}), stem).isEmpty());
		}

		void theOutlineFollowsTheSymbolTurnedAndMirrored()
		{
			ElementPictureFactory::primitives drawing;
			drawing.m_lines << QLineF(0, 10, 0, 40);
			const QTransform turned = QTransform().translate(100, 0).rotate(90);
			const PidTraversal::Outline outline = PidTraversal::outline(drawing, turned);
			QCOMPARE(outline.strokes.size(), 1);
			QCOMPARE(outline.areas.size(), 0);
			QVERIFY(qAbs(outline.rect.left() - 60) < 1e-6);
			QVERIFY(qAbs(outline.rect.right() - 90) < 1e-6);

			ElementPictureFactory::primitives shapes;
			shapes.m_circles << QRectF(-20, -20, 40, 40);
			shapes.m_polygons << QVector<QPointF>({{0, 0}, {10, 0}, {10, 10}, {0, 0}});
			shapes.m_polygons << QVector<QPointF>({{0, 0}, {10, 0}, {10, 10}});
			shapes.m_arcs << QVector<qreal>({-10, -10, 20, 20, 0, 360});
			shapes.m_arcs << QVector<qreal>({-10, -10, 20, 20, 0, 90});
			const PidTraversal::Outline all = PidTraversal::outline(shapes, QTransform());
				// circle, closed polygon and whole ellipse are areas; the open
				// polygon and the arc are lines alone
			QCOMPARE(all.areas.size(), 3);
			QCOMPARE(all.strokes.size(), 5);
		}

			// ---- on a folio ----
		void aPipeIsNotSaidToGoThroughThePumpsItJoins()
		{
			Conductor *pipe = draw(m_pump);
			QVERIFY(pipe);
			QCOMPARE(pipe->properties().m_medium, TerminalData::Pipe);
			const QList<QPointF> at = points(pipe);
			QCOMPARE(at.first(), QPointF(40, m_next_y));
			QCOMPARE(at.last(), QPointF(260, m_next_y));
			QVERIFY(pipe->traversedSymbols().isEmpty());
			QVERIFY(pipe->traversalNote().isEmpty());
			QCOMPARE(orangePixels(pipe), 0);
		}

		void aPipeThroughASymbolIsOrangeThereAndNamesIt()
		{
			Conductor *pipe = draw(m_pump);
			Element *tank = place(m_circle, QPointF(150, m_next_y));
			QVERIFY(pipe && tank);
			QCOMPARE(pipe->traversedSymbols(), QList<Element *>({tank}));
			QCOMPARE(pipe->traversalNote(), QStringLiteral("This pipe goes through the symbol Tank."));
			const int orange = orangePixels(pipe);
			QVERIFY(orange > 0);

			DiagramContext informations = tank->elementInformations();
			informations.addValue(QStringLiteral("label"), QStringLiteral("T01"));
			tank->setElementInformations(informations);
			QCOMPARE(pipe->traversalNote(),
					 QStringLiteral("This pipe goes through the symbol T01 (Tank)."));

				// never printed nor exported
			m_folio->setDrawTerminals(false);
			QCOMPARE(orangePixels(pipe), 0);
			m_folio->setDrawTerminals(true);

				// moved off the line: worked out again, nothing more
			tank->setPos(150, m_next_y + 100);
			QVERIFY(pipe->traversedSymbols().isEmpty());
			QCOMPARE(orangePixels(pipe), 0);
				// back on it, then hidden
			tank->setPos(150, m_next_y);
			QCOMPARE(pipe->traversedSymbols().size(), 1);
			tank->setVisible(false);
			QVERIFY(pipe->traversedSymbols().isEmpty());
			tank->setVisible(true);
			QCOMPARE(pipe->traversedSymbols().size(), 1);
				// gone
			m_folio->removeItem(tank);
			QVERIFY(pipe->traversedSymbols().isEmpty());
			delete tank;
		}

		void aLineAloneCountsAsTheSymbolTurns()
		{
			Conductor *pipe = draw(m_pump);
			Element *stem = place(m_stem, QPointF(150, m_next_y));
			QVERIFY(pipe && stem);
				// under the line: nothing
			QVERIFY(pipe->traversedSymbols().isEmpty());
				// turned a quarter, it lies along the line from x 110 to 140
			stem->setRotation(90);
			QCOMPARE(pipe->traversedSymbols(), QList<Element *>({stem}));
				// mirrored back the other way, from x 160 to 190
			stem->setRotation(0);
			stem->setVerticalMirror(true);
			QVERIFY(pipe->traversedSymbols().isEmpty());
			stem->setRotation(90);
			QCOMPARE(pipe->traversedSymbols(), QList<Element *>({stem}));
		}

		void aPipeEndingInABasinDoesNotGoThroughIt()
		{
			Element *pump = nullptr;
			Conductor *pipe = draw(m_pump, nullptr, &pump);
				// the basin about the second pump: the pipe goes in over its
				// edge and ends on the pump in it
			Element *basin = place(m_basin, pump->pos() + QPointF(20, 0));
			QVERIFY(pipe && basin);
			QVERIFY(pipe->traversedSymbols().isEmpty());
				// in the middle of the line, it goes in and out
			basin->setPos(150, m_next_y);
			QCOMPARE(pipe->traversedSymbols(), QList<Element *>({basin}));
		}

		void aSignalLineIsSaidToo()
		{
			Conductor *line = draw(m_signals);
			Element *tank = place(m_circle, QPointF(150, m_next_y));
			QVERIFY(line && tank);
			QCOMPARE(line->properties().m_medium, TerminalData::Signal);
			QCOMPARE(line->traversalNote(),
					 QStringLiteral("This signal line goes through the symbol Tank."));
			QVERIFY(orangePixels(line) > 0);
		}

		void aWireIsAsBefore()
		{
			Conductor *wire = draw(m_electrical);
			Element *tank = place(m_circle, QPointF(150, m_next_y));
			QVERIFY(wire && tank);
			QCOMPARE(wire->properties().m_medium, TerminalData::Electrical);
			QVERIFY(wire->traversedSymbols().isEmpty());
			QVERIFY(wire->traversalNote().isEmpty());
			QCOMPARE(orangePixels(wire), 0);
		}
};

QET_APP_TEST_MAIN(tst_pidtraversal)
#include "tst_pidtraversal.moc"
