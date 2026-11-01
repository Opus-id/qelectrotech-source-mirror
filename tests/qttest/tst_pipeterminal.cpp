// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "diagramcontent.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/pipeline.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "qetresult.h"
#include "undocommand/deleteqgraphicsitemcommand.h"
#include "factory/propertieseditorfactory.h"
#include "ui/pipepropertieswidget.h"

#include <QComboBox>
#include <QLineEdit>

#include <QDomDocument>
#include <QTemporaryDir>

// A pipe line attached to the nozzles of P&ID symbols (terminals whose
// "connection" is "pipe"): it takes the place and direction of a nozzle,
// follows it when the symbol is moved or turned, one pipe line per nozzle,
// none on another kind of terminal, and is left loose -- not deleted --
// when the symbol goes.
class tst_pipeterminal : public QObject
{
	Q_OBJECT

		// A valve 40 x 40, nozzles N1 (west) and N2 (east) of the kind
		// @p connection ("" for an electrical symbol)
	static QDomElement definition(QDomDocument &doc, const QString &uuid,
				      const QString &connection)
	{
		const QString attr = connection.isEmpty()
				? QString()
				: QStringLiteral(" connection=\"%1\"").arg(connection);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">V</name></names>"
			"<description>"
			"<polygon x1=\"-10\" y1=\"-5\" x2=\"10\" y2=\"5\" x3=\"10\" y3=\"-5\" "
			"x4=\"-10\" y4=\"5\" closed=\"true\" style=\"line-style:normal;"
			"line-weight:normal;filling:none;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%2/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%2/>"
			"</description></definition>").arg(uuid, attr));
		return doc.documentElement();
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_valve, m_wired;

	Element *place(const ElementsLocation &location, const QPointF &pos)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		m_folio->addItem(element);
		element->setPos(pos);
		return element;
	}

	static Terminal *nozzle(Element *element, const QString &name)
	{
		for (Terminal *t : element->terminals())
			if (t->name() == name) return t;
		return nullptr;
	}

		// A loose pipe line on the folio
	PipeLine *pipe()
	{
		auto *p = new PipeLine({QPointF(0, 0), Qet::East}, {QPointF(50, 0), Qet::West});
		m_folio->addItem(p);
		return p;
	}

		// The valve in the collection of a project of its own
	static ElementsLocation addValve(QETProject &project)
	{
		project.embeddedElementCollection()->createDir(
					QStringLiteral("import"), QStringLiteral("pipes"), NamesList());
		QDomDocument doc;
		project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/pipes"), QStringLiteral("valve.elmt"),
					definition(doc, "61111111-1111-1111-1111-111111111111", "pipe"));
		return ElementsLocation(QStringLiteral("embed://import/pipes/valve.elmt"), &project);
	}

	static Element *placeOn(Diagram *folio, const ElementsLocation &location, const QPointF &pos)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		folio->addItem(element);
		element->setPos(pos);
		return element;
	}

	static QList<PipeLine *> pipesOf(Diagram *folio)
	{
		folio->ensureLoaded();
		QList<PipeLine *> pipes;
		for (QGraphicsItem *item : folio->items())
			if (auto *p = qgraphicsitem_cast<PipeLine *>(item)) pipes << p;
		return pipes;
	}

	static Element *elementNamed(Diagram *folio, const QUuid &uuid)
	{
		for (QGraphicsItem *item : folio->items())
			if (auto *e = qgraphicsitem_cast<Element *>(item))
				if (e->uuid() == uuid) return e;
		return nullptr;
	}

		// The <pipes> block of the first folio of the file at @p path, as text
	static QString pipesBlock(const QString &path)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) return QString();
		QDomDocument doc;
		doc.setContent(&file);
		const QDomElement folio = doc.documentElement().firstChildElement(QStringLiteral("diagram"));
		QString text;
		QTextStream stream(&text);
		folio.firstChildElement(QStringLiteral("pipes")).save(stream, 1);
		return text;
	}

		// A folio with two valves joined by a pipe line drawn from N2 to N1,
		// and a loose pipe line, saved at @p path
	static bool saveTwoValves(const QString &path, QUuid *a_uuid, QUuid *b_uuid,
							  QUuid *drawn, QUuid *loose)
	{
		QETProject project;
		const ElementsLocation valve = addValve(project);
		Diagram *folio = project.addNewDiagram();
		Element *a = placeOn(folio, valve, QPointF(100, 100));
		Element *b = placeOn(folio, valve, QPointF(300, 160));
		if (!a || !b) return false;
		b->setRotation(90);
		PipeLine *p = nozzle(a, "N2")->drawPipeTo(nozzle(b, "N1"));
		if (!p) return false;
		PipeProperties properties;
		properties.line_number = QStringLiteral("L-101");
		properties.nominal_diameter = 100;
		properties.fluid = QStringLiteral("eau brute");
		p->setProperties(properties);
		auto *l = new PipeLine({QPointF(0, 400), Qet::East}, {QPointF(200, 440), Qet::West});
		folio->addItem(l);
		*a_uuid = a->uuid();
		*b_uuid = b->uuid();
		*drawn = p->uuid();
		*loose = l->uuid();
		project.setFilePath(path);
		return project.write().isOk();
	}

		// Whether the end @p side of @p p sits on @p t, facing its way
	static bool onNozzle(PipeLine *p, PipeLine::Side side, Terminal *t)
	{
		const PipeLine::End e = side == PipeLine::Side::Start ? p->startEnd() : p->endEnd();
		const QPointF dock = p->mapFromScene(t->dockConductor());
		return qFuzzyCompare(e.point.x() + 1, dock.x() + 1)
			&& qFuzzyCompare(e.point.y() + 1, dock.y() + 1)
			&& e.direction == t->orientation();
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("pipes"), NamesList());
			QDomDocument a, b;
			m_project.embeddedElementCollection()->addElementDefinition(
						QStringLiteral("import/pipes"), QStringLiteral("valve.elmt"),
						definition(a, "61111111-1111-1111-1111-111111111111", "pipe"));
			m_project.embeddedElementCollection()->addElementDefinition(
						QStringLiteral("import/pipes"), QStringLiteral("wired.elmt"),
						definition(b, "62222222-2222-2222-2222-222222222222", ""));
			m_valve = ElementsLocation(QStringLiteral("embed://import/pipes/valve.elmt"), &m_project);
			m_wired = ElementsLocation(QStringLiteral("embed://import/pipes/wired.elmt"), &m_project);
			QVERIFY(m_valve.exist() && m_wired.exist());
			m_folio = m_project.addNewDiagram();
		}

		void bothEndsTakeTheirNozzles()
		{
			Element *a = place(m_valve, QPointF(100, 100));
			Element *b = place(m_valve, QPointF(300, 160));
			QVERIFY(a && b);
			PipeLine *p = pipe();

			QVERIFY(p->attach(PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(p->attach(PipeLine::Side::End, nozzle(b, "N1")));
			QCOMPARE(p->terminal(PipeLine::Side::Start), nozzle(a, "N2"));
			QCOMPARE(p->terminal(PipeLine::Side::End), nozzle(b, "N1"));
			QVERIFY(onNozzle(p, PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(onNozzle(p, PipeLine::Side::End, nozzle(b, "N1")));
			QCOMPARE(p->startEnd().direction, Qet::East);
			QCOMPARE(p->endEnd().direction, Qet::West);
				// the path runs from one nozzle to the other
			QCOMPARE(p->pathPoints().first(), p->startEnd().point);
			QCOMPARE(p->pathPoints().last(), p->endEnd().point);
				// the nozzles know their pipe line
			QCOMPARE(nozzle(a, "N2")->pipes(), QList<PipeLine *>{p});
			QVERIFY(!nozzle(a, "N2")->hasRoomForPipe());
			QVERIFY(nozzle(a, "N1")->hasRoomForPipe());
			delete p;
		}

		void theEndsFollowTheSymbol()
		{
			Element *a = place(m_valve, QPointF(100, 300));
			Element *b = place(m_valve, QPointF(300, 300));
			PipeLine *p = pipe();
			QVERIFY(p->attach(PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(p->attach(PipeLine::Side::End, nozzle(b, "N1")));

			b->setPos(QPointF(340, 380));
			QVERIFY(onNozzle(p, PipeLine::Side::End, nozzle(b, "N1")));
			QCOMPARE(p->pathPoints().last(), p->endEnd().point);

				// turned a quarter: N1 now points north, and so does the end
			b->setRotation(90);
			QVERIFY(onNozzle(p, PipeLine::Side::End, nozzle(b, "N1")));
			QCOMPARE(p->endEnd().direction, nozzle(b, "N1")->orientation());
			QVERIFY(p->endEnd().direction != Qet::West);

				// the start has not moved
			QVERIFY(onNozzle(p, PipeLine::Side::Start, nozzle(a, "N2")));
			delete p;
		}

		void onlyANozzleTakesAPipeLine()
		{
			Element *wired = place(m_wired, QPointF(100, 500));
			QVERIFY(wired);
			PipeLine *p = pipe();
			const PipeLine::End before = p->startEnd();
			QVERIFY(!p->canAttach(PipeLine::Side::Start, nozzle(wired, "N2")));
			QVERIFY(!p->attach(PipeLine::Side::Start, nozzle(wired, "N2")));
			QVERIFY(!p->attach(PipeLine::Side::Start, nullptr));
			QVERIFY(!p->terminal(PipeLine::Side::Start));
			QCOMPARE(p->startEnd().point, before.point);
			QVERIFY(nozzle(wired, "N2")->pipes().isEmpty());
			delete p;
		}

		void oneNozzleOnePipeLine()
		{
			Element *a = place(m_valve, QPointF(100, 700));
			PipeLine *first = pipe();
			PipeLine *second = pipe();
			QVERIFY(first->attach(PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(!second->canAttach(PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(!second->attach(PipeLine::Side::Start, nozzle(a, "N2")));
				// nor both ends of one pipe line on the same nozzle
			QVERIFY(!first->attach(PipeLine::Side::End, nozzle(a, "N2")));
				// attaching again where it already is changes nothing
			QVERIFY(first->attach(PipeLine::Side::Start, nozzle(a, "N2")));
			QCOMPARE(nozzle(a, "N2")->pipes(), QList<PipeLine *>{first});
			delete second;
			delete first;
		}

		void movingAnEndFreesTheOldNozzle()
		{
			Element *a = place(m_valve, QPointF(100, 900));
			PipeLine *p = pipe();
			QVERIFY(p->attach(PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(p->attach(PipeLine::Side::Start, nozzle(a, "N1")));
			QVERIFY(nozzle(a, "N2")->hasRoomForPipe());
			QCOMPARE(nozzle(a, "N1")->pipes(), QList<PipeLine *>{p});
			QVERIFY(onNozzle(p, PipeLine::Side::Start, nozzle(a, "N1")));

				// detached, the end stays where it was
			const QPointF where = p->startEnd().point;
			p->detach(PipeLine::Side::Start);
			QVERIFY(!p->terminal(PipeLine::Side::Start));
			QVERIFY(nozzle(a, "N1")->hasRoomForPipe());
			QCOMPARE(p->startEnd().point, where);
				// and no longer follows the symbol
			a->setPos(QPointF(160, 960));
			QCOMPARE(p->startEnd().point, where);
			delete p;
		}

		void aSymbolThatGoesLeavesItsPipeLineLoose()
		{
			Element *a = place(m_valve, QPointF(500, 100));
			Element *b = place(m_valve, QPointF(700, 100));
			PipeLine *p = pipe();
			QVERIFY(p->attach(PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(p->attach(PipeLine::Side::End, nozzle(b, "N1")));
			const QPointF where = p->startEnd().point;

			delete a;
			QVERIFY(!p->terminal(PipeLine::Side::Start));
			QCOMPARE(p->startEnd().point, where);
				// the other end is still attached and still follows
			QCOMPARE(p->terminal(PipeLine::Side::End), nozzle(b, "N1"));
			b->setPos(QPointF(720, 140));
			QVERIFY(onNozzle(p, PipeLine::Side::End, nozzle(b, "N1")));
			delete p;
		}

		void aPipeLineThatGoesFreesItsNozzles()
		{
			Element *a = place(m_valve, QPointF(500, 300));
			Element *b = place(m_valve, QPointF(700, 300));
			PipeLine *p = pipe();
			QVERIFY(p->attach(PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(p->attach(PipeLine::Side::End, nozzle(b, "N1")));
			delete p;
			QVERIFY(nozzle(a, "N2")->pipes().isEmpty());
			QVERIFY(nozzle(b, "N1")->pipes().isEmpty());
				// moving the symbols afterwards touches no dead pipe line
			a->setPos(QPointF(520, 320));
			b->setRotation(180);
			QVERIFY(nozzle(a, "N2")->hasRoomForPipe());
		}

			// ---- on the folio: selected, deleted, undone ----
		void theSelectionHoldsThePipeLines()
		{
			Element *a = place(m_valve, QPointF(900, 100));
			Element *b = place(m_valve, QPointF(1100, 100));
			PipeLine *p = pipe();
			QVERIFY(p->attach(PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(p->attach(PipeLine::Side::End, nozzle(b, "N1")));

			m_folio->clearSelection();
			p->setSelected(true);
			const DiagramContent dc(m_folio);
			QCOMPARE(dc.m_pipes, QList<PipeLine *>{p});
			QVERIFY(dc.hasDeletableItems());
			QCOMPARE(dc.count(DiagramContent::Pipes), 1);
			QVERIFY(dc.items(DiagramContent::All).contains(p));
				// never moved with the other items
			QVERIFY(!dc.items(DiagramContent::Elements | DiagramContent::Shapes
					  | DiagramContent::TextFields | DiagramContent::Images).contains(p));
			QVERIFY(dc.sentence(DiagramContent::All).contains(QStringLiteral("pipe line")));
			m_folio->clearSelection();
			delete p;
		}

		void deletingAPipeLineFreesItsNozzlesUntilUndone()
		{
			Element *a = place(m_valve, QPointF(900, 300));
			Element *b = place(m_valve, QPointF(1100, 300));
			PipeLine *p = pipe();
			QVERIFY(p->attach(PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(p->attach(PipeLine::Side::End, nozzle(b, "N1")));

			m_folio->clearSelection();
			p->setSelected(true);
			m_folio->undoStack().push(new DeleteQGraphicsItemCommand(m_folio, DiagramContent(m_folio)));
			QVERIFY(!p->scene());
				// free for another pipe line while it is gone...
			QVERIFY(nozzle(a, "N2")->hasRoomForPipe());
			QVERIFY(nozzle(b, "N1")->hasRoomForPipe());

				// ...and back on them when undone
			m_folio->undoStack().undo();
			QCOMPARE(p->scene(), m_folio);
			QCOMPARE(nozzle(a, "N2")->pipes(), QList<PipeLine *>{p});
			QCOMPARE(nozzle(b, "N1")->pipes(), QList<PipeLine *>{p});
			QVERIFY(onNozzle(p, PipeLine::Side::Start, nozzle(a, "N2")));

			m_folio->undoStack().redo();
			QVERIFY(!p->scene());
			QVERIFY(nozzle(a, "N2")->hasRoomForPipe());
			m_folio->undoStack().undo();
			QCOMPARE(p->terminal(PipeLine::Side::End), nozzle(b, "N1"));
		}

		void deletingASymbolLeavesItsPipeLineUntilUndone()
		{
			Element *a = place(m_valve, QPointF(900, 500));
			Element *b = place(m_valve, QPointF(1100, 500));
			PipeLine *p = pipe();
			QVERIFY(p->attach(PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(p->attach(PipeLine::Side::End, nozzle(b, "N1")));
			const QPointF where = p->startEnd().point;

			m_folio->clearSelection();
			a->setSelected(true);
			m_folio->undoStack().push(new DeleteQGraphicsItemCommand(m_folio, DiagramContent(m_folio)));
			QVERIFY(!a->scene());
				// the pipe line stays on the folio, loose at that end
			QCOMPARE(p->scene(), m_folio);
			QVERIFY(!p->terminal(PipeLine::Side::Start));
			QCOMPARE(p->startEnd().point, where);
			QVERIFY(nozzle(a, "N2")->pipes().isEmpty());
			QCOMPARE(p->terminal(PipeLine::Side::End), nozzle(b, "N1"));

			m_folio->undoStack().undo();
			QCOMPARE(a->scene(), m_folio);
			QCOMPARE(p->terminal(PipeLine::Side::Start), nozzle(a, "N2"));
			QCOMPARE(nozzle(a, "N2")->pipes(), QList<PipeLine *>{p});
				// and it follows the symbol again
			a->setPos(QPointF(880, 540));
			QVERIFY(onNozzle(p, PipeLine::Side::Start, nozzle(a, "N2")));
		}

			// ---- drawn from nozzle to nozzle, as the mouse does ----
		void drawingAPipeLineFromNozzleToNozzle()
		{
			Element *a = place(m_valve, QPointF(1300, 100));
			Element *b = place(m_valve, QPointF(1500, 160));
				// where the undo stack stands: the steps undone by the tests
				// before are dropped by a new one
			const int step = m_folio->undoStack().index();

			PipeLine *p = nozzle(a, "N2")->drawPipeTo(nozzle(b, "N1"));
			QVERIFY(p);
			QCOMPARE(p->scene(), m_folio);
			QCOMPARE(p->terminal(PipeLine::Side::Start), nozzle(a, "N2"));
			QCOMPARE(p->terminal(PipeLine::Side::End), nozzle(b, "N1"));
			QVERIFY(onNozzle(p, PipeLine::Side::Start, nozzle(a, "N2")));
			QVERIFY(onNozzle(p, PipeLine::Side::End, nozzle(b, "N1")));
				// one step of the undo stack, named after it
			QCOMPARE(m_folio->undoStack().index(), step + 1);
			QCOMPARE(m_folio->undoStack().undoText(), QStringLiteral("Add a pipe line"));

			m_folio->undoStack().undo();
			QVERIFY(!p->scene());
			QVERIFY(nozzle(a, "N2")->hasRoomForPipe());
			QVERIFY(nozzle(b, "N1")->hasRoomForPipe());

			m_folio->undoStack().redo();
			QCOMPARE(p->scene(), m_folio);
			QCOMPARE(nozzle(a, "N2")->pipes(), QList<PipeLine *>{p});
			QCOMPARE(nozzle(b, "N1")->pipes(), QList<PipeLine *>{p});
				// and it follows the symbols
			b->setPos(QPointF(1520, 220));
			QVERIFY(onNozzle(p, PipeLine::Side::End, nozzle(b, "N1")));
		}

		void drawingAPipeLineIsRefusedWhereItCannotGo()
		{
			Element *a = place(m_valve, QPointF(1300, 400));
			Element *b = place(m_valve, QPointF(1500, 400));
			Element *wired = place(m_wired, QPointF(1300, 600));
			QVERIFY(nozzle(a, "N2")->drawPipeTo(nozzle(b, "N1")));
			const int step = m_folio->undoStack().index();

			QVERIFY(!nozzle(a, "N1")->canBePipedTo(nullptr));
			QVERIFY(!nozzle(a, "N1")->drawPipeTo(nullptr));
				// the same nozzle at both ends
			QVERIFY(!nozzle(a, "N1")->drawPipeTo(nozzle(a, "N1")));
				// a nozzle that already has its pipe line, at either end
			QVERIFY(!nozzle(a, "N2")->drawPipeTo(nozzle(b, "N2")));
			QVERIFY(!nozzle(b, "N2")->drawPipeTo(nozzle(a, "N2")));
				// a terminal that is not a nozzle, at either end
			QVERIFY(!nozzle(a, "N1")->drawPipeTo(nozzle(wired, "N2")));
			QVERIFY(!nozzle(wired, "N2")->drawPipeTo(nozzle(a, "N1")));
				// nothing was done
			QCOMPARE(m_folio->undoStack().index(), step);
			QVERIFY(nozzle(a, "N1")->hasRoomForPipe());
				// two free nozzles still may be joined
			QVERIFY(nozzle(a, "N1")->canBePipedTo(nozzle(b, "N2")));
		}

		void aNozzleTakenMeanwhileLeavesTheEndLoose()
		{
			Element *a = place(m_valve, QPointF(900, 700));
			PipeLine *p = pipe();
			QVERIFY(p->attach(PipeLine::Side::Start, nozzle(a, "N2")));
			m_folio->clearSelection();
			p->setSelected(true);
			m_folio->undoStack().push(new DeleteQGraphicsItemCommand(m_folio, DiagramContent(m_folio)));

				// another pipe line takes the nozzle while the first is gone
			PipeLine *q = pipe();
			QVERIFY(q->attach(PipeLine::Side::Start, nozzle(a, "N2")));

			m_folio->undoStack().undo();
			QCOMPARE(p->scene(), m_folio);
			QVERIFY(!p->terminal(PipeLine::Side::Start));
			QCOMPARE(nozzle(a, "N2")->pipes(), QList<PipeLine *>{q});
			delete q;
		}

			// ---- saved with the folio, in a <pipes> block ----
		void aSavedPipeLineComesBackOnItsNozzles_data()
		{
			QTest::addColumn<QString>("suffix");
			QTest::newRow(".qet") << QStringLiteral(".qet");
			QTest::newRow(".qetz") << QStringLiteral(".qetz");
		}

		void aSavedPipeLineComesBackOnItsNozzles()
		{
			QFETCH(QString, suffix);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("pipes") + suffix);
			QUuid a_uuid, b_uuid, drawn, loose;
			QVERIFY(saveTwoValves(path, &a_uuid, &b_uuid, &drawn, &loose));

			QETProject back(path);
			QCOMPARE(back.diagrams().size(), 1);
			Diagram *folio = back.diagrams().first();
			const QList<PipeLine *> pipes = pipesOf(folio);
			QCOMPARE(pipes.size(), 2);
			Element *a = elementNamed(folio, a_uuid);
			Element *b = elementNamed(folio, b_uuid);
			QVERIFY(a && b);
			PipeLine *p = pipes.at(0)->uuid() == drawn ? pipes.at(0) : pipes.at(1);
			PipeLine *l = p == pipes.at(0) ? pipes.at(1) : pipes.at(0);
			QCOMPARE(p->uuid(), drawn);
			QCOMPARE(l->uuid(), loose);

				// back on its nozzles, with its data
			QCOMPARE(p->terminal(PipeLine::Side::Start), nozzle(a, "N2"));
			QCOMPARE(p->terminal(PipeLine::Side::End), nozzle(b, "N1"));
			QVERIFY(onNozzle(p, PipeLine::Side::End, nozzle(b, "N1")));
			QCOMPARE(p->endEnd().direction, Qet::North);
			QCOMPARE(nozzle(b, "N1")->pipes(), QList<PipeLine *>{p});
			QCOMPARE(p->properties().line_number, QStringLiteral("L-101"));
			QCOMPARE(p->properties().nominal_diameter, 100);
			QCOMPARE(p->properties().fluid, QStringLiteral("eau brute"));
				// and following them again
			b->setPos(QPointF(340, 220));
			QVERIFY(onNozzle(p, PipeLine::Side::End, nozzle(b, "N1")));

				// the loose one where it was
			QVERIFY(!l->terminal(PipeLine::Side::Start));
			QVERIFY(!l->terminal(PipeLine::Side::End));
			QCOMPARE(l->startEnd().point, QPointF(0, 400));
			QCOMPARE(l->endEnd().point, QPointF(200, 440));
		}

		void anUnchangedFolioSavesTheSamePipeLines()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString first = dir.filePath(QStringLiteral("first.qet"));
			const QString second = dir.filePath(QStringLiteral("second.qet"));
			QUuid a_uuid, b_uuid, drawn, loose;
			QVERIFY(saveTwoValves(first, &a_uuid, &b_uuid, &drawn, &loose));
			{
				QETProject back(first);
				pipesOf(back.diagrams().first());
				back.setFilePath(second);
				QVERIFY(back.write().isOk());
			}
			const QString block = pipesBlock(first);
			QVERIFY(block.contains(QStringLiteral("start_element=\"%1\"").arg(a_uuid.toString())));
			QVERIFY(block.contains(QStringLiteral("end_element=\"%1\"").arg(b_uuid.toString())));
			QCOMPARE(pipesBlock(second), block);
		}

		void aFolioWithoutPipeLinesSavesNoBlock()
		{
			QETProject project;
			const ElementsLocation valve = addValve(project);
			Diagram *folio = project.addNewDiagram();
			Element *a = placeOn(folio, valve, QPointF(100, 100));
			Element *b = placeOn(folio, valve, QPointF(300, 100));
			QVERIFY(a && b);
			QVERIFY(folio->toXml().documentElement()
					.firstChildElement(QStringLiteral("pipes")).isNull());

				// nor does a copy, even of joined symbols
			QVERIFY(nozzle(a, "N2")->drawPipeTo(nozzle(b, "N1")));
			QVERIFY(!folio->toXml().documentElement()
					.firstChildElement(QStringLiteral("pipes")).isNull());
			folio->selectAll();
			QVERIFY(folio->toXml(false, true).documentElement()
					.firstChildElement(QStringLiteral("pipes")).isNull());
		}

		void aNozzleNotFoundIsKeptForTheNextSave()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("missing.qet"));
			QUuid a_uuid, b_uuid, drawn, loose;
			QVERIFY(saveTwoValves(path, &a_uuid, &b_uuid, &drawn, &loose));

				// the start now names a symbol the folio does not have
			const QUuid gone = QUuid::createUuid();
			QFile file(path);
			QVERIFY(file.open(QIODevice::ReadOnly));
			QString text = QString::fromUtf8(file.readAll());
			file.close();
			const QString start = QStringLiteral("start_element=\"%1\"").arg(a_uuid.toString());
			QCOMPARE(text.count(start), 1);
			text.replace(start, QStringLiteral("start_element=\"%1\"").arg(gone.toString()));
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			file.write(text.toUtf8());
			file.close();

			QETProject back(path);
			Diagram *folio = back.diagrams().first();
			PipeLine *p = nullptr;
			for (PipeLine *pipe : pipesOf(folio))
				if (pipe->uuid() == drawn) p = pipe;
			QVERIFY(p);
			Element *a = elementNamed(folio, a_uuid);
			Element *b = elementNamed(folio, b_uuid);
			QVERIFY(a && b);
			QVERIFY(!p->terminal(PipeLine::Side::Start));
			QCOMPARE(p->pendingAttachment(PipeLine::Side::Start).element, gone);
			QCOMPARE(p->terminal(PipeLine::Side::End), nozzle(b, "N1"));
				// the nozzle it was on is free
			QVERIFY(nozzle(a, "N2")->hasRoomForPipe());
				// written back as read
			const QDomDocument saved = folio->toXml();
			QDomElement pipe_xml = saved.documentElement()
					.firstChildElement(QStringLiteral("pipes")).firstChildElement();
			while (!pipe_xml.isNull() && QUuid(pipe_xml.attribute(QStringLiteral("uuid"))) != drawn)
				pipe_xml = pipe_xml.nextSiblingElement();
			QVERIFY(!pipe_xml.isNull());
			QCOMPARE(QUuid(pipe_xml.attribute(QStringLiteral("start_element"))), gone);
			QCOMPARE(QUuid(pipe_xml.attribute(QStringLiteral("end_element"))), b_uuid);

				// detached, it forgets it
			p->detach(PipeLine::Side::Start);
			QVERIFY(p->pendingAttachment(PipeLine::Side::Start).isNull());
		}

			// ---- the properties panel of the pipe lines ----
		void theSelectionOfPipeLinesIsEditedInThePanel()
		{
			PipeLine *p = pipe();
			PipeLine *q = pipe();
			PropertiesEditorWidget *editor = PropertiesEditorFactory::propertiesEditor(
						QList<QGraphicsItem *>{p}, nullptr, nullptr);
			auto *panel = qobject_cast<PipePropertiesWidget *>(editor);
			QVERIFY(panel);
			QCOMPARE(panel->pipes(), QList<PipeLine *>{p});
				// another selection of pipe lines: the same panel
			QCOMPARE(PropertiesEditorFactory::propertiesEditor(
						 QList<QGraphicsItem *>{p, q}, editor, nullptr), editor);
			QCOMPARE(panel->pipes(), (QList<PipeLine *>{p, q}));
			delete editor;
			delete p;
			delete q;
		}

		void aFieldEditedChangesThePipeLineWithUndo()
		{
			PipeLine *p = pipe();
			PipePropertiesWidget panel({p});
			auto *line = panel.findChild<QLineEdit *>(QStringLiteral("line_number"));
			QVERIFY(line);
			const int step = m_folio->undoStack().index();

			line->setText(QStringLiteral(" L-101 "));
			line->setModified(true);
			emit line->editingFinished();
			QCOMPARE(p->properties().line_number, QStringLiteral("L-101"));
			QCOMPARE(m_folio->undoStack().index(), step + 1);
			QCOMPARE(m_folio->undoStack().undoText(), QStringLiteral("Change the properties of a pipe line"));

				// the field follows undo and redo
			m_folio->undoStack().undo();
			QVERIFY(p->properties().line_number.isEmpty());
			QVERIFY(line->text().isEmpty());
			m_folio->undoStack().redo();
			QCOMPARE(line->text(), QStringLiteral("L-101"));

				// the diameter: a number, empty for none
			auto *dn = panel.findChild<QLineEdit *>(QStringLiteral("nominal_diameter"));
			QVERIFY(dn);
			dn->setText(QStringLiteral("150"));
			dn->setModified(true);
			emit dn->editingFinished();
			QCOMPARE(p->properties().nominal_diameter, 150);

				// the kind of line and the fluid
			auto *kind = panel.findChild<QComboBox *>(QStringLiteral("line_kind"));
			QVERIFY(kind);
			const int auxiliary = kind->findData(int(PipeProperties::Auxiliary));
			kind->setCurrentIndex(auxiliary);
			emit kind->activated(auxiliary);
			QCOMPARE(p->properties().line_kind, PipeProperties::Auxiliary);
			auto *fluid = panel.findChild<QComboBox *>(QStringLiteral("fluid"));
			QVERIFY(fluid);
			fluid->setCurrentIndex(1);
			emit fluid->activated(1);
			QCOMPARE(p->properties().fluid, PipeProperties::defaultFluids().at(1));

				// leaving a field unchanged makes no step
			const int unchanged = m_folio->undoStack().index();
			emit line->editingFinished();
			QCOMPARE(m_folio->undoStack().index(), unchanged);
			delete p;
		}

		void severalPipeLinesChangeOnlyTheFieldEdited()
		{
			PipeLine *p = pipe();
			PipeLine *q = pipe();
			PipeProperties a, b;
			a.nominal_diameter = 100;
			b.nominal_diameter = 150;
			p->setProperties(a);
			q->setProperties(b);
			PipePropertiesWidget panel({p, q});
			auto *dn = panel.findChild<QLineEdit *>(QStringLiteral("nominal_diameter"));
			auto *material = panel.findChild<QLineEdit *>(QStringLiteral("material"));
			QVERIFY(dn && material);
				// different values: none shown
			QVERIFY(dn->text().isEmpty());
			QVERIFY(!dn->placeholderText().isEmpty());

			material->setText(QStringLiteral("PEHD"));
			material->setModified(true);
			emit material->editingFinished();
			QCOMPARE(p->properties().material, QStringLiteral("PEHD"));
			QCOMPARE(q->properties().material, QStringLiteral("PEHD"));
				// the diameters stay as they were, and one undo step for both
			QCOMPARE(p->properties().nominal_diameter, 100);
			QCOMPARE(q->properties().nominal_diameter, 150);
			m_folio->undoStack().undo();
			QVERIFY(p->properties().material.isEmpty());
			QVERIFY(q->properties().material.isEmpty());
				// a field showing none, left without a change, changes nothing
			emit dn->editingFinished();
			QCOMPARE(p->properties().nominal_diameter, 100);
			delete p;
			delete q;
		}

			// a .qetdb is written from the project database, which has no
			// place for them: refused rather than lost without a word
		void aQetdbFileIsRefusedWhileThereArePipeLines()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString qet = dir.filePath(QStringLiteral("pid.qet"));
			const QString qetdb = dir.filePath(QStringLiteral("pid.qetdb"));
			QUuid a_uuid, b_uuid, drawn, loose;
			QVERIFY(saveTwoValves(qet, &a_uuid, &b_uuid, &drawn, &loose));

			QETProject back(qet);
				// told before any folio is built
			QVERIFY(back.hasPidData());
			back.setFilePath(qetdb);
			const QETResult refused = back.write();
			QVERIFY(!refused.isOk());
			QVERIFY(refused.errorMessage().contains(QStringLiteral(".qetdb")));
			QVERIFY(!QFile::exists(qetdb));

				// without pipe lines, saved as before
			const QList<PipeLine *> pipes = pipesOf(back.diagrams().first());
			QCOMPARE(pipes.size(), 2);
			qDeleteAll(pipes);
			QVERIFY(!back.hasPidData());
			QVERIFY(back.write().isOk());
			QVERIFY(QFile::exists(qetdb));
		}
};

QET_APP_TEST_MAIN(tst_pipeterminal)
#include "tst_pipeterminal.moc"
