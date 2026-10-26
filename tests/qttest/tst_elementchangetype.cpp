// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "undocommand/changeelementlocationcommand.h"

#include <QDomDocument>

// The symbol of a placed element can be swapped for another one of its family
// (same size, same terminals): the element stays where it is with its wires,
// and the wires still find their terminals when the project is read back.
class tst_elementchangetype : public QObject
{
	Q_OBJECT

	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &name,
				      const QString &shape, const QString &t1, const QString &t2,
				      int x2 = 10, int width = 40)
	{
		const QString xml = QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"%1\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%2}\"/><names><name lang=\"en\">%3</name></names>"
			"<description>%4"
			"<terminal uuid=\"{%5}\" name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"/>"
			"<terminal uuid=\"{%6}\" name=\"N2\" x=\"%7\" y=\"0\" orientation=\"e\" type=\"Generic\"/>"
			"</description></definition>")
			.arg(width).arg(uuid, name, shape, t1, t2).arg(x2);
		doc.setContent(xml);
		return doc.documentElement();
	}

	// A project whose embedded collection holds a, b (same family), c (a
	// terminal elsewhere) and d (wider), and a folio with an element of a
	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_a, m_b, m_c, m_d;
	Element *m_element = nullptr;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/test"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/test/") + file, &m_project);
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("test"), NamesList());
			QDomDocument da, db, dc, dd;
			m_a = add("a.elmt", definition(da, "11111111-1111-1111-1111-111111111111", "A",
				"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
				"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
				"filling:none;color:black\" antialias=\"false\"/>",
				"a0000000-0000-0000-0000-000000000001", "a0000000-0000-0000-0000-000000000002"));
			m_b = add("b.elmt", definition(db, "22222222-2222-2222-2222-222222222222", "B",
				"<ellipse x=\"-5\" y=\"-5\" width=\"10\" height=\"10\" style=\"line-style:normal;"
				"line-weight:normal;filling:none;color:black\" antialias=\"false\"/>",
				"b0000000-0000-0000-0000-000000000001", "b0000000-0000-0000-0000-000000000002"));
			m_c = add("c.elmt", definition(dc, "33333333-3333-3333-3333-333333333333", "C",
				"<line x1=\"-10\" y1=\"0\" x2=\"5\" y2=\"0\" end1=\"none\" end2=\"none\" "
				"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
				"filling:none;color:black\" antialias=\"false\"/>",
				"c0000000-0000-0000-0000-000000000001", "c0000000-0000-0000-0000-000000000002",
				5));
			m_d = add("d.elmt", definition(dd, "44444444-4444-4444-4444-444444444444", "D",
				"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
				"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
				"filling:none;color:black\" antialias=\"false\"/>",
				"d0000000-0000-0000-0000-000000000001", "d0000000-0000-0000-0000-000000000002",
				10, 60));
			QVERIFY(m_a.exist() && m_b.exist() && m_c.exist() && m_d.exist());

			m_folio = m_project.addNewDiagram();
			int state = 0;
			m_element = ElementFactory::Instance()->createElement(m_a, nullptr, &state);
			QVERIFY(m_element);
			QCOMPARE(state, 0);
			m_folio->addItem(m_element);
		}

		void onlyTheSameFamilyIsOffered()
		{
			QVERIFY(m_element->canChangeLocationTo(m_b));
			QVERIFY(!m_element->canChangeLocationTo(m_c));   // a terminal elsewhere
			QVERIFY(!m_element->canChangeLocationTo(m_d));   // another size
			QVERIFY(!m_element->canChangeLocationTo(m_a));   // itself
			QVERIFY(!m_element->canChangeLocationTo(ElementsLocation()));
		}

		void theSwapKeepsPlaceAndTerminalsAndUndoes()
		{
			m_element->setPos(QPointF(100, 50));
			const QString name_before = m_element->name();

			auto *undo = new ChangeElementLocationCommand(m_element, m_b);
			m_folio->undoStack().push(undo);
			QCOMPARE(m_element->location(), m_b);
			QCOMPARE(m_element->name(), QStringLiteral("B"));
			QVERIFY(m_element->name() != name_before);
			QCOMPARE(m_element->pos(), QPointF(100, 50));
			QCOMPARE(m_element->terminals().size(), 2);

			m_folio->undoStack().undo();
			QCOMPARE(m_element->location(), m_a);
			QCOMPARE(m_element->name(), name_before);
			m_folio->undoStack().redo();
			QCOMPARE(m_element->location(), m_b);
		}

		void theTerminalsTakeTheUuidsOfTheNewDefinition()
		{
				// the swap above ended on b
			QStringList uuids;
			for (const Terminal *t : m_element->terminals())
				uuids << t->uuid().toString(QUuid::WithoutBraces);
			uuids.sort();
			QCOMPARE(uuids, (QStringList{"b0000000-0000-0000-0000-000000000001",
						     "b0000000-0000-0000-0000-000000000002"}));
			m_folio->undoStack().undo();
			uuids.clear();
			for (const Terminal *t : m_element->terminals())
				uuids << t->uuid().toString(QUuid::WithoutBraces);
			uuids.sort();
			QCOMPARE(uuids, (QStringList{"a0000000-0000-0000-0000-000000000001",
						     "a0000000-0000-0000-0000-000000000002"}));
			m_folio->undoStack().redo();
		}

		void theWiresStayAttached()
		{
			int state = 0;
			Element *other = ElementFactory::Instance()->createElement(m_a, nullptr, &state);
			QVERIFY(other);
			m_folio->addItem(other);
			other->setPos(QPointF(300, 50));

			Terminal *from = nullptr, *to = nullptr;
			for (Terminal *t : m_element->terminals())
				if (t->name() == QLatin1String("N2")) from = t;
			for (Terminal *t : other->terminals())
				if (t->name() == QLatin1String("N1")) to = t;
			QVERIFY(from && to);
			auto *wire = new Conductor(from, to);
			m_folio->addItem(wire);
			QCOMPARE(from->conductorsCount(), 1);

			QVERIFY(m_element->canChangeLocationTo(m_a));
			QVERIFY(m_element->changeLocation(m_a));
			QCOMPARE(from->conductorsCount(), 1);
			QCOMPARE(wire->terminal1, from);
			QCOMPARE(from->uuid().toString(QUuid::WithoutBraces),
				 QStringLiteral("a0000000-0000-0000-0000-000000000002"));
		}
};

QET_APP_TEST_MAIN(tst_elementchangetype)
#include "tst_elementchangetype.moc"
