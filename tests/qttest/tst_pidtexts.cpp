// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "diagram.h"
#include "exportproperties.h"
#include "exportpropertieswidget.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "qetgraphicsitem/dynamicelementtextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "qetresult.h"
#include "ui/configpage/pidconfigpage.h"
#include "undocommand/changepidcodificationcommand.h"

#include <QCheckBox>
#include <QDomDocument>
#include <QImage>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QUndoStack>

// The texts of a P&ID: a size by category, set for a project, what has none
// keeps the size of its symbol. A label placed follows its category until its
// size is changed by hand. The names and diameters of the nozzles have their own
// box in a print or an export, which starts as the project says. The texts of a
// wire are not concerned.
class tst_pidtexts : public QObject
{
	Q_OBJECT

		// A symbol with a label of 8 points, and a terminal showing its name
	static QDomElement symbol(QDomDocument &doc, const QString &uuid,
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
			"<dynamic_text x=\"-10\" y=\"-20\" z=\"10\" uuid=\"{b1111111-1111-1111-1111-111111111111}\" "
			"text_from=\"ElementInfo\" font=\"Liberation Sans,8,-1,5,50,0,0,0,0,0\">"
			"<text/><info_name>label</info_name></dynamic_text>"
			"<terminal uuid=\"{a1111111-1111-1111-1111-111111111111}\" name=\"N1\" x=\"-10\" y=\"0\" "
			"orientation=\"w\" type=\"Generic\"%3 show_name=\"true\" label_x=\"0\" label_y=\"10\"/>"
			"</description></definition>").arg(uuid, family_attr, medium_attr));
		return doc.documentElement();
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_pump, m_valve, m_wired;
	qreal m_next_y = 0;

	ElementsLocation add(QETProject &project, const QString &file, const QDomElement &definition)
	{
		project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/texts"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/texts/") + file, &project);
	}

	Element *place(const ElementsLocation &location, Diagram *folio = nullptr)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		(folio ? folio : m_folio)->addItem(element);
		m_next_y += 100;
		element->setPos(200, m_next_y);
		return element;
	}

	void setCodification(const PidCodification &codification)
	{
		m_project.undoStack()->push(new ChangePidCodificationCommand(&m_project, codification));
	}

	static int labelSize(const Element *element)
	{
		return element->labelTexts().first()->font().pointSize();
	}

	static void setLabelSize(Element *element, int size)
	{
		auto *text = element->labelTexts().first();
		QFont font = text->font();
		font.setPointSize(size);
		text->setFont(font);
	}

		// How many pixels of the area @p rect of the folio are not white,
		// the folio drawn at @p scale without the names of its terminals
	int inkOnPaper(const QRectF &rect, qreal scale)
	{
		const bool names = m_folio->drawTerminalNames();
		const bool terminals = m_folio->drawTerminals();
		const bool grid = m_folio->displayGrid();
		m_folio->setDrawTerminalNames(false);
		m_folio->setDrawTerminals(false);
		m_folio->setDisplayGrid(false);
		QImage image((rect.size() * scale).toSize(), QImage::Format_ARGB32);
		image.fill(Qt::white);
		{
			QPainter painter(&image);
			m_folio->render(&painter, QRectF(image.rect()), rect);
		}
		m_folio->setDrawTerminalNames(names);
		m_folio->setDrawTerminals(terminals);
		m_folio->setDisplayGrid(grid);
		int ink = 0;
		for (int y = 0 ; y < image.height() ; ++y) {
			for (int x = 0 ; x < image.width() ; ++x) {
				if (image.pixelColor(x, y) != QColor(Qt::white)) ++ink;
			}
		}
		return ink;
	}

		// The area of the name of the first terminal of @p element
	static QRectF labelArea(const Element *element)
	{
		return element->mapToScene(QRectF(-25, 5, 50, 30)).boundingRect();
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("texts"), NamesList());
			QDomDocument a, b, c;
			m_pump = add(m_project, "pump.elmt",
						 symbol(a, "91111111-1111-1111-1111-111111111111", "equipment", "pipe"));
			m_valve = add(m_project, "valve.elmt",
						  symbol(b, "92222222-2222-2222-2222-222222222222", "valve", "pipe"));
			m_wired = add(m_project, "wired.elmt",
						  symbol(c, "93333333-3333-3333-3333-333333333333", QString(), QString()));
			QVERIFY(m_pump.exist() && m_valve.exist() && m_wired.exist());
			m_folio = m_project.addNewDiagram();
		}

		void init()
		{
			m_project.undoStack()->clear();
			m_project.setPidCodification(PidCodification());
			for (Element *element : m_folio->elements()) {
				m_folio->removeItem(element);
				delete element;
			}
			m_next_y = 0;
		}

		void aProjectWithoutSettingsWritesNothing()
		{
			QDomDocument document;
			QVERIFY(PidCodification().toXml(document).isNull());
			QVERIFY(!PidCodification().hasTextSettings());
			QVERIFY(PidCodification().nozzle_texts_on_paper);
			QVERIFY(!PidCodification().colored_labels);
		}

		void theSettingsAreReadFromACsvFile()
		{
			int skipped = -1;
			const PidCodification c = PidCodification::fromCsv(QStringLiteral(
				"text_size;equipment;11\n"
				"taille_texte;nozzle;7\n"
				"text_option;colored_labels;true\n"
				"option_texte;nozzle_texts_on_paper;false\n"
				"text_size;valve;0\n"
				"text_size;valve;many\n"
				"text_option;unknown;true\n"
				"text_option;colored_labels;maybe\n"), &skipped);
			QCOMPARE(skipped, 4);
			QCOMPARE(c.text_sizes.value("equipment"), 11);
			QCOMPARE(c.text_sizes.value("nozzle"), 7);
			QVERIFY(!c.text_sizes.contains("valve"));
			QVERIFY(c.colored_labels);
			QVERIFY(!c.nozzle_texts_on_paper);
		}

		void thePaletteKeepsTheSettingsAndNotTheCodes()
		{
			PidCodification c;
			c.fluids << PidCodification::Entry{"W01", "Raw water", "E"};
			c.text_sizes.insert("valve", 8);
			c.colored_labels = true;
			c.nozzle_texts_on_paper = false;
			const PidCodification read = PidCodification::fromCsv(c.paletteToCsv());
			QVERIFY(read.fluids.isEmpty());
			QCOMPARE(read.text_sizes, c.text_sizes);
			QVERIFY(read.colored_labels);
			QVERIFY(!read.nozzle_texts_on_paper);
		}

		void aFileOfCodesKeepsTheSettingsOfTheProject()
		{
			PidCodification project;
			project.text_sizes.insert("valve", 8);
			PidCodification file;
			file.fluids << PidCodification::Entry{"W01", "Raw water", "E"};
			const PidCodification both = file.completedWith(project);
			QCOMPARE(both.fluids.size(), 1);
			QCOMPARE(both.text_sizes.value("valve"), 8);
		}

		void theSettingsAreKeptByEveryFormat_data()
		{
			QTest::addColumn<QString>("extension");
			QTest::newRow(".qet") << QStringLiteral(".qet");
			QTest::newRow(".qetz") << QStringLiteral(".qetz");
			QTest::newRow(".qetdb") << QStringLiteral(".qetdb");
		}

		void theSettingsAreKeptByEveryFormat()
		{
			QFETCH(QString, extension);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("texts") + extension);
			PidCodification c;
			c.text_sizes.insert("equipment", 11);
			c.colored_labels = true;
			c.nozzle_texts_on_paper = false;
			{
				QETProject project;
				project.addNewDiagram();
				project.setPidCodification(c);
				project.setFilePath(path);
				QVERIFY(project.write().isOk());
			}
			QETProject read(path);
			QVERIFY(read.pidCodification() == c);
		}

		void aPlacedLabelFollowsItsFamily()
		{
			Element *pump = place(m_pump);
			Element *valve = place(m_valve);
			QVERIFY(pump && valve);
			QCOMPARE(labelSize(pump), 8);

			PidCodification c;
			c.text_sizes.insert("equipment", 11);
			setCodification(c);
			QCOMPARE(labelSize(pump), 11);
			QCOMPARE(labelSize(valve), 8);
		}

		void aSizeSetByHandStays()
		{
			Element *pump = place(m_pump);
			Element *by_hand = place(m_pump);
			setLabelSize(by_hand, 20);

			PidCodification c;
			c.text_sizes.insert("equipment", 11);
			setCodification(c);
			QCOMPARE(labelSize(pump), 11);
			QCOMPARE(labelSize(by_hand), 20);

			c.text_sizes.insert("equipment", 13);
			setCodification(c);
			QCOMPARE(labelSize(pump), 13);
			QCOMPARE(labelSize(by_hand), 20);
		}

		void oneUndoGivesEverythingBack()
		{
			Element *pump = place(m_pump);
			const int steps = m_project.undoStack()->count();
			PidCodification c;
			c.text_sizes.insert("equipment", 11);
			setCodification(c);
			QCOMPARE(m_project.undoStack()->count(), steps + 1);

			m_project.undoStack()->undo();
			QCOMPARE(labelSize(pump), 8);
			QVERIFY(m_project.pidCodification().text_sizes.isEmpty());

			m_project.undoStack()->redo();
			QCOMPARE(labelSize(pump), 11);
			QCOMPARE(m_project.pidCodification().text_sizes.value("equipment"), 11);
		}

		void aCategoryEmptiedGoesBackToTheSymbol()
		{
			Element *pump = place(m_pump);
			PidCodification c;
			c.text_sizes.insert("equipment", 11);
			setCodification(c);
			setCodification(PidCodification());
			QCOMPARE(labelSize(pump), 8);
		}

		void aNewElementTakesTheSizeOfItsFamily()
		{
			PidCodification c;
			c.text_sizes.insert("valve", 7);
			Element *valve = place(m_valve);
			valve->takeLabelSize(c);
			QCOMPARE(labelSize(valve), 7);

			Element *wired = place(m_wired);
			wired->takeLabelSize(c);
			QCOMPARE(labelSize(wired), 8);
		}

		void anElectricalSymbolIsNotConcerned()
		{
			Element *wired = place(m_wired);
			const int terminal_size = wired->terminals().first()->labelFont().pointSize();
			PidCodification c;
			for (const QString &category : PidCodification::textCategories()) {
				c.text_sizes.insert(category, 20);
			}
			c.colored_labels = true;
			c.object_colors.insert("equipment", Qt::red);
			setCodification(c);
			QCOMPARE(labelSize(wired), 8);
			QCOMPARE(wired->labelSize(c), 0);
			QVERIFY(!wired->labelInk().isValid());
			QCOMPARE(wired->terminals().first()->labelFont().pointSize(), terminal_size);
		}

		void theNozzlesTakeTheSizeOfTheirCategory()
		{
			Element *pump = place(m_pump);
			Terminal *nozzle = pump->terminals().first();
			const int symbol_size = nozzle->labelFont().pointSize();
			const QRectF before = nozzle->boundingRect();

			PidCodification c;
			c.text_sizes.insert("nozzle", symbol_size + 10);
			setCodification(c);
			QCOMPARE(nozzle->labelFont().pointSize(), symbol_size + 10);
			QVERIFY(nozzle->boundingRect().height() > before.height());

			setCodification(PidCodification());
			QCOMPARE(nozzle->labelFont().pointSize(), symbol_size);
		}

		void theLabelTakesTheInkOfItsSymbolOnlyWhenAsked()
		{
			Element *pump = place(m_pump);
			PidCodification c;
			c.object_colors.insert("equipment", QColor("#ff0000"));
			setCodification(c);
			QVERIFY(!pump->labelInk().isValid());

			c.colored_labels = true;
			setCodification(c);
			QCOMPARE(pump->labelInk(), QColor("#ff0000"));
				//Nothing is written in the text itself
			QCOMPARE(pump->labelTexts().first()->color(), QColor(Qt::black));
		}

		void aFolioInBlackHasBlackLabels()
		{
			Element *pump = place(m_pump);
			PidCodification c;
			c.object_colors.insert("equipment", QColor("#ff0000"));
			c.colored_labels = true;
			setCodification(c);
			m_folio->setDrawColoredConductors(false);
			const QColor ink = pump->labelInk();
			m_folio->setDrawColoredConductors(true);
			QVERIFY(!ink.isValid());
		}

		void theNamesOfNozzlesAndOfTerminalsAreSetApart()
		{
			Element *pump = place(m_pump);
			Element *wired = place(m_wired);
			Terminal *nozzle = pump->terminals().first();
			Terminal *terminal = wired->terminals().first();

			ExportProperties properties;
			properties.draw_terminal_names = false;
			properties.draw_nozzle_names = true;
			const ExportProperties before = m_folio->applyProperties(properties);
			const bool nozzle_alone = nozzle->labelIsDrawn();
			const bool terminal_off = terminal->labelIsDrawn();

			properties.draw_terminal_names = true;
			properties.draw_nozzle_names = false;
			m_folio->applyProperties(properties);
			const bool nozzle_off = nozzle->labelIsDrawn();
			const bool terminal_alone = terminal->labelIsDrawn();

			m_folio->applyProperties(before);
			QVERIFY(nozzle_alone);
			QVERIFY(!terminal_off);
			QVERIFY(!nozzle_off);
			QVERIFY(terminal_alone);
			QVERIFY(m_folio->drawNozzleNames());
		}

		void theBoxOfTheNozzlesStartsFromTheProject()
		{
			ExportPropertiesWidget widget;
			auto *box = widget.findChild<QCheckBox *>(QStringLiteral("draw_nozzle_names"));
			QVERIFY(box);
			QVERIFY(box->isHidden());

			widget.offerNozzleNames(false);
			QVERIFY(!box->isHidden());
			QVERIFY(!widget.exportProperties().draw_nozzle_names);
			box->setChecked(true);
			QVERIFY(widget.exportProperties().draw_nozzle_names);
		}

		void aSmallImageKeepsTheNameOfANozzle()
		{
			Element *pump = place(m_pump);
			const QRectF area = labelArea(pump);
			QVERIFY(inkOnPaper(area, 2.0) > 0);
			const int ink = inkOnPaper(area, 0.4);
			QVERIFY(ink > 0);

			m_folio->setDrawNozzleNames(false);
			const int ink_without = inkOnPaper(area, 0.4);
			m_folio->setDrawNozzleNames(true);
			QVERIFY(ink_without < ink);
		}

		void aSmallImageOfATerminalStaysAsBefore()
		{
			Element *wired = place(m_wired);
			QCOMPARE(inkOnPaper(labelArea(wired), 0.4), 0);
		}

		void thePageSetsTheProject()
		{
			Element *pump = place(m_pump);
			PidConfigPage page(&m_project);
			auto *size = page.findChild<QSpinBox *>(QStringLiteral("text_size_equipment"));
			auto *colored = page.findChild<QCheckBox *>(QStringLiteral("colored_labels"));
			auto *paper = page.findChild<QCheckBox *>(QStringLiteral("nozzle_texts_on_paper"));
			QVERIFY(size && colored && paper);
			QCOMPARE(size->value(), 0);
			QVERIFY(!colored->isChecked());
			QVERIFY(paper->isChecked());

			size->setValue(12);
			colored->setChecked(true);
			paper->setChecked(false);
			page.applyProjectConf();
			QCOMPARE(m_project.pidCodification().text_sizes.value("equipment"), 12);
			QVERIFY(m_project.pidCodification().colored_labels);
			QVERIFY(!m_project.pidCodification().nozzle_texts_on_paper);
			QCOMPARE(labelSize(pump), 12);

			PidConfigPage again(&m_project);
			QCOMPARE(again.findChild<QSpinBox *>(QStringLiteral("text_size_equipment"))->value(), 12);
			QVERIFY(again.findChild<QCheckBox *>(QStringLiteral("colored_labels"))->isChecked());
			QVERIFY(!again.findChild<QCheckBox *>(QStringLiteral("nozzle_texts_on_paper"))->isChecked());
		}

		void aButtonGivesTheSizeOfTheSymbolBack()
		{
			PidCodification c;
			c.text_sizes.insert("equipment", 12);
			setCodification(c);

			PidConfigPage page(&m_project);
			auto *size = page.findChild<QSpinBox *>(QStringLiteral("text_size_equipment"));
			auto *reset = page.findChild<QPushButton *>(QStringLiteral("text_size_reset_equipment"));
			QVERIFY(size && reset);
			QCOMPARE(size->value(), 12);

			reset->click();
			QCOMPARE(size->value(), 0);
			page.applyProjectConf();
			QVERIFY(!m_project.pidCodification().text_sizes.contains("equipment"));
		}

		void ctrlZInTheOpenPageTakesBackWhatWasApplied()
		{
			Element *pump = place(m_pump);
			Terminal *nozzle = pump->terminals().first();
			const int symbol_size = nozzle->labelFont().pointSize();

			PidConfigPage page(&m_project);
			page.show();
			QVERIFY(QTest::qWaitForWindowActive(&page));
			auto *size = page.findChild<QSpinBox *>(QStringLiteral("text_size_nozzle"));
			QVERIFY(size);
			size->setValue(symbol_size + 10);
			page.applyProjectConf();
			QCOMPARE(nozzle->labelFont().pointSize(), symbol_size + 10);

			QTest::keySequence(&page, QKeySequence(QKeySequence::Undo));
			QCOMPARE(nozzle->labelFont().pointSize(), symbol_size);
			QCOMPARE(size->value(), 0);

			QTest::keySequence(&page, QKeySequence(QKeySequence::Redo));
			QCOMPARE(nozzle->labelFont().pointSize(), symbol_size + 10);
			QCOMPARE(size->value(), symbol_size + 10);

				//Another step of the project stays out of reach of this page
			m_project.undoStack()->push(new QUndoCommand(QStringLiteral("another step")));
			const int index = m_project.undoStack()->index();
			QTest::keySequence(&page, QKeySequence(QKeySequence::Undo));
			QCOMPARE(m_project.undoStack()->index(), index);
			QCOMPARE(nozzle->labelFont().pointSize(), symbol_size + 10);
		}

		void cleanupTestCase()
		{
			m_project.setModified(false);
		}
};

QET_APP_TEST_MAIN(tst_pidtexts)

#include "tst_pidtexts.moc"
