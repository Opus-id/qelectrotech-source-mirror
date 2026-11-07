// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidtag.h"
#include "qetgraphicsitem/dynamicelementtextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetproject.h"
#include "qetresult.h"
#include "ui/elementpropertieswidget.h"
#include "ui/elementtagwidget.h"
#include "undocommand/changeelementlocationcommand.h"

#include <QComboBox>
#include <QDomDocument>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>

// The tag of a P&ID symbol is made of fields: the functional unit, the
// identifier its symbol gives, the train and the order of the placed element,
// put together by the model of its project. Changing the type of the element
// changes the identifier and keeps the rest.
class tst_elementtag : public QObject
{
	Q_OBJECT

		// A symbol of 40 x 40 with two terminals and a text showing the label;
		// @p attributes go in <definition> (pid_family, prefix...)
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &name,
								  const QString &attributes, bool with_text = true)
	{
		const QString text = with_text
				? QStringLiteral("<dynamic_text x=\"0\" y=\"25\" z=\"100\" text_width=\"-1\" "
								 "Halignment=\"AlignHCenter\" Valignment=\"AlignTop\" frame=\"false\" "
								 "rotation=\"0\" keep_visual_rotation=\"true\" text_from=\"ElementInfo\" "
								 "uuid=\"{%1}\" font=\"Liberation Sans,9,-1,5,50,0,0,0,0,0,Regular\">"
								 "<text></text><info_name>label</info_name></dynamic_text>")
					  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
				: QString();
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" %1 "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%2}\"/><names><name lang=\"en\">%3</name></names>"
			"<description>"
			"<ellipse x=\"-5\" y=\"-5\" width=\"10\" height=\"10\" style=\"line-style:normal;"
			"line-weight:normal;filling:none;color:black\" antialias=\"false\"/>"
			"<terminal uuid=\"{%4}\" name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"<terminal uuid=\"{%5}\" name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\" medium=\"pipe\"/>"
			"%6</description></definition>")
			.arg(attributes, uuid, name,
				 QUuid::createUuid().toString(QUuid::WithoutBraces),
				 QUuid::createUuid().toString(QUuid::WithoutBraces), text));
		return doc.documentElement();
	}

	static ElementsLocation add(QETProject &project, const QString &file, const QDomElement &definition)
	{
		project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/test"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/test/") + file, &project);
	}

	static Element *place(Diagram *folio, const ElementsLocation &location)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (element) folio->addItem(element);
		return element;
	}

	static QString shownLabel(Element *element)
	{
		for (DynamicElementTextItem *text : element->dynamicTextItems()) {
			if (text->infoName() == QLatin1String("label")) return text->toPlainText();
		}
		return QStringLiteral("<no text>");
	}

	static PidCodification withModel(const QString &model)
	{
		PidCodification codification;
		codification.tag_models.insert(QStringLiteral("equipment"), model);
		codification.functional_units << PidCodification::Entry{QStringLiteral("U01"), QStringLiteral("Lifting"), QString()}
									  << PidCodification::Entry{QStringLiteral("U02"), QStringLiteral("Filters"), QString()};
		return codification;
	}

		// A project with a, b (equipments of the same family, identifiers AA
		// and BB), n (an equipment whose symbol gives no identifier) and e, f
		// (electrical symbols with a text, same family)
	QETProject *m_project = nullptr;
	Diagram *m_folio = nullptr;
	ElementsLocation m_a, m_b, m_n, m_e, m_f;

	private slots:
		void init()
		{
			m_project = new QETProject();
			m_project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("test"), NamesList());
			QDomDocument da, db, dn, de, df;
			m_a = add(*m_project, "a.elmt", definition(da, "11111111-1111-1111-1111-111111111111", "A",
							QStringLiteral("pid_family=\"equipment\" prefix=\"AA\"")));
			m_b = add(*m_project, "b.elmt", definition(db, "22222222-2222-2222-2222-222222222222", "B",
							QStringLiteral("pid_family=\"equipment\" prefix=\"BB\"")));
			m_n = add(*m_project, "n.elmt", definition(dn, "33333333-3333-3333-3333-333333333333", "N",
							QStringLiteral("pid_family=\"equipment\"")));
			m_e = add(*m_project, "e.elmt", definition(de, "44444444-4444-4444-4444-444444444444", "E", QString()));
			m_f = add(*m_project, "f.elmt", definition(df, "55555555-5555-5555-5555-555555555555", "F", QString()));
			QVERIFY(m_a.exist() && m_b.exist() && m_n.exist() && m_e.exist() && m_f.exist());
			m_folio = m_project->addNewDiagram();
		}

		void cleanup()
		{
			delete m_project;
			m_project = nullptr;
		}

		void aModelMakesTheLabel_data()
		{
			QTest::addColumn<QString>("model");
			QTest::addColumn<QString>("unit");
			QTest::addColumn<QString>("id");
			QTest::addColumn<QString>("train");
			QTest::addColumn<QString>("order");
			QTest::addColumn<QString>("label");
			const QString usual = QStringLiteral("{unit} {id} {train}{order:2}");
			QTest::newRow("every field") << usual << "U01" << "PC" << "1" << "5" << "U01 PC 105";
			QTest::newRow("order of two digits") << usual << "U01" << "PC" << "2" << "16" << "U01 PC 216";
			QTest::newRow("no train") << usual << "U01" << "PC" << "0" << "1" << "U01 PC 001";
			QTest::newRow("no unit, no gap") << usual << "" << "PC" << "1" << "5" << "PC 105";
			QTest::newRow("no identifier") << usual << "U01" << "" << "1" << "5" << "U01 105";
			QTest::newRow("an order that is no number") << usual << "U01" << "PC" << "1" << "A" << "U01 PC 1A";
			QTest::newRow("three lines") << QStringLiteral("{unit}\\n{id}\\n{train}{order:2}")
										 << "U01" << "LIT" << "1" << "5" << "U01\nLIT\n105";
			QTest::newRow("other separators") << QStringLiteral("{id}-{unit}-{order:3}")
											  << "U01" << "P" << "" << "7" << "P-U01-007";
			QTest::newRow("unknown word kept") << QStringLiteral("{unit} {line}")
											   << "U01" << "" << "" << "" << "U01 {line}";
		}

		void aModelMakesTheLabel()
		{
			QFETCH(QString, model);
			QFETCH(QString, unit);
			QFETCH(QString, id);
			QFETCH(QString, train);
			QFETCH(QString, order);
			QFETCH(QString, label);
			QCOMPARE(PidTag::label(model, unit, id, train, order), label);
		}

		void theSymbolGivesTheIdentifier()
		{
			Element *a = place(m_folio, m_a);
			Element *n = place(m_folio, m_n);
			Element *e = place(m_folio, m_e);
			QVERIFY(a && n && e);
			QCOMPARE(a->getPrefix(), QStringLiteral("AA"));
			QCOMPARE(n->getPrefix(), QString());
			QCOMPARE(e->getPrefix(), QString());
		}

		void theCodificationKeepsUnitsAndModels()
		{
			PidCodification codification = withModel(QStringLiteral("{unit}\\n{id}\\n{train}{order:2}"));
			QDomDocument doc;
			QDomElement holder = doc.createElement(QStringLiteral("project"));
			holder.appendChild(codification.toXml(doc));
			PidCodification back;
			back.fromXml(holder);
			QCOMPARE(back, codification);

			int skipped = -1;
			const PidCodification read = PidCodification::fromCsv(QStringLiteral(
				"kind;code;name;family\n"
				"unit;U01;Lifting\n"
				"unité_fonctionnelle;U02;Filters\n"
				"tag_model;equipment;{unit} {id} {train}{order:2}\n"
				"modele_repere;instrument;\"{unit}\\n{id};{order}\"\n"
				"unit;U01;Again\n"), &skipped);
			QCOMPARE(skipped, 1);
			QCOMPARE(read.functional_units.size(), 2);
			QCOMPARE(read.functional_units.at(1).code, QStringLiteral("U02"));
			QCOMPARE(read.tag_models.value(QStringLiteral("equipment")),
					 QStringLiteral("{unit} {id} {train}{order:2}"));
			QCOMPARE(read.tag_models.value(QStringLiteral("instrument")),
					 QStringLiteral("{unit}\\n{id};{order}"));
				// the models are a convention of the company, kept in the palette;
				// the units belong to the project
			const QString palette = read.paletteToCsv();
			QVERIFY(palette.contains(QStringLiteral("tag_model;equipment;{unit} {id} {train}{order:2}")));
			QVERIFY(!palette.contains(QStringLiteral("U01")));
		}

		void theTabMakesTheLabelAndUndoes()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			Element *a = place(m_folio, m_a);
			QVERIFY(a);
			QVERIFY(ElementTagWidget::isWanted(a));
			QVERIFY(!ElementTagWidget::isWanted(place(m_folio, m_e)));

			ElementTagWidget tab(a);
			QCOMPARE(tab.unitField()->count(), 3);   // nothing, U01, U02
			tab.unitField()->setCurrentIndex(1);
			tab.trainField()->setText(QStringLiteral("1"));
			tab.orderField()->setText(QStringLiteral("5"));
			tab.apply();
			QCOMPARE(a->elementInformations().value(QStringLiteral("label")).toString(), QStringLiteral("U01 AA 105"));
			QCOMPARE(a->elementInformations().value(PidTag::unitKey()).toString(), QStringLiteral("U01"));
			QCOMPARE(shownLabel(a), QStringLiteral("U01 AA 105"));

			m_folio->undoStack().undo();
			QVERIFY(a->elementInformations().value(QStringLiteral("label")).toString().isEmpty());
			QVERIFY(!PidTag::hasFields(a->elementInformations()));
			QCOMPARE(tab.orderField()->text(), QString());   // the tab follows
			m_folio->undoStack().redo();
			QCOMPARE(shownLabel(a), QStringLiteral("U01 AA 105"));
			QCOMPARE(tab.orderField()->text(), QStringLiteral("5"));
		}

		void withoutAModelTheLabelIsTypedByHand()
		{
			Element *a = place(m_folio, m_a);
			DiagramContext information = a->elementInformations();
			information.addValue(QStringLiteral("label"), QStringLiteral("typed"));
			information.addValue(PidTag::orderKey(), QStringLiteral("5"));
			a->setElementInformations(information);
			a->updatePidLabel();
			QCOMPARE(a->elementInformations().value(QStringLiteral("label")).toString(), QStringLiteral("typed"));
		}

		void changingTheTypeTakesItsIdentifier()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			Element *a = place(m_folio, m_a);
			DiagramContext information = a->elementInformations();
			information.addValue(PidTag::unitKey(), QStringLiteral("U02"));
			information.addValue(PidTag::trainKey(), QStringLiteral("2"));
			information.addValue(PidTag::orderKey(), QStringLiteral("3"));
			a->setElementInformations(a->withPidLabel(information));
			QCOMPARE(shownLabel(a), QStringLiteral("U02 AA 203"));

				// a P&ID symbol keeps its texts for another P&ID symbol
			QVERIFY(a->canChangeLocationTo(m_b));
			QVERIFY(!a->canChangeLocationTo(m_e));
			m_folio->undoStack().push(new ChangeElementLocationCommand(a, m_b));
			QCOMPARE(a->getPrefix(), QStringLiteral("BB"));
			QCOMPARE(shownLabel(a), QStringLiteral("U02 BB 203"));
			QCOMPARE(a->dynamicTextItems().size(), 1);
				// a symbol that gives no identifier keeps the one there is
			m_folio->undoStack().push(new ChangeElementLocationCommand(a, m_n));
			QCOMPARE(a->getPrefix(), QStringLiteral("BB"));

			m_folio->undoStack().undo();
			m_folio->undoStack().undo();
			QCOMPARE(a->getPrefix(), QStringLiteral("AA"));
			QCOMPARE(shownLabel(a), QStringLiteral("U02 AA 203"));
			m_folio->undoStack().redo();
			QCOMPARE(shownLabel(a), QStringLiteral("U02 BB 203"));

				// an electrical symbol with a text is still refused, as before
			Element *e = place(m_folio, m_e);
			QVERIFY(!e->canChangeLocationTo(m_f));
		}

		void theLabelsFollowTheModel()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			Element *a = place(m_folio, m_a);
			DiagramContext information = a->elementInformations();
			information.addValue(PidTag::unitKey(), QStringLiteral("U01"));
			information.addValue(PidTag::orderKey(), QStringLiteral("4"));
			a->setElementInformations(a->withPidLabel(information));
			QCOMPARE(shownLabel(a), QStringLiteral("U01 AA 04"));

			m_project->setPidCodification(withModel(QStringLiteral("{id}-{order}")));
			QCOMPARE(shownLabel(a), QStringLiteral("AA-4"));
		}

		void aLabelHeldTwiceIsTold()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			Element *first = place(m_folio, m_a);
			Element *second = place(m_folio, m_a);
			second->setPos(200, 0);
			for (Element *element : {first, second}) {
				DiagramContext information = element->elementInformations();
				information.addValue(PidTag::unitKey(), QStringLiteral("U01"));
				information.addValue(PidTag::trainKey(), QStringLiteral("1"));
				information.addValue(PidTag::orderKey(), QStringLiteral("1"));
				element->setElementInformations(element->withPidLabel(information));
			}
			ElementTagWidget tab(second);
			QVERIFY(!tab.duplicateShown()->text().isEmpty());
			tab.orderField()->setText(QStringLiteral("2"));
			tab.apply();
			QVERIFY(tab.duplicateShown()->text().isEmpty());
		}

		void theTagIsSavedWithTheProject_data()
		{
			QTest::addColumn<QString>("suffix");
			QTest::newRow(".qet") << QStringLiteral(".qet");
			QTest::newRow(".qetz") << QStringLiteral(".qetz");
			QTest::newRow(".qetdb") << QStringLiteral(".qetdb");
		}

		void theTagIsSavedWithTheProject()
		{
			QFETCH(QString, suffix);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("tags") + suffix);
			QUuid uuid;
			{
				m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
				Element *a = place(m_folio, m_a);
				DiagramContext information = a->elementInformations();
				information.addValue(PidTag::unitKey(), QStringLiteral("U02"));
				information.addValue(PidTag::trainKey(), QStringLiteral("1"));
				information.addValue(PidTag::orderKey(), QStringLiteral("7"));
				a->setElementInformations(a->withPidLabel(information));
				m_folio->undoStack().push(new ChangeElementLocationCommand(a, m_b));
				uuid = a->uuid();
				m_project->setFilePath(path);
				QVERIFY(m_project->write().isOk());
			}

			QETProject back(path);
			QCOMPARE(back.diagrams().size(), 1);
			back.diagrams().first()->ensureLoaded();
			Element *a = nullptr;
			for (QGraphicsItem *item : back.diagrams().first()->items()) {
				if (auto *element = qgraphicsitem_cast<Element *>(item)) {
					if (element->uuid() == uuid) a = element;
				}
			}
			QVERIFY(a);
			QCOMPARE(a->getPrefix(), QStringLiteral("BB"));
			QCOMPARE(a->elementInformations().value(PidTag::unitKey()).toString(), QStringLiteral("U02"));
			QCOMPARE(a->elementInformations().value(PidTag::orderKey()).toString(), QStringLiteral("7"));
			QCOMPARE(shownLabel(a), QStringLiteral("U02 BB 107"));
			QCOMPARE(back.pidCodification().functional_units.size(), 2);
			QCOMPARE(back.pidCodification().tag_models.value(QStringLiteral("equipment")),
					 QStringLiteral("{unit} {id} {train}{order:2}"));
		}

			// The panel shows the Tag tab for a P&ID equipment only
		void thePanelHasATagTab()
		{
			Element *a = place(m_folio, m_a);
			Element *e = place(m_folio, m_e);
			ElementPropertiesWidget panel(a);
			QVERIFY(panel.findChild<ElementTagWidget *>());
			ElementPropertiesWidget other(e);
			QVERIFY(!other.findChild<ElementTagWidget *>());
		}
};

QET_APP_TEST_MAIN(tst_elementtag)
#include "tst_elementtag.moc"
