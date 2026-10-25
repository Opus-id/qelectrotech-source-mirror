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
#include "qetgraphicsitem/pipeline.h"

#include <QGraphicsScene>
#include <QtTest>

/**
	PipeLine -- a pipe line of a P&ID. Checked here: the path between its
	two ends is made of horizontal and vertical segments, joins both ends
	and leaves each of them in its direction; the identity and the
	properties are its own; the area it reports holds what it draws.
*/
class tst_pipeline : public QObject
{
	Q_OBJECT

	static PipeLine::End endAt(const QPointF &point, Qet::Orientation direction)
	{
		PipeLine::End e;
		e.point = point;
		e.direction = direction;
		return e;
	}

	static PipeProperties sampleProperties()
	{
		PipeProperties p;
		p.nominal_diameter = 100;
		p.material = QStringLiteral("PVC");
		p.fluid = QStringLiteral("Raw water");
		p.flow = QStringLiteral("120 m3/h");
		p.line_number = QStringLiteral("47121");
		p.piping_class = QStringLiteral("75HB13");
		p.nominal_pressure = QStringLiteral("PN16");
		p.line_kind = PipeProperties::Secondary;
		return p;
	}

		// The text an element is written as, on one line, without the line
		// break QDom puts after it
	static QString elementText(const QDomElement &element)
	{
		QString text;
		QTextStream stream(&text);
		element.save(stream, 0);
		return text.trimmed();
	}

	static bool orthogonal(const QList<QPointF> &points)
	{
		for (int i = 0; i + 1 < points.size(); ++i) {
			const QPointF a = points.at(i), b = points.at(i + 1);
			if (!qFuzzyCompare(1 + a.x(), 1 + b.x())
				&& !qFuzzyCompare(1 + a.y(), 1 + b.y())) {
				return false;
			}
		}
		return true;
	}

		// How far @p to is from @p from towards @p direction: positive when
		// it lies on the side the direction points to
	static qreal alongDirection(const QPointF &from, const QPointF &to,
								Qet::Orientation direction)
	{
		switch (direction) {
			case Qet::North: return from.y() - to.y();
			case Qet::East:  return to.x() - from.x();
			case Qet::South: return to.y() - from.y();
			case Qet::West:  return from.x() - to.x();
		}
		return 0;
	}

	static void fillCases()
	{
		QTest::addColumn<QPointF>("start");
		QTest::addColumn<int>("startDirection");
		QTest::addColumn<QPointF>("end");
		QTest::addColumn<int>("endDirection");

		QTest::newRow("facing, same height")
			<< QPointF(0, 0) << int(Qet::East) << QPointF(100, 0) << int(Qet::West);
		QTest::newRow("facing, offset")
			<< QPointF(0, 0) << int(Qet::East) << QPointF(100, 50) << int(Qet::West);
		QTest::newRow("facing, too close")
			<< QPointF(0, 0) << int(Qet::East) << QPointF(10, 0) << int(Qet::West);
		QTest::newRow("one corner")
			<< QPointF(0, 0) << int(Qet::East) << QPointF(100, 100) << int(Qet::North);
		QTest::newRow("down then across")
			<< QPointF(0, 0) << int(Qet::South) << QPointF(100, 100) << int(Qet::West);
		QTest::newRow("end behind start")
			<< QPointF(100, 0) << int(Qet::East) << QPointF(0, 50) << int(Qet::West);
		QTest::newRow("same direction")
			<< QPointF(0, 0) << int(Qet::North) << QPointF(50, 80) << int(Qet::North);
		QTest::newRow("vertical, facing")
			<< QPointF(0, 0) << int(Qet::South) << QPointF(0, 120) << int(Qet::North);
	}

private slots:
	void pathIsOrthogonal_data() { fillCases(); }
	void pathIsOrthogonal()
	{
		QFETCH(QPointF, start);
		QFETCH(int, startDirection);
		QFETCH(QPointF, end);
		QFETCH(int, endDirection);

		const PipeLine pipe(endAt(start, Qet::Orientation(startDirection)),
							endAt(end, Qet::Orientation(endDirection)));
		const QList<QPointF> points = pipe.pathPoints();
		QVERIFY(points.size() >= 2);
		QVERIFY2(orthogonal(points), "a segment is neither horizontal nor vertical");
	}

