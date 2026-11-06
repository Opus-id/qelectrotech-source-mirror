// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "ui/elementoptionswidget.h"
#include "ui/elementpropertieswidget.h"
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
				      int x2 = 10, int width = 40, const QString &medium = QString())
	{
		const QString attr = medium.isEmpty()
				? QString()
				: QStringLiteral(" medium=\"%1\"").arg(medium);
		const QString xml = QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"%1\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%2}\"/><names><name lang=\"en\">%3</name></names>"
			"<description>%4"
			"<terminal uuid=\"{%5}\" name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%8/>"
			"<terminal uuid=\"{%6}\" name=\"N2\" x=\"%7\" y=\"0\" orientation=\"e\" type=\"Generic\"%8/>"
			"</description></definition>")
			.arg(width).arg(uuid, name, shape, t1, t2).arg(x2).arg(attr);
		doc.setContent(xml);
		return doc.documentElement();
	}

	// @p definition, offering an option @p name
	static QDomElement withOption(QDomDocument &doc, const QString &name, QDomElement definition)
	{
		QDomDocument options;
		options.setContent(QStringLiteral(
			"<options><option name=\"%1\" default=\"one\">"
			"<names><name lang=\"en\">%1</name></names>"
			"<value name=\"one\"><names><name lang=\"en\">One</name></names></value>"
			"<value name=\"two\"><names><name lang=\"en\">Two</name></names></value>"
			"</option></options>").arg(name));
		definition.insertBefore(doc.importNode(options.documentElement(), true),
					definition.firstChildElement(QStringLiteral("description")));
		return definition;
	}

	// A project whose embedded collection holds a, b (same family), c (a
	// terminal elsewhere), d (wider) and e (the same terminals, but
	// nozzles), and a folio with an element of a
	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_a, m_b, m_c, m_d, m_e;
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
			QDomDocument de;
			m_e = add("e.elmt", definition(de, "55555555-5555-5555-5555-555555555555", "E",
				"<ellipse x=\"-5\" y=\"-5\" width=\"10\" height=\"10\" style=\"line-style:normal;"
				"line-weight:normal;filling:none;color:black\" antialias=\"false\"/>",
				"e0000000-0000-0000-0000-000000000001", "e0000000-0000-0000-0000-000000000002",
				10, 40, QStringLiteral("pipe")));
			QVERIFY(m_a.exist() && m_b.exist() && m_c.exist() && m_d.exist() && m_e.exist());

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
			QVERIFY(!m_element->canChangeLocationTo(m_e));   // nozzles, not wire terminals
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

			// QGraphicsView repaints an item only within boundingRect(): a part
			// of the new symbol beyond its frame must not be cut off
		void aDrawingBeyondTheFrameIsNotCutOff()
		{
			QDomDocument doc;
			const ElementsLocation wide = add("f.elmt", definition(doc, "66666666-6666-6666-6666-666666666666", "F",
				"<ellipse x=\"40\" y=\"-5\" width=\"20\" height=\"10\" style=\"line-style:normal;"
				"line-weight:normal;filling:none;color:black\" antialias=\"false\"/>",
				"f0000000-0000-0000-0000-000000000001", "f0000000-0000-0000-0000-000000000002"));

				// the swap above ended on a
			const QRectF before = m_element->boundingRect();
			QVERIFY(before.right() < 60);
			QVERIFY(m_element->changeLocation(wide));
			QVERIFY(m_element->boundingRect().right() >= 60);
			QVERIFY(m_element->changeLocation(m_a));
			QCOMPARE(m_element->boundingRect(), before);
		}

			// The same when an option shows that part
		void anOptionDrawingBeyondTheFrameIsNotCutOff()
		{
			const QString style = QStringLiteral(
				"style=\"line-style:normal;line-weight:normal;filling:none;color:black\" "
				"antialias=\"false\"");
			QDomDocument doc;
			doc.setContent(QStringLiteral(
				"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
				"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
				"<uuid uuid=\"{77777777-7777-7777-7777-777777777777}\"/>"
				"<names><name lang=\"en\">G</name></names>"
				"<options><option name=\"reach\" default=\"short\">"
				"<names><name lang=\"en\">Reach</name></names>"
				"<value name=\"short\"><names><name lang=\"en\">Short</name></names></value>"
				"<value name=\"long\"><names><name lang=\"en\">Long</name></names></value>"
				"</option></options>"
				"<description>"
				"<ellipse x=\"-5\" y=\"-5\" width=\"10\" height=\"10\" %1/>"
				"<ellipse x=\"40\" y=\"-5\" width=\"20\" height=\"10\" show-if=\"reach=long\" %1/>"
				"</description></definition>").arg(style));
			const ElementsLocation location = add("g.elmt", doc.documentElement());

			int state = 0;
			Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
			QVERIFY(element);
			m_folio->addItem(element);

			QVERIFY(element->boundingRect().right() < 60);
			element->setOptionChoices({{QStringLiteral("reach"), QStringLiteral("long")}});
			QVERIFY(element->boundingRect().right() >= 60);
			element->setOptionChoices({});
			QVERIFY(element->boundingRect().right() < 60);
		}

			// A free end draws nothing at all, and it can be swapped to
		void aSymbolThatDrawsNothingIsAccepted()
		{
			QDomDocument doc;
			const ElementsLocation empty = add("h.elmt", definition(doc, "88888888-8888-8888-8888-888888888888", "H",
				QString(),
				"80000000-0000-0000-0000-000000000001", "80000000-0000-0000-0000-000000000002"));
			QVERIFY(m_element->canChangeLocationTo(empty));
			QVERIFY(m_element->changeLocation(empty));
			QCOMPARE(m_element->location(), empty);
			QVERIFY(m_element->changeLocation(m_a));
		}

			// The panel left open on the element follows its new symbol: its
			// options, and the Options tab that comes and goes with them
		void theOpenPanelFollowsTheNewSymbol()
		{
			QDomDocument first_doc, second_doc;
			const ElementsLocation first = add("i.elmt", withOption(first_doc, "first",
				definition(first_doc, "99999999-9999-9999-9999-999999999991", "I",
					QStringLiteral("<ellipse x=\"-5\" y=\"-5\" width=\"10\" height=\"10\" "
						"style=\"line-style:normal;line-weight:normal;filling:none;color:black\" "
						"antialias=\"false\"/>"),
					"90000000-0000-0000-0000-000000000001", "90000000-0000-0000-0000-000000000002")));
			const ElementsLocation second = add("j.elmt", withOption(second_doc, "second",
				definition(second_doc, "99999999-9999-9999-9999-999999999992", "J",
					QStringLiteral("<ellipse x=\"-4\" y=\"-4\" width=\"8\" height=\"8\" "
						"style=\"line-style:normal;line-weight:normal;filling:none;color:black\" "
						"antialias=\"false\"/>"),
					"91000000-0000-0000-0000-000000000001", "91000000-0000-0000-0000-000000000002")));

			int state = 0;
			Element *element = ElementFactory::Instance()->createElement(first, nullptr, &state);
			QVERIFY(element);
			m_folio->addItem(element);

			ElementPropertiesWidget panel(element);
			auto *options = panel.findChild<ElementOptionsWidget *>();
			QVERIFY(options);
			QVERIFY(options->comboFor(QStringLiteral("first")));

			QVERIFY(element->changeLocation(second));
			options = panel.findChild<ElementOptionsWidget *>();
			QVERIFY(options);
			QVERIFY(!options->comboFor(QStringLiteral("first")));
			QVERIFY(options->comboFor(QStringLiteral("second")));

			QVERIFY(element->changeLocation(m_a));
			QVERIFY(!panel.findChild<ElementOptionsWidget *>());
		}
};

QET_APP_TEST_MAIN(tst_elementchangetype)
#include "tst_elementchangetype.moc"
