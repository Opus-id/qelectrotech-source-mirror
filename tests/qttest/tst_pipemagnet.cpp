// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QGraphicsSceneMouseEvent>

// Connecting without zooming in. A pipe drawn from a nozzle and released
// near another nozzle, rather than exactly on it, is caught by it; wires
// keep the behaviour they always had.
class tst_pipemagnet : public QObject
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
					QStringLiteral("import/magnet"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/magnet/") + file, m_project);
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

		// The mouse pressed on @p from, then released at @p at: what the
		// folio does when a conductor is drawn by hand
	void dragFrom(Terminal *from, const QPointF &at)
	{
			//On the nozzle: its room also holds the mark of its connection
		const QPointF start = from->mapToScene(from->shape().boundingRect().center());
		QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
		press.setScenePos(start);
		press.setButton(Qt::LeftButton);
		press.setButtons(Qt::LeftButton);
		QCoreApplication::sendEvent(m_folio, &press);

		QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
		move.setScenePos(at);
		move.setButtons(Qt::LeftButton);
		QCoreApplication::sendEvent(m_folio, &move);

		QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
		release.setScenePos(at);
		release.setButton(Qt::LeftButton);
		release.setButtons(Qt::NoButton);
		QCoreApplication::sendEvent(m_folio, &release);
	}

		// A point near @p t but not on it: 10 units below its docking point
	QPointF nearBelow(Terminal *t)
	{
		return t->dockConductor() + QPointF(0, 10);
	}

	bool onATerminal(const QPointF &pos)
	{
		return qgraphicsitem_cast<Terminal *>(m_folio->itemAt(pos, QTransform())) != nullptr;
	}

	private slots:
		void initTestCase()
		{
			m_project = new QETProject();
			m_project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("magnet"), NamesList());
			QDomDocument a, b;
			m_electrical = add("electrical.elmt",
					   definition(a, "81111111-1111-1111-1111-111111111111", ""));
			m_pipe = add("pipe.elmt",
				     definition(b, "82222222-2222-2222-2222-222222222222", "pipe"));
			QVERIFY(m_electrical.exist() && m_pipe.exist());
			m_folio = m_project->addNewDiagram();
		}

		void aNozzleCatchesAPipeReleasedNearIt()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
			QCOMPARE(from->magnetTarget(nearBelow(to), Terminal::magnetRadius), to);
				// too far
			QCOMPARE(from->magnetTarget(to->dockConductor() + QPointF(0, Terminal::magnetRadius + 5),
						    Terminal::magnetRadius), nullptr);
		}

		void theNearestNozzleWins()
		{
			m_next_y += 200;
			Element *start = place(m_pipe, QPointF(0, m_next_y));
				// two nozzles 16 apart, facing each other: N2 of a, N1 of b
			Element *a = place(m_pipe, QPointF(200, m_next_y));
			Element *b = place(m_pipe, QPointF(236, m_next_y));
			QVERIFY(start && a && b);
			Terminal *from = start->terminals().at(1);
			Terminal *a_east = a->terminals().at(1);
			Terminal *b_west = b->terminals().at(0);
			const QPointF nearer_a = a_east->dockConductor() + QPointF(3, 8);
			QCOMPARE(from->magnetTarget(nearer_a, Terminal::magnetRadius), a_east);
			const QPointF nearer_b = b_west->dockConductor() + QPointF(-3, 8);
			QCOMPARE(from->magnetTarget(nearer_b, Terminal::magnetRadius), b_west);
		}

		void aConnectedNozzleOrOneOfItsOwnSymbolDoesNotCatch()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
				// already connected to a third symbol
			Element *third = place(m_pipe, QPointF(-200, m_next_y));
			QVERIFY(third);
			draw(third->terminals().at(1), to);
			QCOMPARE(from->magnetTarget(nearBelow(to), Terminal::magnetRadius), nullptr);
				// the other nozzle of its own symbol
			Terminal *own = ends.first->terminals().at(0);
			QCOMPARE(from->magnetTarget(nearBelow(own), Terminal::magnetRadius), nullptr);
		}

		void wiresAndTerminalsOfAnotherMediumAreNotCaught()
		{
				// from a wire terminal: nothing changes for wires
			const auto wires = row(m_electrical, m_electrical);
			QVERIFY(wires.first && wires.second);
			Terminal *wire_to = wires.second->terminals().at(0);
			QCOMPARE(wires.first->terminals().at(1)->magnetTarget(nearBelow(wire_to),
									     Terminal::magnetRadius), nullptr);
				// from a nozzle, near a wire terminal
			const auto mixed = row(m_pipe, m_electrical);
			QVERIFY(mixed.first && mixed.second);
			Terminal *electrical = mixed.second->terminals().at(0);
			QCOMPARE(mixed.first->terminals().at(1)->magnetTarget(nearBelow(electrical),
									      Terminal::magnetRadius), nullptr);
		}

			// The whole gesture, through the events of the folio
		void aPipeDrawnByHandIsCaughtByANozzleNearby()
		{
			const auto ends = row(m_pipe, m_pipe);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
			const QPointF at = nearBelow(to);
			QVERIFY2(!onATerminal(at), "the release point must be off the nozzle");
			dragFrom(from, at);
			QCOMPARE(from->conductors().size(), 1);
			QCOMPARE(to->conductors().size(), 1);
			QCOMPARE(int(from->conductors().first()->medium()), int(TerminalData::Pipe));
				// one undo step takes it away
			m_folio->undoStack().undo();
			QCOMPARE(from->conductors().size(), 0);
		}

		void aWireDrawnByHandNearATerminalIsNotCaught()
		{
			const auto ends = row(m_electrical, m_electrical);
			QVERIFY(ends.first && ends.second);
			Terminal *from = ends.first->terminals().at(1);
			Terminal *to = ends.second->terminals().at(0);
			const QPointF at = nearBelow(to);
			QVERIFY(!onATerminal(at));
			dragFrom(from, at);
			QCOMPARE(from->conductors().size(), 0);
				// exactly on it, as always
			dragFrom(from, to->sceneBoundingRect().center());
			QCOMPARE(from->conductors().size(), 1);
		}
};

QET_APP_TEST_MAIN(tst_pipemagnet)
#include "tst_pipemagnet.moc"
