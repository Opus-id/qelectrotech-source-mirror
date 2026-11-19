// SPDX-License-Identifier: GPL-2.0-or-later
#include "signalmark.h"

#include <QtTest>
#include <QPen>

#include <algorithm>

// The marks of a signal line of a P&ID: a fixed catalogue, a mark by default
// for each type of signal, and the rule which places the marks along a line,
// on points only: no conductor, no folio.
class tst_signalmark : public QObject
{
	Q_OBJECT

	static constexpr qreal m = 10;

		// How far along x (or y) the places are, in order
	static QList<qreal> sorted(const QList<SignalMark::Place> &places, bool by_x = true)
	{
		QList<qreal> at;
		for (const SignalMark::Place &place : places) {
			at << (by_x ? place.at.x() : place.at.y());
		}
		std::sort(at.begin(), at.end());
		return at;
	}

	static int moves(const QPainterPath &path)
	{
		int count = 0;
		for (int i = 0 ; i < path.elementCount() ; ++i) {
			if (path.elementAt(i).isMoveTo()) ++count;
		}
		return count;
	}

	private slots:
			// ---- the catalogue ----
		void theWordsOfTheCatalogue()
		{
			const QList<SignalMark::Mark> all = SignalMark::all();
			QCOMPARE(all.size(), 11);
			QSet<QString> words;
			for (SignalMark::Mark mark : all) {
				const QString word = SignalMark::toString(mark);
				QVERIFY(!word.isEmpty());
				QVERIFY2(!words.contains(word), qPrintable(word));
				words << word;
				bool ok = false;
				QCOMPARE(int(SignalMark::fromString(word, &ok)), int(mark));
				QVERIFY(ok);
			}
				// part of the file format
			QCOMPARE(SignalMark::toString(SignalMark::AMark), QStringLiteral("a_mark"));
			QCOMPARE(SignalMark::toString(SignalMark::Bar2), QStringLiteral("bar2"));
			QCOMPARE(SignalMark::toString(SignalMark::Dashes), QStringLiteral("dashes"));
				// a word of a later version: a plain line, and it is said
			bool ok = true;
			QCOMPARE(int(SignalMark::fromString(QStringLiteral("diamond"), &ok)), int(SignalMark::None));
			QVERIFY(!ok);
			QCOMPARE(int(SignalMark::fromString(QString())), int(SignalMark::None));
		}

			// What a list of marks shows: a name each, never the word of the
			// files
		void eachMarkHasAName()
		{
			QSet<QString> names;
			for (SignalMark::Mark mark : SignalMark::all()) {
				const QString name = SignalMark::name(mark);
				QVERIFY2(!name.isEmpty(), qPrintable(SignalMark::toString(mark)));
				QVERIFY2(!names.contains(name), qPrintable(name));
				QVERIFY2(!name.contains(QLatin1Char('_')), qPrintable(name));
				names << name;
			}
		}

			// ISO 14617 where it has a mark, else ISA-5.1-2009
		void theMarkOfEachTypeOfSignal()
		{
			using S = SignalProperties;
			QCOMPARE(int(SignalMark::defaultMark(S::Undefined)), int(SignalMark::Bar1));
			QCOMPARE(int(SignalMark::defaultMark(S::Pneumatic)), int(SignalMark::AMark));
			QCOMPARE(int(SignalMark::defaultMark(S::Electrical)), int(SignalMark::EMark));
			QCOMPARE(int(SignalMark::defaultMark(S::Hydraulic)), int(SignalMark::LMark));
			QCOMPARE(int(SignalMark::defaultMark(S::Capillary)), int(SignalMark::Cross));
			QCOMPARE(int(SignalMark::defaultMark(S::GuidedElectromagnetic)), int(SignalMark::Wave));
			QCOMPARE(int(SignalMark::defaultMark(S::DataLink)), int(SignalMark::Circle));
				// ISA-5.1-2009, table 5.3.1: a thin plain line
			QCOMPARE(int(SignalMark::defaultMark(S::ProcessConnection)), int(SignalMark::None));
				// each type is told from the others
			QSet<int> marks;
			for (S::SignalType type : S::types()) {
				marks << int(SignalMark::defaultMark(type));
			}
			QCOMPARE(marks.size(), S::types().size());
		}

