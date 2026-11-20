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
#include "qetinformation.h"
#include "qetproject.h"
#include "qetresult.h"
#include "ui/configpage/pidconfigpage.h"
#include "undocommand/changepidcodificationcommand.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDomDocument>
#include <QImage>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QtMath>
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

		// An instrument bubble: no label, its identifier and the rest of its
		// tag in two texts of 8 points (the second one composite)
	static QDomElement bubble(QDomDocument &doc)
	{
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" pid_family=\"instrument\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{94444444-4444-4444-4444-444444444444}\"/><names><name lang=\"en\">B</name></names>"
			"<description>"
			"<dynamic_text x=\"-10\" y=\"-10\" z=\"10\" uuid=\"{b2222222-2222-2222-2222-222222222222}\" "
			"text_from=\"ElementInfo\" font=\"Liberation Sans,8,-1,5,50,0,0,0,0,0\">"
			"<text/><info_name>pid_tag_id</info_name></dynamic_text>"
			"<dynamic_text x=\"-10\" y=\"2\" z=\"10\" uuid=\"{b3333333-3333-3333-3333-333333333333}\" "
			"text_from=\"CompositeText\" font=\"Liberation Sans,8,-1,5,50,0,0,0,0,0\">"
			"<text/><composite_text>%{pid_tag_rest}</composite_text></dynamic_text>"
			"</description></definition>"));
		return doc.documentElement();
	}

		// A valve of family @p family (none: electrical) whose label is under
		// its drawing, centred, kept horizontal, as the symbols of the
		// collection have it (ajouter_repere.py)
	static QDomElement turnable(QDomDocument &doc, const QString &uuid, const QString &family)
	{
		const QString family_attr = family.isEmpty() ? QString()
			: QStringLiteral(" pid_family=\"%1\"").arg(family);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\"%2 "
			"width=\"40\" height=\"20\" hotspot_x=\"20\" hotspot_y=\"10\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">T</name></names><description>"
			"<line x1=\"-20\" y1=\"0\" x2=\"20\" y2=\"0\" length1=\"1.5\" length2=\"1.5\" end1=\"none\" end2=\"none\" "
			"antialias=\"false\" style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
			"<dynamic_text x=\"-4\" y=\"12\" z=\"100\" text_width=\"-1\" Halignment=\"AlignHCenter\" "
			"Valignment=\"AlignTop\" frame=\"false\" rotation=\"0\" keep_visual_rotation=\"true\" "
			"rotation_point_center=\"false\" text_from=\"ElementInfo\" "
			"uuid=\"{b4444444-4444-4444-4444-444444444444}\" font=\"Liberation Sans,9,-1,5,50,0,0,0,0,0\">"
			"<text/><info_name>label</info_name></dynamic_text>"
			"<terminal uuid=\"{a2222222-2222-2222-2222-222222222222}\" name=\"N1\" x=\"-20\" y=\"0\" orientation=\"w\" type=\"Generic\"/>"
			"<terminal uuid=\"{a3333333-3333-3333-3333-333333333333}\" name=\"N2\" x=\"20\" y=\"0\" orientation=\"e\" type=\"Generic\"/>"
			"</description></definition>").arg(uuid, family_attr));
		return doc.documentElement();
	}

		// The box of the label of @p element on the folio
	static QRectF labelBox(const Element *element)
	{
		const DynamicElementTextItem *text = element->labelTexts().first();
		return text->mapRectToScene(text->boundingRect());
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_pump, m_valve, m_wired, m_bubble, m_turnable, m_turnable_wired;
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

		// The area of the name of the first nozzle of @p element: above its
		// pipe, wherever its symbol puts it (step 5l-1o)
	static QRectF nozzleNameArea(const Element *element)
	{
		const Terminal *nozzle = element->terminals().first();
		const QFontMetrics metrics(nozzle->labelFont());
		const TerminalData::NozzleLabels labels = nozzle->nozzleLabelsFor(
					TerminalData::labelSize(metrics, nozzle->name()), QSizeF());
		const QTransform frame = nozzle->nozzleLabelFrame(labels.rotation);
		return nozzle->mapRectToScene(frame.mapRect(labels.name)).adjusted(-2, -2, 2, 2);
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("texts"), NamesList());
			QDomDocument a, b, c, d, e, f;
			m_bubble = add(m_project, "bubble.elmt", bubble(d));
			QVERIFY(m_bubble.exist());
			m_turnable = add(m_project, "turnable.elmt",
							 turnable(e, "95555555-5555-5555-5555-555555555555", "valve"));
			m_turnable_wired = add(m_project, "turnable_wired.elmt",
								   turnable(f, "96666666-6666-6666-6666-666666666666", QString()));
			QVERIFY(m_turnable.exist() && m_turnable_wired.exist());
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
			QVERIFY(!PidCodification().colored_designations);
			QVERIFY(!PidCodification().label_color.isValid());
			QVERIFY(!PidCodification().designation_color.isValid());
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
				//A former file: the labels and the designations
			QVERIFY(c.colored_labels);
			QVERIFY(c.colored_designations);
			QVERIFY(!c.nozzle_texts_on_paper);
		}

		void theInksAreReadFromACsvFile()
		{
			int skipped = -1;
			const PidCodification c = PidCodification::fromCsv(QStringLiteral(
				"text_option;label_ink;Objet\n"
				"option_texte;designation_ink;#0000FF\n"
				"text_option;label_ink;nothing\n"), &skipped);
			QCOMPARE(skipped, 1);
			QVERIFY(c.colored_labels);
			QVERIFY(!c.label_color.isValid());
			QVERIFY(!c.colored_designations);
			QCOMPARE(c.designation_color, QColor(0, 0, 255));
			QVERIFY(c.hasTextSettings());
		}

		void aFormerProjectColorsBoth()
		{
			QDomDocument document;
			document.setContent(QStringLiteral(
				"<project><pid_codification colored_labels=\"true\"/></project>"));
			PidCodification c;
			c.fromXml(document.documentElement());
			QVERIFY(c.colored_labels);
			QVERIFY(c.colored_designations);
				//Written again in the words of now
			QDomDocument written;
			const QDomElement block = c.toXml(written);
			QVERIFY(!block.hasAttribute(QStringLiteral("colored_labels")));
			QCOMPARE(block.attribute(QStringLiteral("label_ink")), QStringLiteral("object"));
			QCOMPARE(block.attribute(QStringLiteral("designation_ink")), QStringLiteral("object"));
		}

		void thePaletteKeepsTheSettingsAndNotTheCodes()
		{
			PidCodification c;
			c.fluids << PidCodification::Entry{"W01", "Raw water", "E"};
			c.text_sizes.insert("valve", 8);
			c.colored_labels = true;
			c.designation_color = QColor(0, 0, 255);
			c.nozzle_texts_on_paper = false;
			const PidCodification read = PidCodification::fromCsv(c.presentationToCsv());
			QVERIFY(read.fluids.isEmpty());
			QCOMPARE(read.text_sizes, c.text_sizes);
			QVERIFY(read.colored_labels);
			QVERIFY(!read.colored_designations);
			QCOMPARE(read.designation_color, QColor(0, 0, 255));
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
			c.label_color = QColor(0x80, 0, 0);
			c.colored_designations = true;
			c.vertical_texts = PidCodification::VerticalTexts::AsTurned;
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

		void aFixedColorInksTheLabels()
		{
			Element *pump = place(m_pump);
			PidCodification c;
			c.object_colors.insert("equipment", QColor("#ff0000"));
			c.label_color = QColor("#00ff00");
			setCodification(c);
			QCOMPARE(pump->labelInk(), QColor("#00ff00"));
				//Nothing is written in the text itself
			QCOMPARE(pump->labelTexts().first()->color(), QColor(Qt::black));
			m_folio->setDrawColoredConductors(false);
			const QColor ink = pump->labelInk();
			m_folio->setDrawColoredConductors(true);
			QVERIFY(!ink.isValid());
		}

			// Step 7k: turned a quarter, the label kept horizontal is beside
			// the line, its near edge as far from it as under the symbol
			// upright, at mid-height; nothing saved changes
		void theLabelOfATurnedSymbolIsBesideTheLine()
		{
			Element *valve = place(m_turnable);
			QVERIFY(valve);
			DiagramContext info = valve->elementInformations();
			info.addValue(QETInformation::ELMT_LABEL, QStringLiteral("U01 V 101"));
			valve->setElementInformations(info);
			DynamicElementTextItem *text = valve->labelTexts().first();
			QCOMPARE(text->toPlainText(), QStringLiteral("U01 V 101"));
			const QPointF axis = valve->scenePos();
			const QRectF upright = labelBox(valve);
			QVERIFY(upright.width() > upright.height());
			const qreal gap = upright.top() - axis.y();
			QVERIFY(gap > 0);
			const qreal along = upright.center().x() - axis.x();
			const QPointF pos = text->pos();

			qreal sides[2] = {0, 0};
			const int turns[2] = {90, 270};
			for (int i = 0 ; i < 2 ; ++i) {
				valve->setRotation(turns[i]);
				const QRectF box = labelBox(valve);
				QVERIFY2(qAbs(box.width() - upright.width()) < 0.5, "the text reads horizontally");
				QVERIFY2(box.left() > axis.x() || box.right() < axis.x(), "the text crosses the line");
				const qreal near_edge = box.left() > axis.x() ? box.left() - axis.x() : axis.x() - box.right();
				QVERIFY2(qAbs(near_edge - gap) < 0.5, qPrintable(QString::number(near_edge)));
				QVERIFY2(qAbs(qAbs(box.center().y() - axis.y()) - qAbs(along)) < 0.5,
						 qPrintable(QString::number(box.center().y() - axis.y())));
				sides[i] = box.center().x() - axis.x();
				QCOMPARE(text->pos(), pos);
			}
			QVERIFY(sides[0] * sides[1] < 0);

				//Upright again: as before
			valve->setRotation(0);
			QVERIFY(valve->labelBesideLine(text).isIdentity());
			const QRectF again = labelBox(valve);
			QVERIFY(qAbs(again.left() - upright.left()) < 0.01 && qAbs(again.top() - upright.top()) < 0.01);

				//An electrical symbol is not concerned
			Element *wired = place(m_turnable_wired);
			QVERIFY(wired);
			wired->setRotation(90);
			QVERIFY(wired->labelBesideLine(wired->labelTexts().first()).isIdentity());
		}

			// Step 7k-bis: a project which does not keep the texts of its
			// turned symbols horizontal has the label of a P&ID symbol turn
			// with it, the file being the same either way
		void theLabelFollowsTheSettingOfTheProject()
		{
			QVERIFY(m_project.uprightSymbolTexts());
			Element *valve = place(m_turnable);
			QVERIFY(valve);
			DynamicElementTextItem *text = valve->labelTexts().first();
			QVERIFY(text->followsProjectTurn());
			valve->setRotation(90);
			auto level = [](const DynamicElementTextItem *t) {
				return qAbs(t->sceneTransform().m12()) < 0.01;
			};
			QVERIFY(level(text));
			QDomDocument doc;
			const QString kept = text->toXml(doc).attribute(QStringLiteral("rotation"));

			m_project.setUprightSymbolTexts(false);
			const bool turned = !level(text) && text->turnsWithSymbol();
			const QString saved_turned = text->toXml(doc).attribute(QStringLiteral("rotation"));
			QVERIFY(valve->labelBesideLine(text).isIdentity());
				//Read again in that project: turned with its symbol
			QHash<Terminal *, int> out;
			QDomElement element_xml = valve->toXml(doc, out);
			Element *copy = place(m_turnable);
			QVERIFY(copy);
			QHash<int, Terminal *> in;
			QVERIFY(copy->fromXml(element_xml, in));
			const bool copy_turned = !level(copy->labelTexts().first());
				//Turned back upright: level, as in the symbol
			valve->setRotation(0);
			const bool level_upright = level(text);
			valve->setRotation(90);

			m_project.setUprightSymbolTexts(true);
			const bool level_again = level(text) && level(copy->labelTexts().first());
			QVERIFY(turned);
			QCOMPARE(saved_turned, kept);
			QVERIFY(copy_turned);
			QVERIFY(level_upright);
			QVERIFY(level_again);

				//An electrical symbol keeps its own setting
			Element *wired = place(m_turnable_wired);
			QVERIFY(!wired->labelTexts().first()->followsProjectTurn());
		}

			// Step 7k-bis: the texts of a nozzle turn with their symbol as
			// its texts do, and read from the bottom when the vertical texts
			// of the symbols do
		void theNozzlesReadAsTheTextsOfTheSymbol()
		{
				//Pure placement: turned clockwise, the name on the right
			const TerminalData::NozzleLabels down = TerminalData::nozzleLabels(
						Qet::South, true, QSizeF(20, 10), QSizeF(30, 10), 0, 0, true);
			QCOMPARE(down.rotation, 90.0);
			QVERIFY(down.name.left() > 0);
			QVERIFY(down.name.bottom() < 0);
			const TerminalData::NozzleLabels up = TerminalData::nozzleLabels(
						Qet::South, true, QSizeF(20, 10), QSizeF(30, 10));
			QCOMPARE(up.rotation, -90.0);
			QVERIFY(up.name.right() < 0);

			Element *valve = place(m_turnable);
			QVERIFY(valve);
			Terminal *west = valve->terminals().first();
			const bool from_bottom = DynamicElementTextItem::verticalFromBottom();
			DynamicElementTextItem::setVerticalFromBottom(false);
			m_project.setUprightSymbolTexts(false);
			valve->setRotation(90);
			const qreal clockwise = west->nozzleLabelsFor(QSizeF(20, 10), QSizeF()).rotation;
			valve->setRotation(270);
			const qreal anticlockwise = west->nozzleLabelsFor(QSizeF(20, 10), QSizeF()).rotation;
			valve->setRotation(90);
			DynamicElementTextItem::setVerticalFromBottom(true);
			const qreal from_bottom_on = west->nozzleLabelsFor(QSizeF(20, 10), QSizeF()).rotation;
			DynamicElementTextItem::setVerticalFromBottom(from_bottom);
			m_project.setUprightSymbolTexts(true);
			QCOMPARE(clockwise, 90.0);
			QCOMPARE(anticlockwise, -90.0);
			QCOMPARE(from_bottom_on, -90.0);
		}

			// Step 7k-ter: whatever the turn and the mirror of the symbol,
			// texts kept level or not, read from the bottom or not, a P&ID
			// text is never upside down; the label stays on its place; the
			// two texts of a bubble keep their order, the letters first
		void noTextIsUpsideDown_data()
		{
			QTest::addColumn<bool>("level");
			QTest::addColumn<bool>("from_bottom");
			QTest::newRow("turned, from the top") << false << false;
			QTest::newRow("turned, from the bottom") << false << true;
			QTest::newRow("level") << true << false;
		}

		void noTextIsUpsideDown()
		{
			QFETCH(bool, level);
			QFETCH(bool, from_bottom);
			const bool was_from_bottom = DynamicElementTextItem::verticalFromBottom();
			DynamicElementTextItem::setVerticalFromBottom(from_bottom);
			m_project.setUprightSymbolTexts(level);
			auto restore = qScopeGuard([&]() {
				DynamicElementTextItem::setVerticalFromBottom(was_from_bottom);
				m_project.setUprightSymbolTexts(true);
			});

				//The angle a text reads at on the folio, 0 to 359
			auto readAt = [](const QGraphicsItem *item) {
				const QTransform t = item->sceneTransform();
				const qreal a = qRadiansToDegrees(qAtan2(t.m12(), t.m11()));
				return qRound(a + 360) % 360;
			};
			auto centre = [](const QGraphicsItem *item) {
				return item->mapToScene(item->boundingRect().center());
			};
			auto turned = [](const QPointF &v, int angle) {
				return QTransform().rotate(angle).map(v);
			};
			auto near = [](const QPointF &a, const QPointF &b) {
				return (a - b).manhattanLength() < 0.5;
			};

			Element *valve = place(m_turnable);
			Element *instrument = place(m_bubble);
			QVERIFY(valve && instrument);
			DiagramContext info = valve->elementInformations();
			info.addValue(QETInformation::ELMT_LABEL, QStringLiteral("U01 V 101"));
			valve->setElementInformations(info);
			DynamicElementTextItem *label = valve->labelTexts().first();
			DynamicElementTextItem *letters = nullptr, *rest = nullptr;
			for (DynamicElementTextItem *text : instrument->labelTexts()) {
				(text->infoName() == QETInformation::PID_TAG_ID ? letters : rest) = text;
			}
			QVERIFY(letters && rest);
				//On the symbol upright: where each text of the bubble is
				//from the origin of the bubble
			const QPointF origin0 = instrument->scenePos();
			const QPointF letters0 = centre(letters) - origin0;
			const QPointF rest0 = centre(rest) - origin0;
			QVERIFY(letters0.y() < rest0.y());

			const int vertical = from_bottom ? 270 : 90;
			for (int mirror = 0 ; mirror < 3 ; ++mirror) {
				for (Element *e : {valve, instrument}) {
					e->setHorizontalMirror(mirror == 1);
					e->setVerticalMirror(mirror == 2);
				}
				for (int turn : {0, 90, 180, 270}) {
					valve->setRotation(turn);
					instrument->setRotation(turn);
					const QByteArray where = QStringLiteral("mirror %1, turn %2").arg(mirror).arg(turn).toUtf8();
					const char *at = where.constData();

					const int label_reads = readAt(label);
					QVERIFY2(label_reads != 180, at);
						//Turned clockwise, it reads from the top unless the
						//vertical texts read from the bottom; turned the
						//other way, from the bottom
					if (level) {
						QVERIFY2(label_reads == 0, at);
					} else if (turn == 90) {
						QVERIFY2(label_reads == vertical, at);
					} else if (turn == 270) {
						QVERIFY2(label_reads == 270, at);
					}
						//The middle of the label on the place the symbol
						//gives it, turned and mirrored; beside an upright
						//line when level (step 7k)
					if (!level || turn == 0 || turn == 180) {
						const QPointF place = valve->mapToScene(label->pos() + label->boundingRect().center());
						QVERIFY2(near(centre(label), place), at);
					}

						//The bubble: both texts read alike, never upside
						//down, the letters first
					const int reads = readAt(letters);
					QVERIFY2(reads == readAt(rest), at);
					QVERIFY2(reads != 180, at);
						//Part of the drawing of the bubble, they turn with
						//it even when the project keeps texts level
					QVERIFY2(reads == (turn == 90 ? vertical : turn == 270 ? 270 : 0), at);
					const QPointF origin = instrument->scenePos();
					QVERIFY2(near(centre(letters) - origin, turned(letters0, reads)), at);
					QVERIFY2(near(centre(rest) - origin, turned(rest0, reads)), at);
				}
			}
			for (Element *e : {valve, instrument}) {
				e->setHorizontalMirror(false);
				e->setVerticalMirror(false);
				e->setRotation(0);
			}
		}

			// Step 7k-quater: the project may say how the vertical texts of
			// its P&ID symbols read, before the setting of the computer
		void theReadingOfTheProjectComesFirst()
		{
			int skipped = -1;
			const PidCodification read = PidCodification::fromCsv(QStringLiteral(
				"text_option;vertical_texts;as_turned\n"
				"text_option;vertical_texts;sideways\n"), &skipped);
			QCOMPARE(skipped, 1);
			QCOMPARE(read.vertical_texts, PidCodification::VerticalTexts::AsTurned);
			QVERIFY(read.hasTextSettings());
			QCOMPARE(PidCodification::fromCsv(read.presentationToCsv()).vertical_texts,
					 PidCodification::VerticalTexts::AsTurned);

			const bool was_from_bottom = DynamicElementTextItem::verticalFromBottom();
			DynamicElementTextItem::setVerticalFromBottom(true);
			m_project.setUprightSymbolTexts(false);
			auto restore = qScopeGuard([&]() {
				DynamicElementTextItem::setVerticalFromBottom(was_from_bottom);
				m_project.setUprightSymbolTexts(true);
			});
			auto readAt = [](const QGraphicsItem *item) {
				const QTransform t = item->sceneTransform();
				return qRound(qRadiansToDegrees(qAtan2(t.m12(), t.m11())) + 360) % 360;
			};
			Element *valve = place(m_turnable);
			Element *wired = place(m_turnable_wired);
			QVERIFY(valve && wired);
			valve->setRotation(90);
			DynamicElementTextItem *label = valve->labelTexts().first();
			Terminal *west = valve->terminals().first();
			QCOMPARE(readAt(label), 270);

			PidCodification c;
			c.vertical_texts = PidCodification::VerticalTexts::AsTurned;
			setCodification(c);
			QVERIFY(!valve->readsVerticalFromBottom());
			QCOMPARE(readAt(label), 90);
			QCOMPARE(west->nozzleLabelsFor(QSizeF(20, 10), QSizeF()).rotation, 90.0);
				//An electrical symbol keeps the setting of the computer
			QVERIFY(wired->readsVerticalFromBottom());

			DynamicElementTextItem::setVerticalFromBottom(false);
			c.vertical_texts = PidCodification::VerticalTexts::FromBottom;
			setCodification(c);
			QCOMPARE(readAt(label), 270);
			QCOMPARE(west->nozzleLabelsFor(QSizeF(20, 10), QSizeF()).rotation, -90.0);

				//The page
			PidConfigPage page(&m_project);
			auto *list = page.findChild<QComboBox *>(QStringLiteral("vertical_texts"));
			QVERIFY(list);
			QCOMPARE(list->currentData().toInt(), int(PidCodification::VerticalTexts::FromBottom));
			list->setCurrentIndex(list->findData(int(PidCodification::VerticalTexts::Computer)));
			emit list->activated(list->currentIndex());
			page.applyProjectConf();
			QCOMPARE(m_project.pidCodification().vertical_texts, PidCodification::VerticalTexts::Computer);
			QCOMPARE(readAt(label), 90);
			valve->setRotation(0);
		}

		void theTextsOfABubbleArePartsOfTheLabel()
		{
			Element *instrument = place(m_bubble);
			QVERIFY(instrument);
				//Both take the colour (DynamicElementTextItem::drawnColor(),
				//which asks labelInk() of a label) and the size of the label
			const QList<DynamicElementTextItem *> texts = instrument->labelTexts();
			QCOMPARE(texts.size(), 2);
			PidCodification c;
			c.object_colors.insert("instrument", QColor("#ff0000"));
			c.colored_labels = true;
			c.text_sizes.insert("instrument", 11);
			setCodification(c);
			QCOMPARE(instrument->labelInk(), QColor("#ff0000"));
			for (const DynamicElementTextItem *text : texts) {
				QVERIFY(text->isLabel());
				QCOMPARE(text->font().pointSize(), 11);
			}
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
			const QRectF area = nozzleNameArea(pump);
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
			auto *colored = page.findChild<QRadioButton *>(QStringLiteral("label_ink_object"));
			auto *fixed = page.findChild<QRadioButton *>(QStringLiteral("label_ink_fixed"));
			auto *paper = page.findChild<QCheckBox *>(QStringLiteral("nozzle_texts_on_paper"));
			QVERIFY(size && colored && fixed && paper);
			QCOMPARE(size->value(), 0);
			QVERIFY(!colored->isChecked());
			QVERIFY(fixed->isChecked());
			QVERIFY(paper->isChecked());

			size->setValue(12);
			colored->setChecked(true);
			paper->setChecked(false);
			page.applyProjectConf();
			QCOMPARE(m_project.pidCodification().text_sizes.value("equipment"), 12);
			QVERIFY(m_project.pidCodification().colored_labels);
			QVERIFY(!m_project.pidCodification().colored_designations);
			QVERIFY(!m_project.pidCodification().nozzle_texts_on_paper);
			QCOMPARE(labelSize(pump), 12);

			PidConfigPage again(&m_project);
			QCOMPARE(again.findChild<QSpinBox *>(QStringLiteral("text_size_equipment"))->value(), 12);
			QVERIFY(again.findChild<QRadioButton *>(QStringLiteral("label_ink_object"))->isChecked());
			QVERIFY(again.findChild<QRadioButton *>(QStringLiteral("designation_ink_fixed"))->isChecked());
			QVERIFY(!again.findChild<QCheckBox *>(QStringLiteral("nozzle_texts_on_paper"))->isChecked());
		}

		void theButtonChoosesAFixedColor()
		{
			PidConfigPage page(&m_project);
			auto *object = page.findChild<QRadioButton *>(QStringLiteral("designation_ink_object"));
			auto *fixed = page.findChild<QRadioButton *>(QStringLiteral("designation_ink_fixed"));
			auto *button = page.findChild<QPushButton *>(QStringLiteral("designation_ink_color"));
			QVERIFY(object && fixed && button);
			QVERIFY(button->isEnabled());
			int asked = 0;
			page.setColorAsker([&asked](const QColor &current) {
				++asked;
				return current == QColor(Qt::black) ? QColor(0, 0, 255) : QColor(Qt::black);
			});
			button->click();
			QCOMPARE(asked, 1);
			page.applyProjectConf();
			QCOMPARE(m_project.pidCodification().designation_color, QColor(0, 0, 255));

				//The colour of its object: the button is of no use, the fixed
				//colour is not kept in the project
			object->setChecked(true);
			QVERIFY(!button->isEnabled());
			page.applyProjectConf();
			QVERIFY(m_project.pidCodification().colored_designations);
			QVERIFY(!m_project.pidCodification().designation_color.isValid());
				//But the page keeps it while it is open
			fixed->setChecked(true);
			page.applyProjectConf();
			QCOMPARE(m_project.pidCodification().designation_color, QColor(0, 0, 255));

				//Black is none
			button->click();
			page.applyProjectConf();
			QVERIFY(!m_project.pidCodification().designation_color.isValid());
			QVERIFY(!m_project.pidCodification().hasTextSettings());
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