	void pathJoinsBothEnds_data() { fillCases(); }
	void pathJoinsBothEnds()
	{
		QFETCH(QPointF, start);
		QFETCH(int, startDirection);
		QFETCH(QPointF, end);
		QFETCH(int, endDirection);

		const PipeLine pipe(endAt(start, Qet::Orientation(startDirection)),
							endAt(end, Qet::Orientation(endDirection)));
		QCOMPARE(pipe.pathPoints().first(), start);
		QCOMPARE(pipe.pathPoints().last(), end);
	}

	void pathLeavesEachEndInItsDirection_data() { fillCases(); }
	void pathLeavesEachEndInItsDirection()
	{
		QFETCH(QPointF, start);
		QFETCH(int, startDirection);
		QFETCH(QPointF, end);
		QFETCH(int, endDirection);

		const PipeLine pipe(endAt(start, Qet::Orientation(startDirection)),
							endAt(end, Qet::Orientation(endDirection)));
		const QList<QPointF> points = pipe.pathPoints();
		QVERIFY(points.size() >= 2);
		QVERIFY2(alongDirection(points.at(0), points.at(1), Qet::Orientation(startDirection)) > 0,
				 "the path does not leave the start in its direction");
		const int n = points.size();
		QVERIFY2(alongDirection(points.at(n - 1), points.at(n - 2), Qet::Orientation(endDirection)) > 0,
				 "the path does not leave the end in its direction");
	}

	void movingTheEndsDrawsThePathAgain()
	{
		PipeLine pipe(endAt(QPointF(0, 0), Qet::East),
					  endAt(QPointF(100, 0), Qet::West));
		const QList<QPointF> before = pipe.pathPoints();

		pipe.setEnds(endAt(QPointF(0, 0), Qet::East),
					 endAt(QPointF(200, 80), Qet::West));
		QVERIFY(pipe.pathPoints() != before);
		QCOMPARE(pipe.pathPoints().first(), QPointF(0, 0));
		QCOMPARE(pipe.pathPoints().last(), QPointF(200, 80));
		QVERIFY(orthogonal(pipe.pathPoints()));
		QCOMPARE(pipe.endEnd().point, QPointF(200, 80));
	}

	void endsOnTheSamePointGiveAPath()
	{
		const QPointF p(50, 50);
		const PipeLine pipe(endAt(p, Qet::East), endAt(p, Qet::West));
		QVERIFY(pipe.pathPoints().size() >= 2);
		QCOMPARE(pipe.pathPoints().first(), p);
		QCOMPARE(pipe.pathPoints().last(), p);
	}

	void everyPipeLineHasItsOwnIdentity()
	{
		PipeLine a(endAt(QPointF(0, 0), Qet::East), endAt(QPointF(100, 0), Qet::West));
		PipeLine b(endAt(QPointF(0, 0), Qet::East), endAt(QPointF(100, 0), Qet::West));
		QVERIFY(!a.uuid().isNull());
		QVERIFY(a.uuid() != b.uuid());
	}

	void identityIsKept()
	{
		PipeLine pipe(endAt(QPointF(0, 0), Qet::East), endAt(QPointF(100, 0), Qet::West));
		const QUuid chosen = QUuid::createUuid();
		pipe.setUuid(chosen);
		QCOMPARE(pipe.uuid(), chosen);

			// Moving it and changing its data do not give it a new identity
		pipe.setEnds(endAt(QPointF(0, 0), Qet::East), endAt(QPointF(50, 50), Qet::West));
		PipeProperties properties;
		properties.nominal_diameter = 100;
		pipe.setProperties(properties);
		QCOMPARE(pipe.uuid(), chosen);
	}

