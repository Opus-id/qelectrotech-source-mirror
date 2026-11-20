// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include "qettesttimeout.h"

#include "wirehops.h"

// Hops where two wires cross (issue #436). The geometry is tested on its
// own; the project setting is tested through the real binary: --resave
// must keep a <wire_crossings> element, and must not add one to a project
// that never had it, so existing projects save exactly as before.
class tst_wirehops : public QObject
{
	Q_OBJECT

	using Mode = WireHops::Mode;
	QTemporaryDir m_dir;
	int m_run = 0;

	static QVector<QPointF> line(QPointF a, QPointF b) { return {a, b}; }

		// Runs --resave on @p in, returns the saved file's text (empty on failure)
	QString resave(const QString &in)
	{
		const QString home = m_dir.filePath(QStringLiteral("home%1").arg(m_run));
		const QString tmp = m_dir.filePath(QStringLiteral("tmp%1").arg(m_run));
		const QString out = m_dir.filePath(QStringLiteral("out%1.qet").arg(m_run++));
		QDir().mkpath(home);
		QDir().mkpath(tmp);
		QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		env.insert(QStringLiteral("HOME"), home);
		env.insert(QStringLiteral("XDG_CONFIG_HOME"), home + QStringLiteral("/.config"));
		env.insert(QStringLiteral("XDG_DATA_HOME"), home + QStringLiteral("/.local/share"));
		env.insert(QStringLiteral("TMPDIR"), tmp);

		QProcess proc;
		proc.setProcessEnvironment(env);
		proc.start(QStringLiteral(QET_TEST_BINARY_PATH), {QStringLiteral("--resave"), in, out});
		if (!proc.waitForFinished(QetTest::timeout(120000)) || proc.exitCode() != 0)
			return {};
		QFile file(out);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return {};
		return QString::fromUtf8(file.readAll());
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
	}

	void modeNames()
	{
		QCOMPARE(int(WireHops::fromString(WireHops::toString(Mode::Horizontal))), int(Mode::Horizontal));
		QCOMPARE(int(WireHops::fromString(WireHops::toString(Mode::Vertical))), int(Mode::Vertical));
		QCOMPARE(int(WireHops::fromString(WireHops::toString(Mode::None))), int(Mode::None));
		QCOMPARE(int(WireHops::fromString(QStringLiteral("diagonal"))), int(Mode::None));
		QCOMPARE(int(WireHops::fromString(QString())), int(Mode::None));
	}

	void crossing_data()
	{
		QTest::addColumn<QVector<QPointF>>("wire");
		QTest::addColumn<QVector<QPointF>>("other");
		QTest::addColumn<int>("mode");
		QTest::addColumn<QList<QPointF>>("expected");

		const auto horizontal = line({0, 0}, {100, 0});
		const auto vertical = line({50, -20}, {50, 20});
		const int h = int(Mode::Horizontal), v = int(Mode::Vertical), none = int(Mode::None);

		QTest::newRow("off") << horizontal << vertical << none << QList<QPointF>();
		QTest::newRow("horizontal hops") << horizontal << vertical << h << QList<QPointF>{{50, 0}};
		QTest::newRow("horizontal mode, vertical wire") << vertical << horizontal << h << QList<QPointF>();
		QTest::newRow("vertical hops") << vertical << horizontal << v << QList<QPointF>{{50, 0}};
		QTest::newRow("vertical mode, horizontal wire") << horizontal << vertical << v << QList<QPointF>();
		QTest::newRow("reversed wire") << line({100, 0}, {0, 0}) << vertical << h << QList<QPointF>{{50, 0}};
		QTest::newRow("T: other ends on the wire") << horizontal << line({50, 0}, {50, 20}) << h << QList<QPointF>();
		QTest::newRow("T: other starts on the wire") << horizontal << line({50, -20}, {50, 0}) << h << QList<QPointF>();
		QTest::newRow("too near the wire's end") << horizontal << line({2, -20}, {2, 20}) << h << QList<QPointF>();
		QTest::newRow("parallel") << horizontal << line({0, 10}, {100, 10}) << h << QList<QPointF>();
		QTest::newRow("misses") << horizontal << line({150, -20}, {150, 20}) << h << QList<QPointF>();
		QTest::newRow("bent wire, second segment")
				<< QVector<QPointF>{{0, -40}, {0, 0}, {100, 0}} << vertical << h << QList<QPointF>{{50, 0}};
	}

