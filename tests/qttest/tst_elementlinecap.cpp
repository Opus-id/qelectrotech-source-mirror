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

#include "editor/graphicspart/partline.h"
#include "factory/elementpicturefactory.h"

#include <QDomDocument>
#include <QImage>
#include <QPainter>
#include <QPicture>

/**
	The line-cap style of the primitives of an element: how the two ends of
	a line are drawn. Square is what QET always did, and stays the default:
	an element that does not say anything is drawn and saved as before. A
	style a version does not know is ignored, so an older QET reads the rest.
	Checked on the drawing of a placed element (ElementPictureFactory) and on
	what the element editor reads and writes (CustomElementGraphicPart).
*/
class tst_elementlinecap : public QObject
{
	Q_OBJECT

	static constexpr int zoom = 4;

		// A horizontal line from x = 10 to x = 30, 5 units wide ("eleve"), so
		// that its caps are 2.5 units long, drawn the way a placed element is.
	static QImage draw(const QString &extra)
	{
		const QString style =
			QStringLiteral("line-style:normal;line-weight:eleve;filling:none;color:black")
			+ extra;
		QDomDocument doc;
		doc.setContent(QStringLiteral(
			"<definition><description>"
			"<line x1=\"10\" y1=\"10\" x2=\"30\" y2=\"10\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"%1\" antialias=\"false\"/>"
			"</description></definition>").arg(style));
		const QPicture picture = ElementPictureFactory::instance()
				->pictureFromDefinition(doc.documentElement());
		QImage image(40 * zoom, 20 * zoom, QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		painter.scale(zoom, zoom);
		painter.drawPicture(0, 0, picture);
		painter.end();
		return image;
	}

		// A corner at (20, 20), 5 units wide: the outer corner of the stroke,
		// as a polygon (bevelled unless told otherwise) and as a rectangle
		// (sharp unless told otherwise).
	static QImage drawCorner(const QString &primitive, const QString &extra)
	{
		const QString style =
			QStringLiteral("line-style:normal;line-weight:eleve;filling:none;color:black")
			+ extra;
		QDomDocument doc;
		doc.setContent(QStringLiteral("<definition><description>%1</description></definition>")
			       .arg(primitive.arg(style)));
		const QPicture picture = ElementPictureFactory::instance()
				->pictureFromDefinition(doc.documentElement());
		QImage image(40 * zoom, 30 * zoom, QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		painter.scale(zoom, zoom);
		painter.drawPicture(0, 0, picture);
		painter.end();
		return image;
	}

	static QImage polygonCorner(const QString &extra)
	{
		return drawCorner(QStringLiteral(
			"<polygon x1=\"10\" y1=\"20\" x2=\"20\" y2=\"20\" x3=\"20\" y3=\"10\" "
			"closed=\"false\" style=\"%1\" antialias=\"false\"/>"), extra);
	}

	static QImage rectangleCorner(const QString &extra)
	{
		return drawCorner(QStringLiteral(
			"<rect x=\"10\" y=\"10\" width=\"10\" height=\"10\" "
			"style=\"%1\" antialias=\"false\"/>"), extra);
	}

		// the very tip of a sharp corner: 3.25 units from the vertex
	static bool atTheTip(const QImage &i) { return inked(i, 22.3, 22.3); }
		// 1.98 units from the vertex: inside a round corner, past the cut
		// of a bevelled one
	static bool pastTheBevel(const QImage &i) { return inked(i, 21.4, 21.4); }

	static bool inked(const QImage &image, qreal x, qreal y)
	{
		return qGray(image.pixel(int(x * zoom), int(y * zoom))) < 128;
	}

		// 1 unit beyond the end, on the axis of the line: inside any cap
		// that goes beyond the end
	static bool beyondTheEnd(const QImage &i) { return inked(i, 31.0, 10.0); }
		// the corner of a square cap: 2.2 units beyond the end and 2.2 off
		// the axis, outside the half circle of a round cap (2.5 units)
	static bool inTheCorner(const QImage &i) { return inked(i, 32.2, 7.8); }
		// the line itself, to be sure something is drawn
	static bool onTheLine(const QImage &i) { return inked(i, 20.0, 10.0); }

private slots:
	void noStyleIsASquareCap()
	{
		const QImage image = draw(QString());
		QVERIFY(onTheLine(image));
		QVERIFY(beyondTheEnd(image));
		QVERIFY(inTheCorner(image));
	}

	void squareIsWhatNothingGives()
	{
		QCOMPARE(draw(QStringLiteral(";line-cap:square")), draw(QString()));
	}

	void roundCapGoesBeyondTheEndWithoutTheCorner()
	{
		const QImage image = draw(QStringLiteral(";line-cap:round"));
		QVERIFY(onTheLine(image));
		QVERIFY(beyondTheEnd(image));
		QVERIFY(!inTheCorner(image));
	}

	void flatCapStopsAtTheEnd()
	{
		const QImage image = draw(QStringLiteral(";line-cap:flat"));
		QVERIFY(onTheLine(image));
		QVERIFY(!beyondTheEnd(image));
		QVERIFY(!inTheCorner(image));
	}

	void anUnknownCapIsIgnored()
	{
		QCOMPARE(draw(QStringLiteral(";line-cap:wavy")), draw(QString()));
	}

		// Written in the order the other styles are, but only when asked for
	void theEditorKeepsTheCap_data()
	{
		QTest::addColumn<QString>("cap");
		QTest::addColumn<int>("expected");
		QTest::addColumn<bool>("written");
		QTest::newRow("round")   << "round"   << int(CustomElementGraphicPart::RoundLineCap) << true;
		QTest::newRow("flat")    << "flat"    << int(CustomElementGraphicPart::FlatLineCap)  << true;
		QTest::newRow("square")  << "square"  << int(CustomElementGraphicPart::SquareLineCap) << false;
		QTest::newRow("unknown") << "wavy"    << int(CustomElementGraphicPart::SquareLineCap) << false;
		QTest::newRow("absent")  << ""        << int(CustomElementGraphicPart::SquareLineCap) << false;
	}

	void theEditorKeepsTheCap()
	{
		QFETCH(QString, cap);
		QFETCH(int, expected);
		QFETCH(bool, written);

		const QString style =
			QStringLiteral("line-style:normal;line-weight:normal;filling:none;color:black")
			+ (cap.isEmpty() ? QString() : ";line-cap:" + cap);
		QDomDocument doc;
		QVERIFY(doc.setContent(QStringLiteral(
			"<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"%1\" antialias=\"true\"/>").arg(style)));

		PartLine line(nullptr);
		line.fromXml(doc.documentElement());
		QCOMPARE(int(line.lineCap()), expected);

		QDomDocument out;
		const QDomElement written_element = line.toXml(out);
		const QString written_style = written_element.attribute(QStringLiteral("style"));
		QCOMPARE(written_style.contains(QStringLiteral("line-cap:")), written);
		if (written) {
			QVERIFY(written_style.contains(QStringLiteral("line-cap:") + cap));
		}
		QVERIFY(written_style.contains(QStringLiteral("line-weight:normal")));
	}

		// The corners: sharp, round or bevelled, drawn at the same place
	void aPolygonIsBevelledUnlessTold()
	{
		const QImage image = polygonCorner(QString());
		QVERIFY(!atTheTip(image));
		QVERIFY(!pastTheBevel(image));
		QCOMPARE(polygonCorner(QStringLiteral(";line-join:bevel")), image);
	}

	void aRectangleIsSharpUnlessTold()
	{
		const QImage image = rectangleCorner(QString());
		QVERIFY(atTheTip(image));
		QVERIFY(pastTheBevel(image));
		QCOMPARE(rectangleCorner(QStringLiteral(";line-join:miter")), image);
	}

	void theCornersFollowTheStyle()
	{
		for (auto corner : {polygonCorner, rectangleCorner}) {
			const QImage sharp = corner(QStringLiteral(";line-join:miter"));
			QVERIFY(atTheTip(sharp));
			QVERIFY(pastTheBevel(sharp));

			const QImage round = corner(QStringLiteral(";line-join:round"));
			QVERIFY(!atTheTip(round));
			QVERIFY(pastTheBevel(round));

			const QImage bevel = corner(QStringLiteral(";line-join:bevel"));
			QVERIFY(!atTheTip(bevel));
			QVERIFY(!pastTheBevel(bevel));
		}
	}

	void anUnknownJoinIsIgnored()
	{
		QCOMPARE(polygonCorner(QStringLiteral(";line-join:wavy")), polygonCorner(QString()));
		QCOMPARE(rectangleCorner(QStringLiteral(";line-join:wavy")), rectangleCorner(QString()));
	}

	void theEditorKeepsTheJoin_data()
	{
		QTest::addColumn<QString>("join");
		QTest::addColumn<int>("expected");
		QTest::addColumn<bool>("written");
		QTest::newRow("miter")   << "miter" << int(CustomElementGraphicPart::MiterLineJoin)   << true;
		QTest::newRow("round")   << "round" << int(CustomElementGraphicPart::RoundLineJoin)   << true;
		QTest::newRow("bevel")   << "bevel" << int(CustomElementGraphicPart::BevelLineJoin)   << true;
		QTest::newRow("unknown") << "wavy"  << int(CustomElementGraphicPart::DefaultLineJoin) << false;
		QTest::newRow("absent")  << ""      << int(CustomElementGraphicPart::DefaultLineJoin) << false;
	}

	void theEditorKeepsTheJoin()
	{
		QFETCH(QString, join);
		QFETCH(int, expected);
		QFETCH(bool, written);

		const QString style =
			QStringLiteral("line-style:normal;line-weight:normal;filling:none;color:black")
			+ (join.isEmpty() ? QString() : ";line-join:" + join);
		QDomDocument doc;
		QVERIFY(doc.setContent(QStringLiteral(
			"<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"%1\" antialias=\"true\"/>").arg(style)));

		PartLine line(nullptr);
		line.fromXml(doc.documentElement());
		QCOMPARE(int(line.lineJoin()), expected);

		QDomDocument out;
		const QString written_style = line.toXml(out).attribute(QStringLiteral("style"));
		QCOMPARE(written_style.contains(QStringLiteral("line-join:")), written);
		if (written) {
			QVERIFY(written_style.contains(QStringLiteral("line-join:") + join));
		}
	}

	void theEditorDefaultsToSquare()
	{
		PartLine line(nullptr);
		QCOMPARE(int(line.lineCap()), int(CustomElementGraphicPart::SquareLineCap));
		QCOMPARE(int(line.lineJoin()), int(CustomElementGraphicPart::DefaultLineJoin));
	}
};

QET_APP_TEST_MAIN(tst_elementlinecap)
#include "tst_elementlinecap.moc"
