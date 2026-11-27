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
#include "dataBase/projectdatabase.h"
#include "diagram.h"
#include "diagramevent/diagrameventaddrevisioncloud.h"
#include "dxfexport.h"
#include "exportproperties.h"
#include "factory/propertieseditorfactory.h"
#include "lastusedstyle.h"
#include "properties/revisionlist.h"
#include "qetapptest.h"
#include "qetgraphicsitem/revisionclouditem.h"
#include "qetproject.h"
#include "qetresult.h"
#include "ui/revisioncloudpropertieswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDomDocument>
#include <QFile>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

/**
	Revision clouds and review notes (step 9i): one shape of two kinds, a
	cloud of arcs around a rectangle or a free outline, saved as a <shape>
	that an older QElectroTech reads as a plain one; drawn with the tool of
	the Add menu; edited in one undo step; a review note printed only when
	asked.
*/
class tst_revisioncloud : public QObject
{
	Q_OBJECT

		//A cloud added to @p diagram, as a load or a paste does
	static RevisionCloudItem *cloud(Diagram *diagram, const QRectF &rect,
									RevisionCloudItem::Kind kind = RevisionCloudItem::Revision)
	{
		auto *item = new RevisionCloudItem(rect.topLeft(), rect.bottomRight(),
										   QetShapeItem::Rectangle, kind);
		diagram->addItem(item);
		return item;
	}

	static QList<RevisionCloudItem *> cloudsOf(Diagram *diagram)
	{
		QList<RevisionCloudItem *> clouds;
		for (QGraphicsItem *item : diagram->items())
			if (auto c = qobject_cast<RevisionCloudItem *>(item->toGraphicsObject()))
				clouds << c;
		return clouds;
	}