	void crossing()
	{
		QFETCH(QVector<QPointF>, wire);
		QFETCH(QVector<QPointF>, other);
		QFETCH(int, mode);
		QFETCH(QList<QPointF>, expected);
		QCOMPARE(WireHops::crossings(wire, {other}, Mode(mode)), expected);
	}

	void closeCrossings()
	{
		const auto wire = line({0, 0}, {100, 0});
			// 6 apart: the arcs would overlap, so only the first hops
		QCOMPARE(WireHops::crossings(wire, {line({40, -20}, {40, 20}), line({46, -20}, {46, 20})}, Mode::Horizontal),
				 (QList<QPointF>{{40, 0}}));
			// 10 apart, one grid step: both hop, in the wire's order
		QCOMPARE(WireHops::crossings(wire, {line({60, -20}, {60, 20}), line({50, -20}, {50, 20})}, Mode::Horizontal),
				 (QList<QPointF>{{50, 0}, {60, 0}}));
	}

	void pathWithoutHopsIsThePlainWire()
	{
		const QVector<QPointF> wire{{0, -40}, {0, 0}, {100, 0}};
		QPainterPath plain;
		plain.moveTo(wire.at(0));
		plain.lineTo(wire.at(1));
		plain.lineTo(wire.at(2));
		QCOMPARE(WireHops::path(wire, {}, Mode::Horizontal), plain);
		QCOMPARE(WireHops::path(wire, {{50, 0}}, Mode::None), plain);
	}

	void pathArcs_data()
	{
		QTest::addColumn<QVector<QPointF>>("wire");
		QTest::addColumn<int>("mode");
		QTest::addColumn<QRectF>("bounds");

		const qreal r = WireHops::radius;
			// The arc goes above a horizontal wire, right of a vertical one,
			// whichever way the wire runs
		QTest::newRow("left to right") << line({0, 0}, {100, 0}) << int(Mode::Horizontal)
									   << QRectF(0, -r, 100, r);
		QTest::newRow("right to left") << line({100, 0}, {0, 0}) << int(Mode::Horizontal)
									   << QRectF(0, -r, 100, r);
		QTest::newRow("top to bottom") << line({50, -20}, {50, 20}) << int(Mode::Vertical)
									   << QRectF(50, -20, r, 40);
		QTest::newRow("bottom to top") << line({50, 20}, {50, -20}) << int(Mode::Vertical)
									   << QRectF(50, -20, r, 40);
	}

	void pathArcs()
	{
		QFETCH(QVector<QPointF>, wire);
		QFETCH(int, mode);
		QFETCH(QRectF, bounds);

		const QPointF hop(50, 0);
		const QPainterPath path = WireHops::path(wire, {hop}, Mode(mode));
		const QRectF actual = path.boundingRect();
		QVERIFY2(qAbs(actual.left() - bounds.left()) < 0.01 && qAbs(actual.top() - bounds.top()) < 0.01
				 && qAbs(actual.right() - bounds.right()) < 0.01 && qAbs(actual.bottom() - bounds.bottom()) < 0.01,
				 qPrintable(QStringLiteral("bounds %1,%2 %3x%4").arg(actual.x()).arg(actual.y())
							.arg(actual.width()).arg(actual.height())));
		QCOMPARE(path.pointAtPercent(0), wire.first());
		QCOMPARE(path.pointAtPercent(1), wire.last());
	}

	// ---- the thinner line gives way, arc or gap (P&ID, 7c) ----
	void thinnerAndShapeNames()
	{
		QCOMPARE(int(WireHops::fromString(WireHops::toString(Mode::Thinner))), int(Mode::Thinner));
		QCOMPARE(WireHops::toString(Mode::Thinner), QStringLiteral("thinner"));
		QCOMPARE(WireHops::toString(WireHops::Shape::Gap), QStringLiteral("gap"));
		QCOMPARE(int(WireHops::shapeFromString(QStringLiteral("gap"))), int(WireHops::Shape::Gap));
		QCOMPARE(int(WireHops::shapeFromString(QStringLiteral("arc"))), int(WireHops::Shape::Arc));
		QCOMPARE(int(WireHops::shapeFromString(QString())), int(WireHops::Shape::Arc));
		QCOMPARE(int(WireHops::shapeFromString(QStringLiteral("zigzag"))), int(WireHops::Shape::Arc));
	}

