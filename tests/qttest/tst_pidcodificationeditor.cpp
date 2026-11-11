// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidtag.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "ui/configpage/pidconfigpage.h"
#include "undocommand/changepidcodificationcommand.h"

#include <QDomDocument>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUndoStack>

// The codification of a P&ID edited in the page of the project, without a
// file. A pipe keeps a code: a name changed touches no pipe, a code changed is
// given to the pipes and symbols bearing it in the same undo step, a code
// removed stays on them as free text after a question, a code is unique in its
// list. The file of codes is written as it is read. The codes are invented.
class tst_pidcodificationeditor : public QObject
{
	Q_OBJECT

		// A symbol of the family @p family with two terminals N1 and N2 of the
		// medium @p medium ("" for none)
	static QDomElement definition(QDomDocument &doc, const QString &uuid,
								  const QString &family, const QString &medium)
	{
		const QString family_attr = family.isEmpty() ? QString()
			: QStringLiteral(" pid_family=\"%1\"").arg(family);
		const QString medium_attr = medium.isEmpty() ? QString()
			: QStringLiteral(" medium=\"%1\"").arg(medium);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\"%2 "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names><description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%3/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%3/>"
			"</description></definition>").arg(uuid, family_attr, medium_attr));
		return doc.documentElement();
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_equipment, m_valve, m_electrical;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/codes"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/codes/") + file, &m_project);
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

		// A conductor between two symbols of a row of their own
	Conductor *draw(const ElementsLocation &location)
	{
		m_next_y += 100;
		Element *a = place(location, QPointF(0, m_next_y));
		Element *b = place(location, QPointF(200, m_next_y));
		if (!a || !b) return nullptr;
		auto *conductor = new Conductor(a->terminals().at(1), b->terminals().at(0));
		m_folio->addItem(conductor);
		return conductor;
	}

	Conductor *pipe(const QString &fluid, const QString &material)
	{
		Conductor *conductor = draw(m_equipment);
		if (!conductor) return nullptr;
		ConductorProperties properties = conductor->properties();
		properties.m_pipe.fluid = fluid;
		properties.m_pipe.material = material;
		conductor->setProperties(properties);
		return conductor;
	}

	Element *withUnit(const ElementsLocation &location, const QString &unit)
	{
		m_next_y += 100;
		Element *element = place(location, QPointF(0, m_next_y));
		if (!element) return nullptr;
		DiagramContext information = element->elementInformations();
		information.addValue(PidTag::unitKey(), unit);
		element->setElementInformations(information);
		return element;
	}

	static QString unitOf(const Element *element)
	{
		return element->elementInformations().value(PidTag::unitKey()).toString();
	}

	static PidCodification::Entry entry(const QString &code, const QString &name,
										const QString &family = QString())
	{
		PidCodification::Entry e;
		e.code = code;
		e.name = name;
		e.family = family;
		return e;
	}

		// The codification every test starts from
	void startFrom()
	{
		PidCodification codification;
		codification.fluids = {entry("W01", "Raw water", "W"), entry("W02", "Treated water", "W"),
							   entry("SL1", "Sludge", "S"), entry("A01", "Air", "A")};
		codification.materials = {entry("M01", "Steel"), entry("M02", "Cast iron")};
		codification.functional_units = {entry("U01", "Intake"), entry("U02", "Filters")};
		codification.object_colors.insert(QStringLiteral("valve"), QColor(Qt::blue));
		m_project.undoStack()->push(new ChangePidCodificationCommand(&m_project, codification));
	}

	static int rowOf(const QTableWidget *table, const QString &code)
	{
		for (int row = 0 ; row < table->rowCount() ; ++row) {
			if (table->item(row, 0) && table->item(row, 0)->text() == code) return row;
		}
		return -1;
	}

	static void selectRow(QTableWidget *table, int row)
	{
		table->clearSelection();
		table->selectRow(row);
	}

	static QString message(const PidConfigPage &page)
	{
		return page.findChild<QLabel *>(QStringLiteral("codification_message"))->text();
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("codes"), NamesList());
			QDomDocument a, b, c;
			m_equipment = add("equipment.elmt",
							  definition(a, "71111111-1111-1111-1111-111111111111", "equipment", "pipe"));
			m_valve = add("valve.elmt",
						  definition(b, "72222222-2222-2222-2222-222222222222", "valve", "pipe"));
			m_electrical = add("electrical.elmt",
							   definition(c, "73333333-3333-3333-3333-333333333333", "", ""));
			QVERIFY(m_equipment.exist() && m_valve.exist() && m_electrical.exist());
			m_folio = m_project.addNewDiagram();
		}

		void init()
		{
			startFrom();
		}

			// ---- the file of codes ----

		void aFileOfCodesIsReadBackAsWritten()
		{
			PidCodification codification;
			codification.fluids = {entry("W01", "Water; raw", "W"), entry("R01", "Reagent \"A\"", "R"),
								   entry("A01", "Air")};
			codification.materials = {entry("M01", "Steel")};
			codification.functional_units = {entry("U01", "Intake")};
			codification.fluid_family_colors.insert(QStringLiteral("W"), QColor(Qt::green));
			codification.text_sizes.insert(QStringLiteral("valve"), 9);
			int skipped = -1;
			const PidCodification read = PidCodification::fromCsv(codification.toCsv(), &skipped);
			QCOMPARE(skipped, 0);
			QCOMPARE(read, codification);
		}

		void aFieldBetweenQuotesMayHoldTheSeparator()
		{
			const PidCodification read = PidCodification::fromCsv(
						QStringLiteral("fluid;\"W01\";\"Water; raw\";W\n"
									   "fluid;W02;\"Say \"\"treated\"\"\"\n"));
			QCOMPARE(read.fluids.size(), 2);
			QCOMPARE(read.fluids.at(0).name, QStringLiteral("Water; raw"));
			QCOMPARE(read.fluids.at(0).family, QStringLiteral("W"));
			QCOMPARE(read.fluids.at(1).name, QStringLiteral("Say \"treated\""));
		}

		void aFileNotInUtf8IsReadAsLatin1()
		{
			const QByteArray latin1 = QStringLiteral("fluid;W01;Eau épaissie").toLatin1();
			QCOMPARE(PidCodification::textOfFile(latin1), QStringLiteral("fluid;W01;Eau épaissie"));
			const QByteArray utf8 = QStringLiteral("fluid;W01;Eau épaissie").toUtf8();
			QCOMPARE(PidCodification::textOfFile(utf8), QStringLiteral("fluid;W01;Eau épaissie"));
		}

			// ---- the tables of the page ----

		void thePageShowsTheCodesOfTheProject()
		{
			PidConfigPage page(&m_project);
			QCOMPARE(page.codeTable(PidCodification::List::Fluids)->rowCount(), 4);
			QCOMPARE(page.codeTable(PidCodification::List::Materials)->rowCount(), 2);
			QCOMPARE(page.codeTable(PidCodification::List::Units)->rowCount(), 2);
			const QTableWidget *fluids = page.codeTable(PidCodification::List::Fluids);
			const int row = rowOf(fluids, "SL1");
			QVERIFY(row >= 0);
			QCOMPARE(fluids->item(row, 1)->text(), QStringLiteral("Sludge"));
			QCOMPARE(fluids->item(row, 2)->text(), QStringLiteral("S"));
				// the family is shown beside the code, the name after them
			QCOMPARE(fluids->horizontalHeader()->visualIndex(2), 1);
			QCOMPARE(fluids->horizontalHeader()->visualIndex(1), 2);
		}

		void codesComeInTheirNaturalOrder()
		{
			QVERIFY(PidCodification::codeLessThan("W2", "W10"));
			QVERIFY(!PidCodification::codeLessThan("W10", "W2"));
			QVERIFY(PidCodification::codeLessThan("W02", "W3"));
			QVERIFY(PidCodification::codeLessThan("w1", "W2"));
			QVERIFY(PidCodification::codeLessThan("W1", "W1A"));
			QVERIFY(PidCodification::codeLessThan("A99", "B01"));
				// two codes alike but not the same keep an order of their own
			QVERIFY(PidCodification::codeLessThan("W02", "W2") != PidCodification::codeLessThan("W2", "W02"));
			QVERIFY(!PidCodification::codeLessThan("W2", "W2"));

				// the project keeps its codes in that order, whatever its file says
			PidCodification codification = m_project.pidCodification();
			codification.fluids.prepend(entry("W10", "Water ten", "W"));
			codification.fluids.prepend(entry("W3", "Water three", "W"));
			m_project.undoStack()->push(new ChangePidCodificationCommand(&m_project, codification));
			QStringList codes;
			for (const auto &fluid : m_project.pidCodification().fluids) codes << fluid.code;
			QCOMPARE(codes, (QStringList{"A01", "SL1", "W01", "W02", "W3", "W10"}));

				// so does the table, and a line added takes its place
			PidConfigPage page(&m_project);
			QTableWidget *fluids = page.codeTable(PidCodification::List::Fluids);
			page.addEntry();
			QCOMPARE(fluids->rowCount(), 7);
			QTableWidgetItem *added = fluids->item(6, 0);
			added->setText(QStringLiteral("W15"));
			fluids->item(added->row(), 1)->setText(QStringLiteral("Water fifteen"));
			QStringList shown;
			for (int row = 0 ; row < fluids->rowCount() ; ++row) shown << fluids->item(row, 0)->text();
			QCOMPARE(shown, (QStringList{"A01", "SL1", "W01", "W02", "W3", "W10", "W15"}));
			QCOMPARE(fluids->item(6, 1)->text(), QStringLiteral("Water fifteen"));
				// a code changed too
			fluids->item(rowOf(fluids, "W01"), 0)->setText(QStringLiteral("W12"));
			QCOMPARE(fluids->item(5, 0)->text(), QStringLiteral("W12"));
			page.applyProjectConf();
			codes.clear();
			for (const auto &fluid : m_project.pidCodification().fluids) codes << fluid.code;
			QCOMPARE(codes, (QStringList{"A01", "SL1", "W02", "W3", "W10", "W12", "W15"}));
		}

		void aNameChangedTouchesNoPipe()
		{
			Conductor *line = pipe("W01", "M01");
			QVERIFY(line);
			const ConductorProperties before = line->properties();
			PidConfigPage page(&m_project);
			QTableWidget *fluids = page.codeTable(PidCodification::List::Fluids);
			fluids->item(rowOf(fluids, "W01"), 1)->setText(QStringLiteral("Water from the river"));
			QVERIFY(page.codeChanges().isEmpty());
			page.applyProjectConf();
			QCOMPARE(m_project.pidCodification().fluid("W01")->name, QStringLiteral("Water from the river"));
			QVERIFY(line->properties() == before);
		}

		void aCodeChangedReachesThePipesInOneStep()
		{
			Conductor *line = pipe("W01", "M01");
			Conductor *other = pipe("W02", "M02");
			Conductor *wire = draw(m_electrical);
			QVERIFY(line && other && wire);
			const ConductorProperties wire_before = wire->properties();
			PidConfigPage page(&m_project);
			QTableWidget *fluids = page.codeTable(PidCodification::List::Fluids);
			QTableWidget *materials = page.codeTable(PidCodification::List::Materials);
			QTableWidgetItem *renamed = fluids->item(rowOf(fluids, "W01"), 0);
			renamed->setText(QStringLiteral(" W10 "));
			materials->item(rowOf(materials, "M01"), 0)->setText(QStringLiteral("M10"));
				// the code is trimmed, and the page says what will happen
			QCOMPARE(renamed->text(), QStringLiteral("W10"));
			QVERIFY(message(page).contains(QStringLiteral("M10")));
			const auto changes = page.codeChanges();
			QCOMPARE(changes.fluids.value("W01"), QStringLiteral("W10"));
			QCOMPARE(changes.materials.value("M01"), QStringLiteral("M10"));
				// nothing before the page is applied
			QCOMPARE(line->properties().m_pipe.fluid, QStringLiteral("W01"));

			const int steps = m_project.undoStack()->count();
			page.applyProjectConf();
			QCOMPARE(m_project.undoStack()->count(), steps + 1);
			QCOMPARE(line->properties().m_pipe.fluid, QStringLiteral("W10"));
			QCOMPARE(line->properties().m_pipe.material, QStringLiteral("M10"));
			QCOMPARE(other->properties().m_pipe.fluid, QStringLiteral("W02"));
			QCOMPARE(other->properties().m_pipe.material, QStringLiteral("M02"));
			QVERIFY(m_project.pidCodification().fluid("W10"));
			QVERIFY(!m_project.pidCodification().fluid("W01"));
			QVERIFY(wire->properties() == wire_before);
				// the page reads the project again: nothing more to rename
			QVERIFY(page.codeChanges().isEmpty());

			m_project.undoStack()->undo();
			QCOMPARE(line->properties().m_pipe.fluid, QStringLiteral("W01"));
			QCOMPARE(line->properties().m_pipe.material, QStringLiteral("M01"));
			QVERIFY(m_project.pidCodification().fluid("W01"));
			m_project.undoStack()->redo();
			QCOMPARE(line->properties().m_pipe.fluid, QStringLiteral("W10"));
			QVERIFY(m_project.pidCodification().fluid("W10"));
		}

		void aCodeChangedReachesTheSymbols()
		{
			Element *pump = withUnit(m_equipment, "U01");
			Element *other = withUnit(m_equipment, "U02");
				// a valve whose tag starts with a fluid
			Element *valve = withUnit(m_valve, "W02");
				// an equipment never bears a fluid
			Element *odd = withUnit(m_equipment, "W02");
			QVERIFY(pump && other && valve && odd);
			PidConfigPage page(&m_project);
			QTableWidget *units = page.codeTable(PidCodification::List::Units);
			QTableWidget *fluids = page.codeTable(PidCodification::List::Fluids);
			units->item(rowOf(units, "U01"), 0)->setText(QStringLiteral("U10"));
			fluids->item(rowOf(fluids, "W02"), 0)->setText(QStringLiteral("W20"));
			page.applyProjectConf();
			QCOMPARE(unitOf(pump), QStringLiteral("U10"));
			QCOMPARE(unitOf(other), QStringLiteral("U02"));
			QCOMPARE(unitOf(valve), QStringLiteral("W20"));
			QCOMPARE(unitOf(odd), QStringLiteral("W02"));
			m_project.undoStack()->undo();
			QCOMPARE(unitOf(pump), QStringLiteral("U01"));
			QCOMPARE(unitOf(valve), QStringLiteral("W02"));
		}

		void aCodeTwiceOrEmptyIsRefused()
		{
			PidConfigPage page(&m_project);
			QTableWidget *fluids = page.codeTable(PidCodification::List::Fluids);
			const int row = rowOf(fluids, "W01");
			fluids->item(row, 0)->setText(QStringLiteral("W02"));
			QCOMPARE(fluids->item(row, 0)->text(), QStringLiteral("W01"));
			QVERIFY(message(page).contains(QStringLiteral("W02")));
			fluids->item(row, 0)->setText(QStringLiteral("  "));
			QCOMPARE(fluids->item(row, 0)->text(), QStringLiteral("W01"));
				// the same code in another list is no double
			QTableWidget *materials = page.codeTable(PidCodification::List::Materials);
			QTableWidgetItem *material = materials->item(0, 0);
			material->setText(QStringLiteral("W01"));
			QCOMPARE(material->text(), QStringLiteral("W01"));
		}

		void aLineIsAdded()
		{
			PidConfigPage page(&m_project);
			page.findChild<QTabWidget *>(QStringLiteral("code_lists"))->setCurrentIndex(1);
			page.addEntry();
			QTableWidget *materials = page.codeTable(PidCodification::List::Materials);
			QCOMPARE(materials->rowCount(), 3);
				// no entry as long as it has no code
			materials->item(2, 1)->setText(QStringLiteral("Stainless steel"));
			page.applyProjectConf();
			QCOMPARE(m_project.pidCodification().materials.size(), 2);
			page.findChild<QTabWidget *>(QStringLiteral("code_lists"))->setCurrentIndex(1);
			page.addEntry();
			materials->item(2, 0)->setText(QStringLiteral("M03"));
			materials->item(2, 1)->setText(QStringLiteral("Stainless steel"));
			QVERIFY(page.codeChanges().isEmpty());
			page.applyProjectConf();
			QCOMPARE(m_project.pidCodification().material("M03")->name, QStringLiteral("Stainless steel"));
		}

		void aCodeUsedIsRemovedAfterAQuestion()
		{
			Conductor *line = pipe("SL1", "M01");
			QVERIFY(line);
			PidConfigPage page(&m_project);
			QString asked;
			bool answer = false;
			page.setConfirmer([&](const QString &question) { asked = question; return answer; });
			QTableWidget *fluids = page.codeTable(PidCodification::List::Fluids);
			selectRow(fluids, rowOf(fluids, "SL1"));
			page.removeSelectedEntries();
			QVERIFY(asked.contains(QStringLiteral("1")));
			QCOMPARE(fluids->rowCount(), 4);

			answer = true;
			page.removeSelectedEntries();
			QCOMPARE(fluids->rowCount(), 3);
			page.applyProjectConf();
			QVERIFY(!m_project.pidCodification().fluid("SL1"));
				// the pipe keeps its code as free text
			QCOMPARE(line->properties().m_pipe.fluid, QStringLiteral("SL1"));

				// a code nothing bears goes without a question
			asked.clear();
			selectRow(fluids, rowOf(fluids, "A01"));
			page.removeSelectedEntries();
			QVERIFY(asked.isEmpty());
			QCOMPARE(fluids->rowCount(), 2);
		}

		void theFileOfThePageIsImportedByAnotherProject()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("codes.csv"));
			{
				PidConfigPage page(&m_project);
				QVERIFY(page.exportCsv(path));
			}
			QFile file(path);
			QVERIFY(file.open(QIODevice::ReadOnly));
			QVERIFY(file.readAll().startsWith("\xEF\xBB\xBF"));
			file.close();

			QETProject other;
			other.addNewDiagram();
			PidConfigPage page(&other);
			QVERIFY(page.importCsv(path));
				// nothing before the page is applied, no code renamed
			QVERIFY(other.pidCodification().isEmpty());
			QVERIFY(page.codeChanges().isEmpty());
			page.applyProjectConf();
			QCOMPARE(other.pidCodification(), m_project.pidCodification());

				// a file read again asks before replacing the codes
			page.setConfirmer([](const QString &) { return false; });
			QTableWidget *fluids = page.codeTable(PidCodification::List::Fluids);
			fluids->item(0, 1)->setText(QStringLiteral("Changed"));
			QVERIFY(!page.importCsv(path));
			QCOMPARE(fluids->item(0, 1)->text(), QStringLiteral("Changed"));
		}

		void aFileWithoutCodesKeepsThoseOfThePage()
		{
			QTemporaryDir dir;
			const QString path = dir.filePath(QStringLiteral("palette.csv"));
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			file.write("object_color;equipment;#ff0000\n");
			file.close();
			PidConfigPage page(&m_project);
			bool asked = false;
			page.setConfirmer([&](const QString &) { asked = true; return true; });
			QVERIFY(page.importCsv(path));
			QVERIFY(!asked);
			page.applyProjectConf();
			QCOMPARE(m_project.pidCodification().fluids.size(), 4);
			QCOMPARE(m_project.pidCodification().object_colors.value("equipment"), QColor(Qt::red));
		}

			// A file of sizes alone gives them to the materials the page has
			// already: those it names take its sizes, the others keep theirs
		void aFileOfSizesAloneGoesToTheMaterialsOfThePage()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("sizes.csv"));
			auto write = [&path](const QByteArray &lines) {
				QFile file(path);
				if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
				file.write(lines);
				return true;
			};
			QVERIFY(write("size;M01;;80\nsize;M01;;100\nsize;M09;;50\ndn_equal;60;65\n"));
			PidConfigPage page(&m_project);
			QString asked;
			bool answer = true;
			page.setConfirmer([&](const QString &question) { asked = question; return answer; });
			QVERIFY(page.importCsv(path));
				// no code replaced, no size either: nothing to ask; the
				// material the page has not is said
			QVERIFY(asked.isEmpty());
			QVERIFY(message(page).contains(QStringLiteral("M09")));
			QTableWidget *materials = page.codeTable(PidCodification::List::Materials);
			QCOMPARE(materials->rowCount(), 2);
			QCOMPARE(page.sizeTable()->rowCount(), 2);
			QVERIFY(page.codeChanges().isEmpty());
				// nothing before the page is applied
			QVERIFY(m_project.pidCodification().material("M01")->sizes.isEmpty());
			page.applyProjectConf();
			QCOMPARE(m_project.pidCodification().material("M01")->sizes,
					 (QList<PidCodification::Size>{{0, 80}, {0, 100}}));
			QVERIFY(m_project.pidCodification().material("M02")->sizes.isEmpty());
			QCOMPARE(m_project.pidCodification().fluids.size(), 4);
			QCOMPARE(m_project.pidCodification().equal_nominals, (QList<QPair<int, int>>{{60, 65}}));

				// sizes replaced: asked first
			QVERIFY(write("size;M01;;150\n"));
			answer = false;
			QVERIFY(!page.importCsv(path));
			QVERIFY(!asked.isEmpty());
			QCOMPARE(page.sizeTable()->rowCount(), 2);
			answer = true;
			QVERIFY(page.importCsv(path));
			QCOMPARE(page.sizeTable()->rowCount(), 1);
				// a code changed in the page is not forgotten for all that
			materials->item(rowOf(materials, "M02"), 0)->setText(QStringLiteral("M20"));
			QVERIFY(write("size;M20;;200\n"));
			QVERIFY(page.importCsv(path));
			QCOMPARE(page.codeChanges().materials.value("M02"), QStringLiteral("M20"));
			page.applyProjectConf();
			QCOMPARE(m_project.pidCodification().material("M20")->sizes,
					 (QList<PidCodification::Size>{{0, 200}}));
			QCOMPARE(m_project.pidCodification().material("M01")->sizes,
					 (QList<PidCodification::Size>{{0, 150}}));

				// materials the page has not, only: nothing changed, and said
			QVERIFY(write("size;M09;;50\n"));
			QVERIFY(!page.importCsv(path));
			QVERIFY(message(page).contains(QStringLiteral("M09")));
		}
};

QTEST_MAIN(tst_pidcodificationeditor)
#include "tst_pidcodificationeditor.moc"
