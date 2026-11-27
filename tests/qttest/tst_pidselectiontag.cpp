// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "factory/propertieseditorfactory.h"
#include "pidcodification.h"
#include "pidtag.h"
#include "qetgraphicsitem/dynamicelementtextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetinformation.h"
#include "qetproject.h"
#include "titleblockproperties.h"
#include "ui/multielementinfowidget.h"
#include "ui/pidselectiontagwidget.h"

#include <QComboBox>
#include <QDomDocument>
#include <QLabel>
#include <QLineEdit>
#include <QUndoStack>

#include <memory>

// The tag of several P&ID symbols at once (9e-0-bis): the functional unit and
// the train of a selection, and "Number", which gives the symbols without an
// order the next ones, folio by folio, then from left to right and from top
// to bottom, or every symbol again. Each is one undo step of its own.
class tst_pidselectiontag : public QObject
{
	Q_OBJECT

		// A symbol of 40 x 40 with a text showing its label; @p attributes go
		// in <definition> (pid_family, prefix...)
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &name,
								  const QString &attributes)
	{
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" %1 "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%2}\"/><names><name lang=\"en\">%3</name></names>"
			"<description>"
			"<ellipse x=\"-5\" y=\"-5\" width=\"10\" height=\"10\" style=\"line-style:normal;"
			"line-weight:normal;filling:none;color:black\" antialias=\"false\"/>"
			"<dynamic_text x=\"0\" y=\"25\" z=\"100\" text_width=\"-1\" "
			"Halignment=\"AlignHCenter\" Valignment=\"AlignTop\" frame=\"false\" "
			"rotation=\"0\" keep_visual_rotation=\"true\" text_from=\"ElementInfo\" "
			"uuid=\"{%4}\" font=\"Liberation Sans,9,-1,5,50,0,0,0,0,0,Regular\">"
			"<text></text><info_name>label</info_name></dynamic_text>"
			"</description></definition>")
			.arg(attributes, uuid, name, QUuid::createUuid().toString(QUuid::WithoutBraces)));
		return doc.documentElement();
	}

	static ElementsLocation add(QETProject &project, const QString &file, const QDomElement &definition)
	{
		project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/test"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/test/") + file, &project);
	}

	Element *place(const ElementsLocation &location, qreal x, qreal y = 0)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (element) {
			m_folio->addItem(element);
			element->setPos(x, y);
		}
		return element;
	}

	static QString label(const Element *element)
	{
		return element->elementInformations().value(QStringLiteral("label")).toString();
	}

	static QString order(const Element *element)
	{
		return element->elementInformations().value(PidTag::orderKey()).toString();
	}

		// Give @p element a tag, its label made by the model of its project
	static Element *tag(Element *element, const QString &unit, const QString &train,
						const QString &order, const QString &letters = QString())
	{
		DiagramContext information = element->elementInformations();
		if (!unit.isEmpty()) information.addValue(PidTag::unitKey(), unit);
		if (!train.isEmpty()) information.addValue(PidTag::trainKey(), train);
		if (!order.isEmpty()) information.addValue(PidTag::orderKey(), order);
		if (!letters.isEmpty()) information.addValue(PidTag::lettersKey(), letters);
		element->setElementInformations(element->withPidLabel(information));
		return element;
	}

	static PidCodification codification()
	{
		PidCodification codification;
		const QString model = QStringLiteral("{unit} {id} {train}{order:2}");
		codification.tag_models.insert(QStringLiteral("equipment"), model);
		codification.tag_models.insert(QStringLiteral("instrument"), model);
		codification.functional_units << PidCodification::Entry{QStringLiteral("U01"), QStringLiteral("Lifting"), QString()}
									  << PidCodification::Entry{QStringLiteral("U02"), QStringLiteral("Filters"), QString()};
		return codification;
	}

	QUndoStack &stack() { return m_folio->undoStack(); }

	QETProject *m_project = nullptr;
	Diagram *m_folio = nullptr;
		// a: an equipment, identifier AA; i: an instrument bubble; e: an
		// electrical symbol
	ElementsLocation m_a, m_i, m_e;

	private slots:
		void init()
		{
			m_project = new QETProject();
			m_project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("test"), NamesList());
			QDomDocument da, di, de;
			m_a = add(*m_project, "a.elmt", definition(da, "11111111-1111-1111-1111-111111111111", "A",
							QStringLiteral("pid_family=\"equipment\" prefix=\"AA\"")));
			m_i = add(*m_project, "i.elmt", definition(di, "66666666-6666-6666-6666-666666666666", "I",
							QStringLiteral("pid_family=\"instrument\"")));
			m_e = add(*m_project, "e.elmt", definition(de, "44444444-4444-4444-4444-444444444444", "E", QString()));
			QVERIFY(m_a.exist() && m_i.exist() && m_e.exist());
			m_folio = m_project->addNewDiagram();
			m_project->setPidCodification(codification());
		}

		void cleanup()
		{
			delete m_project;
			m_project = nullptr;
		}

			// "On dessine d'abord, on repère ensuite": a unit for the
			// selection, then "Number", from left to right; two undo steps
		void aUnitThenNumbersAreTwoSteps()
		{
			Element *right = place(m_a, 200);
			Element *left = place(m_a, 0);
			Element *middle = place(m_a, 100);
			PidSelectionTagWidget panel({right, left, middle});
			QVERIFY(!panel.unitIsMixed());
			QCOMPARE(panel.unitText(), QString());

			const int before = stack().index();
			panel.setUnitText(QStringLiteral("U02"));
			panel.apply();
			QCOMPARE(stack().index(), before + 1);
				// Giving a unit numbers nothing
			QCOMPARE(label(left), QStringLiteral("U02 AA"));
			QCOMPARE(order(left), QString());

			QCOMPARE(panel.number(), 3);
			QCOMPARE(stack().index(), before + 2);
			QCOMPARE(label(left), QStringLiteral("U02 AA 001"));
			QCOMPARE(label(middle), QStringLiteral("U02 AA 002"));
			QCOMPARE(label(right), QStringLiteral("U02 AA 003"));
			QCOMPARE(left->elementInformations().value(PidTag::trainKey()).toString(), QStringLiteral("0"));

			stack().undo();
			QCOMPARE(label(middle), QStringLiteral("U02 AA"));
			stack().undo();
			QVERIFY(!middle->elementInformations().contains(PidTag::unitKey()));
			stack().redo();
			stack().redo();
			QCOMPARE(label(right), QStringLiteral("U02 AA 003"));
		}

			// By default only the symbols without an order get one, after the
			// highest of the project; the numbered ones keep theirs
		void onlyTheUnnumberedByDefault()
		{
			tag(place(m_a, 500), "U01", "1", "5");                 // not selected
			Element *kept = tag(place(m_a, 0), "U01", "1", "2");
			Element *b = tag(place(m_a, 200), "U01", "1", "");
			Element *c = tag(place(m_a, 100), "U01", "1", "");
			Element *other_unit = tag(place(m_a, 300), "U02", "1", "");
			PidSelectionTagWidget panel({kept, b, c, other_unit});
			QCOMPARE(panel.number(), 3);
			QCOMPARE(label(kept), QStringLiteral("U01 AA 102"));
			QCOMPARE(label(c), QStringLiteral("U01 AA 106"));
			QCOMPARE(label(b), QStringLiteral("U01 AA 107"));
			QCOMPARE(label(other_unit), QStringLiteral("U02 AA 101"));
				// A second click: nothing left to number, no undo step
			const int before = stack().index();
			QCOMPARE(panel.number(), 0);
			QCOMPARE(stack().index(), before);
			QVERIFY(!panel.findChild<QLabel *>(QStringLiteral("result"))->text().isEmpty());
		}

			// "Number again": the whole selection in order, the orders it
			// had freed, after those of the symbols left out
		void numberAgainFreesTheOrders()
		{
			tag(place(m_a, 500), "U01", "1", "1");                 // not selected
			Element *a = tag(place(m_a, 200), "U01", "1", "7");
			Element *b = tag(place(m_a, 0), "U01", "1", "3");
			Element *c = tag(place(m_a, 100), "U01", "1", "9");
			PidSelectionTagWidget panel({a, b, c});
			panel.setNumberAgain(true);
			QCOMPARE(panel.number(), 3);
			QCOMPARE(order(b), QStringLiteral("2"));
			QCOMPARE(order(c), QStringLiteral("3"));
			QCOMPARE(order(a), QStringLiteral("4"));
			stack().undo();
			QCOMPARE(order(a), QStringLiteral("7"));
			QCOMPARE(order(c), QStringLiteral("9"));
		}

			// Instruments: numbered one after the other by loop (in series,
			// whatever their letters); numbered again, a loop stays a loop
		void instrumentsInARowAndLoopsKept()
		{
			Element *pt = tag(place(m_i, 0), "U01", "1", "", "PT");
			Element *pic = tag(place(m_i, 100), "U01", "1", "", "PIC");
			PidSelectionTagWidget panel({pt, pic});
			QCOMPARE(panel.number(), 2);
			QCOMPARE(label(pt), QStringLiteral("U01 PT 101"));
			QCOMPARE(label(pic), QStringLiteral("U01 PIC 102"));

			Element *lt = tag(place(m_i, 300), "U01", "1", "5", "LT");
			Element *lic = tag(place(m_i, 200), "U01", "1", "5", "LIC");
			tag(pic, "U01", "1", "8", "PIC");
			PidSelectionTagWidget again({pt, pic, lt, lic});
			again.setNumberAgain(true);
			again.number();
			QCOMPARE(order(pt), QStringLiteral("1"));
			QCOMPARE(order(pic), QStringLiteral("2"));
			QCOMPARE(order(lic), QStringLiteral("3"));
			QCOMPARE(order(lt), QStringLiteral("3"));
		}

			// A selection with electrical symbols: the P&ID part edits the
			// P&ID symbols only and says how many
		void aMixedSelectionTagsTheSymbolsOfAModel()
		{
			Element *a = place(m_a, 0);
			Element *b = place(m_a, 100);
			Element *e = place(m_e, 200);
			PidSelectionTagWidget panel({a, b, e});
			QCOMPARE(PidSelectionTagWidget::taggedElements({a, b, e}).size(), 2);
			QVERIFY(panel.findChild<QLabel *>(QStringLiteral("header"))->text().contains(QStringLiteral("2")));
			panel.setUnitText(QStringLiteral("U01"));
			panel.apply();
			QCOMPARE(a->elementInformations().value(PidTag::unitKey()).toString(), QStringLiteral("U01"));
			QVERIFY(!e->elementInformations().contains(PidTag::unitKey()));
			QCOMPARE(panel.number(), 2);
			QVERIFY(!e->elementInformations().contains(PidTag::orderKey()));
		}

			// The selection panel and the Edit action take this editor for
			// P&ID symbols of a model, the panel of upstream otherwise
		void theFactoryGivesThisEditor()
		{
			Element *a = place(m_a, 0);
			Element *b = place(m_a, 100);
			Element *e = place(m_e, 200);
			Element *f = place(m_e, 300);
			std::unique_ptr<PropertiesEditorWidget> pid(PropertiesEditorFactory::propertiesEditor({a, b}, nullptr));
			QVERIFY(qobject_cast<PidSelectionTagWidget *>(pid.get()));
				// The same editor, given the new selection
			QCOMPARE(PropertiesEditorFactory::propertiesEditor({a, e}, pid.get()), pid.get());
			std::unique_ptr<PropertiesEditorWidget> wires(PropertiesEditorFactory::propertiesEditor({e, f}, nullptr));
			QVERIFY(qobject_cast<MultiElementInfoWidget *>(wires.get()));

				// No model of tag: the panel of upstream alone
			m_project->setPidCodification(PidCodification());
			std::unique_ptr<PropertiesEditorWidget> none(PropertiesEditorFactory::propertiesEditor({a, b}, nullptr));
			QVERIFY(qobject_cast<MultiElementInfoWidget *>(none.get()));
		}

			// The units offered: the plant of the folio first, then those of
			// the codification; units which differ show "Multiple values"
		void theUnitsOffered()
		{
			TitleBlockProperties block = m_folio->border_and_titleblock.exportTitleBlock();
			block.plant = QStringLiteral("U05");
			m_folio->border_and_titleblock.importTitleBlock(block);
			Element *a = tag(place(m_a, 0), "U01", "", "");
			Element *b = tag(place(m_a, 100), "U02", "", "");
			PidSelectionTagWidget panel({a, b});
			QCOMPARE(panel.offeredUnits().mid(0, 3),
					 QStringList({QStringLiteral("U05"), QStringLiteral("U01"), QStringLiteral("U02")}));
			QVERIFY(panel.unitIsMixed());
			QCOMPARE(panel.unitText(), QString());
				// Left alone: nothing changes
			QVERIFY(!panel.associatedUndo());
		}

			// In the window of a selection, the unit and the information
			// below are applied as one undo step
		void theWindowAppliesAllInOneStep()
		{
			Element *a = place(m_a, 0);
			Element *b = place(m_a, 100);
			PidSelectionTagWidget panel({a, b});
			panel.informationWidget()->setFieldText(QETInformation::ELMT_MANUFACTURER, QStringLiteral("ACME"));
			panel.setUnitText(QStringLiteral("U02"));
			const int before = stack().index();
			panel.apply();
			QCOMPARE(stack().index(), before + 1);
			QCOMPARE(a->elementInformations().value(QETInformation::ELMT_MANUFACTURER).toString(), QStringLiteral("ACME"));
			QCOMPARE(label(b), QStringLiteral("U02 AA"));
			stack().undo();
			QVERIFY(!a->elementInformations().contains(QETInformation::ELMT_MANUFACTURER));
			QVERIFY(!b->elementInformations().contains(PidTag::unitKey()));
		}

			// In the selection panel (live), the unit is applied when it is
			// left, and the Plant row below and the unit follow each other
		void livePanelAndPlantRowFollowEachOther()
		{
			Element *a = place(m_a, 0);
			Element *b = place(m_a, 100);
			PidSelectionTagWidget panel({a, b});
			panel.setLiveEdit(true);
			auto *unit = panel.findChild<QComboBox *>(QStringLiteral("unit"));
			QVERIFY(unit);
			panel.setUnitText(QStringLiteral("U01"));
			const int before = stack().index();
			emit unit->lineEdit()->editingFinished();
			QCOMPARE(stack().index(), before + 1);
			QCOMPARE(label(a), QStringLiteral("U01 AA"));
				// Left again without a change: nothing more
			emit unit->lineEdit()->editingFinished();
			QCOMPARE(stack().index(), before + 1);

			panel.informationWidget()->setFieldText(QETInformation::ELMT_PLANT, QStringLiteral("U07"));
			QCOMPARE(label(b), QStringLiteral("U07 AA"));
			QCoreApplication::processEvents();
			QCOMPARE(panel.unitText(), QStringLiteral("U07"));
		}
};

QET_APP_TEST_MAIN(tst_pidselectiontag)
#include "tst_pidselectiontag.moc"