	void thinnerGivesWay_data()
	{
		QTest::addColumn<QVector<QPointF>>("wire");
		QTest::addColumn<qreal>("width");
		QTest::addColumn<qreal>("other_width");
		QTest::addColumn<bool>("gives_way");

		const auto horizontal = line({0, 0}, {100, 0});
		const auto vertical = line({50, -50}, {50, 50});
			// a signal line (0.25 mm) across a main pipe (1 mm), and back
		QTest::newRow("thin horizontal") << horizontal << 1.0 << 4.0 << true;
		QTest::newRow("thick horizontal") << horizontal << 4.0 << 1.0 << false;
		QTest::newRow("thin vertical") << vertical << 1.0 << 4.0 << true;
		QTest::newRow("thick vertical") << vertical << 4.0 << 1.0 << false;
			// as thick: the vertical one
		QTest::newRow("as thick, horizontal") << horizontal << 4.0 << 4.0 << false;
		QTest::newRow("as thick, vertical") << vertical << 4.0 << 4.0 << true;
	}

	void thinnerGivesWay()
	{
		QFETCH(QVector<QPointF>, wire);
		QFETCH(qreal, width);
		QFETCH(qreal, other_width);
		QFETCH(bool, gives_way);
		const bool is_horizontal = wire.first().y() == wire.last().y();
		const WireHops::Line other{is_horizontal ? line({50, -50}, {50, 50}) : line({0, 0}, {100, 0}),
								   other_width};
		for (const auto shape : {WireHops::Shape::Arc, WireHops::Shape::Gap}) {
			const auto cuts = WireHops::interruptions({wire, width}, {other}, Mode::Thinner, shape,
													  width / 2);
			QCOMPARE(cuts.size(), gives_way ? 1 : 0);
			if (gives_way) {
				QCOMPARE(cuts.first().arc, shape == WireHops::Shape::Arc);
			}
		}
	}

		// A gap leaves 1 mm (gapClearance) between what is seen of the line
		// and the edge of the other one, its square ends counted
	void gapLeavesOneMillimetre()
	{
		const qreal reach = 4.0 / 2 + WireHops::gapClearance + 0.5;
		const WireHops::Line pipe{line({50, -50}, {50, 50}), 4.0};
		auto cuts = WireHops::interruptions({line({0, 0}, {100, 0}), 1.0}, {pipe},
											Mode::Thinner, WireHops::Shape::Gap, 0.5);
		QCOMPARE(cuts.size(), 1);
		QCOMPARE(cuts.first().from, QPointF(50 - reach, 0));
		QCOMPARE(cuts.first().to, QPointF(50 + reach, 0));
		QVERIFY(!cuts.first().arc);
			// the other way: from the right
		cuts = WireHops::interruptions({line({100, 0}, {0, 0}), 1.0}, {pipe},
									   Mode::Thinner, WireHops::Shape::Gap, 0.5);
		QCOMPARE(cuts.first().from, QPointF(50 + reach, 0));
		QCOMPARE(cuts.first().to, QPointF(50 - reach, 0));

			// drawn: two pieces, nothing between them
		const QPainterPath path = WireHops::path(line({0, 0}, {100, 0}),
												 WireHops::interruptions({line({0, 0}, {100, 0}), 1.0}, {pipe},
																		 Mode::Thinner, WireHops::Shape::Gap, 0.5));
		QCOMPARE(path.elementCount(), 4);
		QVERIFY(path.elementAt(0).isMoveTo());
		QCOMPARE(QPointF(path.elementAt(1)), QPointF(50 - reach, 0));
		QVERIFY(path.elementAt(2).isMoveTo());
		QCOMPARE(QPointF(path.elementAt(2)), QPointF(50 + reach, 0));
		QCOMPARE(QPointF(path.elementAt(3)), QPointF(100, 0));
	}

		// Not a crossing: a line ending on the other one (a T), or with no
		// room for the gap before the end of its segment
	void noGapWithoutACrossing()
	{
		const auto wire = line({0, 0}, {100, 0});
		const auto gap = WireHops::Shape::Gap;
		QVERIFY(WireHops::interruptions({wire, 1}, {{line({50, 0}, {50, 50}), 4}}, Mode::Thinner, gap, 0.5).isEmpty());
		QVERIFY(WireHops::interruptions({line({50, 0}, {50, 50}), 1}, {{wire, 4}}, Mode::Thinner, gap, 0.5).isEmpty());
		QVERIFY(WireHops::interruptions({wire, 1}, {{line({4, -50}, {4, 50}), 4}}, Mode::Thinner, gap, 0.5).isEmpty());
		QVERIFY(WireHops::interruptions({wire, 1}, {{line({50, -50}, {50, 50}), 4}}, Mode::None, gap, 0.5).isEmpty());
	}