	void propertiesBelongToThePipeLine()
	{
		PipeLine pipe(endAt(QPointF(0, 0), Qet::East), endAt(QPointF(100, 0), Qet::West));
		QCOMPARE(pipe.properties(), PipeProperties());

		QSignalSpy spy(&pipe, &PipeLine::propertiesChange);
		PipeProperties properties;
		properties.nominal_diameter = 150;
		properties.fluid = QStringLiteral("Sludge");
		pipe.setProperties(properties);
		QCOMPARE(pipe.properties(), properties);
		QCOMPARE(spy.count(), 1);

			// The same properties again change nothing and say nothing
		pipe.setProperties(properties);
		QCOMPARE(spy.count(), 1);
	}

	void pipeLinesDoNotShareProperties()
	{
		PipeLine a(endAt(QPointF(0, 0), Qet::East), endAt(QPointF(100, 0), Qet::West));
		PipeLine b(endAt(QPointF(100, 0), Qet::East), endAt(QPointF(200, 0), Qet::West));
		PipeProperties properties;
		properties.nominal_diameter = 100;
		a.setProperties(properties);
		QCOMPARE(b.properties(), PipeProperties());
	}

	void boundingRectHoldsThePath_data() { fillCases(); }
	void boundingRectHoldsThePath()
	{
		QFETCH(QPointF, start);
		QFETCH(int, startDirection);
		QFETCH(QPointF, end);
		QFETCH(int, endDirection);

		const PipeLine pipe(endAt(start, Qet::Orientation(startDirection)),
							endAt(end, Qet::Orientation(endDirection)));
		for (const QPointF &p : pipe.pathPoints()) {
			QVERIFY(pipe.boundingRect().contains(p));
		}
	}

	void clickAreaCoversThePath()
	{
		const PipeLine pipe(endAt(QPointF(0, 0), Qet::East),
							endAt(QPointF(100, 50), Qet::West));
		const QList<QPointF> points = pipe.pathPoints();
		for (int i = 0; i < points.size(); ++i) {
			QVERIFY(pipe.shape().contains(points.at(i)));
			if (i + 1 < points.size()) {
				QVERIFY(pipe.shape().contains((points.at(i) + points.at(i + 1)) / 2));
			}
		}
			// and not a point far away from it
		QVERIFY(!pipe.shape().contains(QPointF(500, 500)));
	}

	void itIsAGraphicsItemOfItsOwnKind()
	{
		PipeLine pipe(endAt(QPointF(0, 0), Qet::East), endAt(QPointF(100, 0), Qet::West));
		QGraphicsScene scene;
		scene.addItem(&pipe);

		QGraphicsItem *item = &pipe;
		QCOMPARE(item->type(), int(PipeLine::Type));
		QVERIFY(qgraphicsitem_cast<PipeLine *>(item) == &pipe);
		QVERIFY(item->flags() & QGraphicsItem::ItemIsSelectable);
		scene.removeItem(&pipe);
	}

		// The code that tests for conductors, terminals and the others must
		// never take a pipe line for one of them: its type is none of the
		// numbers QET gives its items (UserType + n), as found in the
		// sources when this class was written, and it is in the block kept
		// for the P&ID items.
	void typeIsKeptForPipeLines()
	{
		const QList<int> used = {
			1, 2, 3,
			1000, 1001, 1002, 1004, 1005, 1006, 1007, 1008, 1009, 1010, 1011, 1012,
			1101, 1103, 1104, 1105, 1106, 1107, 1109, 1110, 1120,
			1200, 1300, 1301, 2200
		};
		const int offset = int(PipeLine::Type) - int(QGraphicsItem::UserType);
		QVERIFY2(!used.contains(offset), "the type of PipeLine is used by another item");
		QVERIFY2(offset >= 4000 && offset < 4100, "PipeLine is outside the P&ID block");
	}

		// ISO 10628-1 5.3: the main flow lines are drawn twice as wide as the
		// others (1 mm and 0,5 mm); the auxiliary system lines are dashed.
	void mainLinesAreTheWidestAndAuxiliaryOnesDashed()
	{
		const QPen main = PipeLine::penFor(PipeProperties::Main);
		const QPen secondary = PipeLine::penFor(PipeProperties::Secondary);
		const QPen auxiliary = PipeLine::penFor(PipeProperties::Auxiliary);

		QCOMPARE(main.widthF(), 2 * secondary.widthF());
		QCOMPARE(auxiliary.widthF(), secondary.widthF());
		QCOMPARE(int(main.style()), int(Qt::SolidLine));
		QCOMPARE(int(secondary.style()), int(Qt::SolidLine));
		QCOMPARE(int(auxiliary.style()), int(Qt::DashLine));
	}