			// ---- the shapes ----
		void aPlainAndADashedLineHaveNoMark()
		{
			for (SignalMark::Mark mark : {SignalMark::None, SignalMark::Dashes}) {
				QVERIFY(!SignalMark::isRepeated(mark));
				QVERIFY(SignalMark::shapes(mark).isEmpty());
				QCOMPARE(SignalMark::length(mark), 0.0);
				QVERIFY(SignalMark::marksPath(mark, {QPointF(0, 0), QPointF(200, 0)}).isEmpty());
			}
		}

			// A mark is small, about the line and about its own place
		void everyOtherMarkHasAShape()
		{
			for (SignalMark::Mark mark : SignalMark::all()) {
				if (!SignalMark::isRepeated(mark)) {
					continue;
				}
				const QByteArray word = SignalMark::toString(mark).toLatin1();
				const QRectF bounds = SignalMark::shapes(mark).boundingRect();
				QVERIFY2(!SignalMark::shapes(mark).isEmpty(), word);
				QVERIFY2(bounds.width() > 0 && bounds.height() > 0, word);
				QVERIFY2(bounds.top() >= -SignalMark::height / 2 - 1.01, word);
				QVERIFY2(bounds.bottom() <= SignalMark::height / 2 + 0.01, word);
				QVERIFY2(qAbs(bounds.center().x()) < 0.01, word);
				QCOMPARE(SignalMark::length(mark), bounds.width());
				QVERIFY2(SignalMark::length(mark) <= 2 * SignalMark::height, word);
			}
				// one bar more, one gap more
			QCOMPARE(SignalMark::length(SignalMark::Bar2) - SignalMark::length(SignalMark::Bar1),
					 SignalMark::barGap);
			QCOMPARE(SignalMark::length(SignalMark::Bar3) - SignalMark::length(SignalMark::Bar2),
					 SignalMark::barGap);
				// two marks are never the same drawing
			for (SignalMark::Mark a : SignalMark::all()) {
				for (SignalMark::Mark b : SignalMark::all()) {
					if (a < b && SignalMark::isRepeated(a)) {
						QVERIFY(SignalMark::shapes(a) != SignalMark::shapes(b));
					}
				}
			}
		}

		void onlyACircleStandsInTheLine()
		{
			for (SignalMark::Mark mark : SignalMark::all()) {
				QCOMPARE(SignalMark::lineGap(mark) > 0, mark == SignalMark::Circle);
			}
			QCOMPARE(SignalMark::lineGap(SignalMark::Circle), SignalMark::circleDiameter);
		}

			// In drawing units whatever the width of the pen, with flat ends
		void theDashesOfADashedLine()
		{
			QPen thin(Qt::black, 1.0);
			SignalMark::applyDashes(thin);
			QCOMPARE(thin.dashPattern(), (QList<qreal>{SignalMark::dashLength, SignalMark::dashGap}));
			QCOMPARE(thin.capStyle(), Qt::FlatCap);

			QPen hovered(Qt::black, 5.0);
			SignalMark::applyDashes(hovered);
			QCOMPARE(hovered.dashPattern(),
					 (QList<qreal>{SignalMark::dashLength / 5, SignalMark::dashGap / 5}));
		}

			// ---- where the marks stand ----
		void marksAreEvenAboutTheMiddleOfARun()
		{
			const auto places = SignalMark::places({QPointF(0, 0), QPointF(200, 0)}, m, 48);
			QCOMPARE(sorted(places), (QList<qreal>{28, 76, 124, 172}));
			for (const SignalMark::Place &place : places) {
				QCOMPARE(place.at.y(), 0.0);
				QCOMPARE(place.along, QPointF(1, 0));
				QCOMPARE(place.run, 0);
			}
				// the pitch of the catalogue when none is given
			QCOMPARE(SignalMark::places({QPointF(0, 0), QPointF(200, 0)}, m).size(), places.size());
		}

