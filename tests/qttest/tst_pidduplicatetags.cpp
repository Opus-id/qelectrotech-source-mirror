// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "duplicatelabels.h"
#include "exportproperties.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidduplicatetags.h"
#include "pidtag.h"
#include "qetgraphicsitem/dynamicelementtextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetinformation.h"
#include "qetproject.h"
#include "qetresult.h"
#include "titleblockproperties.h"

#include <QDomDocument>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QStyleOptionGraphicsItem>
#include <QTemporaryDir>
#include <QUndoCommand>
#include <QUndoStack>

#include <memory>

// P&ID tags held twice (9h). A tag names one device in the whole project:
// two symbols with the same unit, identifier, train and order clash, whatever
// their folio, plant or location; a loop of instruments does not; a symbol
// without an order never does; a label typed by hand clashes with any label
// it equals. Folios not built count and stay so. On the screen, an orange
// veil behind the tag and a tool tip; "Give the next number" in "Correct";
// the duplicate list of upstream shows them too.
class tst_pidduplicatetags : public QObject
{
	Q_OBJECT

		// A symbol of 40 x 40 with a text showing its label
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

	static Element *place(Diagram *folio, const ElementsLocation &location, qreal x)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (element) {
			folio->addItem(element);
			element->setPos(x, 0);
		}
		return element;
	}

	static QString label(const Element *element)
	{
		return element->elementInformations().value(QStringLiteral("label")).toString();
	}

		// Give @p element a tag, its label made by the model of its project
	static Element *tag(Element *element, const QString &unit, const QString &order,
						const QString &letters = QString())
	{
		DiagramContext information = element->elementInformations();
		information.addValue(PidTag::unitKey(), unit);
		information.addValue(PidTag::trainKey(), QStringLiteral("1"));
		if (!order.isEmpty()) information.addValue(PidTag::orderKey(), order);
		if (!letters.isEmpty()) information.addValue(PidTag::lettersKey(), letters);
		element->setElementInformations(element->withPidLabel(information));
		return element;
	}

	static Element *withInfo(Element *element, const QString &key, const QString &value)
	{
		DiagramContext information = element->elementInformations();
		information.addValue(key, value);
		element->setElementInformations(information);
		return element;
	}

	static PidCodification codification()
	{
		PidCodification codification;
		const QString model = QStringLiteral("{unit} {id} {train}{order:2}");
		codification.tag_models.insert(QStringLiteral("equipment"), model);
		codification.tag_models.insert(QStringLiteral("instrument"), model);
		return codification;
	}

	static QList<QUuid> othersOf(const Element *element)
	{
		QList<QUuid> uuids;
		for (const PidDuplicateTags::Other &other : PidDuplicateTags::othersOf(element)) {
			uuids << other.element;
		}
		return uuids;
	}

		// The colour painted at the top left corner of the text of the tag
		// of @p element, on white
	static QColor cornerOfTag(Element *element)
	{
		DynamicElementTextItem *text = element->dynamicTextItems().value(0);
		if (!text) {
			return QColor();
		}
		const QRectF rect = text->boundingRect();
		QImage image(rect.size().toSize() + QSize(2, 2), QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		painter.translate(-rect.topLeft());
		QStyleOptionGraphicsItem option;
			// paint() is protected in the text, public in QGraphicsItem
		static_cast<QGraphicsItem *>(text)->paint(&painter, &option, nullptr);
		painter.end();
		return image.pixelColor(1, 1);
	}

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

			// The same unit, identifier, train and order on two folios
			// clash; another order or no order at all does not
		void theSameTagOnTwoFoliosClashes()
		{
			Diagram *second = m_project->addNewDiagram();
			Element *a = tag(place(m_folio, m_a, 0), "U01", "1");
			Element *b = tag(place(second, m_a, 0), "U01", "1");
			Element *c = tag(place(m_folio, m_a, 100), "U01", "2");
			Element *d = tag(place(m_folio, m_a, 200), "U02", "1");
			Element *e = tag(place(m_folio, m_a, 300), "U01", "");
			Element *f = tag(place(second, m_a, 300), "U01", "");
			QCOMPARE(label(a), QStringLiteral("U01 AA 101"));
			QCOMPARE(label(e), QStringLiteral("U01 AA 1"));   // the train, no order
			QCOMPARE(othersOf(a), QList<QUuid>{b->uuid()});
			QCOMPARE(othersOf(b), QList<QUuid>{a->uuid()});
			QVERIFY(othersOf(c).isEmpty());
			QVERIFY(othersOf(d).isEmpty());
			QVERIFY(othersOf(e).isEmpty());
			QVERIFY(othersOf(f).isEmpty());
			QCOMPARE(PidDuplicateTags::groups(m_project).size(), 1);

				// A tag changed: what is kept is read again
			tag(b, "U01", "3");
			QVERIFY(othersOf(a).isEmpty());
			QVERIFY(othersOf(b).isEmpty());
			tag(c, "U01", "03");
			QCOMPARE(othersOf(b), QList<QUuid>{c->uuid()});
		}

			// PT 105 and PIC 105 are one loop; two PT 105 clash
		void aLoopIsNoDuplicate()
		{
			Element *pt = tag(place(m_folio, m_i, 0), "U01", "5", "PT");
			Element *pic = tag(place(m_folio, m_i, 100), "U01", "5", "PIC");
			QVERIFY(othersOf(pt).isEmpty());
			QVERIFY(othersOf(pic).isEmpty());
			Element *again = tag(place(m_folio, m_i, 200), "U01", "5", "PT");
			QCOMPARE(othersOf(pt), QList<QUuid>{again->uuid()});
			QVERIFY(othersOf(pic).isEmpty());
		}

			// A label typed by hand on a symbol of a model clashes with the
			// labels it equals, of the model or typed; never with a tag of
			// the model which has no order
		void aLabelTypedByHand()
		{
			Element *made = tag(place(m_folio, m_a, 0), "U01", "1");
			Element *typed = withInfo(place(m_folio, m_a, 100), QStringLiteral("label"), QStringLiteral("U01 AA 101"));
			QCOMPARE(label(typed), QStringLiteral("U01 AA 101"));
			QCOMPARE(othersOf(made), QList<QUuid>{typed->uuid()});
			QCOMPARE(othersOf(typed), QList<QUuid>{made->uuid()});

			Element *x = withInfo(place(m_folio, m_a, 200), QStringLiteral("label"), QStringLiteral("X1"));
			Element *y = withInfo(place(m_folio, m_a, 300), QStringLiteral("label"), QStringLiteral("X1"));
			QCOMPARE(othersOf(x), QList<QUuid>{y->uuid()});

			Element *unnumbered = tag(place(m_folio, m_a, 400), "U02", "");
			QCOMPARE(label(unnumbered), QStringLiteral("U02 AA 1"));
			Element *same_text = withInfo(place(m_folio, m_a, 500), QStringLiteral("label"), QStringLiteral("U02 AA 1"));
			QVERIFY(othersOf(unnumbered).isEmpty());
			QVERIFY(othersOf(same_text).isEmpty());
		}

			// A device drawn twice on purpose is excluded from the parts list;
			// an electrical symbol is never compared here
		void excludedAndElectricalSymbols()
		{
			Element *a = tag(place(m_folio, m_a, 0), "U01", "1");
			Element *b = tag(place(m_folio, m_a, 100), "U01", "1");
			QCOMPARE(othersOf(a).size(), 1);
			withInfo(b, QStringLiteral("exclude_from_bom"), QStringLiteral("true"));
			QVERIFY(othersOf(a).isEmpty());
			QVERIFY(othersOf(b).isEmpty());

			Element *e1 = withInfo(place(m_folio, m_e, 200), QStringLiteral("label"), QStringLiteral("U01 AA 101"));
			Element *e2 = withInfo(place(m_folio, m_e, 300), QStringLiteral("label"), QStringLiteral("U01 AA 101"));
			QVERIFY(!PidDuplicateTags::comparedHere(e1));
			QVERIFY(othersOf(a).isEmpty());
			QVERIFY(othersOf(e1).isEmpty());
			QVERIFY(othersOf(e2).isEmpty());

				// Without a model of tag, nothing is compared here
			m_project->setPidCodification(PidCodification());
			QVERIFY(!PidDuplicateTags::comparedHere(a));
			QVERIFY(PidDuplicateTags::find(m_project).isEmpty());
		}

			// The duplicate list of upstream: two P&ID symbols of one tag
			// under two locations, and with "one designation, one device",
			// still clash; electrical symbols keep the rules of upstream
		void theDuplicateListComparesInTheWholeProject()
		{
			Element *a = withInfo(tag(place(m_folio, m_a, 0), "U01", "1"),
								  QETInformation::ELMT_LOCATION, QStringLiteral("L1"));
			Element *b = withInfo(tag(place(m_folio, m_a, 100), "U01", "1"),
								  QETInformation::ELMT_LOCATION, QStringLiteral("L2"));
			Element *e1 = withInfo(withInfo(place(m_folio, m_e, 200), QStringLiteral("label"), QStringLiteral("K1")),
								   QETInformation::ELMT_LOCATION, QStringLiteral("L1"));
			Element *e2 = withInfo(withInfo(place(m_folio, m_e, 300), QStringLiteral("label"), QStringLiteral("K1")),
								   QETInformation::ELMT_LOCATION, QStringLiteral("L2"));
			Q_UNUSED(e1)
			Q_UNUSED(e2)
			for (const auto rule : {DuplicateLabels::ComponentRule::LinkedOnly,
									DuplicateLabels::ComponentRule::SameDesignation})
			{
				const auto groups = DuplicateLabels::find(m_project, Qt::CaseSensitive, rule);
					// K1 in L1 and K1 in L2: two devices, as upstream says
				QCOMPARE(groups.size(), 1);
				const auto &group = groups.first();
				QCOMPARE(group.kind, DuplicateLabels::Kind::Component);
				QCOMPARE(group.label, QStringLiteral("U01 AA 101"));
				QCOMPARE(group.place, QString());
				QCOMPARE(group.entity_count, 2);
				QSet<QGraphicsObject *> items;
				for (const auto &occurrence : group.occurrences) items.insert(occurrence.item.data());
				QCOMPARE(items, (QSet<QGraphicsObject *>{a, b}));
			}
		}

			// A folio not built counts, read from the project database, and
			// stays so; the tool tip names its folio
		void aFolioNotBuiltCountsAndStaysSo()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			Element *a = tag(place(m_folio, m_a, 0), "U01", "1");
			Diagram *second = m_project->addNewDiagram();
			TitleBlockProperties block = second->border_and_titleblock.exportTitleBlock();
			block.title = QStringLiteral("Filters");
			second->border_and_titleblock.importTitleBlock(block);
			tag(place(second, m_a, 0), "U01", "1");
			const QUuid a_uuid = a->uuid();
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
			Element *back_a = nullptr;
			for (Element *element : first->elements()) {
				if (element->uuid() == a_uuid) back_a = element;
			}
			QVERIFY(back_a);

			QCOMPARE(PidDuplicateTags::othersOf(back_a).size(), 1);
			const QStringList warnings = PidDuplicateTags::warnings(back_a);
			QCOMPARE(warnings.size(), 2);
			QVERIFY2(warnings.at(1).contains(QStringLiteral("U01 AA 101"))
					 && warnings.at(1).contains(QStringLiteral("Filters")), qPrintable(warnings.at(1)));
			PidDuplicateTags::refresh();
			QVERIFY(!other->isLoaded());

				// "Give the next number" counts the folio not built too
			const auto proposal = PidDuplicateTags::proposal(back_a);
			QVERIFY(proposal);
			QCOMPARE(proposal->order, QStringLiteral("2"));
			QVERIFY(!other->isLoaded());
		}

			// The veil behind the tag, on the screen only; none without a
			// duplicate
		void theVeilBehindTheTag()
		{
			Element *a = tag(place(m_folio, m_a, 0), "U01", "1");
			Element *b = tag(place(m_folio, m_a, 100), "U01", "2");
			const QColor white(Qt::white);
			QCOMPARE(cornerOfTag(a), white);
			tag(b, "U01", "1");
			const QColor veiled = cornerOfTag(a);
			QCOMPARE(veiled.red(), 255);
			QVERIFY2(veiled.blue() < 230, qPrintable(veiled.name()));
				// Printed or exported as the options of rendering say
				// (9h-bis): by default, no mark
			ExportProperties paper;
			QVERIFY(!paper.draw_warnings);
			const ExportProperties screen = m_folio->applyProperties(paper);
			QVERIFY(screen.draw_warnings);
			QCOMPARE(cornerOfTag(a), white);
				// asked for: the mark, terminals or not
			paper.draw_warnings = true;
			m_folio->applyProperties(paper);
			QVERIFY(!m_folio->drawTerminals());
			QCOMPARE(cornerOfTag(a).red(), 255);
			QVERIFY(cornerOfTag(a) != white);
			m_folio->applyProperties(screen);
			QVERIFY(m_folio->drawWarnings());
			QCOMPARE(PidDuplicateTags::warnings(a).size(), 2);
			QVERIFY(PidDuplicateTags::warnings(tag(b, "U01", "4")).isEmpty());
			QCOMPARE(cornerOfTag(a), white);
		}

			// The option of rendering is kept with the others of an export
			// or a print, off when never given
		void theOptionOfRenderingIsKept()
		{
			QSettings settings(QSettings::IniFormat, QSettings::UserScope,
							   QStringLiteral("qet-tests"), QStringLiteral("tst_pidduplicatetags"));
			settings.clear();
			ExportProperties read;
			read.draw_warnings = true;
			read.fromSettings(settings, QStringLiteral("print/default"));
			QVERIFY(!read.draw_warnings);
			ExportProperties written;
			written.draw_warnings = true;
			written.toSettings(settings, QStringLiteral("print/default"));
			read.fromSettings(settings, QStringLiteral("print/default"));
			QVERIFY(read.draw_warnings);
			settings.clear();
		}

			// "Give the next number": this symbol alone, the next order of
			// its unit, identifier and train; one undo step
		void giveTheNextNumber()
		{
			Element *a = tag(place(m_folio, m_a, 0), "U01", "1");
			Element *b = tag(place(m_folio, m_a, 100), "U01", "1");
			tag(place(m_folio, m_a, 200), "U01", "6");
			const auto proposal = PidDuplicateTags::proposal(b);
			QVERIFY(proposal);
			QCOMPARE(proposal->text, QStringLiteral("Give the next number (U01 AA 107)"));
			QUndoCommand *undo = PidDuplicateTags::apply(*proposal);
			QVERIFY(undo);
			m_folio->undoStack().push(undo);
			QCOMPARE(label(b), QStringLiteral("U01 AA 107"));
			QCOMPARE(label(a), QStringLiteral("U01 AA 101"));
			QVERIFY(othersOf(a).isEmpty());
			QVERIFY(!PidDuplicateTags::proposal(a));
			m_folio->undoStack().undo();
			QCOMPARE(label(b), QStringLiteral("U01 AA 101"));
			QCOMPARE(othersOf(a), QList<QUuid>{b->uuid()});
				// Applied again when it is done already: nothing
			m_folio->undoStack().redo();
			QVERIFY(!PidDuplicateTags::apply(*proposal));

				// A label typed by hand: no number to give
			Element *x = withInfo(place(m_folio, m_a, 300), QStringLiteral("label"), QStringLiteral("X1"));
			withInfo(place(m_folio, m_a, 400), QStringLiteral("label"), QStringLiteral("X1"));
			QCOMPARE(othersOf(x).size(), 1);
			QVERIFY(!PidDuplicateTags::proposal(x));
				// From the text of the tag too
			Element *c = tag(place(m_folio, m_a, 500), "U01", "6");
			QVERIFY(PidDuplicateTags::proposal(c->dynamicTextItems().value(0)));
		}
};

QET_APP_TEST_MAIN(tst_pidduplicatetags)
#include "tst_pidduplicatetags.moc"