	void theKindOfLineChangesTheAreaItTakes()
	{
		PipeLine pipe(endAt(QPointF(0, 0), Qet::East), endAt(QPointF(100, 0), Qet::West));
		PipeProperties properties;
		properties.line_kind = PipeProperties::Secondary;
		pipe.setProperties(properties);
		const qreal thin = pipe.boundingRect().height();

		properties.line_kind = PipeProperties::Main;
		pipe.setProperties(properties);
		QVERIFY(pipe.boundingRect().height() > thin);

			// and it holds the whole width of what is drawn
		const qreal half = PipeLine::penFor(PipeProperties::Main).widthF() / 2;
		for (const QPointF &p : pipe.pathPoints()) {
			QVERIFY(pipe.boundingRect().contains(p + QPointF(half, half)));
			QVERIFY(pipe.boundingRect().contains(p - QPointF(half, half)));
		}
	}

		// A click on the edge of a wide line still selects it
	void clickAreaCoversTheWholeWidthOfEachKind()
	{
		const QList<PipeProperties::LineKind> kinds = {
			PipeProperties::Main, PipeProperties::Secondary, PipeProperties::Auxiliary};
		for (PipeProperties::LineKind kind : kinds) {
			PipeLine pipe(endAt(QPointF(0, 0), Qet::East), endAt(QPointF(100, 0), Qet::West));
			PipeProperties properties;
			properties.line_kind = kind;
			pipe.setProperties(properties);

			const qreal half = PipeLine::penFor(kind).widthF() / 2;
			QVERIFY(pipe.shape().contains(QPointF(50, half - 0.1)));
			QVERIFY(pipe.shape().contains(QPointF(50, -(half - 0.1))));
		}
	}

	void everyKindOfLineComesBackThroughThePipeLine()
	{
		const QList<PipeProperties::LineKind> kinds = {
			PipeProperties::Main, PipeProperties::Secondary, PipeProperties::Auxiliary};
		for (PipeProperties::LineKind kind : kinds) {
			PipeLine saved(endAt(QPointF(0, 0), Qet::East), endAt(QPointF(100, 0), Qet::West));
			PipeProperties properties;
			properties.line_kind = kind;
			saved.setProperties(properties);

			QDomDocument doc;
			const QDomElement element = saved.toXml(doc);
			PipeLine loaded(endAt(QPointF(5, 5), Qet::North), endAt(QPointF(7, 7), Qet::North));
			QVERIFY(loaded.fromXml(element));
			QCOMPARE(int(loaded.properties().line_kind), int(kind));
		}
	}

	void savedPipeLineComesBack()
	{
		PipeLine saved(endAt(QPointF(10, 20), Qet::East),
					   endAt(QPointF(110.5, 80), Qet::North));
		saved.setProperties(sampleProperties());

			// Through a real text, not only the DOM tree
		QDomDocument doc;
		doc.appendChild(saved.toXml(doc));
		QDomDocument reread;
		QVERIFY(reread.setContent(doc.toString()));

		PipeLine loaded(endAt(QPointF(), Qet::South), endAt(QPointF(1, 1), Qet::South));
		QVERIFY(loaded.fromXml(reread.documentElement()));
		QCOMPARE(loaded.uuid(), saved.uuid());
		QCOMPARE(loaded.properties(), saved.properties());
		QCOMPARE(loaded.startEnd().point, saved.startEnd().point);
		QCOMPARE(int(loaded.startEnd().direction), int(saved.startEnd().direction));
		QCOMPARE(loaded.endEnd().point, saved.endEnd().point);
		QCOMPARE(int(loaded.endEnd().direction), int(saved.endEnd().direction));
			// The path is not in the file: it is worked out again, the same
		QCOMPARE(loaded.pathPoints(), saved.pathPoints());
	}