			// Never nearer than a mark to an end, whatever the length
		void noMarkNearAnEnd()
		{
			for (qreal length = 3 * m ; length < 400 ; length += 7) {
				const QList<qreal> at = sorted(
							SignalMark::places({QPointF(50, 0), QPointF(50 + length, 0)}, m, 48));
				QVERIFY(!at.isEmpty());
					// a mark is m long: its middle a mark and a half from the end
				QVERIFY2(at.first() - 50 >= 1.5 * m - 0.01, qPrintable(QString::number(length)));
				QVERIFY2(50 + length - at.last() >= 1.5 * m - 0.01, qPrintable(QString::number(length)));
				QVERIFY(qAbs((at.first() + at.last()) / 2 - (50 + length / 2)) < 0.01);
				for (int i = 0 ; i + 1 < at.size() ; ++i) {
					QVERIFY(qAbs(at.at(i + 1) - at.at(i) - 48) < 0.01);
				}
					// and as many as fit: one more would come too near
				QVERIFY(length - 3 * m < at.size() * 48);
			}
		}

		void aShortRunHasOneMarkOrNone()
		{
				// two marks long: one, in the middle
			const auto one = SignalMark::places({QPointF(0, 0), QPointF(2 * m, 0)}, m, 48);
			QCOMPARE(one.size(), 1);
			QCOMPARE(one.first().at, QPointF(m, 0));
				// shorter, beside a long run: none on it
			const auto corner = SignalMark::places(
						{QPointF(0, 0), QPointF(2 * m - 1, 0), QPointF(2 * m - 1, 100)}, m, 48);
			QVERIFY(!corner.isEmpty());
			for (const SignalMark::Place &place : corner) {
				QCOMPARE(place.run, 1);
				QCOMPARE(place.at.x(), 2 * m - 1);
			}
		}

			// Its type is always said: one mark on its longest run
		void aLineOfShortRunsHasOneMark()
		{
			const auto places = SignalMark::places(
						{QPointF(0, 0), QPointF(8, 0), QPointF(8, 14), QPointF(20, 14)}, m, 48);
			QCOMPARE(places.size(), 1);
			QCOMPARE(places.first().at, QPointF(8, 7));
			QCOMPARE(places.first().run, 1);
				// no line, no mark
			QVERIFY(SignalMark::places({}, m, 48).isEmpty());
			QVERIFY(SignalMark::places({QPointF(3, 3)}, m, 48).isEmpty());
			QVERIFY(SignalMark::places({QPointF(3, 3), QPointF(3, 3)}, m, 48).isEmpty());
		}

		void eachRunOfALineHasItsMarks()
		{
			const QList<QPointF> line{QPointF(0, 0), QPointF(100, 0), QPointF(100, 100), QPointF(40, 100)};
			const auto places = SignalMark::places(line, m, 48);
			QList<int> by_run{0, 0, 0};
			for (const SignalMark::Place &place : places) {
				++by_run[place.run];
			}
				// 100 long: two marks, 26 from each corner; 60 long: one
			QCOMPARE(by_run, (QList<int>{2, 2, 1}));
			QCOMPARE(places.at(0).at, QPointF(26, 0));
			QCOMPARE(places.at(1).at, QPointF(74, 0));
			QCOMPARE(places.at(2).at, QPointF(100, 26));
			QCOMPARE(places.at(4).at, QPointF(70, 100));
		}

			// ---- which way a mark is read ----
		void aMarkReadsTheSameWhicheverWayTheLineIsDrawn()
		{
			const QList<QPointF> to_right{QPointF(0, 0), QPointF(200, 0)};
			const QList<QPointF> to_left{QPointF(200, 0), QPointF(0, 0)};
			const auto a = SignalMark::places(to_right, m, 48);
			const auto b = SignalMark::places(to_left, m, 48);
			QCOMPARE(sorted(a), sorted(b));
			for (const SignalMark::Place &place : b) {
				QCOMPARE(place.along, QPointF(1, 0));
			}
			QCOMPARE(SignalMark::marksPath(SignalMark::AMark, to_right).boundingRect(),
					 SignalMark::marksPath(SignalMark::AMark, to_left).boundingRect());
				// the top of the mark is above the line
			const QTransform frame = SignalMark::frame(b.first());
			QCOMPARE(frame.map(QPointF(0, -5)), b.first().at + QPointF(0, -5));
			QCOMPARE(frame.map(QPointF(3, 0)), b.first().at + QPointF(3, 0));
		}

