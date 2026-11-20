// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "leaderline.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/conductortextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "undocommand/resetconductortextscommand.h"

#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>
#include <QUndoStack>

// The text of a conductor moved away from it by hand (step 7b-quater): a
// thin leader line ties it to its conductor, from the distance the project
// gives (none by default, so that a diagram looks and saves as before), ended
// as the project says; and a command puts the texts back in their place,
// leaving the path as it is. For wires and pipes alike.
class tst_textleader : public QObject
{
	Q_OBJECT

		// Two terminals N1 and N2 of the medium @p medium ("" for none)
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &medium)
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

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_electrical;
	qreal m_next_y = 0;

	Element *place(const ElementsLocation &location, const QPointF &pos)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		m_folio->addItem(element);
		element->setPos(pos);
		return element;
	}

		// A horizontal wire with the text "X12", shown
	Conductor *wire()
	{
		m_next_y += 200;
		Element *a = place(m_electrical, QPointF(0, m_next_y));
		Element *b = place(m_electrical, QPointF(300, m_next_y));
		if (!a || !b) return nullptr;
		auto *conductor = new Conductor(a->terminals().at(1), b->terminals().at(0));
		m_folio->addItem(conductor);
		ConductorProperties p = conductor->properties();
		p.text = QStringLiteral("X12");
		p.m_formula.clear();
		p.m_show_text = true;
		conductor->setProperties(p);
		return conductor;
	}

		// The text of @p conductor moved by hand @p down under its line
	static void moveText(Conductor *conductor, qreal down)
	{
		ConductorTextItem *text = conductor->textItem();
		const QPointF start = conductor->mapFromScene(conductor->terminal1->dockConductor());
		text->setPos(start + QPointF(100, down));
		text->forceMovedByUser(true);
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("leader"), NamesList());
			QDomDocument a;
			m_project.embeddedElementCollection()->addElementDefinition(
						QStringLiteral("import/leader"), QStringLiteral("electrical.elmt"),
						definition(a, "e1111111-1111-1111-1111-111111111111", ""));
			m_electrical = ElementsLocation(QStringLiteral("embed://import/leader/electrical.elmt"), &m_project);
			QVERIFY(m_electrical.exist());
			m_folio = m_project.addNewDiagram();
		}

		void cleanup()
		{
			m_project.setTextLeaders(0, LeaderLine::End::None);
		}

			// From the edge of the text nearest to the line to the line, when
			// the gap is more than the distance; a dot or an arrow at its end
		void thePathTiesTheTextToTheLine()
		{
			const QList<QPointF> line{QPointF(0, 0), QPointF(200, 0)};
			const QRectF text(80, 40, 40, 10);
			QVERIFY(LeaderLine::path(text, line, 0, LeaderLine::End::None, 4).isEmpty());
			QVERIFY(LeaderLine::path(text, line, 50, LeaderLine::End::None, 4).isEmpty());

			const QPainterPath plain = LeaderLine::path(text, line, 20, LeaderLine::End::None, 4);
			QCOMPARE(plain.elementCount(), 2);
			QCOMPARE(QPointF(plain.elementAt(0)), QPointF(100, 40));
			QCOMPARE(QPointF(plain.elementAt(1)), QPointF(100, 0));

			const QPainterPath dot = LeaderLine::path(text, line, 20, LeaderLine::End::Dot, 4);
			QVERIFY(dot.elementCount() > 2);
			QCOMPARE(dot.boundingRect().top(), -2.0);
			const QPainterPath arrow = LeaderLine::path(text, line, 20, LeaderLine::End::Arrow, 4);
			QVERIFY(arrow.elementCount() > 2);
			QVERIFY(arrow.boundingRect().top() >= 0);

				// a text across the line: no leader
			QVERIFY(LeaderLine::path(QRectF(80, -5, 40, 10), line, 1, LeaderLine::End::None, 4).isEmpty());
			QVERIFY(LeaderLine::fromString(LeaderLine::toString(LeaderLine::End::Dot)) == LeaderLine::End::Dot);
			QVERIFY(LeaderLine::fromString(QStringLiteral("unknown")) == LeaderLine::End::None);
		}

			// None by default; drawn from the distance of the project, for a
			// text moved by hand only; it follows the setting and the text
		void aMovedTextIsTiedToItsWire()
		{
			Conductor *conductor = wire();
			QVERIFY(conductor);
			moveText(conductor, 60);
			QVERIFY(conductor->leaderPath().isEmpty());

			m_project.setTextLeaders(5, LeaderLine::End::None);
			QVERIFY(!conductor->leaderPath().isEmpty());
				// it ends on the wire
			const QPainterPath leader = conductor->leaderPath();
			const QPointF end = leader.elementAt(leader.elementCount() - 1);
			const QPointF dock = conductor->mapFromScene(conductor->terminal1->dockConductor());
			QCOMPARE(end.y(), dock.y());

				// near the wire again: none; further than 5 mm: one
			moveText(conductor, 10);
			QVERIFY(conductor->leaderPath().isEmpty());
			moveText(conductor, 60);
			QVERIFY(!conductor->leaderPath().isEmpty());
				// a text placed by its wire never has one
			conductor->textItem()->forceMovedByUser(false);
			QVERIFY(conductor->leaderPath().isEmpty());
		}

			// Written only when on; read back
		void theSettingIsSavedOnlyWhenOn()
		{
			auto has = [this]() {
				return !m_project.toXml().documentElement()
						.firstChildElement(QStringLiteral("text_leaders")).isNull();
			};
			QVERIFY(!has());
			m_project.setTextLeaders(5, LeaderLine::End::Dot);
			QVERIFY(has());

			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("leaders.qet"));
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			file.write(m_project.toXml().toByteArray());
			file.close();
			QETProject read(path);
			QCOMPARE(read.textLeaderDistance(), 5.0);
			QVERIFY(read.textLeaderEnd() == LeaderLine::End::Dot);

			m_project.setTextLeaders(0, LeaderLine::End::Dot);
			QVERIFY(!has());
		}

			// The texts moved go back in their place, the path is kept; one
			// undo step puts them back where they were
		void theTextsGoBackInTheirPlace()
		{
			Conductor *moved = wire();
			Conductor *placed = wire();
			QVERIFY(moved && placed);
			const QPointF origin = moved->textItem()->pos();
			moveText(moved, 60);
			const QPointF away = moved->textItem()->pos();
			QVERIFY(away != origin);
			QVERIFY(ResetConductorTextsCommand::canReset({moved, placed}));
			QVERIFY(!ResetConductorTextsCommand::canReset({placed}));

			m_project.setTextLeaders(5, LeaderLine::End::None);
			QUndoStack stack;
			auto *command = new ResetConductorTextsCommand({moved, placed});
			QVERIFY(command->isValid());
			stack.push(command);
			QVERIFY(!moved->textItem()->wasMovedByUser());
			QCOMPARE(moved->textItem()->pos(), origin);
			QVERIFY(moved->leaderPath().isEmpty());

			stack.undo();
			QVERIFY(moved->textItem()->wasMovedByUser());
			QCOMPARE(moved->textItem()->pos(), away);
			QVERIFY(!moved->leaderPath().isEmpty());
		}
};

QET_APP_TEST_MAIN(tst_textleader)
#include "tst_textleader.moc"
