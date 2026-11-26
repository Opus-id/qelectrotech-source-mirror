// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidtag.h"
#include "qetgraphicsitem/element.h"
#include "qetproject.h"
#include "ui/customelementinfopartwidget.h"
#include "ui/elementdatawidget.h"
#include "ui/elementinfopartwidget.h"
#include "ui/elementinfowidget.h"
#include "ui/elementpropertieswidget.h"
#include "ui/elementtagwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDomDocument>
#include <QLabel>
#include <QLineEdit>

// The data a project asks of its P&ID symbols (step 9a and 9b): declared by
// class of symbol in its codification, typed in the Data section of the P&ID
// tab, kept as information of the placed element ("pid_" then the name), as a
// custom property of QElectroTech is. The Informations tab does not show them
// for a P&ID symbol, and does not lose them; an electrical element is as before.
class tst_elementdata : public QObject
{
	Q_OBJECT

		// A symbol of 40 x 40 with two terminals; @p attributes go in
		// <definition> (pid_family, pid_class...)
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
			"<terminal uuid=\"{%4}\" name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"/>"
			"<terminal uuid=\"{%5}\" name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"/>"
			"</description></definition>")
			.arg(attributes, uuid, name,
				 QUuid::createUuid().toString(QUuid::WithoutBraces),
				 QUuid::createUuid().toString(QUuid::WithoutBraces)));
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

	static void give(Element *element, const QString &key, const QString &value)
	{
		DiagramContext information = element->elementInformations();
		information.addValue(key, value);
		element->setElementInformations(information);
	}

	static QString held(Element *element, const QString &key)
	{
		return element->elementInformations().value(key).toString();
	}

		// What a company would import: the data of its pumps and blowers
	static QString dataLines()
	{
		return QStringLiteral(
			"kind;code;name;family\n"
			"data;pump blower;power;Rated power;kW;number\n"
			"data;pump blower;starter;Starter;;list;direct|soft starter|drive\n"
			"data;pump blower;insulated;Insulated;;yes_no\n"
			"data;pump;checked;Checked;;yes_no\n"
			"data;pump;note;;;text;;PumpNote\n");
	}

	static QStringList customKeys(QWidget *tab)
	{
		QStringList keys;
		for (CustomElementInfoPartWidget *row : tab->findChildren<CustomElementInfoPartWidget *>()) {
			keys << row->key();
		}
		keys.sort();
		return keys;
	}

		// A project with a pump and a valve (P&ID symbols with a class), a
		// fitting without class and an electrical symbol
	QETProject *m_project = nullptr;
	Diagram *m_folio = nullptr;
	ElementsLocation m_pump, m_valve, m_fitting, m_electrical;

	private slots:
		void init()
		{
			m_project = new QETProject();
			m_project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("test"), NamesList());
			QDomDocument d1, d2, d3, d4;
			m_pump = add(*m_project, "pump.elmt", definition(d1, "11111111-1111-1111-1111-111111111111", "Pump",
							QStringLiteral("pid_family=\"equipment\" pid_class=\"pump\" prefix=\"PC\"")));
			m_valve = add(*m_project, "valve.elmt", definition(d2, "22222222-2222-2222-2222-222222222222", "Valve",
							QStringLiteral("pid_family=\"valve\" pid_class=\"valve\"")));
			m_fitting = add(*m_project, "tee.elmt", definition(d3, "33333333-3333-3333-3333-333333333333", "Tee",
							QStringLiteral("pid_family=\"pipe\"")));
			m_electrical = add(*m_project, "coil.elmt", definition(d4, "44444444-4444-4444-4444-444444444444", "Coil",
							QString()));
			QVERIFY(m_pump.exist() && m_valve.exist() && m_fitting.exist() && m_electrical.exist());
			m_folio = m_project->addNewDiagram();
		}

		void cleanup()
		{
			delete m_project;
			m_project = nullptr;
		}

			// A file declares the data, by class; what cannot be one is left out
		void aFileDeclaresTheData()
		{
			int skipped = -1;
			const PidCodification read = PidCodification::fromCsv(dataLines() + QStringLiteral(
				"donnée;pump;pid_Flow;Débit;m3/h;nombre\n"          // French words, the prefix, a capital
				"data;pump;unit;Unit;;text\n"                       // a field of the tag
				"data;pump;power;Again;kW;number\n"                 // said twice
				"data;pump;speed;Speed;rpm;quantity\n"              // no such kind
				"data;;head;Head;m;number\n"                        // no class
				"data;pump;duty;Duty;;list\n"                       // a list without values
				"data;pump;model;Model;;text;a|b\n"                 // values without a list
				"data;pump;2nd;Second;;text\n"), &skipped);         // a name which cannot be one
			QCOMPARE(skipped, 7);
			QCOMPARE(read.data_fields.size(), 6);
			QVERIFY(read.hasCodes());
			QVERIFY(!read.hasCodeLists());

			const PidCodification::DataField power = read.data_fields.at(0);
			QCOMPARE(power.name, QStringLiteral("power"));
			QCOMPARE(power.key(), QStringLiteral("pid_power"));
			QCOMPARE(power.classes, QStringList({QStringLiteral("pump"), QStringLiteral("blower")}));
			QCOMPARE(power.title(), QStringLiteral("Rated power"));
			QCOMPARE(power.unit, QStringLiteral("kW"));
			QCOMPARE(power.kind, PidCodification::DataKind::Number);

			const PidCodification::DataField starter = read.data_fields.at(1);
			QCOMPARE(starter.kind, PidCodification::DataKind::List);
			QCOMPARE(starter.values, QStringList({QStringLiteral("direct"), QStringLiteral("soft starter"),
												 QStringLiteral("drive")}));
			QCOMPARE(read.data_fields.at(2).kind, PidCodification::DataKind::YesNo);
				// no label: the name; the name of DEXPI is kept
			QCOMPARE(read.data_fields.at(4).title(), QStringLiteral("note"));
			QCOMPARE(read.data_fields.at(4).dexpi, QStringLiteral("PumpNote"));
			QCOMPARE(read.data_fields.at(5).key(), QStringLiteral("pid_flow"));
			QCOMPARE(read.data_fields.at(5).kind, PidCodification::DataKind::Number);

				// by class, in the order of the file
			QCOMPARE(read.dataFieldsOf(QStringLiteral("pump")).size(), 6);
			QCOMPARE(read.dataFieldsOf(QStringLiteral("blower")).size(), 3);
			QVERIFY(read.dataFieldsOf(QStringLiteral("valve")).isEmpty());
			QVERIFY(read.dataFieldsOf(QString()).isEmpty());
		}

			// The project keeps them, and gives them back as a file of codes;
			// a project without any writes nothing
		void theProjectKeepsTheData()
		{
			const PidCodification read = PidCodification::fromCsv(dataLines());
			QDomDocument doc;
			QDomElement holder = doc.createElement(QStringLiteral("project"));
			holder.appendChild(read.toXml(doc));
			QCOMPARE(holder.firstChildElement().elementsByTagName(QStringLiteral("data_field")).size(), 5);
			PidCodification back;
			back.fromXml(holder);
			QCOMPARE(back, read);

			QCOMPARE(PidCodification::fromCsv(read.codesToCsv()), read);
			QVERIFY(read.codesToCsv().contains(QStringLiteral("data;pump blower;power;Rated power;kW;number\n")));
			QVERIFY(!read.presentationToCsv().contains(QStringLiteral("power")));

			PidCodification plain;
			plain.fluids << PidCodification::Entry{QStringLiteral("W01"), QStringLiteral("Raw water"), QString()};
			QDomDocument other;
			QVERIFY(plain.toXml(other).elementsByTagName(QStringLiteral("data_field")).isEmpty());
			QVERIFY(!plain.codesToCsv().contains(QStringLiteral("data")));

				// a file of fluids takes no data away; one of data replaces them
			const PidCodification kept = plain.completedWith(read);
			QCOMPARE(kept.data_fields, read.data_fields);
			QCOMPARE(kept.fluids.size(), 1);
			const PidCodification one = PidCodification::fromCsv(QStringLiteral("data;pump;head;Head;m;number\n"));
			const PidCodification replaced = one.completedWith(kept);
			QCOMPARE(replaced.data_fields.size(), 1);
			QCOMPARE(replaced.fluids.size(), 1);
		}

			// The section has the lines of the class of the symbol, and only
			// a P&ID symbol which has some has the section
		void theSectionHasTheDataOfTheClass()
		{
			Element *pump = place(m_folio, m_pump);
			Element *valve = place(m_folio, m_valve);
			Element *coil = place(m_folio, m_electrical);
			QVERIFY(pump && valve && coil);
			QVERIFY(!ElementDataWidget::isWanted(pump));   // the project declares nothing yet

			m_project->setPidCodification(PidCodification::fromCsv(dataLines()));
			QVERIFY(ElementDataWidget::isWanted(pump));
			QVERIFY(!ElementDataWidget::isWanted(valve));
			QVERIFY(!ElementDataWidget::isWanted(coil));
			QVERIFY(!ElementDataWidget::isWanted(nullptr));

			ElementDataWidget section(pump);
			QVERIFY(qobject_cast<QLineEdit *>(section.fieldOf(QStringLiteral("pid_power"))));
			QVERIFY(qobject_cast<QComboBox *>(section.fieldOf(QStringLiteral("pid_starter"))));
			QVERIFY(qobject_cast<QCheckBox *>(section.fieldOf(QStringLiteral("pid_insulated"))));
			QVERIFY(qobject_cast<QCheckBox *>(section.fieldOf(QStringLiteral("pid_checked"))));
			QVERIFY(qobject_cast<QLineEdit *>(section.fieldOf(QStringLiteral("pid_note"))));
			QVERIFY(!section.fieldOf(QStringLiteral("pid_unit")));
				// named in clear, with the unit
			QStringList labels;
			for (QLabel *label : section.findChildren<QLabel *>()) labels << label->text();
			QVERIFY(labels.contains(QStringLiteral("Rated power:")));
			QVERIFY(labels.contains(QStringLiteral("kW")));
			QVERIFY(labels.contains(QStringLiteral("note:")));
				// nothing, then the values of the list
			QCOMPARE(qobject_cast<QComboBox *>(section.fieldOf(QStringLiteral("pid_starter")))->count(), 4);

				// in the P&ID tab of the properties
			ElementPropertiesWidget panel(pump);
			QVERIFY(panel.findChild<ElementDataWidget *>());
			ElementPropertiesWidget other(valve);
			QVERIFY(!other.findChild<ElementDataWidget *>());
		}

			// A line gives the element an information, with undo; emptied, it
			// takes it away
		void aLineIsAnInformationOfTheElement()
		{
			PidCodification codification = PidCodification::fromCsv(dataLines());
			codification.decimal = PidCodification::Decimal::Comma;
			m_project->setPidCodification(codification);
			Element *pump = place(m_folio, m_pump);
			ElementDataWidget section(pump);
			QVERIFY(!section.associatedUndo());

			auto *power = qobject_cast<QLineEdit *>(section.fieldOf(QStringLiteral("pid_power")));
			auto *starter = qobject_cast<QComboBox *>(section.fieldOf(QStringLiteral("pid_starter")));
			auto *insulated = qobject_cast<QCheckBox *>(section.fieldOf(QStringLiteral("pid_insulated")));
			QVERIFY(power && starter && insulated);
			power->setText(QStringLiteral("7,5"));
			starter->setCurrentIndex(starter->findData(QStringLiteral("drive")));
			insulated->setChecked(true);
			section.apply();
				// a file has a point, whatever the project writes
			QCOMPARE(held(pump, QStringLiteral("pid_power")), QStringLiteral("7.5"));
			QCOMPARE(held(pump, QStringLiteral("pid_starter")), QStringLiteral("drive"));
			QCOMPARE(held(pump, QStringLiteral("pid_insulated")), QStringLiteral("true"));
			QVERIFY(!pump->elementInformations().contains(QStringLiteral("pid_checked")));
			QVERIFY(!pump->elementInformations().contains(QStringLiteral("pid_note")));
			QCOMPARE(power->text(), QStringLiteral("7,5"));
			QVERIFY(!section.associatedUndo());

			m_folio->undoStack().undo();
			QVERIFY(!pump->elementInformations().contains(QStringLiteral("pid_power")));
			QCOMPARE(power->text(), QString());   // the section follows
			QVERIFY(!insulated->isChecked());
			m_folio->undoStack().redo();
			QCOMPARE(power->text(), QStringLiteral("7,5"));
			QCOMPARE(starter->currentData().toString(), QStringLiteral("drive"));

				// emptied, unticked, nothing picked: the information goes
			power->clear();
			starter->setCurrentIndex(0);
			insulated->setChecked(false);
			section.apply();
			for (const QString &key : {QStringLiteral("pid_power"), QStringLiteral("pid_starter"),
									   QStringLiteral("pid_insulated")}) {
				QVERIFY2(!pump->elementInformations().contains(key), qPrintable(key));
			}
		}

			// Live, a line is applied when it is left
		void liveALineIsAppliedWhenLeft()
		{
			m_project->setPidCodification(PidCodification::fromCsv(dataLines()));
			Element *pump = place(m_folio, m_pump);
			ElementDataWidget section(pump);
			section.setLiveEdit(true);
			auto *checked = qobject_cast<QCheckBox *>(section.fieldOf(QStringLiteral("pid_checked")));
			QVERIFY(checked);
			checked->click();
			QCOMPARE(held(pump, QStringLiteral("pid_checked")), QStringLiteral("true"));
			auto *note = qobject_cast<QLineEdit *>(section.fieldOf(QStringLiteral("pid_note")));
			note->setText(QStringLiteral("to be confirmed"));
			emit note->editingFinished();
			QCOMPARE(held(pump, QStringLiteral("pid_note")), QStringLiteral("to be confirmed"));
		}

			// A line which was not touched changes nothing, whatever it holds
		void anUntouchedLineChangesNothing()
		{
			m_project->setPidCodification(PidCodification::fromCsv(dataLines()));
			Element *pump = place(m_folio, m_pump);
			give(pump, QStringLiteral("pid_insulated"), QStringLiteral("false"));
			give(pump, QStringLiteral("pid_note"), QStringLiteral("two\nlines"));
			give(pump, QStringLiteral("pid_power"), QStringLiteral("about 7"));
			give(pump, QStringLiteral("pid_starter"), QStringLiteral("star-delta"));
			const DiagramContext before = pump->elementInformations();

			ElementDataWidget section(pump);
			QCOMPARE(section.edited(), before);
			QVERIFY(!section.associatedUndo());
				// a value the list has no more is shown, and kept
			auto *starter = qobject_cast<QComboBox *>(section.fieldOf(QStringLiteral("pid_starter")));
			QCOMPARE(starter->currentData().toString(), QStringLiteral("star-delta"));
			QCOMPARE(starter->count(), 5);

				// another line edited: these stay as they are
			qobject_cast<QCheckBox *>(section.fieldOf(QStringLiteral("pid_checked")))->setChecked(true);
			section.apply();
			QCOMPARE(held(pump, QStringLiteral("pid_checked")), QStringLiteral("true"));
			QCOMPARE(held(pump, QStringLiteral("pid_insulated")), QStringLiteral("false"));
			QCOMPARE(held(pump, QStringLiteral("pid_note")), QStringLiteral("two\nlines"));
			QCOMPARE(held(pump, QStringLiteral("pid_power")), QStringLiteral("about 7"));
		}

			// What is filled and the project does not declare for this class
			// is shown under its key, never hidden; emptied, its line goes
		void aValueNotDeclaredIsShown()
		{
			Element *valve = place(m_folio, m_valve);
			Element *tee = place(m_folio, m_fitting);
			give(valve, QStringLiteral("pid_power"), QStringLiteral("0.37"));
				// a field of the tag has its line in the Tag section...
			give(valve, PidTag::unitKey(), QStringLiteral("U01"));
			QVERIFY(ElementTagWidget::shownKeys(valve).contains(PidTag::unitKey()));
			const QList<PidCodification::DataField> fields = ElementDataWidget::fieldsOf(valve);
			QCOMPARE(fields.size(), 1);
			QCOMPARE(fields.first().key(), QStringLiteral("pid_power"));
				// ...unless the symbol has no tag: then here
			give(tee, PidTag::unitKey(), QStringLiteral("U01"));
			QVERIFY(ElementTagWidget::shownKeys(tee).isEmpty());
			QCOMPARE(ElementDataWidget::fieldsOf(tee).size(), 1);

			ElementDataWidget section(valve);
			auto *power = qobject_cast<QLineEdit *>(section.fieldOf(QStringLiteral("pid_power")));
			QVERIFY(power);
			QCOMPARE(power->text(), QStringLiteral("0.37"));
			QStringList labels;
			for (QLabel *label : section.findChildren<QLabel *>()) labels << label->text();
			QVERIFY(labels.contains(QStringLiteral("pid_power:")));

			section.setLiveEdit(true);
			power->clear();
			emit power->editingFinished();
			QVERIFY(!valve->elementInformations().contains(QStringLiteral("pid_power")));
			QVERIFY(!section.fieldOf(QStringLiteral("pid_power")));
			QCOMPARE(held(valve, PidTag::unitKey()), QStringLiteral("U01"));
				// undone, the line is back
			m_folio->undoStack().undo();
			QVERIFY(section.fieldOf(QStringLiteral("pid_power")));
		}

			// What may not be said together is one datum with a list: a
			// standby is installed, or kept in its crate, or the symbol is none
		void aStandbyIsOneListOfThreeStates()
		{
			m_project->setPidCodification(PidCodification::fromCsv(dataLines() + QStringLiteral(
				"data;pump;standby;Standby;;list;installed|crate\n")));
			Element *pump = place(m_folio, m_pump);
			ElementDataWidget section(pump);
			section.setLiveEdit(true);
			auto *standby = qobject_cast<QComboBox *>(section.fieldOf(QStringLiteral("pid_standby")));
			QVERIFY(standby);
			QCOMPARE(standby->count(), 3);   // none, installed, crate
			QCOMPARE(standby->currentIndex(), 0);
			QVERIFY(!pump->elementInformations().contains(QStringLiteral("pid_standby")));

			standby->setCurrentIndex(standby->findData(QStringLiteral("crate")));
			emit standby->activated(standby->currentIndex());
			QCOMPARE(held(pump, QStringLiteral("pid_standby")), QStringLiteral("crate"));
			standby->setCurrentIndex(standby->findData(QStringLiteral("installed")));
			emit standby->activated(standby->currentIndex());
			QCOMPARE(held(pump, QStringLiteral("pid_standby")), QStringLiteral("installed"));
				// no standby: nothing is written
			standby->setCurrentIndex(0);
			emit standby->activated(0);
			QVERIFY(!pump->elementInformations().contains(QStringLiteral("pid_standby")));
		}

			// The Informations tab shows no "pid_" row for a P&ID symbol, and
			// keeps what it does not show when it writes the others back
		void theInformationsTabNeitherShowsNorLosesThem()
		{
			m_project->setPidCodification(PidCodification::fromCsv(dataLines()));
			Element *pump = place(m_folio, m_pump);
			give(pump, QStringLiteral("pid_power"), QStringLiteral("7.5"));
			give(pump, QStringLiteral("pid_other"), QStringLiteral("x"));
			give(pump, PidTag::unitKey(), QStringLiteral("U01"));
			give(pump, QStringLiteral("supplier_ref"), QStringLiteral("AB-12"));
			QVERIFY(ElementDataWidget::hides(pump, QStringLiteral("pid_power")));
			QVERIFY(!ElementDataWidget::hides(pump, QStringLiteral("supplier_ref")));

			ElementInfoWidget tab(pump);
			QCOMPARE(customKeys(&tab), QStringList(QStringLiteral("supplier_ref")));
				// what the tab would write back still has them
			const DiagramContext written = tab.currentInfo();
			QCOMPARE(written.value(QStringLiteral("pid_power")).toString(), QStringLiteral("7.5"));
			QCOMPARE(written.value(QStringLiteral("pid_other")).toString(), QStringLiteral("x"));
			QCOMPARE(written.value(PidTag::unitKey()).toString(), QStringLiteral("U01"));

				// another field typed there: the hidden ones stay
			for (ElementInfoPartWidget *field : tab.findChildren<ElementInfoPartWidget *>()) {
				if (field->key() == QStringLiteral("comment")) field->setText(QStringLiteral("typed"));
			}
			tab.apply();
			QCOMPARE(held(pump, QStringLiteral("comment")), QStringLiteral("typed"));
			QCOMPARE(held(pump, QStringLiteral("pid_power")), QStringLiteral("7.5"));
			QCOMPARE(held(pump, QStringLiteral("pid_other")), QStringLiteral("x"));
			QCOMPARE(held(pump, PidTag::unitKey()), QStringLiteral("U01"));
			QCOMPARE(held(pump, QStringLiteral("supplier_ref")), QStringLiteral("AB-12"));
				// none is hidden everywhere: the section has the two
			QVERIFY(ElementDataWidget(pump).fieldOf(QStringLiteral("pid_power")));
			QVERIFY(ElementDataWidget(pump).fieldOf(QStringLiteral("pid_other")));
		}

			// An electrical element has no P&ID tab: its custom properties
			// are all shown in the Informations tab, as before
		void anElectricalElementIsAsBefore()
		{
			m_project->setPidCodification(PidCodification::fromCsv(dataLines()));
			Element *coil = place(m_folio, m_electrical);
			give(coil, QStringLiteral("pid_power"), QStringLiteral("7.5"));
			give(coil, QStringLiteral("supplier_ref"), QStringLiteral("AB-12"));
			QVERIFY(!ElementDataWidget::hides(coil, QStringLiteral("pid_power")));
			QVERIFY(ElementDataWidget::fieldsOf(coil).isEmpty());

			ElementInfoWidget tab(coil);
			QCOMPARE(customKeys(&tab), QStringList({QStringLiteral("pid_power"), QStringLiteral("supplier_ref")}));
			QCOMPARE(tab.currentInfo().value(QStringLiteral("pid_power")).toString(), QStringLiteral("7.5"));
		}
};

QET_APP_TEST_MAIN(tst_elementdata)
#include "tst_elementdata.moc"