	void allDirectionsComeBack()
	{
		const QList<Qet::Orientation> all = {Qet::North, Qet::East, Qet::South, Qet::West};
		for (Qet::Orientation s : all) {
			for (Qet::Orientation e : all) {
				PipeLine saved(endAt(QPointF(0, 0), s), endAt(QPointF(100, 60), e));
				QDomDocument doc;
				const QDomElement element = saved.toXml(doc);
				PipeLine loaded(endAt(QPointF(5, 5), Qet::North), endAt(QPointF(7, 7), Qet::North));
				QVERIFY(loaded.fromXml(element));
				QCOMPARE(int(loaded.startEnd().direction), int(s));
				QCOMPARE(int(loaded.endEnd().direction), int(e));
			}
		}
	}

		// What is written depends on the data and nothing else, so that two
		// saves of the same project are the same text and a file kept under
		// git only shows what changed.
	void outputIsStable()
	{
		const QUuid fixed(QStringLiteral("{11111111-2222-3333-4444-555555555555}"));
		PipeLine a(endAt(QPointF(10, 20), Qet::East), endAt(QPointF(110, 80), Qet::West));
		PipeLine b(endAt(QPointF(10, 20), Qet::East), endAt(QPointF(110, 80), Qet::West));
		for (PipeLine *pipe : {&a, &b}) {
			pipe->setUuid(fixed);
			pipe->setProperties(sampleProperties());
		}

		QDomDocument da, db;
		const QString text_a = elementText(a.toXml(da));
		QCOMPARE(elementText(b.toXml(db)), text_a);
			// and the same again from the same object
		QDomDocument dc;
		QCOMPARE(elementText(a.toXml(dc)), text_a);

		QCOMPARE(text_a, QStringLiteral(
			"<pipe dn=\"100\" end_direction=\"w\" end_x=\"110\" end_y=\"80\" "
			"flow=\"120 m3/h\" fluid=\"Raw water\" line_kind=\"secondary\" "
			"line_number=\"47121\" "
			"material=\"PVC\" nominal_pressure=\"PN16\" piping_class=\"75HB13\" "
			"start_direction=\"e\" start_x=\"10\" start_y=\"20\" "
			"uuid=\"{11111111-2222-3333-4444-555555555555}\"/>"));
	}

	void minimalElementIsRead()
	{
		QDomDocument doc;
		QVERIFY(doc.setContent(QStringLiteral(
			"<pipe start_x=\"0\" start_y=\"0\" start_direction=\"e\" "
			"end_x=\"100\" end_y=\"0\" end_direction=\"w\"/>")));
		PipeLine pipe(endAt(QPointF(5, 5), Qet::North), endAt(QPointF(7, 7), Qet::North));
		QVERIFY(PipeLine::valideXml(doc.documentElement()));
		QVERIFY(pipe.fromXml(doc.documentElement()));
		QCOMPARE(pipe.startEnd().point, QPointF(0, 0));
		QCOMPARE(int(pipe.startEnd().direction), int(Qet::East));
		QCOMPARE(pipe.endEnd().point, QPointF(100, 0));
		QCOMPARE(int(pipe.endEnd().direction), int(Qet::West));
		QCOMPARE(pipe.properties(), PipeProperties());
	}

		// A file with no identifier gets one at run time, and it is not
		// written back: every save of an unchanged file would otherwise
		// differ by a new random number.
	void missingOrMalformedIdentifierIsNotWrittenBack_data()
	{
		QTest::addColumn<QString>("uuidAttribute");
		QTest::newRow("absent")    << "";
		QTest::newRow("empty")     << "uuid=\"\"";
		QTest::newRow("malformed") << "uuid=\"not-a-uuid\"";
	}

	void missingOrMalformedIdentifierIsNotWrittenBack()
	{
		QFETCH(QString, uuidAttribute);
		QDomDocument doc;
		QVERIFY(doc.setContent(QStringLiteral(
			"<pipe %1 start_x=\"0\" start_y=\"0\" start_direction=\"e\" "
			"end_x=\"100\" end_y=\"0\" end_direction=\"w\"/>").arg(uuidAttribute)));
		PipeLine pipe(endAt(QPointF(5, 5), Qet::North), endAt(QPointF(7, 7), Qet::North));
		QVERIFY(pipe.fromXml(doc.documentElement()));
		QVERIFY(!pipe.uuid().isNull());

		QDomDocument out;
		QVERIFY(!pipe.toXml(out).hasAttribute(QStringLiteral("uuid")));
	}

