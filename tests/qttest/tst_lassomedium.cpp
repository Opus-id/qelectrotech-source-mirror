// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "utils/conductorcreator.h"

#include <QDomDocument>
#include <QPolygonF>

// The lasso of the folio (Ctrl + drag, then "Connect the selected
// terminals") gives the electrical terminals it holds a potential, as it
// always did. A potential is for wires only: two nozzles get one pipe, more
// than two none (which goes with which cannot be guessed), and terminals of
// two media are never joined.
class tst_lassomedium : public QObject
{
	Q_OBJECT

		// A symbol 40 x 40 with two terminals N1 (west) and N2 (east), both
		// with the medium @p medium ("" for none)
	static QDomElement definition(QDomDocument &doc, const QString &uuid,
				      const QString &medium)
	{
		const QString attr = medium.isEmpty()
				? QString()
				: QStringLiteral(" medium=\"%1\"").arg(medium);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%2/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%2/>"
			"</description></definition>").arg(uuid, attr));
		return doc.documentElement();
	}

	QETProject *m_project = nullptr;
	Diagram *m_folio = nullptr;
	ElementsLocation m_electrical, m_pipe;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project->embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/lasso"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/lasso/") + file, m_project);
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

		// Two symbols on a row of their own, 200 apart
	QPair<Element *, Element *> row(const ElementsLocation &first,
					const ElementsLocation &second)
	{
		m_next_y += 200;
		return {place(first, QPointF(0, m_next_y)), place(second, QPointF(200, m_next_y))};
	}

	Conductor *draw(Terminal *from, Terminal *to)
	{
		auto *conductor = new Conductor(from, to);
		m_folio->addItem(conductor);
		return conductor;
	}

	private slots:
		void initTestCase()
		{
			m_project = new QETProject();
			m_project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("lasso"), NamesList());
			QDomDocument a, b;
			m_electrical = add("electrical.elmt",
					   definition(a, "81111111-1111-1111-1111-111111111111", ""));
			m_pipe = add("pipe.elmt",
				     definition(b, "82222222-2222-2222-2222-222222222222", "pipe"));
			QVERIFY(m_electrical.exist() && m_pipe.exist());
			m_folio = m_project->addNewDiagram();
		}

		void theLassoJoinsTwoNozzlesWithOnePipe()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
				// drawn around the two nozzles only, as on the folio
			QPolygonF lasso;
			const QRectF r = QRectF(from->dockConductor(), to->dockConductor())
					.normalized().adjusted(-5, -5, 5, 5);
			lasso << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
			QCOMPARE(ConductorCreator::create(m_folio, lasso), QString());
			QCOMPARE(from->conductors().size(), 1);
			QCOMPARE(to->conductors().size(), 1);
			QCOMPARE(int(from->conductors().first()->medium()), int(TerminalData::Pipe));
			QCOMPARE(ends.first->terminals().at(0)->conductors().size(), 0);
		}

		void theLassoJoinsNoMoreThanTwoNozzles()
		{
			QList<Terminal *> nozzles;
			for (int i = 0 ; i < 3 ; ++i) {
				const auto ends = row(m_pipe, m_pipe);
				QVERIFY(ends.first && ends.second);
				nozzles << ends.first->terminals().at(1) << ends.second->terminals().at(0);
			}
			const int steps = m_folio->undoStack().count();
			const QString message = ConductorCreator::create(m_folio, nozzles);
			QVERIFY(!message.isEmpty());
			for (Terminal *t : nozzles) {
				QCOMPARE(t->conductors().size(), 0);
			}
			QCOMPARE(m_folio->undoStack().count(), steps);
		}

		void theLassoDoesNotJoinANozzleAlreadyConnected()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
			Element *third = place(m_pipe, QPointF(400, m_next_y));
			QVERIFY(third);
			draw(third->terminals().at(0), to);
			QVERIFY(!ConductorCreator::create(m_folio, QList<Terminal *>{from, to}).isEmpty());
			QCOMPARE(from->conductors().size(), 0);
			QCOMPARE(to->conductors().size(), 1);
		}

		void theLassoNeverJoinsTwoMedia()
		{
			const auto ends = row(m_pipe, m_electrical);
			QVERIFY(ends.first && ends.second);
			Terminal *nozzle = ends.first->terminals().at(1);
			Terminal *electrical = ends.second->terminals().at(0);
			QVERIFY(!ConductorCreator::create(m_folio, QList<Terminal *>{nozzle, electrical}).isEmpty());
			QCOMPARE(nozzle->conductors().size(), 0);
			QCOMPARE(electrical->conductors().size(), 0);
		}

			// Electrical terminals: their potential, as always, and the
			// nozzles of the same lasso their pipe, apart
		void theLassoStillGivesWiresTheirPotential()
		{
			QList<Terminal *> wires;
			const auto first = row(m_electrical, m_electrical);
			QVERIFY(first.first && first.second);
			wires << first.first->terminals().at(1) << first.second->terminals().at(0)
			      << first.second->terminals().at(1);
				// all electrical: nothing to say, one wire to each from the hub
			QCOMPARE(ConductorCreator::create(m_folio, wires), QString());
			QCOMPARE(wires.at(0)->conductors().size(), 2);
			QCOMPARE(int(wires.at(0)->conductors().first()->medium()),
				 int(TerminalData::Electrical));

			const auto wires2 = row(m_electrical, m_electrical);
			const auto pipes = row(m_pipe, m_pipe);
			QVERIFY(wires2.first && wires2.second && pipes.first && pipes.second);
			Terminal *w1 = wires2.first->terminals().at(1);
			Terminal *w2 = wires2.second->terminals().at(0);
			Terminal *p1 = pipes.first->terminals().at(1);
			Terminal *p2 = pipes.second->terminals().at(0);
			QCOMPARE(ConductorCreator::create(m_folio, QList<Terminal *>{p1, w1, p2, w2}), QString());
			QCOMPARE(w1->conductors().size(), 1);
			QCOMPARE(p1->conductors().size(), 1);
			QVERIFY(w1->isLinkedTo(w2));
			QVERIFY(p1->isLinkedTo(p2));
			QCOMPARE(int(p1->conductors().first()->medium()), int(TerminalData::Pipe));
			QCOMPARE(int(w1->conductors().first()->medium()), int(TerminalData::Electrical));
		}
};

QET_APP_TEST_MAIN(tst_lassomedium)
#include "tst_lassomedium.moc"