			// From the right of the sheet: upwards, its top to the left
		void aVerticalRunIsReadFromTheRight()
		{
			for (const QList<QPointF> &line : {QList<QPointF>{QPointF(0, 0), QPointF(0, 200)},
											   QList<QPointF>{QPointF(0, 200), QPointF(0, 0)}}) {
				const auto places = SignalMark::places(line, m, 48);
				QCOMPARE(sorted(places, false), (QList<qreal>{28, 76, 124, 172}));
				for (const SignalMark::Place &place : places) {
					QCOMPARE(place.along, QPointF(0, -1));
					const QTransform frame = SignalMark::frame(place);
					QCOMPARE(frame.map(QPointF(0, -5)), place.at + QPointF(-5, 0));
					QCOMPARE(frame.map(QPointF(3, 0)), place.at + QPointF(0, -3));
				}
			}
				// an oblique run: to the right
			const auto oblique = SignalMark::places({QPointF(100, 0), QPointF(0, 100)}, m, 48);
			QVERIFY(!oblique.isEmpty());
			QVERIFY(oblique.first().along.x() > 0);
			QVERIFY(oblique.first().along.y() < 0);
		}

			// ---- the points of a line ----
		void pointsWhichChangeNothingAreLeftOut()
		{
			QCOMPARE(SignalMark::straightRuns({QPointF(0, 0), QPointF(0, 0), QPointF(50, 0),
											   QPointF(120, 0), QPointF(120, 0), QPointF(120, 40)}),
					 (QList<QPointF>{QPointF(0, 0), QPointF(120, 0), QPointF(120, 40)}));
				// going back is a corner
			QCOMPARE(SignalMark::straightRuns({QPointF(0, 0), QPointF(50, 0), QPointF(20, 0)}).size(), 3);
				// so a run cut in two has the marks of the whole run
			QCOMPARE(sorted(SignalMark::places({QPointF(0, 0), QPointF(90, 0), QPointF(200, 0)}, m, 48)),
					 (QList<qreal>{28, 76, 124, 172}));
		}

			// ---- the line and its marks ----
		void theMarksOfALineArePlaced()
		{
			const QList<QPointF> line{QPointF(0, 0), QPointF(200, 0)};
			const QPainterPath bars = SignalMark::marksPath(SignalMark::Bar2, line);
				// four places, two bars each
			QCOMPARE(moves(bars), 8);
			const QRectF bounds = bars.boundingRect();
			QVERIFY(bounds.left() >= m && bounds.right() <= 200 - m);
			QCOMPARE(bounds.top(), -SignalMark::height / 2);
			QCOMPARE(bounds.bottom(), SignalMark::height / 2);
		}

		void theLineStopsEitherSideOfACircle()
		{
			const QList<QPointF> line{QPointF(0, 0), QPointF(200, 0), QPointF(200, 30)};
			const auto places = SignalMark::places(line, SignalMark::length(SignalMark::Circle));
			QVERIFY(places.size() >= 2);
			const QPainterPath broken = SignalMark::brokenLine(SignalMark::Circle, line);
				// as many pieces as circles, and one
			QCOMPARE(moves(broken), places.size() + 1);
			const qreal r = SignalMark::circleDiameter / 2;
			QPainterPathStroker stroker;
			stroker.setWidth(1);
			const QPainterPath inked = stroker.createStroke(broken);
			for (const SignalMark::Place &place : places) {
				QVERIFY(!inked.contains(place.at));
				const QPointF step = place.run == 0 ? QPointF(1, 0) : QPointF(0, 1);
				QVERIFY(!inked.contains(place.at + (r - 1) * step));
				QVERIFY(inked.contains(place.at + (r + 1) * step));
				QVERIFY(inked.contains(place.at - (r + 1) * step));
			}
				// and its corner is still one line: a piece ends at each
				// circle only
			QCOMPARE(broken.elementCount(), 2 * places.size() + 3);

				// any other mark is drawn over the whole line
			const QPainterPath whole = SignalMark::brokenLine(SignalMark::Bar1, line);
			QCOMPARE(moves(whole), 1);
			QCOMPARE(whole.elementCount(), 3);
		}
};

QTEST_GUILESS_MAIN(tst_signalmark)
#include "tst_signalmark.moc"
