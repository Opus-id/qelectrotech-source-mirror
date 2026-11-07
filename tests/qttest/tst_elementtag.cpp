// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "diagramcommands.h"
#include "diagramcontent.h"
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
#include <QPushButton>
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

		// Give @p element a tag, its label made by the model of its project
	static Element *tag(Element *element, const QString &unit, const QString &train, const QString &order)
	{
		DiagramContext information = element->elementInformations();
		information.addValue(PidTag::unitKey(), unit);
		information.addValue(PidTag::trainKey(), train);
		information.addValue(PidTag::orderKey(), order);
		element->setElementInformations(element->withPidLabel(information));
		return element;
	}

		// The copy of @p elements, as Ctrl+C makes it
	QDomDocument copyOf(const QList<Element *> &elements)
	{
		m_folio->clearSelection();
		for (Element *element : elements) element->setSelected(true);
		const QDomDocument document = m_folio->toXml(false, true);
		m_folio->clearSelection();
		return document;
	}

		// Paste @p document at @p position, as Ctrl+V does
	QList<Element *> pasteDocument(QDomDocument document, const QPointF &position)
	{
		DiagramContent pasted;
		m_folio->fromXml(document, position, false, &pasted);
		m_folio->undoStack().push(new PasteDiagramCommand(m_folio, pasted));
		return pasted.m_elements;
	}

	QList<Element *> paste(const QList<Element *> &elements, const QPointF &position)
	{
		return pasteDocument(copyOf(elements), position);
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

				// R3: the order of a tag is counted by unit, identifier and train,
			// one more than the highest held; no counter is kept
		void theNextOrderCountsPerUnitIdentifierAndTrain()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			Element *a3 = tag(place(m_folio, m_a), "U01", "1", "3");
			Element *a7 = tag(place(m_folio, m_a), "U01", "1", "07");
			tag(place(m_folio, m_a), "U01", "2", "9");
			tag(place(m_folio, m_b), "U01", "1", "20");
			tag(place(m_folio, m_a), "U02", "1", "50");
			tag(place(m_folio, m_a), "U01", "1", "A");   // no number: not counted
			tag(place(m_folio, m_e), "U01", "1", "40");  // no identifier: another key

			QCOMPARE(PidTag::nextOrder(m_project, "U01", "AA", "1"), 8);
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "AA", "01"), 8);
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "AA", "2"), 10);
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "BB", "1"), 21);
			QCOMPARE(PidTag::nextOrder(m_project, "U02", "AA", "1"), 51);
			QCOMPARE(PidTag::nextOrder(m_project, "U03", "AA", "1"), 1);
				// the element numbered is left out
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "AA", "1", a7), 4);

				// a number freed below the highest is not given again
			m_folio->removeItem(a3);
			delete a3;
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "AA", "1"), 8);
		}

			// The tab gives an empty order the next one, which follows the unit
			// and the train until it is typed; an order there is kept
		void theTabNumbersAnEmptyOrder()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			tag(place(m_folio, m_a), "U01", "1", "4");
			Element *a = place(m_folio, m_a);
			a->setPos(200, 0);

			ElementTagWidget tab(a);
			tab.setLiveEdit(true);
			QVERIFY(tab.nextButton()->isEnabled());
			tab.trainField()->setText(QStringLiteral("1"));
			emit tab.trainField()->editingFinished();
			QCOMPARE(tab.orderField()->text(), QString());   // no unit yet
			tab.unitField()->setCurrentIndex(1);             // U01
			emit tab.unitField()->activated(1);
			QCOMPARE(tab.orderField()->text(), QStringLiteral("5"));
			QCOMPARE(shownLabel(a), QStringLiteral("U01 AA 105"));
				// one step to undo
			m_folio->undoStack().undo();
			QVERIFY(a->elementInformations().value(PidTag::orderKey()).toString().isEmpty());
			m_folio->undoStack().redo();
			QCOMPARE(shownLabel(a), QStringLiteral("U01 AA 105"));

				// untouched, it follows the train
			tab.trainField()->setText(QStringLiteral("2"));
			emit tab.trainField()->editingFinished();
			QCOMPARE(tab.orderField()->text(), QStringLiteral("1"));
			QCOMPARE(shownLabel(a), QStringLiteral("U01 AA 201"));

				// typed, it stays
			tab.orderField()->setText(QStringLiteral("9"));
			emit tab.orderField()->textEdited(QStringLiteral("9"));
			emit tab.orderField()->editingFinished();
			tab.unitField()->setCurrentIndex(2);             // U02
			emit tab.unitField()->activated(2);
			QCOMPARE(tab.orderField()->text(), QStringLiteral("9"));
			QCOMPARE(shownLabel(a), QStringLiteral("U02 AA 209"));

				// "Next number" gives it on demand
			tab.nextButton()->click();
			QCOMPARE(tab.orderField()->text(), QStringLiteral("1"));
			QCOMPARE(shownLabel(a), QStringLiteral("U02 AA 201"));

				// an element numbered already keeps its order
			Element *kept = tag(place(m_folio, m_a), "U01", "1", "4");
			ElementTagWidget other(kept);
			other.setLiveEdit(true);
			other.unitField()->setCurrentIndex(2);
			emit other.unitField()->activated(2);
			QCOMPARE(other.orderField()->text(), QStringLiteral("4"));

				// without a model, nothing is numbered
			m_project->setPidCodification(PidCodification());
			Element *plain = place(m_folio, m_a);
			ElementTagWidget none(plain);
			QVERIFY(!none.nextButton()->isEnabled());
		}

			// A model that shows the train: a numbered tag always has one, 0
			// when there is none, and no train counts as train 0
		void aNumberedTagHasATrain()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			QVERIFY(PidTag::showsTrain(QStringLiteral("{unit} {id} {train}{order:2}")));
			QVERIFY(PidTag::showsTrain(QStringLiteral("{train:1}-{order}")));
			QVERIFY(!PidTag::showsTrain(QStringLiteral("{id}-{unit}-{order:3}")));

				// a tag numbered before the rule, with no train
			tag(place(m_folio, m_a), "U01", "", "5");
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "AA", "0"), 6);
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "AA", ""), 6);

				// numbered by the tab: the train gets 0
			Element *a = place(m_folio, m_a);
			ElementTagWidget tab(a);
			tab.setLiveEdit(true);
			tab.unitField()->setCurrentIndex(1);
			emit tab.unitField()->activated(1);
			QCOMPARE(tab.orderField()->text(), QStringLiteral("6"));
			QCOMPARE(tab.trainField()->text(), QStringLiteral("0"));
			QCOMPARE(a->elementInformations().value(PidTag::trainKey()).toString(), QStringLiteral("0"));
			QCOMPARE(shownLabel(a), QStringLiteral("U01 AA 006"));

				// an order typed with no train: 0 too
			Element *typed = place(m_folio, m_a);
			ElementTagWidget other(typed);
			other.setLiveEdit(true);
			other.orderField()->setText(QStringLiteral("12"));
			emit other.orderField()->textEdited(QStringLiteral("12"));
			emit other.orderField()->editingFinished();
			QCOMPARE(typed->elementInformations().value(PidTag::trainKey()).toString(), QStringLiteral("0"));
			QCOMPARE(shownLabel(typed), QStringLiteral("AA 012"));

				// no order, no train
			Element *blank = place(m_folio, m_a);
			ElementTagWidget empty(blank);
			empty.setLiveEdit(true);
			empty.trainField()->setText(QString());
			emit empty.trainField()->editingFinished();
			QVERIFY(blank->elementInformations().value(PidTag::trainKey()).toString().isEmpty());

				// a model without the train adds none
			m_project->setPidCodification(withModel(QStringLiteral("{id}-{unit}-{order:3}")));
			Element *plain = place(m_folio, m_a);
			ElementTagWidget third(plain);
			third.setLiveEdit(true);
			third.unitField()->setCurrentIndex(2);
			emit third.unitField()->activated(2);
			QCOMPARE(third.orderField()->text(), QStringLiteral("1"));
			QVERIFY(third.trainField()->text().isEmpty());
			QCOMPARE(shownLabel(plain), QStringLiteral("AA-U02-001"));
		}

			// A copy gets the next order of its unit, identifier and train, in
			// folio, left to right order; a cut and paste keeps its own
		void aPastedTagGetsTheNextOrder()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			Element *first = tag(place(m_folio, m_a), "U01", "1", "1");
			Element *second = tag(place(m_folio, m_a), "U01", "1", "2");
			second->setPos(100, 0);
			Element *other_unit = tag(place(m_folio, m_a), "U02", "1", "1");
			other_unit->setPos(200, 0);
			tag(place(m_folio, m_a), "U01", "1", "6")->setPos(0, 300);

			const QList<Element *> copied = paste({second, first, other_unit}, QPointF(0, 500));
			QCOMPARE(copied.size(), 3);
			QStringList labels;
			for (Element *element : copied) labels << shownLabel(element);
			labels.sort();
				// first (left) gets 7, second 8; U02 has its own counter
			QCOMPARE(labels, QStringList({"U01 AA 107", "U01 AA 108", "U02 AA 102"}));
			Element *left = copied.first();
			for (Element *element : copied) {
				if (element->pos().x() < left->pos().x()) left = element;
			}
			QCOMPARE(shownLabel(left), QStringLiteral("U01 AA 107"));

				// undone with the paste
			m_folio->undoStack().undo();
			QVERIFY(!copied.first()->scene());
			m_folio->undoStack().redo();
			QCOMPARE(shownLabel(left), QStringLiteral("U01 AA 107"));

				// cut and paste: the original gone, the copy keeps its tag
			Element *alone = tag(place(m_folio, m_b), "U03", "0", "4");
			alone->setPos(400, 0);
			QDomDocument document = copyOf({alone});
			m_folio->removeItem(alone);
			delete alone;
			const QList<Element *> moved = pasteDocument(document, QPointF(400, 500));
			QCOMPARE(moved.size(), 1);
			QCOMPARE(shownLabel(moved.first()), QStringLiteral("U03 BB 004"));
		}

			// The folios not built yet (a .qetdb opens with them) count, read
			// from the database, and are not built for it
		void theNextOrderCountsTheFoliosNotBuilt()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("orders.qetdb"));
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			tag(place(m_folio, m_a), "U01", "1", "3");
			Diagram *second = m_project->addNewDiagram();
			tag(place(second, m_b), "U01", "1", "12");
			m_project->setFilePath(path);
			QVERIFY(m_project->write().isOk());

			QETProject back(path);
			QCOMPARE(back.folios().size(), 2);
			QVERIFY(back.unloadedFolioCount() > 0);
			const int unloaded = back.unloadedFolioCount();
			QCOMPARE(PidTag::nextOrder(&back, "U01", "AA", "1"), 4);
			QCOMPARE(PidTag::nextOrder(&back, "U01", "BB", "1"), 13);
			QCOMPARE(back.unloadedFolioCount(), unloaded);   // nothing built
			back.folios().at(0)->ensureLoaded();
			back.folios().at(1)->ensureLoaded();
			QCOMPARE(PidTag::nextOrder(&back, "U01", "AA", "1"), 4);
			QCOMPARE(PidTag::nextOrder(&back, "U01", "BB", "1"), 13);
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
