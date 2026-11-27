// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "diagramcommands.h"
#include "diagramcontent.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidtag.h"
#include "placecopy.h"
#include "placerename.h"
#include "qetgraphicsitem/dynamicelementtextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/structureboxitem.h"
#include "qetinformation.h"
#include "qetproject.h"
#include "qetresult.h"
#include "scripting/qetscriptapi.h"
#include "titleblockproperties.h"
#include "ui/elementcommentwidget.h"
#include "ui/elementinfopartwidget.h"
#include "ui/elementpropertieswidget.h"
#include "ui/elementtagwidget.h"
#include "ui/multielementinfowidget.h"
#include "undocommand/changeelementinformationcommand.h"
#include "undocommand/changeelementlocationcommand.h"

#include <QAbstractItemModel>
#include <QComboBox>
#include <QCompleter>
#include <QDomDocument>
#include <QFile>
#include <QUndoStack>
#include <QLabel>
#include <QFocusEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>

// The tag of a P&ID symbol is made of fields: the functional unit, the
// identifier its symbol gives, the train and the order of the placed element,
// put together by the model of its project. Changing the type of the element
// changes the identifier and keeps the rest.
class tst_elementtag : public QObject
{
	Q_OBJECT

		// A symbol of 40 x 40 with two terminals and a text showing the label
		// (or one text for each information of @p infos: label_line_1...);
		// @p attributes go in <definition> (pid_family, prefix...)
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &name,
								  const QString &attributes, bool with_text = true,
								  const QStringList &infos = {QStringLiteral("label")})
	{
		QString text;
		for (const QString &info : infos) {
			if (!with_text) break;
			text += QStringLiteral("<dynamic_text x=\"0\" y=\"25\" z=\"100\" text_width=\"-1\" "
								   "Halignment=\"AlignHCenter\" Valignment=\"AlignTop\" frame=\"false\" "
								   "rotation=\"0\" keep_visual_rotation=\"true\" text_from=\"ElementInfo\" "
								   "uuid=\"{%1}\" font=\"Liberation Sans,9,-1,5,50,0,0,0,0,0,Regular\">"
								   "<text></text><info_name>%2</info_name></dynamic_text>")
					.arg(QUuid::createUuid().toString(QUuid::WithoutBraces), info);
		}
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

	static QString shownLabel(Element *element, const QString &info = QStringLiteral("label"))
	{
		for (DynamicElementTextItem *text : element->dynamicTextItems()) {
			if (text->infoName() == info) return text->toPlainText();
		}
		return QStringLiteral("<no text>");
	}

	static DynamicElementTextItem *textOf(Element *element, const QString &info)
	{
		for (DynamicElementTextItem *text : element->dynamicTextItems()) {
			if (text->infoName() == info) return text;
		}
		return nullptr;
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
		// i: an instrument bubble, whose symbol gives no identifier
	ElementsLocation m_i;

		// A project numbering the loops of its instruments in series (the
		// default) or in @p parallel, with the model "{unit} {id}
		// {train}{order:2}", letters PT and LIC offered
	static PidCodification instruments(bool parallel = false)
	{
		PidCodification codification = withModel(QStringLiteral("{unit} {id} {train}{order:2}"));
		codification.tag_models.insert(QStringLiteral("instrument"), QStringLiteral("{unit} {id} {train}{order:2}"));
		codification.instrument_letters
				<< PidCodification::Entry{QStringLiteral("LIC"), QStringLiteral("Level indicating controller"), QString()}
				<< PidCodification::Entry{QStringLiteral("PT"), QStringLiteral("Pressure transmitter"), QString()};
		codification.parallel_numbering = parallel;
		return codification;
	}

		// Give the instrument @p element the letters @p letters and a tag
	static Element *instrument(Element *element, const QString &letters,
							   const QString &unit, const QString &train, const QString &order)
	{
		DiagramContext information = element->elementInformations();
		information.addValue(PidTag::lettersKey(), letters);
		element->setElementInformations(information);
		return tag(element, unit, train, order);
	}

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
			QDomDocument di;
			m_i = add(*m_project, "i.elmt", definition(di, "66666666-6666-6666-6666-666666666666", "I",
							QStringLiteral("pid_family=\"instrument\"")));
			QVERIFY(m_a.exist() && m_b.exist() && m_n.exist() && m_e.exist() && m_f.exist() && m_i.exist());
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
				// the models are a convention of the company, kept in the
				// presentation; the units belong to the project
			const QString palette = read.presentationToCsv();
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
				// a symbol that gives no identifier: none, as when it is
				// placed -- never the identifier of the old type
			m_folio->undoStack().push(new ChangeElementLocationCommand(a, m_n));
			QCOMPARE(a->getPrefix(), QString());
			QCOMPARE(shownLabel(a), QStringLiteral("U02 203"));
			m_folio->undoStack().undo();
			QCOMPARE(a->getPrefix(), QStringLiteral("BB"));
			QCOMPARE(shownLabel(a), QStringLiteral("U02 BB 203"));
			m_folio->undoStack().redo();
			QCOMPARE(a->getPrefix(), QString());

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

			// 9e-0: the functional unit is the plant of QElectroTech, the
			// symbol's own; any change of it (Informations tab, script)
			// writes the label again, undone in the same step
		void theUnitIsThePlant()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			QCOMPARE(PidTag::unitKey(), QETInformation::ELMT_PLANT);
			Element *a = tag(place(m_folio, m_a), "U01", "1", "5");
			QCOMPARE(a->elementInformations().value(QETInformation::ELMT_PLANT).toString(), QStringLiteral("U01"));
			QVERIFY(!a->elementInformations().contains(QStringLiteral("pid_unit")));
			QCOMPARE(shownLabel(a), QStringLiteral("U01 AA 105"));

			DiagramContext information = a->elementInformations();
			information.addValue(QETInformation::ELMT_PLANT, QStringLiteral("U02"));
			m_folio->undoStack().push(new ChangeElementInformationCommand(a, a->elementInformations(), information));
			QCOMPARE(shownLabel(a), QStringLiteral("U02 AA 105"));
			QCOMPARE(a->elementInformations().value(QStringLiteral("label")).toString(), QStringLiteral("U02 AA 105"));
			m_folio->undoStack().undo();
			QCOMPARE(shownLabel(a), QStringLiteral("U01 AA 105"));
			m_folio->undoStack().redo();
			QCOMPARE(shownLabel(a), QStringLiteral("U02 AA 105"));

				// an electrical symbol with a plant: its label stays its own
				// and its order is never counted with the P&ID tags
			Element *e = place(m_folio, m_e);
			DiagramContext wire_side = e->elementInformations();
			wire_side.addValue(QStringLiteral("label"), QStringLiteral("K1"));
			wire_side.addValue(QETInformation::ELMT_PLANT, QStringLiteral("U02"));
			wire_side.addValue(PidTag::orderKey(), QStringLiteral("40"));
			e->setElementInformations(wire_side);
			QCOMPARE(shownLabel(e), QStringLiteral("K1"));
			QCOMPARE(PidTag::nextOrder(m_project, "U02", "", "0"), 1);
			QCOMPARE(PidTag::nextOrder(m_project, "U02", "AA", "1"), 6);
		}

			// The panel of several symbols gives them a unit: each label is
			// written again, all in one undo step
		void severalSymbolsTakeAUnitInOneStep()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			QList<Element *> elements{tag(place(m_folio, m_a), "", "1", "1"),
									  tag(place(m_folio, m_b), "", "1", "2"),
									  tag(place(m_folio, m_a), "", "1", "3")};
			elements.at(1)->setPos(100, 0);
			elements.at(2)->setPos(200, 0);
			QCOMPARE(shownLabel(elements.at(0)), QStringLiteral("AA 101"));
			MultiElementInfoWidget panel(elements);
			panel.setFieldText(QETInformation::ELMT_PLANT, QStringLiteral("U02"));
			const int before = m_folio->undoStack().index();
			panel.apply();
			QCOMPARE(m_folio->undoStack().index(), before + 1);
			QCOMPARE(shownLabel(elements.at(0)), QStringLiteral("U02 AA 101"));
			QCOMPARE(shownLabel(elements.at(1)), QStringLiteral("U02 BB 102"));
			QCOMPARE(shownLabel(elements.at(2)), QStringLiteral("U02 AA 103"));
			m_folio->undoStack().undo();
			QCOMPARE(shownLabel(elements.at(0)), QStringLiteral("AA 101"));
			QCOMPARE(shownLabel(elements.at(1)), QStringLiteral("BB 102"));
			m_folio->undoStack().redo();
			QCOMPARE(shownLabel(elements.at(2)), QStringLiteral("U02 AA 103"));
		}

			// Renaming a plant in the window of plants and locations renames
			// the unit of the symbols, whose tags follow; copying a plant
			// gives its copies the new unit, their orders kept
		void renamingOrCopyingAPlantRewritesTheTags()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			TitleBlockProperties block = m_folio->border_and_titleblock.exportTitleBlock();
			block.plant = QStringLiteral("U01");
			m_folio->border_and_titleblock.importTitleBlock(block);
			Element *a = tag(place(m_folio, m_a), "U01", "1", "5");
			Element *b = tag(place(m_folio, m_b), "U01", "1", "6");
			b->setPos(100, 0);

			PlaceRename::Request request;
			request.plant = QStringLiteral("U01");
			request.new_name = QStringLiteral("U09");
			QUndoCommand *rename = PlaceRename::command(m_project, request);
			QVERIFY(rename);
			const int before = m_project->undoStack()->index();
			m_project->undoStack()->push(rename);
			QCOMPARE(m_project->undoStack()->index(), before + 1);
			QCOMPARE(shownLabel(a), QStringLiteral("U09 AA 105"));
			QCOMPARE(shownLabel(b), QStringLiteral("U09 BB 106"));
			QCOMPARE(m_folio->border_and_titleblock.plant(), QStringLiteral("U09"));
			m_project->undoStack()->undo();
			QCOMPARE(shownLabel(a), QStringLiteral("U01 AA 105"));
			QCOMPARE(a->elementInformations().value(QStringLiteral("label")).toString(), QStringLiteral("U01 AA 105"));

			QVERIFY(PlaceCopy::copy(m_project, QStringLiteral("U01"), QStringLiteral("U05")));
			QCOMPARE(m_project->folios().size(), 2);
			Diagram *copy = m_project->folios().at(1);
			QStringList labels;
			for (Element *element : copy->elements()) labels << shownLabel(element);
			labels.sort();
			QCOMPARE(labels, QStringList({"U05 AA 105", "U05 BB 106"}));
			QCOMPARE(shownLabel(a), QStringLiteral("U01 AA 105"));
			QCOMPARE(PidTag::nextOrder(m_project, "U05", "AA", "1"), 6);
			m_project->undoStack()->undo();
			QCOMPARE(m_project->folios().size(), 1);
		}

			// Opening a file never changes it: a label which is not what the
			// model would make is read and saved as it is
		void aLabelReadIsKeptAsSaved()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString first = dir.filePath(QStringLiteral("first.qet"));
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			tag(place(m_folio, m_a), "U01", "1", "5");
			m_project->setFilePath(first);
			QVERIFY(m_project->write().isOk());

			QFile file(first);
			QVERIFY(file.open(QIODevice::ReadOnly));
			QByteArray content = file.readAll();
			file.close();
			QVERIFY(content.contains("U01 AA 105"));
			content.replace("U01 AA 105", "typed by hand");
			const QString typed = dir.filePath(QStringLiteral("typed.qet"));
			QFile typed_file(typed);
			QVERIFY(typed_file.open(QIODevice::WriteOnly));
			typed_file.write(content);
			typed_file.close();

			const QString again = dir.filePath(QStringLiteral("again.qet"));
			{
				QETProject back(typed);
				for (Diagram *folio : back.folios()) folio->ensureLoaded();
				Element *a = nullptr;
				for (Element *element : back.folios().first()->elements()) a = element;
				QVERIFY(a);
				QCOMPARE(a->elementInformations().value(QStringLiteral("label")).toString(),
						 QStringLiteral("typed by hand"));
				back.setFilePath(again);
				QVERIFY(back.write().isOk());
			}
			QFile again_file(again);
			QVERIFY(again_file.open(QIODevice::ReadOnly));
				// the date and time of the save (<properties>) change on
				// every save: all the rest is the same, to the byte
			const auto withoutSaveTime = [](QByteArray text) {
				const qsizetype from = text.indexOf("<properties>");
				const qsizetype to = text.indexOf("</properties>");
				if (from >= 0 && to > from) text.remove(from, to - from);
				return text;
			};
			const QByteArray saved = withoutSaveTime(again_file.readAll());
			content = withoutSaveTime(content);
			qsizetype at = 0;
			while (at < saved.size() && at < content.size() && saved.at(at) == content.at(at)) ++at;
			QVERIFY2(saved == content, qPrintable(QStringLiteral("first difference at %1: read [%2] saved [%3]")
					 .arg(at).arg(QString::fromUtf8(content.mid(qMax<qsizetype>(0, at - 80), 200)),
								  QString::fromUtf8(saved.mid(qMax<qsizetype>(0, at - 80), 200)))));
		}

			// A script changing a field of the tag of a P&ID symbol on a folio
			// not built (lazy folios): the folio is built and the label made
			// again, as a label formula would have it; one undo gives both
			// back. An electrical symbol, or a key the tag does not read, is
			// changed through the project database, its folio left unbuilt
		void aScriptOnAFolioNotBuiltRewritesTheLabel_data()
		{
			QTest::addColumn<bool>("pid");
			QTest::addColumn<QString>("key");
			QTest::addColumn<bool>("built");
			QTest::newRow("P&ID, unit") << true << PidTag::unitKey() << true;
			QTest::newRow("P&ID, train") << true << PidTag::trainKey() << true;
			QTest::newRow("electrical, plant") << false << PidTag::unitKey() << false;
			QTest::newRow("P&ID, comment") << true << QStringLiteral("comment") << false;
			QTest::newRow("P&ID, pid_power") << true << QStringLiteral("pid_power") << false;
		}

		void aScriptOnAFolioNotBuiltRewritesTheLabel()
		{
			QFETCH(bool, pid);
			QFETCH(QString, key);
			QFETCH(bool, built);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			Diagram *second = m_project->addNewDiagram();
			Element *symbol = pid ? tag(place(second, m_a), "U01", "1", "5") : place(second, m_e);
			QVERIFY(symbol);
			const QString uuid = symbol->uuid().toString();
			const QString label = symbol->elementInformations().value(QStringLiteral("label")).toString();
			QCOMPARE(label, pid ? QStringLiteral("U01 AA 105") : QString());
			const QString file = dir.filePath(QStringLiteral("lazy.qet"));
			m_project->setFilePath(file);
			QVERIFY(m_project->write().isOk());

			const QByteArray previous = qgetenv("QET_EAGER_FOLIOS");
			qputenv("QET_EAGER_FOLIOS", "0");
			QETProject back(file);
			if (previous.isNull()) {
				qunsetenv("QET_EAGER_FOLIOS");
			} else {
				qputenv("QET_EAGER_FOLIOS", previous);
			}
			QCOMPARE(back.state(), QETProject::Ok);
			Diagram *folio = back.folios().at(1);
			QVERIFY(!folio->isLoaded());

			QetScriptApi api(&back);
			const QString value = key == PidTag::unitKey() ? QStringLiteral("U02") : QStringLiteral("2");
			QVERIFY(api.setElementInfo(1, uuid, key, value));
			QCOMPARE(folio->isLoaded(), built);
			QCOMPARE(api.elementInfo(1, uuid, key), value);
			QCOMPARE(folio->isLoaded(), built);
			const QString expected = !pid ? QString()
								   : key == PidTag::unitKey() ? QStringLiteral("U02 AA 105")
								   : key == PidTag::trainKey() ? QStringLiteral("U01 AA 205")
								   : label;
			QCOMPARE(api.elementInfo(1, uuid, QStringLiteral("label")), expected);
			if (built) {
				Element *element = nullptr;
				for (Element *e : folio->elements())
					if (e->uuid().toString() == uuid) element = e;
				QVERIFY(element);
				QCOMPARE(shownLabel(element), expected);
			}

			back.undoStack()->undo();
			QCOMPARE(api.elementInfo(1, uuid, key), key == PidTag::unitKey() ? (pid ? QStringLiteral("U01") : QString())
													 : key == PidTag::trainKey() ? QStringLiteral("1") : QString());
			QCOMPARE(api.elementInfo(1, uuid, QStringLiteral("label")), label);
			QCOMPARE(folio->isLoaded(), built);
		}

			// The Tag section and the panel of several symbols build no folio
			// (#1766): on a project whose second folio is not built, the
			// next order counts its symbols from the project database, and
			// a unit given to the symbols of the first folio leaves it so
		void theTagAndThePanelBuildNoFolio()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			Element *a = tag(place(m_folio, m_a), "U01", "1", "1");
			Element *b = tag(place(m_folio, m_a), "U01", "1", "2");
			b->setPos(100, 0);
			Diagram *second = m_project->addNewDiagram();
			tag(place(second, m_a), "U01", "1", "7");
			const QUuid a_uuid = a->uuid(), b_uuid = b->uuid();
			const QString file = dir.filePath(QStringLiteral("lazy.qet"));
			m_project->setFilePath(file);
			QVERIFY(m_project->write().isOk());

			const QByteArray previous = qgetenv("QET_EAGER_FOLIOS");
			qputenv("QET_EAGER_FOLIOS", "0");
			QETProject back(file);
			if (previous.isNull()) {
				qunsetenv("QET_EAGER_FOLIOS");
			} else {
				qputenv("QET_EAGER_FOLIOS", previous);
			}
			QCOMPARE(back.state(), QETProject::Ok);
			Diagram *first = back.folios().first();
			Diagram *other = back.folios().at(1);
			back.buildFolios({first});
			QVERIFY(first->isLoaded());
			QVERIFY(!other->isLoaded());
			Element *back_a = nullptr, *back_b = nullptr;
			for (Element *element : first->elements()) {
				if (element->uuid() == a_uuid) back_a = element;
				if (element->uuid() == b_uuid) back_b = element;
			}
			QVERIFY(back_a && back_b);

			QCOMPARE(PidTag::nextOrder(&back, QStringLiteral("U01"), QStringLiteral("AA"), QStringLiteral("1"),
									   nullptr, QStringLiteral("equipment")), 8);
			{
				ElementTagWidget tab(back_a);
				QVERIFY(tab.unitField()->count() > 1);
			}
			QVERIFY(!other->isLoaded());

			MultiElementInfoWidget panel({back_a, back_b});
			panel.setFieldText(QETInformation::ELMT_PLANT, QStringLiteral("U02"));
			panel.apply();
			QCOMPARE(shownLabel(back_a), QStringLiteral("U02 AA 101"));
			QCOMPARE(shownLabel(back_b), QStringLiteral("U02 AA 102"));
			QVERIFY(!other->isLoaded());
			first->undoStack().undo();
			QCOMPARE(shownLabel(back_a), QStringLiteral("U01 AA 101"));
			QVERIFY(!other->isLoaded());
		}

			// A symbol whose model writes its unit shows its label alone in
			// a project which shows the reference designation: the unit is
			// its plant, never written twice
		void theDesignationShowsTheUnitOnce()
		{
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			Element *a = tag(place(m_folio, m_a), "U01", "1", "5");
			Element *e = place(m_folio, m_e);
			e->setPos(200, 0);
			DiagramContext wire_side = e->elementInformations();
			wire_side.addValue(QStringLiteral("label"), QStringLiteral("K1"));
			wire_side.addValue(QETInformation::ELMT_PLANT, QStringLiteral("U01"));
			e->setElementInformations(wire_side);
			for (QETProject::DesignationDisplay display : {QETProject::DesignationDisplay::Short,
														   QETProject::DesignationDisplay::Full}) {
				m_project->setDesignationDisplay(display);
				QCOMPARE(a->infoForDisplay(QStringLiteral("label")), QStringLiteral("U01 AA 105"));
			}
			QCOMPARE(e->infoForDisplay(QStringLiteral("label")), QStringLiteral("=U01-K1"));
				// a model which does not write the unit: the project's rule
			m_project->setPidCodification(withModel(QStringLiteral("{id} {train}{order:2}")));
			QCOMPARE(a->infoForDisplay(QStringLiteral("label")), QStringLiteral("=U01-AA 105"));
			m_project->setDesignationDisplay(QETProject::DesignationDisplay::Label);
			QCOMPARE(a->infoForDisplay(QStringLiteral("label")), QStringLiteral("AA 105"));
		}

			// The list of the Tag tab: the plant its structure box gives first,
			// else its folio's, then the units of the codification, then the
			// other plants of the project; a valve has no fluids there
		void theListOffersThePlaceFirst()
		{
			PidCodification codification = withModel(QStringLiteral("{unit} {id} {train}{order:2}"));
			codification.fluids << PidCodification::Entry{QStringLiteral("W01"), QStringLiteral("Raw water"), QString()};
			m_project->setPidCodification(codification);
			TitleBlockProperties block = m_folio->border_and_titleblock.exportTitleBlock();
			block.plant = QStringLiteral("U07");
			m_folio->border_and_titleblock.importTitleBlock(block);
			Element *a = place(m_folio, m_a);
			Element *elsewhere = tag(place(m_folio, m_b), "U08", "1", "1");
			elsewhere->setPos(600, 0);
			const auto codes = [](ElementTagWidget &tab) {
				QStringList list;
				for (int i = 1 ; i < tab.unitField()->count() ; ++i)
					list << tab.unitField()->itemData(i).toString();
				return list;
			};
			{
				ElementTagWidget tab(a);
				QCOMPARE(codes(tab), QStringList({"U07", "U01", "U02", "U08"}));
				QCOMPARE(tab.unitField()->itemText(2), QStringLiteral("U01 – Lifting"));
					// nothing is taken by itself
				QVERIFY(a->elementInformations().value(PidTag::unitKey()).toString().isEmpty());
			}
			auto *box = new StructureBoxItem(QPointF(-60, -60), QPointF(60, 60));
			box->setPlant(QStringLiteral("U02"));
			m_folio->addItem(box);
			{
				ElementTagWidget tab(a);
				QCOMPARE(codes(tab), QStringList({"U02", "U01", "U07", "U08"}));
				QCOMPARE(tab.unitField()->itemText(1), QStringLiteral("U02 – Filters"));
			}
			QDomDocument dv;
			const ElementsLocation valve_location = add(*m_project, "v.elmt",
					definition(dv, "77777777-7777-7777-7777-777777777777", "V",
							   QStringLiteral("pid_family=\"valve\" prefix=\"VBO\"")));
			Element *valve = place(m_folio, valve_location);
			valve->setPos(300, 0);
			ElementTagWidget tab(valve);
			QVERIFY(!codes(tab).contains(QStringLiteral("W01")));
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

			// With a model of tag for its family, the label of the
			// Information tab is made by the P&ID tab: not typed there
			// (5l-1m); without a model it is typed there, as before
		void theLabelOfTheInformationTabIsLockedByAModel()
		{
			const auto labelField = [](QWidget *panel) -> ElementInfoPartWidget * {
				for (ElementInfoPartWidget *field : panel->findChildren<ElementInfoPartWidget *>()) {
					if (field->key() == QStringLiteral("label")) return field;
				}
				return nullptr;
			};
			m_project->setPidCodification(PidCodification());
			Element *a = place(m_folio, m_a);
			QVERIFY(a && a->pidTagModel().isEmpty());
			{
				ElementPropertiesWidget panel(a);
				ElementInfoPartWidget *label = labelField(&panel);
				QVERIFY(label && label->findChild<QLineEdit *>());
				QVERIFY(label->findChild<QLineEdit *>()->isEnabled());
			}
			m_project->setPidCodification(withModel(QStringLiteral("{unit} {id} {train}{order:2}")));
			QVERIFY(!a->pidTagModel().isEmpty());
			ElementPropertiesWidget panel(a);
			ElementInfoPartWidget *label = labelField(&panel);
			QVERIFY(label && label->findChild<QLineEdit *>());
			QVERIFY(!label->findChild<QLineEdit *>()->isEnabled());
			QVERIFY(!label->toolTip().isEmpty());
		}

			// 6b-3d1: the letters of the instruments the project offers, and
			// its numbering of the loops in parallel (in series without), are
			// saved: the letters with the codes, the numbering with the models
		void theCodificationKeepsLettersAndNumbering()
		{
			PidCodification codification = instruments(true);
			QDomDocument doc;
			QDomElement holder = doc.createElement(QStringLiteral("project"));
			holder.appendChild(codification.toXml(doc));
			PidCodification back;
			back.fromXml(holder);
			QCOMPARE(back, codification);
			QVERIFY(back.parallel_numbering);
			QCOMPARE(back.instrument_letters.size(), 2);

				// in series, the default: nothing written
			codification.parallel_numbering = false;
			QDomDocument plain;
			QVERIFY(!codification.toXml(plain).hasAttribute(QStringLiteral("instrument_numbering")));
			QVERIFY(!codification.presentationToCsv().contains(QStringLiteral("instrument_numbering")));

			int skipped = -1;
			const PidCodification read = PidCodification::fromCsv(QStringLiteral(
				"kind;code;name;family\n"
				"letters;PT;Pressure transmitter\n"
				"lettres;FT;Flow transmitter\n"
				"letters;PT;Again\n"
				"option_repere;instrument_numbering;parallèle\n"
				"tag_option;instrument_numbering;sometimes\n"), &skipped);
			QCOMPARE(skipped, 2);
			QCOMPARE(read.instrument_letters.size(), 2);
			QCOMPARE(read.instrument_letters.at(1).code, QStringLiteral("FT"));
			QCOMPARE(read.instrument_letters.at(1).name, QStringLiteral("Flow transmitter"));
			QVERIFY(read.parallel_numbering);
			QVERIFY(read.codesToCsv().contains(QStringLiteral("letters;FT;Flow transmitter")));
			QVERIFY(read.presentationToCsv().contains(QStringLiteral("tag_option;instrument_numbering;parallel")));
			QVERIFY(!read.presentationToCsv().contains(QStringLiteral("FT")));
			QCOMPARE(PidCodification::fromCsv(read.toCsv()), read);
				// "serial" says the default
			QVERIFY(!PidCodification::fromCsv(QStringLiteral(
				"tag_option;instrument_numbering;serial\n")).parallel_numbering);
		}

			// An instrument has letters, typed in capitals, instead of the
			// identifier of its symbol; those of the project are offered
		void theLettersOfAnInstrumentAreItsIdentifier()
		{
			m_project->setPidCodification(instruments());
			Element *i = place(m_folio, m_i);
			Element *a = place(m_folio, m_a);
			QVERIFY(i && a);
			ElementTagWidget equipment(a);
			QVERIFY(!equipment.asksLetters());
			QVERIFY(equipment.lettersField()->isHidden());

			ElementTagWidget tab(i);
			QVERIFY(tab.asksLetters());
			QVERIFY(!tab.lettersField()->isHidden());
			QCOMPARE(tab.lettersField()->count(), 3);   // nothing, LIC, PT
			QCOMPARE(tab.lettersField()->itemData(2).toString(), QStringLiteral("PT"));
			tab.setLiveEdit(true);
			QLineEdit *typed = tab.lettersField()->lineEdit();
			typed->setText(QStringLiteral("pt"));
			emit typed->textEdited(QStringLiteral("pt"));
			QCOMPARE(typed->text(), QStringLiteral("PT"));
			emit typed->editingFinished();
			QCOMPARE(i->elementInformations().value(PidTag::lettersKey()).toString(), QStringLiteral("PT"));
			QCOMPARE(shownLabel(i), QStringLiteral("PT"));

				// the unit numbers it
			tab.unitField()->setCurrentIndex(1);             // U01
			emit tab.unitField()->activated(1);
			QCOMPARE(tab.orderField()->text(), QStringLiteral("1"));
			QCOMPARE(shownLabel(i), QStringLiteral("U01 PT 001"));
				// the changes of the tag of an element make one step
				// (ChangeElementInformationCommand::mergeWith())
			m_folio->undoStack().undo();
			QCOMPARE(shownLabel(i), QString());
			m_folio->undoStack().redo();
			QCOMPARE(shownLabel(i), QStringLiteral("U01 PT 001"));

				// other letters, the order stays
			typed->setText(QStringLiteral("LIC"));
			emit typed->editingFinished();
			QCOMPARE(shownLabel(i), QStringLiteral("U01 LIC 001"));
			QCOMPARE(PidTag::identifier(i), QStringLiteral("LIC"));
				// the letters of an equipment are not asked, nor written
			QVERIFY(!equipment.edited().contains(PidTag::lettersKey()));
		}

			// Step 7m: what is typed in the units and the letters is looked
			// for anywhere in their codes and names
		void theListsOfferWhatContainsTheText()
		{
			m_project->setPidCodification(instruments());
			Element *i = place(m_folio, m_i);
			QVERIFY(i);
			ElementTagWidget tab(i);
			auto offered = [](QComboBox *list, const QString &typed) {
				QCompleter *completer = list->completer();
				completer->setCompletionPrefix(typed);
				QStringList texts;
				for (int row = 0 ; row < completer->completionCount() ; ++row) {
					completer->setCurrentRow(row);
					texts << completer->currentCompletion();
				}
				return texts;
			};
			QCOMPARE(offered(tab.unitField(), QStringLiteral("ift")), QStringList{tab.unitField()->itemText(1)});
			QCOMPARE(offered(tab.unitField(), QStringLiteral("filt")), QStringList{tab.unitField()->itemText(2)});
			QCOMPARE(offered(tab.lettersField(), QStringLiteral("PRESS")), QStringList{tab.lettersField()->itemText(2)});
			QCOMPARE(offered(tab.lettersField(), QStringLiteral("IC")), QStringList{tab.lettersField()->itemText(1)});

				// picked in the list of the completer, as with the mouse
			QCompleter *completer = tab.unitField()->completer();
			completer->setCompletionPrefix(QStringLiteral("filt"));
			emit completer->activated(completer->completionModel()->index(0, 0));
			QCOMPARE(tab.edited().value(PidTag::unitKey()).toString(), QStringLiteral("U02"));
		}

			// The loops of the instruments (ANSI/ISA-5.1, A.6): in series (the
			// default), an instrument is numbered with all those of its unit
			// and train, whatever their letters; in parallel, with those of
			// its measured variable, its first letter. An equipment keeps its
			// own counter, by identifier.
		void instrumentsAreNumberedByLoop()
		{
			m_project->setPidCodification(instruments());
			instrument(place(m_folio, m_i), "PT", "U01", "1", "5");
			instrument(place(m_folio, m_i), "LIC", "U01", "1", "7");
			tag(place(m_folio, m_a), "U01", "1", "20");   // an equipment: its own counter
			const QString family = PidTag::instrumentFamily();
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "FT", "1", nullptr, family), 8);
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "PT", "1", nullptr, family), 8);
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "PT", "2", nullptr, family), 1);
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "AA", "1"), 21);

			m_project->setPidCodification(instruments(true));
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "FT", "1", nullptr, family), 1);
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "PT", "1", nullptr, family), 6);
				// the loop P counts PT and PIC together
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "PIC", "1", nullptr, family), 6);
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "LSH", "1", nullptr, family), 8);
			QCOMPARE(PidTag::nextOrder(m_project, "U01", "AA", "1"), 21);
		}

			// A loop copied whole stays one loop: one new order for all its
			// instruments; two instruments of a loop do not clash
		void aLoopCopiedWholeStaysOneLoop()
		{
			m_project->setPidCodification(instruments());
			Element *pt = instrument(place(m_folio, m_i), "PT", "U01", "1", "5");
			Element *lic = instrument(place(m_folio, m_i), "LIC", "U01", "1", "5");
			lic->setPos(100, 0);
			instrument(place(m_folio, m_i), "FT", "U01", "1", "6")->setPos(200, 0);

			const QList<Element *> copied = paste({pt, lic}, QPointF(0, 500));
			QCOMPARE(copied.size(), 2);
			QStringList labels;
			for (Element *element : copied) labels << shownLabel(element);
			labels.sort();
			QCOMPARE(labels, QStringList({"U01 LIC 107", "U01 PT 107"}));

				// in parallel: the loops P and L, each its own counter
			m_project->setPidCodification(instruments(true));
			const QList<Element *> again = paste({pt, lic}, QPointF(0, 800));
			labels.clear();
			for (Element *element : again) labels << shownLabel(element);
			labels.sort();
			QCOMPARE(labels, QStringList({"U01 LIC 108", "U01 PT 108"}));
		}

			// The loops count the folios not built yet, the family of their
			// symbols read from the collection of the project
		void theLoopsCountTheFoliosNotBuilt()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("loops.qetdb"));
			m_project->setPidCodification(instruments());
			instrument(place(m_folio, m_i), "PT", "U01", "1", "3");
			Diagram *second = m_project->addNewDiagram();
			instrument(place(second, m_i), "LIC", "U01", "1", "12");
			m_project->setFilePath(path);
			QVERIFY(m_project->write().isOk());

			QETProject back(path);
			QVERIFY(back.unloadedFolioCount() > 0);
			const int unloaded = back.unloadedFolioCount();
			QCOMPARE(PidTag::nextOrder(&back, "U01", "FT", "1", nullptr, PidTag::instrumentFamily()), 13);
			QCOMPARE(back.unloadedFolioCount(), unloaded);   // nothing built
		}

			// The tag without its identifier, for the lower half of an
			// instrument bubble (step 6b-3d2, ISA 5.1 annex B.3.1)
		void theTagWithoutItsIdentifier_data()
		{
			QTest::addColumn<QString>("model");
			QTest::addColumn<QString>("unit");
			QTest::addColumn<QString>("train");
			QTest::addColumn<QString>("order");
			QTest::addColumn<QString>("rest");
			QTest::newRow("SIR") << "{unit} {id} {train}{order:2}" << "REL" << "1" << "2" << "REL 102";
			QTest::newRow("C01") << "{id} {train}.{order:2}" << "" << "4712" << "1" << "4712.01";
			QTest::newRow("no train, no dot") << "{id} {train}.{order:2}" << "" << "" << "3" << "03";
			QTest::newRow("separators") << "{id}-{unit}-{order:3}" << "U01" << "" << "7" << "U01-007";
			QTest::newRow("three lines") << "{unit}\\n{id}\\n{train}{order:2}" << "REL" << "1" << "5" << "REL 105";
			QTest::newRow("nothing but the identifier") << "{id}" << "REL" << "1" << "5" << "";
		}

		void theTagWithoutItsIdentifier()
		{
			QFETCH(QString, model);
			QFETCH(QString, unit);
			QFETCH(QString, train);
			QFETCH(QString, order);
			QFETCH(QString, rest);
			QCOMPARE(PidTag::labelWithoutIdentifier(model, unit, train, order), rest);
		}

			// The instrument bubble writes its tag in two texts: the letters
			// above, the rest below; the tag stays one label, in the order of
			// the bill of materials
		void theBubbleWritesItsTagInTwoTexts()
		{
			m_project->setPidCodification(instruments());   // {unit} {id} {train}{order:2}
			QDomDocument dl;
			const ElementsLocation bubble = add(*m_project, "l.elmt",
				definition(dl, "77777777-7777-7777-7777-777777777777", "L",
						   QStringLiteral("pid_family=\"instrument\""), true,
						   QETInformation::pidTagPartKeys()));
			Element *i = instrument(place(m_folio, bubble), "LIT", "REL", "1", "2");
			QVERIFY(i);
			QCOMPARE(i->elementInformations().value(QStringLiteral("label")).toString(),
					 QStringLiteral("REL LIT 102"));
			QCOMPARE(shownLabel(i, QETInformation::PID_TAG_ID), QStringLiteral("LIT"));
			QCOMPARE(shownLabel(i, QETInformation::PID_TAG_REST), QStringLiteral("REL 102"));

				// the letters change, the text follows
			DiagramContext information = i->elementInformations();
			information.addValue(PidTag::lettersKey(), QStringLiteral("PT"));
			i->setElementInformations(i->withPidLabel(information));
			QCOMPARE(shownLabel(i, QETInformation::PID_TAG_ID), QStringLiteral("PT"));
			QCOMPARE(shownLabel(i, QETInformation::PID_TAG_REST), QStringLiteral("REL 102"));

				// no field: the label typed by hand, its first line above
			Element *hand = place(m_folio, bubble);
			QVERIFY(hand);
			DiagramContext typed = hand->elementInformations();
			typed.addValue(QStringLiteral("label"), QStringLiteral("PI\n4712.01"));
			hand->setElementInformations(typed);
			QCOMPARE(shownLabel(hand, QETInformation::PID_TAG_ID), QStringLiteral("PI"));
			QCOMPARE(shownLabel(hand, QETInformation::PID_TAG_REST), QStringLiteral("4712.01"));

				// the texts read the tag, nothing more is saved
			QVERIFY(textOf(i, QETInformation::PID_TAG_ID));
			const QString saved = m_folio->toXml().toString();
			QVERIFY(saved.contains(QStringLiteral("<info_name>pid_tag_rest</info_name>")));
			QVERIFY(!saved.contains(QStringLiteral("name=\"pid_tag_")));
				// a name in the lists of the information
			QVERIFY(!QETInformation::translatedInfoKey(QETInformation::PID_TAG_REST).isEmpty());
		}

			// A P&ID symbol which shows its comment (an arrow of inlet or
			// outlet, 8b) has a section of its P&ID tab to type it, applied
			// when left, with undo; one which does not show it, or an
			// electrical one, has none
		void aSymbolShowingItsCommentHasItInItsPidTab()
		{
			QDomDocument dc, dp, dx;
			const ElementsLocation shows = add(*m_project, "c.elmt",
				definition(dc, "77777777-7777-7777-7777-777777777777", "C",
						   QStringLiteral("pid_family=\"pipe\""), true, {QStringLiteral("comment")}));
			const ElementsLocation plain = add(*m_project, "p.elmt",
				definition(dp, "88888888-8888-8888-8888-888888888888", "P",
						   QStringLiteral("pid_family=\"pipe\"")));
			const ElementsLocation electrical = add(*m_project, "x.elmt",
				definition(dx, "99999999-9999-9999-9999-999999999999", "X",
						   QString(), true, {QStringLiteral("comment")}));
			Element *arrow = place(m_folio, shows);
			QVERIFY(arrow);
			QVERIFY(ElementCommentWidget::isWanted(arrow));
			QVERIFY(!ElementCommentWidget::isWanted(place(m_folio, plain)));
			QVERIFY(!ElementCommentWidget::isWanted(place(m_folio, electrical)));

			ElementPropertiesWidget properties(arrow);
			auto *section = properties.findChild<ElementCommentWidget *>();
			QVERIFY(section);
			section->setLiveEdit(true);
				// typed, then left
			const auto typeAndLeave = [section](const QString &text) {
				section->commentField()->setPlainText(text);
				QFocusEvent left(QEvent::FocusOut);
				QCoreApplication::sendEvent(section->commentField(), &left);
			};
			typeAndLeave(QStringLiteral("Raw water from well F1"));
			QCOMPARE(arrow->elementInformations().value(QETInformation::ELMT_COMMENT).toString(),
					 QStringLiteral("Raw water from well F1"));
			QCOMPARE(shownLabel(arrow, QStringLiteral("comment")), QStringLiteral("Raw water from well F1"));

			m_folio->undoStack().undo();
			QVERIFY(arrow->elementInformations().value(QETInformation::ELMT_COMMENT).toString().isEmpty());
			QCOMPARE(section->commentField()->toPlainText(), QString());

				// on several lines, typed in the field itself: the line
				// break is kept, in the information and on the folio
			const QString lines = QStringLiteral("Raw water\nfrom well F1");
			typeAndLeave(lines);
			QCOMPARE(arrow->elementInformations().value(QETInformation::ELMT_COMMENT).toString(), lines);
			QCOMPARE(shownLabel(arrow, QStringLiteral("comment")), lines);
			QCOMPARE(section->commentField()->toPlainText(), lines);

				// in a tall panel, the section is no taller than its field:
				// the room left goes below the sections, not around the field
			properties.resize(420, 1200);
			properties.show();
			QVERIFY(QTest::qWaitForWindowExposed(&properties));
			QVERIFY2(section->height() <= section->commentField()->height() + 4,
					 qPrintable(QStringLiteral("%1 > %2").arg(section->height())
								.arg(section->commentField()->height())));
		}

			// A symbol with one composite text, %{comment} then
			// "to %{notes}", as the off-page connectors and battery limits
			// of a pipe (8c-1)
		static QDomElement cartouche(QDomDocument &doc, const QString &uuid, const QString &link_type)
		{
			doc.setContent(QStringLiteral(
				"<definition version=\"0.100.0\" type=\"element\" link_type=\"%1\" pid_family=\"pipe\" "
				"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
				"<uuid uuid=\"{%2}\"/><names><name lang=\"en\">K</name></names><description>"
				"<dynamic_text x=\"0\" y=\"0\" z=\"100\" text_width=\"-1\" Halignment=\"AlignLeft\" "
				"Valignment=\"AlignTop\" frame=\"false\" rotation=\"0\" text_from=\"CompositeText\" "
				"uuid=\"{%3}\" font=\"Liberation Sans,9,-1,5,50,0,0,0,0,0,Regular\"><text></text>"
				"<composite_text>%{comment}\nto %{notes}</composite_text></dynamic_text>"
				"<terminal uuid=\"{%4}\" name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
				"</description></definition>")
				.arg(link_type, uuid, QUuid::createUuid().toString(QUuid::WithoutBraces),
					 QUuid::createUuid().toString(QUuid::WithoutBraces)));
			return doc.documentElement();
		}

			// An off-page connector shows its service and the tag on the
			// other side in one composite text: its P&ID tab has both, as
			// a folio report has no Informations tab to type them; one
			// change for both, undone at once
		void aConnectorTypesItsServiceAndTheOtherSide()
		{
			QDomDocument dk;
			const ElementsLocation connector = add(*m_project, "connector.elmt",
				cartouche(dk, "aaaaaaaa-0000-0000-0000-00000000000a", QStringLiteral("next_report")));
			Element *k = place(m_folio, connector);
			QVERIFY(k);
			QVERIFY(k->linkType() & Element::AllReport);
			QVERIFY(ElementCommentWidget::isWanted(k));
			QVERIFY(ElementCommentWidget::showsInformation(k, QETInformation::ELMT_NOTES));

			ElementPropertiesWidget properties(k);
			auto *section = properties.findChild<ElementCommentWidget *>();
			QVERIFY(section);
			QVERIFY(section->notesField());
			section->setLiveEdit(true);
			section->commentField()->setPlainText(QStringLiteral("Filtrate"));
			section->notesField()->setText(QStringLiteral("B01"));
			emit section->notesField()->editingFinished();
			QCOMPARE(k->elementInformations().value(QETInformation::ELMT_COMMENT).toString(),
					 QStringLiteral("Filtrate"));
			QCOMPARE(k->elementInformations().value(QETInformation::ELMT_NOTES).toString(),
					 QStringLiteral("B01"));
			QCOMPARE(k->dynamicTextItems().first()->toPlainText(), QStringLiteral("Filtrate\nto B01"));

			m_folio->undoStack().undo();
			QVERIFY(!k->elementInformations().contains(QETInformation::ELMT_COMMENT));
			QVERIFY(!k->elementInformations().contains(QETInformation::ELMT_NOTES));
			QVERIFY(section->notesField()->text().isEmpty());
		}

			// The panel keeps its sections from one symbol to a like one,
			// not when the comment section, or its line for the other side,
			// comes or goes
		void theCommentSectionFollowsTheSelectedSymbol()
		{
			QDomDocument dl, da, dp;
			Element *limit = place(m_folio, add(*m_project, "limit.elmt",
				cartouche(dl, "bbbbbbbb-0000-0000-0000-00000000000b", QStringLiteral("simple"))));
			Element *arrow = place(m_folio, add(*m_project, "arrow.elmt",
				definition(da, "cccccccc-0000-0000-0000-00000000000c", "A",
						   QStringLiteral("pid_family=\"pipe\""), true, {QStringLiteral("comment")})));
			Element *plain = place(m_folio, add(*m_project, "plain.elmt",
				definition(dp, "dddddddd-0000-0000-0000-00000000000d", "Q",
						   QStringLiteral("pid_family=\"pipe\""), true, {QStringLiteral("label")})));
			QVERIFY(limit && arrow && plain);

			ElementPropertiesWidget properties(limit);
			auto *section = properties.findChild<ElementCommentWidget *>();
			QVERIFY(section && section->notesField());
			properties.setElement(arrow);
			section = properties.findChild<ElementCommentWidget *>();
			QVERIFY(section);
			QVERIFY(!section->notesField());
			properties.setElement(plain);
			QVERIFY(!properties.findChild<ElementCommentWidget *>());
		}
};

QET_APP_TEST_MAIN(tst_elementtag)
#include "tst_elementtag.moc"
