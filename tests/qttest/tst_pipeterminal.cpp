// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/pipeline.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>

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
};

QET_APP_TEST_MAIN(tst_pipeterminal)
#include "tst_pipeterminal.moc"