	static void press(DiagramEventInterface &tool, const QPointF &at)
	{
		QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMousePress);
		event.setButton(Qt::LeftButton);
		event.setButtons(Qt::LeftButton);
		event.setScenePos(at);
		tool.mousePressEvent(&event);
	}

	static void move(DiagramEventInterface &tool, const QPointF &at)
	{
		QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMouseMove);
		event.setScenePos(at);
		tool.mouseMoveEvent(&event);
	}

	static void doubleClick(DiagramEventInterface &tool, const QPointF &at)
	{
		QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMouseDoubleClick);
		event.setButton(Qt::LeftButton);
		event.setButtons(Qt::LeftButton);
		event.setScenePos(at);
		tool.mouseDoubleClickEvent(&event);
	}

		//Answers the dialog asking for the comment of a note
	static void answerComment(const QString &comment, bool accept)
	{
		QTimer::singleShot(0, [comment, accept] {
			QWidget *dialog = QApplication::activeModalWidget();
			QVERIFY(dialog);
			QCOMPARE(dialog->objectName(), QStringLiteral("askReviewNote"));
			dialog->findChild<QPlainTextEdit *>(QStringLiteral("comment"))->setPlainText(comment);
			if (accept)
				static_cast<QDialog *>(dialog)->accept();
			else
				static_cast<QDialog *>(dialog)->reject();
		});
	}

		//How many pixels of @p color a render of @p diagram has
	static int pixels(Diagram *diagram, const QColor &color)
	{
		const QRectF source(0, 0, 400, 400);
		QImage image(400, 400, QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		diagram->render(&painter, QRectF(image.rect()), source);
		painter.end();
		int count = 0;
		for (int y = 0 ; y < image.height() ; ++y)
			for (int x = 0 ; x < image.width() ; ++x) {
				const QColor pixel = image.pixelColor(x, y);
				if (qAbs(pixel.red() - color.red()) < 60 && qAbs(pixel.green() - color.green()) < 60
						&& qAbs(pixel.blue() - color.blue()) < 60 && pixel != QColor(Qt::white))
					++count;
			}
		return count;
	}

	private slots:
		void cleanup()
		{
			QSettings().remove(QStringLiteral("diagrameditor/revision_cloud_color"));
			QSettings().remove(QStringLiteral("diagrameditor/review_note_color"));
		}

			//What is not filled is not written; an older QElectroTech reads
			//a plain rectangle
		void aCloudSavesOnlyWhatItHas()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *c = cloud(diagram, QRectF(100, 100, 200, 100));
			QDomDocument document;
			const QDomElement plain = c->toXml(document);
			QCOMPARE(plain.tagName(), QStringLiteral("shape"));
			QCOMPARE(plain.attribute(QStringLiteral("cloud")), QStringLiteral("revision"));
			for (const char *name : {"revision", "comment", "author", "date", "reply", "done",
									 "label_x", "label_y", "label_size", "arc"})
				QVERIFY2(!plain.hasAttribute(QLatin1String(name)), name);

			QetShapeItem older(QPointF(0, 0));
			QVERIFY(older.fromXml(plain));
			QCOMPARE(older.shapeType(), QetShapeItem::Rectangle);
			QCOMPARE(older.rect().normalized(), QRectF(100, 100, 200, 100));
		}

		void bothKindsComeBackFromTheirXml()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *r = cloud(diagram, QRectF(0, 0, 100, 100));
			r->setRevision(QStringLiteral("B"));
			r->setArcSize(30);
			RevisionCloudItem *n = cloud(diagram, QRectF(200, 0, 100, 100), RevisionCloudItem::Review);
			n->setComment(QStringLiteral("DN 80, not 100"));
			n->setAuthor(QStringLiteral("JD"));
			n->setDate(QStringLiteral("2026-11-27"));
			n->setDone(true);
			n->setReply(QStringLiteral("done, DN 80"));
			n->setLabelSize(10);

			QDomDocument document;
			const QDomElement rx = r->toXml(document);
			const QDomElement nx = n->toXml(document);
			QCOMPARE(rx.attribute(QStringLiteral("revision")), QStringLiteral("B"));
			QCOMPARE(rx.attribute(QStringLiteral("arc")), QStringLiteral("30"));
			QCOMPARE(nx.attribute(QStringLiteral("cloud")), QStringLiteral("review"));
			QCOMPARE(nx.attribute(QStringLiteral("done")), QStringLiteral("true"));

			RevisionCloudItem read_r(QPointF(0, 0));
			QVERIFY(read_r.fromXml(rx));
			QCOMPARE(read_r.kind(), RevisionCloudItem::Revision);
			QCOMPARE(read_r.revision(), QStringLiteral("B"));
			QCOMPARE(read_r.arcSize(), 30.0);
			RevisionCloudItem read_n(QPointF(0, 0));
			QVERIFY(read_n.fromXml(nx));
			QCOMPARE(read_n.kind(), RevisionCloudItem::Review);
			QCOMPARE(read_n.comment(), QStringLiteral("DN 80, not 100"));
			QCOMPARE(read_n.author(), QStringLiteral("JD"));
			QCOMPARE(read_n.date(), QStringLiteral("2026-11-27"));
			QVERIFY(read_n.isDone());
			QCOMPARE(read_n.reply(), QStringLiteral("done, DN 80"));
			QCOMPARE(read_n.labelSize(), 10.0);
			QCOMPARE(read_n.label()->toPlainText(),
					 QStringLiteral("DN 80, not 100\n→ done, DN 80"));
				//Done: greyed out
			QVERIFY(read_n.opacity() < 1.0);
		}

			//Opening a file never changes it: a cloud read without an index
			//stays without one, though the project has revisions
		void readingGivesNothingByDefault()
		{
			QETProject project;
			RevisionList list;
			list.setRevisions({Revision{QStringLiteral("A"), {}, {}, {}, {}},
							   Revision{QStringLiteral("B"), {}, {}, {}, {}}});
			project.setRevisions(list);
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			cloud(diagram, QRectF(0, 0, 100, 100));
			cloud(diagram, QRectF(200, 0, 100, 100), RevisionCloudItem::Review);
			QDomDocument saved = diagram->toXml();
			Diagram *reloaded = project.addNewDiagram();
			QVERIFY(reloaded->fromXml(saved));
			const QList<RevisionCloudItem *> clouds = cloudsOf(reloaded);
			QCOMPARE(clouds.size(), 2);
			for (RevisionCloudItem *c : clouds) {
				QVERIFY(c->revision().isEmpty());
				QVERIFY(c->author().isEmpty());
				QVERIFY(c->date().isEmpty());
			}
				//And its clouds are written as they were read
			const auto shapes = [](const QDomDocument &folio) {
				QString text;
				QTextStream stream(&text);
				folio.documentElement().firstChildElement(QStringLiteral("shapes")).save(stream, 0);
				return text;
			};
			QVERIFY(!shapes(saved).isEmpty());
			QCOMPARE(shapes(reloaded->toXml()), shapes(saved));
		}

		void aLoadedFolioAndAPasteKeepTheClouds()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *n = cloud(diagram, QRectF(0, 0, 100, 100), RevisionCloudItem::Review);
			n->setComment(QStringLiteral("Check"));
			diagram->addItem(new QetShapeItem(QPointF(200, 0), QPointF(300, 100), QetShapeItem::Rectangle));
			QDomDocument saved = diagram->toXml();

			QETProject other;
			Diagram *reloaded = other.addNewDiagram();
			QVERIFY(reloaded->fromXml(saved));
			int clouds = 0, shapes = 0;
			for (QGraphicsItem *item : reloaded->items()) {
				if (auto c = qobject_cast<RevisionCloudItem *>(item->toGraphicsObject())) {
					++clouds;
					QCOMPARE(c->kind(), RevisionCloudItem::Review);
					QCOMPARE(c->comment(), QStringLiteral("Check"));
						//A cloud keeps the type of a shape: cut, copy, group...
					QCOMPARE(c->type(), int(QetShapeItem::Type));
				} else if (item->type() == QetShapeItem::Type) {
					++shapes;
				}
			}
			QCOMPARE(clouds, 1);
			QCOMPARE(shapes, 1);

				//A paste into the same folio: the same data
			QVERIFY(diagram->fromXml(saved, QPointF(0, 300), false));
			const QList<RevisionCloudItem *> both = cloudsOf(diagram);
			QCOMPARE(both.size(), 2);
			for (RevisionCloudItem *c : both)
				QCOMPARE(c->comment(), QStringLiteral("Check"));
		}

			//The arcs: closed, outside the outline, as many as the size asks
		void theArcsBulgeOutwards_data()
		{
			QTest::addColumn<QPolygonF>("outline");
			const QPolygonF square({QPointF(0, 0), QPointF(100, 0), QPointF(100, 100), QPointF(0, 100)});
			QPolygonF reversed;
			for (int i = int(square.size()) - 1 ; i >= 0 ; --i)
				reversed << square.at(i);
			QTest::newRow("clockwise") << square;
			QTest::newRow("anticlockwise") << reversed;
			QTest::newRow("triangle") << QPolygonF({QPointF(0, 0), QPointF(200, 0), QPointF(100, 150)});
		}

		void theArcsBulgeOutwards()
		{
			QFETCH(QPolygonF, outline);
			const QPainterPath path = RevisionCloudItem::cloudPath(outline, 20);
			const QPolygonF flat = path.toFillPolygon();
				//Every point of the outline is inside, or on, the cloud
			QPainterPath base;
			base.addPolygon(outline);
			base.closeSubpath();
			QVERIFY(path.boundingRect().contains(base.boundingRect()));
			QVERIFY(path.boundingRect().width() + path.boundingRect().height()
					> base.boundingRect().width() + base.boundingRect().height() + 5);
				//The middle of a side is beyond it, not inside the outline
			const QPointF middle = (outline.at(0) + outline.at(1)) / 2;
			bool outside = false;
			for (const QPointF &point : flat)
				if (QLineF(point, middle).length() < 10 && !base.contains(point))
					outside = true;
			QVERIFY(outside);
				//Closed
			QCOMPARE(path.elementAt(0).x, path.elementAt(path.elementCount() - 1).x);
			QCOMPARE(path.elementAt(0).y, path.elementAt(path.elementCount() - 1).y);
		}

		void aSideGetsArcsOfAboutTheSizeGiven()
		{
				//A 100 long side and arcs of 20: five arcs, each a cubic
			const QPolygonF square({QPointF(0, 0), QPointF(100, 0), QPointF(100, 100), QPointF(0, 100)});
			int curves = 0;
			const QPainterPath path = RevisionCloudItem::cloudPath(square, 20);
			for (int i = 0 ; i < path.elementCount() ; ++i)
				if (path.elementAt(i).type == QPainterPath::CurveToElement)
					++curves;
			QCOMPARE(curves, 20);
			int bigger = 0;
			const QPainterPath fewer = RevisionCloudItem::cloudPath(square, 50);
			for (int i = 0 ; i < fewer.elementCount() ; ++i)
				if (fewer.elementAt(i).type == QPainterPath::CurveToElement)
					++bigger;
			QCOMPARE(bigger, 8);
		}

			//Mirrored, the arcs stay outside: the whole drawing is mirrored
		void aMirroredCloudKeepsItsArcsOutside()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *c = cloud(diagram, QRectF(100, 100, 100, 100));
			const QRectF before = c->sceneOutlineRect();
			c->setScaleFactorX(-1);
			const QRectF after = c->sceneOutlineRect();
			QCOMPARE(after.size(), before.size());
			QVERIFY(after.width() > 100);
				//And its text stays readable
			QVERIFY(c->label()->sceneTransform().m11() > 0);
		}

			//Alt + a handle makes a rectangle a polygon: still a cloud
		void aPromotedCloudStaysACloud()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *c = cloud(diagram, QRectF(0, 0, 100, 100));
			c->setRevision(QStringLiteral("C"));
			QDomElement before = c->snapshotXml();
			QDomDocument document;
			QDomElement after = before.cloneNode(true).toElement();
			after.setAttribute(QStringLiteral("type"), QStringLiteral("Polygon"));
			QDomElement points = document.createElement(QStringLiteral("points"));
			for (const QPointF &p : {QPointF(0, 0), QPointF(120, 0), QPointF(100, 100), QPointF(0, 100)}) {
				QDomElement point = document.createElement(QStringLiteral("point"));
				point.setAttribute(QStringLiteral("x"), p.x());
				point.setAttribute(QStringLiteral("y"), p.y());
				points.appendChild(point);
			}
			after.appendChild(points);
			after.setAttribute(QStringLiteral("closed"), 1);
			QVERIFY(c->fromXml(after));
			QCOMPARE(c->shapeType(), QetShapeItem::Polygon);
			QCOMPARE(c->revision(), QStringLiteral("C"));
			QVERIFY(c->sceneOutlineRect().width() > 120);
		}

		void theToolDrawsARevisionOfTheLastIndex()
		{
			QETProject project;
			RevisionList list;
			list.setRevisions({Revision{QStringLiteral("A"), {}, {}, {}, {}},
							   Revision{QStringLiteral("B"), QStringLiteral("2026-11-27"),
										QStringLiteral("Pumps"), {}, {}}});
			project.setRevisions(list);
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
				//The style last given to a shape is not a cloud's
			QPen blue(Qt::blue);
			blue.setWidth(3);
			LastUsedStyle::setShapePen(blue);
			DiagramEventAddRevisionCloud tool(diagram, RevisionCloudItem::Revision);
			QVERIFY(!tool.drawsFreeOutline());
			press(tool, QPointF(100, 100));
			move(tool, QPointF(300, 200));
			press(tool, QPointF(300, 200));
			const QList<RevisionCloudItem *> clouds = cloudsOf(diagram);
			QCOMPARE(clouds.size(), 1);
			QCOMPARE(clouds.first()->revision(), QStringLiteral("B"));
			QCOMPARE(clouds.first()->pen().color(), QColor(Qt::black));
			QCOMPARE(clouds.first()->label()->toPlainText(), QStringLiteral("B"));
			QVERIFY(!clouds.first()->label()->triangle().isEmpty());
			QVERIFY(clouds.first()->toolTipText().contains(QLatin1String("Pumps")));
			QCOMPARE(diagram->undoStack().count(), steps + 1);
			diagram->undoStack().undo();
			QVERIFY(cloudsOf(diagram).isEmpty());
		}

		void aRevisionWithoutAListHasNoIndex()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			DiagramEventAddRevisionCloud tool(diagram, RevisionCloudItem::Revision);
			press(tool, QPointF(100, 100));
			press(tool, QPointF(300, 200));
			const QList<RevisionCloudItem *> clouds = cloudsOf(diagram);
			QCOMPARE(clouds.size(), 1);
			QVERIFY(clouds.first()->revision().isEmpty());
				//Nothing to say: no triangle to click
			QVERIFY(!clouds.first()->label()->isVisible());
		}

		void aReviewNoteAsksForItsComment()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem::setNewCloudColor(RevisionCloudItem::Review, QColor(Qt::magenta));
			LastUsedStyle::setShapePen(QPen(Qt::blue));
			DiagramEventAddRevisionCloud tool(diagram, RevisionCloudItem::Review);
			press(tool, QPointF(100, 100));
			answerComment(QStringLiteral("Wrong valve"), true);
			press(tool, QPointF(300, 200));
			QList<RevisionCloudItem *> clouds = cloudsOf(diagram);
			QCOMPARE(clouds.size(), 1);
			RevisionCloudItem *note = clouds.first();
			QCOMPARE(note->comment(), QStringLiteral("Wrong valve"));
			QCOMPARE(note->date(), RevisionCloudItem::today());
			QCOMPARE(note->pen().color(), QColor(Qt::magenta));
			QVERIFY(!note->isDone());
				//One step: the note and its comment
			QCOMPARE(diagram->undoStack().count(), steps + 1);

				//Cancelled: no note
			press(tool, QPointF(100, 300));
			answerComment(QStringLiteral("Never mind"), false);
			press(tool, QPointF(300, 380));
			QCOMPARE(cloudsOf(diagram).size(), 1);
			QCOMPARE(diagram->undoStack().count(), steps + 1);
		}

		void theDefaultColoursAreBlackAndRed()
		{
			QCOMPARE(RevisionCloudItem::newCloudColor(RevisionCloudItem::Revision), QColor(Qt::black));
			QCOMPARE(RevisionCloudItem::newCloudColor(RevisionCloudItem::Review), QColor(Qt::red));
		}

		void spaceDrawsAFreeOutline()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			DiagramEventAddRevisionCloud tool(diagram, RevisionCloudItem::Revision);
			QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
			QVERIFY(tool.handlesShortcutOverride(&space));
				//Before the first click, Space does nothing
			tool.keyPressEvent(&space);
			QVERIFY(!tool.drawsFreeOutline());
			const QList<QPointF> points = {QPointF(100, 100), QPointF(300, 100), QPointF(300, 200),
										   QPointF(200, 250)};
			for (const QPointF &point : points) {
				move(tool, point);
				press(tool, point);
				if (point == points.first()) {
					tool.keyPressEvent(&space);
					QVERIFY(tool.drawsFreeOutline());
				}
			}
			doubleClick(tool, points.last());
			const QList<RevisionCloudItem *> clouds = cloudsOf(diagram);
			QCOMPARE(clouds.size(), 1);
			QCOMPARE(clouds.first()->shapeType(), QetShapeItem::Polygon);
			QVERIFY(clouds.first()->isClosed());
			QCOMPARE(clouds.first()->polygon().size(), 4);
			QCOMPARE(diagram->undoStack().count(), steps + 1);
				//The next cloud starts as a rectangle again
			press(tool, QPointF(400, 100));
			QVERIFY(!tool.drawsFreeOutline());
		}

			//Space right after the first click switches the outline being
			//drawn, from the same first point; not once a second point is placed
		void spaceAfterTheFirstClickKeepsTheFirstPoint()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			DiagramEventAddRevisionCloud tool(diagram, RevisionCloudItem::Revision);
			QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);

				//A rectangle begun becomes a free outline
			press(tool, QPointF(100, 100));
			move(tool, QPointF(300, 100));
			tool.keyPressEvent(&space);
			QVERIFY(tool.drawsFreeOutline());
			QVERIFY(tool.hint().contains(QStringLiteral("second point")));
				//Only one shape being drawn, never the rectangle left behind
			QCOMPARE(cloudsOf(diagram).size(), 1);
			press(tool, QPointF(300, 100));
			QVERIFY(!tool.canSwitchOutline());
			tool.keyPressEvent(&space);
			QVERIFY(tool.drawsFreeOutline());
			move(tool, QPointF(200, 250));
			press(tool, QPointF(200, 250));
			doubleClick(tool, QPointF(200, 250));
			QList<RevisionCloudItem *> clouds = cloudsOf(diagram);
			QCOMPARE(clouds.size(), 1);
			QCOMPARE(clouds.first()->shapeType(), QetShapeItem::Polygon);
			QCOMPARE(clouds.first()->polygon(),
					 QPolygonF({QPointF(100, 100), QPointF(300, 100), QPointF(200, 250)}));
			QCOMPARE(diagram->undoStack().count(), steps + 1);

				//A free outline with its first point only becomes a rectangle
			press(tool, QPointF(400, 100));
			QVERIFY(!tool.drawsFreeOutline());
			move(tool, QPointF(500, 200));
			tool.keyPressEvent(&space);
			QVERIFY(tool.drawsFreeOutline());
			tool.keyPressEvent(&space);
			QVERIFY(!tool.drawsFreeOutline());
			QCOMPARE(cloudsOf(diagram).size(), 2);
			press(tool, QPointF(500, 200));
			clouds = cloudsOf(diagram);
			QCOMPARE(clouds.size(), 2);
			RevisionCloudItem *rectangle = clouds.first()->shapeType() == QetShapeItem::Rectangle
					? clouds.first() : clouds.last();
			QCOMPARE(rectangle->shapeType(), QetShapeItem::Rectangle);
			QCOMPARE(rectangle->mapRectToScene(rectangle->rect()).normalized(), QRectF(400, 100, 100, 100));
			QCOMPARE(diagram->undoStack().count(), steps + 2);
		}

			//A double click after one point is no cloud
		void tooFewPointsDrawNothing()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			DiagramEventAddRevisionCloud tool(diagram, RevisionCloudItem::Revision);
			QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
			move(tool, QPointF(100, 100));
			press(tool, QPointF(100, 100));
			tool.keyPressEvent(&space);
			move(tool, QPointF(200, 100));
			press(tool, QPointF(200, 100));
			doubleClick(tool, QPointF(200, 100));
			QVERIFY(cloudsOf(diagram).isEmpty());
			QCOMPARE(diagram->undoStack().count(), steps);
		}

		void anApplyIsOneUndoStep()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *n = cloud(diagram, QRectF(0, 0, 100, 100), RevisionCloudItem::Review);
			RevisionCloudPropertiesWidget editor(n);
			editor.findChild<QPlainTextEdit *>(QStringLiteral("comment"))->setPlainText(QStringLiteral("Too close"));
			editor.findChild<QLineEdit *>(QStringLiteral("reply"))->setText(QStringLiteral("moved"));
			editor.findChild<QCheckBox *>(QStringLiteral("done"))->setChecked(true);
			editor.apply();
			QCOMPARE(n->comment(), QStringLiteral("Too close"));
			QCOMPARE(n->reply(), QStringLiteral("moved"));
			QVERIFY(n->isDone());
			QCOMPARE(n->label()->toPlainText(), QStringLiteral("Too close\n→ moved"));
			QCOMPARE(diagram->undoStack().count(), steps + 1);
			diagram->undoStack().undo();
			QVERIFY(n->comment().isEmpty());
			QVERIFY(!n->isDone());
			QCOMPARE(n->opacity(), 1.0);
				//The editor follows the undo
			QVERIFY(editor.findChild<QPlainTextEdit *>(QStringLiteral("comment"))->toPlainText().isEmpty());
		}

		void theRevisionIsOfferedFromTheList()
		{
			QETProject project;
			RevisionList list;
			list.setRevisions({Revision{QStringLiteral("A"), {}, QStringLiteral("First"), {}, {}},
							   Revision{QStringLiteral("B"), {}, QStringLiteral("Pumps"), {}, {}}});
			project.setRevisions(list);
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *r = cloud(diagram, QRectF(0, 0, 100, 100));
			RevisionCloudPropertiesWidget editor(r);
			auto *revision = editor.findChild<QComboBox *>(QStringLiteral("revision"));
			QCOMPARE(revision->count(), 2);
			revision->setCurrentIndex(1);
			QCOMPARE(editor.revisionText(), QStringLiteral("B"));
			editor.apply();
			QCOMPARE(r->revision(), QStringLiteral("B"));
				//Typed: kept as typed
			revision->setEditText(QStringLiteral("C1"));
			QCOMPARE(editor.revisionText(), QStringLiteral("C1"));
			editor.apply();
			QCOMPARE(r->revision(), QStringLiteral("C1"));
		}

			//Selected alone, a cloud is edited as a cloud; its text, too
		void theDockEditsACloud()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *r = cloud(diagram, QRectF(0, 0, 100, 100));
			r->setRevision(QStringLiteral("A"));
			PropertiesEditorWidget *editor = PropertiesEditorFactory::propertiesEditor(
						QList<QGraphicsItem *>{r}, nullptr);
			QVERIFY(qobject_cast<RevisionCloudPropertiesWidget *>(editor));
			delete editor;
			editor = PropertiesEditorFactory::propertiesEditor(QList<QGraphicsItem *>{r->label()}, nullptr);
			QVERIFY(qobject_cast<RevisionCloudPropertiesWidget *>(editor));
			delete editor;
		}

		void aMovedTextIsSavedAndUndone()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *r = cloud(diagram, QRectF(0, 0, 100, 100));
			r->setRevision(QStringLiteral("A"));
			const QPointF corner = r->labelOffset();
			QDomDocument document;
			QVERIFY(!r->toXml(document).hasAttribute(QStringLiteral("label_x")));
			r->setLabelOffset(corner + QPointF(30, 40));
			QVERIFY(r->labelMoved());
			const QDomElement moved = r->toXml(document);
			QVERIFY(moved.hasAttribute(QStringLiteral("label_x")));
			RevisionCloudItem read(QPointF(0, 0));
			QVERIFY(read.fromXml(moved));
				//Written to a tenth
			QVERIFY(QLineF(read.labelOffset(), corner + QPointF(30, 40)).length() < 0.1);
			r->setLabelOffset(corner);
			QVERIFY(!r->labelMoved());
			QVERIFY(!r->toXml(document).hasAttribute(QStringLiteral("label_x")));
		}

			//On the screen, both; on paper, a review note only when asked
		void aReviewNoteIsPrintedOnlyWhenAsked()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *r = cloud(diagram, QRectF(40, 40, 120, 120));
			QPen blue(Qt::blue);
			blue.setWidth(4);
			r->setPen(blue);
			RevisionCloudItem *n = cloud(diagram, QRectF(220, 40, 120, 120), RevisionCloudItem::Review);
			QPen red(Qt::red);
			red.setWidth(4);
			n->setPen(red);
			n->setComment(QStringLiteral("Check"));

			QVERIFY(diagram->drawReviewNotes());	//the screen
			QVERIFY(pixels(diagram, Qt::red) > 100);

			ExportProperties paper;
			QVERIFY(!paper.draw_review_notes);	//off by default
			const ExportProperties screen = diagram->applyProperties(paper);
			QVERIFY(!diagram->drawReviewNotes());
			QCOMPARE(pixels(diagram, Qt::red), 0);
			QVERIFY(pixels(diagram, Qt::blue) > 100);

			paper.draw_review_notes = true;
			diagram->applyProperties(paper);
			QVERIFY(pixels(diagram, Qt::red) > 100);
			diagram->applyProperties(screen);
			QVERIFY(diagram->drawReviewNotes());
		}

		void theOptionIsKeptInTheSettings()
		{
			ExportProperties properties;
			properties.draw_review_notes = true;
			QSettings settings;
			properties.toSettings(settings, QStringLiteral("tst_revisioncloud/"));
			ExportProperties read;
			read.fromSettings(settings, QStringLiteral("tst_revisioncloud/"));
			QVERIFY(read.draw_review_notes);
			settings.remove(QStringLiteral("tst_revisioncloud"));
			ExportProperties none;
			none.fromSettings(settings, QStringLiteral("tst_revisioncloud/"));
			QVERIFY(!none.draw_review_notes);
		}

		void theDxfLeavesOutANoteNotAsked()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *r = cloud(diagram, QRectF(40, 40, 120, 120));
			r->setRevision(QStringLiteral("REVQ"));
			RevisionCloudItem *n = cloud(diagram, QRectF(220, 40, 120, 120), RevisionCloudItem::Review);
			n->setComment(QStringLiteral("NOTEZ"));
			const auto write = [&](bool notes) {
				ExportProperties properties;
				properties.draw_review_notes = notes;
				const QString path = dir.filePath(notes ? QStringLiteral("with.dxf") : QStringLiteral("without.dxf"));
				const QSize size = DxfExport::folioSize(diagram, properties);
				DxfExport::write(diagram, size.width(), size.height(), path, properties);
				QFile file(path);
				if (!file.open(QIODevice::ReadOnly))
					return QString();
				return QString::fromUtf8(file.readAll());
			};
			const QString without = write(false);
			QVERIFY(!without.isEmpty());
			QVERIFY(without.contains(QLatin1String("REVQ")));
			QVERIFY(!without.contains(QLatin1String("NOTEZ")));
			QVERIFY(write(true).contains(QLatin1String("NOTEZ")));
		}

			//Its row is what a .qetdb saves: data and the place of its text
		void theRowOfACloudFollowsIt()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *n = cloud(diagram, QRectF(0, 0, 100, 100), RevisionCloudItem::Review);
			n->setComment(QStringLiteral("First"));
			n->setComment(QStringLiteral("Second"));
			n->setLabelOffset(n->labelOffset() + QPointF(20, 20));
			QString error;
			QSqlQuery query = project.dataBase()->newQuery(QStringLiteral(
				"SELECT name, value FROM drawing_item WHERE item_uuid = '%1' "
				"AND name IN ('cloud', 'comment', 'label_x')").arg(n->uuid().toString()), &error);
			QVERIFY2(error.isEmpty(), qPrintable(error));
			QHash<QString, QString> values;
			while (query.next()) values.insert(query.value(0).toString(), query.value(1).toString());
			QCOMPARE(values.value(QStringLiteral("cloud")), QStringLiteral("review"));
			QCOMPARE(values.value(QStringLiteral("comment")), QStringLiteral("Second"));
			QVERIFY(values.contains(QStringLiteral("label_x")));
		}

		void aCloudTravelsInEveryFormat()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
				//What the undo stack holds already: the steps below come on top
			[[maybe_unused]] const int steps = diagram->undoStack().count();
			RevisionCloudItem *n = cloud(diagram, QRectF(0, 0, 100, 100), RevisionCloudItem::Review);
			n->setComment(QStringLiteral("Check the slope"));
			n->setDone(true);
			for (const QString &name : {QStringLiteral("clouds.qet"), QStringLiteral("clouds.qetz"),
										QStringLiteral("clouds.qetdb")}) {
				project.setFilePath(dir.filePath(name));
				QVERIFY2(project.write().isOk(), qPrintable(name));
				QETProject reopened(project.filePath());
				QVERIFY2(!reopened.folios().isEmpty(), qPrintable(name));
				Diagram *folio = reopened.folios().first();
				reopened.buildFolios({folio});
				const QList<RevisionCloudItem *> clouds = cloudsOf(folio);
				QCOMPARE(clouds.size(), 1);
				QCOMPARE(clouds.first()->comment(), QStringLiteral("Check the slope"));
				QVERIFY2(clouds.first()->isDone(), qPrintable(name));
			}
		}
};

QET_APP_TEST_MAIN(tst_revisioncloud)
#include "tst_revisioncloud.moc"
