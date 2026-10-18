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
};

QTEST_MAIN(tst_pipeline)
#include "tst_pipeline.moc"