		// A conductor is drawn in segments which may be empty or follow
		// one another on one line: a crossing on their joint is one
	void crossingOnAJointOfSegments()
	{
		const QVector<QPointF> wire{{50, -90}, {50, -80}, {50, 0}, {50, 0}, {50, 80}, {50, 90}};
		const WireHops::Line pipe{{{0, 0}, {40, 0}, {100, 0}}, 4.0};
		const auto cuts = WireHops::interruptions({wire, 1.0}, {pipe}, Mode::Thinner,
												  WireHops::Shape::Gap, 0.5);
		QCOMPARE(cuts.size(), 1);
		const QPainterPath path = WireHops::path(wire, cuts);
		QCOMPARE(path.elementCount(), 4);
		QCOMPARE(QPointF(path.elementAt(0)), QPointF(50, -90));
		QCOMPARE(QPointF(path.elementAt(3)), QPointF(50, 90));
		QCOMPARE(WireHops::crossingPoints(wire, {pipe.points}), (QList<QPointF>{{50, 0}}));
	}

		// Two pipes side by side crossed by a thin line: one gap over both
	void nearCrossingsMakeOneGap()
	{
		const qreal reach = 4.0 / 2 + WireHops::gapClearance + 0.5;
		const auto cuts = WireHops::interruptions(
					{line({0, 0}, {100, 0}), 1.0},
					{{line({45, -50}, {45, 50}), 4.0}, {line({55, -50}, {55, 50}), 4.0}},
					Mode::Thinner, WireHops::Shape::Gap, 0.5);
		QCOMPARE(cuts.size(), 1);
		QCOMPARE(cuts.first().from, QPointF(45 - reach, 0));
		QCOMPARE(cuts.first().to, QPointF(55 + reach, 0));
	}

		// The arc of the thinner line clears a thick one: never smaller
		// than the hop of upstream, larger over a main pipe
	void arcOverAThickLine()
	{
		const auto wire = line({0, 0}, {100, 0});
		auto cuts = WireHops::interruptions({wire, 1.0}, {{line({50, -50}, {50, 50}), 1.0}},
											Mode::Horizontal, WireHops::Shape::Arc, 0.5);
		QCOMPARE(cuts.size(), 1);
		QCOMPARE(cuts.first().from, QPointF(50 - WireHops::radius, 0));
		cuts = WireHops::interruptions({wire, 1.0}, {{line({50, -50}, {50, 50}), 4.0}},
									   Mode::Thinner, WireHops::Shape::Arc, 0.5);
		const qreal r = 2.0 + WireHops::gapClearance / 2 + 0.5;
		QCOMPARE(cuts.first().from, QPointF(50 - r, 0));
		const QRectF bounds = WireHops::path(wire, cuts).boundingRect();
		QVERIFY2(qAbs(bounds.top() + r) < 0.01, qPrintable(QString::number(bounds.top())));
	}

		// A dashed line crosses on a dash (ISO 128-20): a dash each side of
		// a gap, a dash over a crossing where it goes on, kept on its run
	void dashedLineCrossesOnADash()
	{
		const auto wire = line({0, 0}, {100, 0});
		const WireHops::Interruption gap{{43.5, 0}, {56.5, 0}, false};
		QPainterPath solid = WireHops::solidAtCrossings(wire, {gap}, {{50, 0}}, 6);
		QCOMPARE(solid.elementCount(), 4);
		QCOMPARE(QPointF(solid.elementAt(0)), QPointF(37.5, 0));
		QCOMPARE(QPointF(solid.elementAt(1)), QPointF(43.5, 0));
		QVERIFY(solid.elementAt(2).isMoveTo());
		QCOMPARE(QPointF(solid.elementAt(2)), QPointF(56.5, 0));
		QCOMPARE(QPointF(solid.elementAt(3)), QPointF(62.5, 0));

			// going on over a crossing: a dash across it, at least 3 mm
		solid = WireHops::solidAtCrossings(wire, {}, {{30, 0}}, 6);
		QCOMPARE(solid.elementCount(), 2);
		QCOMPARE(QPointF(solid.elementAt(0)), QPointF(24, 0));
		QCOMPARE(QPointF(solid.elementAt(1)), QPointF(36, 0));
			// a long dash, and near the end of the line: no further
		solid = WireHops::solidAtCrossings(wire, {}, {{95, 0}}, 20);
		QCOMPARE(QPointF(solid.elementAt(0)), QPointF(85, 0));
		QCOMPARE(QPointF(solid.elementAt(1)), QPointF(100, 0));
			// an arc: the dashes and the arc between them
		const WireHops::Interruption arc{{45.5, 0}, {54.5, 0}, true};
		solid = WireHops::solidAtCrossings(wire, {arc}, {{50, 0}}, 6);
		QVERIFY(solid.boundingRect().top() < -4);
		QCOMPARE(solid.boundingRect().left(), 39.5);
		QCOMPARE(solid.boundingRect().right(), 60.5);
			// nothing to cross, no dash
		QVERIFY(WireHops::solidAtCrossings(wire, {}, {}, 6).isEmpty());
		QVERIFY(WireHops::solidAtCrossings(wire, {gap}, {{50, 0}}, 0).isEmpty());
	}