	void unknownAttributesAreIgnored()
	{
		QDomDocument doc;
		QVERIFY(doc.setContent(QStringLiteral(
			"<pipe start_x=\"0\" start_y=\"0\" start_direction=\"e\" "
			"end_x=\"100\" end_y=\"0\" end_direction=\"w\" later=\"1\"/>")));
		PipeLine pipe(endAt(QPointF(5, 5), Qet::North), endAt(QPointF(7, 7), Qet::North));
		QVERIFY(pipe.fromXml(doc.documentElement()));
		QCOMPARE(pipe.endEnd().point, QPointF(100, 0));
	}

	void unreadableElementIsRefusedAndChangesNothing_data()
	{
		QTest::addColumn<QString>("xml");
		const QString ends =
			QStringLiteral("start_x=\"0\" start_y=\"0\" start_direction=\"e\" "
						   "end_x=\"100\" end_y=\"0\" end_direction=\"w\"");
		QTest::newRow("another element")
			<< QStringLiteral("<conductor %1/>").arg(ends);
		QTest::newRow("no attribute")
			<< QStringLiteral("<pipe/>");
		QTest::newRow("no end")
			<< QStringLiteral("<pipe start_x=\"0\" start_y=\"0\" start_direction=\"e\"/>");
		QTest::newRow("text coordinate")
			<< QStringLiteral("<pipe start_x=\"left\" start_y=\"0\" start_direction=\"e\" "
							  "end_x=\"100\" end_y=\"0\" end_direction=\"w\"/>");
		QTest::newRow("nan")
			<< QStringLiteral("<pipe start_x=\"nan\" start_y=\"0\" start_direction=\"e\" "
							  "end_x=\"100\" end_y=\"0\" end_direction=\"w\"/>");
		QTest::newRow("inf")
			<< QStringLiteral("<pipe start_x=\"0\" start_y=\"0\" start_direction=\"e\" "
							  "end_x=\"100\" end_y=\"inf\" end_direction=\"w\"/>");
		QTest::newRow("direction not a letter")
			<< QStringLiteral("<pipe start_x=\"0\" start_y=\"0\" start_direction=\"east\" "
							  "end_x=\"100\" end_y=\"0\" end_direction=\"w\"/>");
		QTest::newRow("empty direction")
			<< QStringLiteral("<pipe start_x=\"0\" start_y=\"0\" start_direction=\"\" "
							  "end_x=\"100\" end_y=\"0\" end_direction=\"w\"/>");
		QTest::newRow("no direction")
			<< QStringLiteral("<pipe start_x=\"0\" start_y=\"0\" "
							  "end_x=\"100\" end_y=\"0\" end_direction=\"w\"/>");
	}

	void unreadableElementIsRefusedAndChangesNothing()
	{
		QFETCH(QString, xml);
		QDomDocument doc;
		QVERIFY(doc.setContent(xml));
		PipeLine pipe(endAt(QPointF(5, 5), Qet::North), endAt(QPointF(7, 7), Qet::South));
		pipe.setProperties(sampleProperties());
		const QUuid uuid = pipe.uuid();
		const QList<QPointF> path = pipe.pathPoints();

		QVERIFY(!PipeLine::valideXml(doc.documentElement()));
		QVERIFY(!pipe.fromXml(doc.documentElement()));
		QCOMPARE(pipe.uuid(), uuid);
		QCOMPARE(pipe.properties(), sampleProperties());
		QCOMPARE(pipe.startEnd().point, QPointF(5, 5));
		QCOMPARE(pipe.endEnd().point, QPointF(7, 7));
		QCOMPARE(pipe.pathPoints(), path);
	}
};

QTEST_MAIN(tst_pipeline)
#include "tst_pipeline.moc"
