// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "undocommand/mirrorselectioncommand.h"

#include <QDomDocument>

// The mirror of a symbol on its folio (step 7g). A P&ID symbol has its
// hotspot on the axis of its line: it is mirrored about it, as it is rotated,
// so that a valve with a handwheel turned upside down stays on its line and
// its pipes stay straight. An electrical symbol keeps its centre where it was,
// as upstream does (#1335).
class tst_pidmirror : public QObject
{
	Q_OBJECT

		// A valve: a body on the line between N1 (west) and N2 (east), of the
		// medium @p medium ("" for none), and a handwheel above it, so that
		// the centre of its drawing is above its hotspot
	static QDomElement valve(QDomDocument &doc, const QString &uuid, const QString &medium)
	{
		const QString attr = medium.isEmpty()
				? QString()
				: QStringLiteral(" medium=\"%1\"").arg(medium);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"60\" hotspot_x=\"20\" hotspot_y=\"40\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">V</name></names>"
			"<description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"<line x1=\"0\" y1=\"0\" x2=\"0\" y2=\"-30\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%2/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%2/>"
			"</description></definition>").arg(uuid, attr));
		return doc.documentElement();
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_pid, m_electrical;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/mirror"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/mirror/") + file, &m_project);
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

	static QList<QPointF> docks(const Element *element)
	{
		QList<QPointF> points;
		for (const Terminal *terminal : element->terminals()) {
			points << terminal->dockConductor();
		}
		return points;
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("mirror"), NamesList());
			QDomDocument a, b;
			m_pid = add("pid.elmt", valve(a, "d1111111-1111-1111-1111-111111111111", "pipe"));
			m_electrical = add("electrical.elmt", valve(b, "d2222222-2222-2222-2222-222222222222", ""));
			QVERIFY(m_pid.exist() && m_electrical.exist());
			m_folio = m_project.addNewDiagram();
		}

			// Upside down, a P&ID valve stays on its line: same hotspot, same
			// terminals, its handwheel below; a second mirror puts it back
		void aPidSymbolIsMirroredAboutItsHotspot()
		{
			m_next_y += 200;
			Element *element = place(m_pid, QPointF(200, m_next_y));
			QVERIFY(element);
			QVERIFY(MirrorSelectionCommand::aboutHotspot(element));
			const QPointF pos = element->pos();
			const QList<QPointF> before = docks(element);
			QVERIFY(element->sceneBoundingRect().center().y() < pos.y());

			MirrorSelectionCommand mirror({element}, Qt::Vertical);
			mirror.redo();
			QCOMPARE(element->pos(), pos);
			QCOMPARE(docks(element), before);
			QVERIFY(element->sceneBoundingRect().center().y() > pos.y());

			mirror.undo();
			QCOMPARE(element->pos(), pos);
			QVERIFY(element->sceneBoundingRect().center().y() < pos.y());
		}

			// The pipes of a valve mirrored stay straight
		void itsPipesStayStraight()
		{
			m_next_y += 200;
			Element *left = place(m_pid, QPointF(0, m_next_y));
			Element *middle = place(m_pid, QPointF(200, m_next_y));
			Element *right = place(m_pid, QPointF(400, m_next_y));
			QVERIFY(left && middle && right);
			auto *in = new Conductor(left->terminals().at(1), middle->terminals().at(0));
			auto *out = new Conductor(middle->terminals().at(1), right->terminals().at(0));
			m_folio->addItem(in);
			m_folio->addItem(out);
			auto level = [](const Conductor *pipe) {
				return pipe->terminal1->dockConductor().y() == pipe->terminal2->dockConductor().y();
			};
			QVERIFY(level(in) && level(out));

			MirrorSelectionCommand mirror({middle}, Qt::Vertical);
			mirror.redo();
			QVERIFY(level(in) && level(out));
		}

			// An electrical symbol keeps its centre where it was, as before
		void anElectricalSymbolKeepsItsCentre()
		{
			m_next_y += 200;
			Element *element = place(m_electrical, QPointF(200, m_next_y));
			QVERIFY(element);
			QVERIFY(!MirrorSelectionCommand::aboutHotspot(element));
			const QPointF centre = element->sceneBoundingRect().center();
			const QPointF pos = element->pos();

			MirrorSelectionCommand mirror({element}, Qt::Vertical);
			mirror.redo();
			QVERIFY(element->pos() != pos);
			QCOMPARE(element->sceneBoundingRect().center(), centre);
		}
};

QET_APP_TEST_MAIN(tst_pidmirror)
#include "tst_pidmirror.moc"