		// Every crossing, whoever gives way: what the marks keep clear of
	void crossingPointsWhoeverGivesWay()
	{
		const auto wire = line({0, 0}, {100, 0});
		QCOMPARE(WireHops::crossingPoints(wire, {line({30, -20}, {30, 20}), line({70, 20}, {70, -20}),
												 line({50, 0}, {50, 20}), line({0, 10}, {100, 10})}),
				 (QList<QPointF>{{30, 0}, {70, 0}}));
	}

	void gapAndThinnerSurviveResave()
	{
		const QString fixture = QFINDTESTDATA("fixtures/wiring_list_arrows.qet");
		QVERIFY2(!fixture.isEmpty(), "fixture project not found");
		QFile source(fixture);
		QVERIFY(source.open(QIODevice::ReadOnly | QIODevice::Text));
		QString text = QString::fromUtf8(source.readAll());
		const int newdiagrams = text.indexOf(QLatin1String("<newdiagrams"));
		QVERIFY(newdiagrams > 0);
		text.insert(newdiagrams, QStringLiteral("<wire_crossings hop=\"thinner\" shape=\"gap\"/>\n    "));
		const QString with_setting = m_dir.filePath(QStringLiteral("with_gap.qet"));
		QFile out(with_setting);
		QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Text));
		out.write(text.toUtf8());
		out.close();

		const QString saved = resave(with_setting);
		QVERIFY2(!saved.isEmpty(), "--resave failed");
			// in whichever order the attributes are written
		QVERIFY2(saved.contains(QRegularExpression(QStringLiteral("<wire_crossings[^>]*hop=\"thinner\"")))
				 && saved.contains(QRegularExpression(QStringLiteral("<wire_crossings[^>]*shape=\"gap\""))),
				 "the setting was lost on save");
	}

	void savedSettingSurvivesResave()
	{
		const QString fixture = QFINDTESTDATA("fixtures/wiring_list_arrows.qet");
		QVERIFY2(!fixture.isEmpty(), "fixture project not found");

			// The fixture has no setting: a resave must not add one
		const QString plain = resave(fixture);
		QVERIFY2(!plain.isEmpty(), "--resave failed");
		QVERIFY(!plain.contains(QLatin1String("wire_crossings")));

			// With the setting, a resave keeps it
		QFile source(fixture);
		QVERIFY(source.open(QIODevice::ReadOnly | QIODevice::Text));
		QString text = QString::fromUtf8(source.readAll());
		const int newdiagrams = text.indexOf(QLatin1String("<newdiagrams"));
		QVERIFY(newdiagrams > 0);
		text.insert(newdiagrams, QStringLiteral("<wire_crossings hop=\"vertical\"/>\n    "));
		const QString with_setting = m_dir.filePath(QStringLiteral("with_setting.qet"));
		QFile out(with_setting);
		QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Text));
		out.write(text.toUtf8());
		out.close();

		const QString saved = resave(with_setting);
		QVERIFY2(!saved.isEmpty(), "--resave failed");
		QVERIFY2(saved.contains(QLatin1String("<wire_crossings hop=\"vertical\"/>")),
				 "the setting was lost on save");
	}
};

QTEST_GUILESS_MAIN(tst_wirehops)

#include "tst_wirehops.moc"
