// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidlinedesignation.h"
#include "pipeproperties.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/conductortextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "ui/pipepropertieswidget.h"

#include <QCheckBox>
#include <QDomDocument>
#include <QUndoStack>

// The designation of a pipe line (step 7b): the model the project gives the
// lines (a model of tags under the key "line", CSV "tag_model;line;...")
// puts the data of a pipe together, "{fluid} {size} {material}"; a pipe
// shows it when one chooses so (its text shown, none by default), above a
// horizontal pipe and on the left of a vertical one, in the size the project
// gives the designations and, when it colours the labels, in the colour of
// the pipe. A wire keeps its text as before.
class tst_pipedesignation : public QObject
{
	Q_OBJECT

		// A symbol with two terminals N1 and N2 of the medium @p medium (""
		// for none), west and east, or north and south when @p upright
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &medium,
								  bool upright = false)
	{
		const QString attr = medium.isEmpty()
				? QString()
				: QStringLiteral(" medium=\"%1\"").arg(medium);
		const QString terminals = upright
				? QStringLiteral("<terminal name=\"N1\" x=\"0\" y=\"-10\" orientation=\"n\" type=\"Generic\"%1/>"
								 "<terminal name=\"N2\" x=\"0\" y=\"10\" orientation=\"s\" type=\"Generic\"%1/>")
				: QStringLiteral("<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%1/>"
								 "<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%1/>");
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"%2"
			"</description></definition>").arg(uuid, terminals.arg(attr)));
		return doc.documentElement();
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_electrical, m_nozzles, m_upright;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/designation"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/designation/") + file, &m_project);
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

		// A horizontal conductor of @p length between two symbols of a row
		// of their own
	Conductor *draw(const ElementsLocation &location, qreal length = 200)
	{
		m_next_y += 100;
		Element *a = place(location, QPointF(0, m_next_y));
		Element *b = place(location, QPointF(length, m_next_y));
		if (!a || !b) return nullptr;
		auto *conductor = new Conductor(a->terminals().at(1), b->terminals().at(0));
		m_folio->addItem(conductor);
		return conductor;
	}

		// A vertical pipe, from a symbol down to another
	Conductor *drawUpright()
	{
		m_next_y += 100;
		Element *a = place(m_upright, QPointF(1000, m_next_y));
		Element *b = place(m_upright, QPointF(1000, m_next_y + 200));
		m_next_y += 200;
		if (!a || !b) return nullptr;
		auto *conductor = new Conductor(a->terminals().at(1), b->terminals().at(0));
		m_folio->addItem(conductor);
		return conductor;
	}

	static void setData(Conductor *pipe, const QString &fluid, int dn, bool shown = true)
	{
		ConductorProperties p = pipe->properties();
		p.m_pipe.fluid = fluid;
		p.m_pipe.nominal = dn ? PipeDiameter::nominal(dn) : PipeDiameter();
		p.m_show_text = shown;
		pipe->setProperties(p);
	}

	void useModel(const QString &csv)
	{
		m_project.setPidCodification(PidCodification::fromCsv(csv));
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("designation"), NamesList());
			QDomDocument a, b, c;
			m_electrical = add("electrical.elmt",
							   definition(a, "c1111111-1111-1111-1111-111111111111", ""));
			m_nozzles = add("nozzles.elmt",
							definition(b, "c2222222-2222-2222-2222-222222222222", "pipe"));
			m_upright = add("upright.elmt",
							definition(c, "c3333333-3333-3333-3333-333333333333", "pipe", true));
			QVERIFY(m_electrical.exist() && m_nozzles.exist() && m_upright.exist());
			m_folio = m_project.addNewDiagram();
		}

		void cleanup()
		{
			m_project.setPidCodification(PidCodification());
		}

			// The fields of the model, as typed; a missing one leaves no gap,
			// an unknown word stays, "\n" starts a line, no model gives nothing
		void theModelPutsTheDataTogether()
		{
			PidCodification codification;
			PipeProperties pipe;
			pipe.fluid = QStringLiteral("W01");
			pipe.line_number = QStringLiteral("4712");
			pipe.piping_class = QStringLiteral("75HB13");
			pipe.nominal_pressure = QStringLiteral("PN16");
			pipe.material = QStringLiteral("M02");
			pipe.flow = QStringLiteral("120 m3/h");
			pipe.nominal = PipeDiameter::nominal(80);

			QCOMPARE(PidLineDesignation::text(QStringLiteral("{fluid} {line_number} {piping_class} {dn}"),
											  pipe, codification),
					 QStringLiteral("W01 4712 75HB13 80"));
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{fluid} {size} {material} {pn}"),
											  pipe, codification),
					 QStringLiteral("W01 DN 80 M02 PN16"));
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{fluid}-{line_number}\\n{flow}"),
											  pipe, codification),
					 QStringLiteral("W01-4712\n120 m3/h"));
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{fluid} {spool} {size}"), pipe, codification),
					 QStringLiteral("W01 {spool} DN 80"));

			pipe.line_number.clear();
			QCOMPARE(PidLineDesignation::text(QStringLiteral("  {fluid}  {line_number} {dn} "),
											  pipe, codification),
					 QStringLiteral("W01 80"));
			QVERIFY(PidLineDesignation::text(QString(), pipe, codification).isEmpty());
			QVERIFY(PidLineDesignation::text(QStringLiteral("  "), pipe, codification).isEmpty());
		}

			// A pipe without a size says so: "DN ?", and "?" for the DN alone
		void aMissingSizeIsAsked()
		{
			PipeProperties pipe;
			pipe.fluid = QStringLiteral("W01");
			const PidCodification codification;
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{fluid} {size}"), pipe, codification),
					 QStringLiteral("W01 DN ?"));
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{fluid} {dn}"), pipe, codification),
					 QStringLiteral("W01 ?"));
		}

			// The size as the drawing writes it: the outside diameter of a
			// plastic, with the separator of the project; the DN alone stays
			// the DN. Equal DN without a material are written together.
		void theSizeIsWrittenAsOnTheDrawing()
		{
			PidCodification codification;
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.addSize(1100, 100);
			codification.materials = {plastic};
			codification.decimal = PidCodification::Decimal::Comma;
			QVERIFY(codification.addEqualNominals(60, 65));

			PipeProperties pipe;
			pipe.material = QStringLiteral("M01");
			pipe.nominal = PipeDiameter::nominal(100);
			pipe.outside = PipeDiameter::outside(110);
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{size}"), pipe, codification),
					 QStringLiteral("Ø 110"));
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{dn}"), pipe, codification),
					 QStringLiteral("100"));
			pipe.outside = PipeDiameter::outside(88.9);
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{size}"), pipe, codification),
					 QStringLiteral("Ø 88,9"));

			PipeProperties cast;
			cast.nominal = PipeDiameter::nominal(60);
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{size}"), cast, codification),
					 QStringLiteral("DN 60/65"));
			QCOMPARE(PidLineDesignation::text(QStringLiteral("{dn}"), cast, codification),
					 QStringLiteral("60/65"));
		}

			// Shown only on the pipes chosen; written after the model of the
			// project, which may change; no model, nothing shown
		void aPipeShowsItsDesignationWhenChosen()
		{
			useModel(QStringLiteral("tag_model;line;{fluid} {size}\n"));
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
				// none by default
			QVERIFY(!pipe->properties().m_show_text);
			QVERIFY(!pipe->textItem()->isVisible());

			setData(pipe, QStringLiteral("W01"), 100);
			QCOMPARE(pipe->textItem()->toPlainText(), QStringLiteral("W01 DN 100"));
			QCOMPARE(pipe->properties().text, QStringLiteral("W01 DN 100"));
			QVERIFY(pipe->properties().m_formula.isEmpty());
			QVERIFY(pipe->textItem()->isVisible());

			useModel(QStringLiteral("tag_model;line;{dn} {fluid}\n"));
			QCOMPARE(pipe->textItem()->toPlainText(), QStringLiteral("100 W01"));
			QCOMPARE(pipe->properties().text, QStringLiteral("100 W01"));

			setData(pipe, QStringLiteral("W01"), 0);
			QCOMPARE(pipe->textItem()->toPlainText(), QStringLiteral("? W01"));

			m_project.setPidCodification(PidCodification());
			QVERIFY(pipe->textItem()->toPlainText().isEmpty());
			QVERIFY(!pipe->textItem()->isVisible());
		}

			// Above a horizontal pipe, level; on the left of a vertical one,
			// read from the bottom; near the middle of the pipe
		void itStandsAboveOrOnTheLeft()
		{
			useModel(QStringLiteral("tag_model;line;{fluid} {size}\n"));
			Conductor *level = draw(m_nozzles);
			QVERIFY(level);
			setData(level, QStringLiteral("W01"), 100);
			const QRectF pipe = level->sceneBoundingRect();
			const QRectF text = level->textItem()->sceneBoundingRect();
			QCOMPARE(level->textItem()->rotation(), 0.0);
			QVERIFY2(text.bottom() <= pipe.center().y(),
					 qPrintable(QStringLiteral("text %1, pipe at %2").arg(text.bottom()).arg(pipe.center().y())));
			QVERIFY(qAbs(text.center().x() - pipe.center().x()) < 20);

			Conductor *upright = drawUpright();
			QVERIFY(upright);
			setData(upright, QStringLiteral("W01"), 100);
			const QRectF column = upright->sceneBoundingRect();
			const QRectF side = upright->textItem()->sceneBoundingRect();
			QCOMPARE(upright->textItem()->rotation(), 270.0);
			QVERIFY2(side.right() <= column.center().x(),
					 qPrintable(QStringLiteral("text %1, pipe at %2").arg(side.right()).arg(column.center().x())));
			QVERIFY(qAbs(side.center().y() - column.center().y()) < 20);
		}

			// The size of the designations of the project, else that of a
			// conductor text; the colour of the pipe when the project
			// colours the labels, else the colour of a text
		void theSizeAndTheColourComeFromTheProject()
		{
			useModel(QStringLiteral("tag_model;line;{fluid}\n"));
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			setData(pipe, QStringLiteral("W01"), 100);
			const int plain = pipe->properties().text_size;
			QCOMPARE(pipe->textItem()->font().pointSize(), plain);
			const QColor text_color = pipe->textItem()->color();

			useModel(QStringLiteral("tag_model;line;{fluid}\n"
									"text_size;line;14\n"
									"fluid;W01;Raw water;water\n"
									"fluid_color;water;#0000ff\n"));
			QCOMPARE(pipe->textItem()->font().pointSize(), 14);
			QCOMPARE(pipe->textItem()->color(), text_color);

			PidCodification colored = m_project.pidCodification();
			colored.colored_labels = true;
			m_project.setPidCodification(colored);
			QCOMPARE(pipe->textItem()->color(), QColor(0, 0, 255));
		}

			// A wire keeps its text, whatever the model of the lines
		void aWireIsLeftAlone()
		{
			Conductor *wire = draw(m_electrical);
			QVERIFY(wire);
			ConductorProperties p = wire->properties();
			p.text = QStringLiteral("X12");
			p.m_formula.clear();
			p.m_show_text = true;
			wire->setProperties(p);
			const ConductorProperties before = wire->properties();
			useModel(QStringLiteral("tag_model;line;{fluid} {size}\n"));
			QCOMPARE(wire->textItem()->toPlainText(), QStringLiteral("X12"));
			QVERIFY(wire->properties() == before);
		}

			// The page of a pipe: one box for the pipes edited, one undo
			// step; half checked when some of them show it
		void thePageChoosesThePipes()
		{
			useModel(QStringLiteral("tag_model;line;{fluid}\n"));
			Conductor *a = draw(m_nozzles);
			Conductor *b = draw(m_nozzles);
			QVERIFY(a && b);
			setData(a, QStringLiteral("W01"), 100, false);
			setData(b, QStringLiteral("W01"), 100, false);

			PipePropertiesWidget page({a, b});
			auto *box = page.findChild<QCheckBox *>(QStringLiteral("show_designation"));
			QVERIFY(box);
			QCOMPARE(box->checkState(), Qt::Unchecked);

			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.index();
			box->click();
			QCOMPARE(stack.index(), steps + 1);
			QVERIFY(a->properties().m_show_text && b->properties().m_show_text);
			QCOMPARE(box->checkState(), Qt::Checked);
			stack.undo();
			QVERIFY(!a->properties().m_show_text && !b->properties().m_show_text);
			QCOMPARE(box->checkState(), Qt::Unchecked);

			setData(a, QStringLiteral("W01"), 100, true);
			QCOMPARE(box->checkState(), Qt::PartiallyChecked);
			box->click();
			QVERIFY(a->properties().m_show_text && b->properties().m_show_text);
			QCOMPARE(box->checkState(), Qt::Checked);
		}

			// A pipe cut in pieces which all took its data writes its
			// designation once, on the longest piece
		void aCutWritesItOnce()
		{
			useModel(QStringLiteral("tag_model;line;{fluid}\n"));
			Conductor *long_piece = draw(m_nozzles, 400);
			Conductor *short_piece = draw(m_nozzles, 100);
			Conductor *hidden = draw(m_nozzles, 600);
			QVERIFY(long_piece && short_piece && hidden);
			setData(long_piece, QStringLiteral("W01"), 100);
			setData(short_piece, QStringLiteral("W01"), 100);
			setData(hidden, QStringLiteral("W01"), 100, false);
			Conductor::keepOneDesignation({short_piece, long_piece, hidden});
			QVERIFY(long_piece->properties().m_show_text);
			QVERIFY(!short_piece->properties().m_show_text);
			QVERIFY(!hidden->properties().m_show_text);
		}

			// A designation moved by hand stays where it was: the piece
			// nearest to it keeps it there, though another piece is longer
		void aMovedDesignationStaysWhereItWas()
		{
			useModel(QStringLiteral("tag_model;line;{fluid}\n"));
			Conductor *whole = draw(m_nozzles, 600);
			QVERIFY(whole);
			setData(whole, QStringLiteral("W01"), 100);
			const QPointF left = whole->terminal1->dockConductor();
			whole->textItem()->setPos(whole->mapFromScene(left + QPointF(20, -40)));
			whole->textItem()->forceMovedByUser(true);
			const QPointF was = whole->textItem()->scenePos();

				//Cut by a symbol 100 from its left end: a short piece on
				//the left, by the text, and a long one on the right
			const qreal y = left.y();
			Element *a = place(m_nozzles, QPointF(left.x() - 10, y));
			Element *cut = place(m_nozzles, QPointF(left.x() + 110, y));
			Element *b = place(m_nozzles, QPointF(left.x() + 610, y));
			QVERIFY(a && cut && b);
			auto *short_piece = new Conductor(a->terminals().at(1), cut->terminals().at(0));
			auto *long_piece = new Conductor(cut->terminals().at(1), b->terminals().at(0));
			short_piece->setProperties(whole->properties());
			long_piece->setProperties(whole->properties());
			QVERIFY(short_piece->length() < long_piece->length());
			Conductor::keepOneDesignation({long_piece, short_piece}, whole);
			m_folio->addItem(short_piece);
			m_folio->addItem(long_piece);

			QVERIFY(short_piece->properties().m_show_text);
			QVERIFY(!long_piece->properties().m_show_text);
			QVERIFY(short_piece->textItem()->wasMovedByUser());
			QCOMPARE(short_piece->textItem()->scenePos(), was);
		}

			// A piece of a cut pipe gets its properties before it is on the
			// folio: put there, it takes the size and the colour of the project
		void aPieceTakesTheLookOnceOnTheFolio()
		{
			useModel(QStringLiteral("tag_model;line;{fluid}\n"
									"text_size;line;14\n"
									"text_option;colored_labels;true\n"
									"fluid;W01;Raw water;water\n"
									"fluid_color;water;#0000ff\n"));
			Conductor *whole = draw(m_nozzles);
			QVERIFY(whole);
			setData(whole, QStringLiteral("W01"), 100);
			QCOMPARE(whole->textItem()->color(), QColor(0, 0, 255));

			m_next_y += 100;
			Element *a = place(m_nozzles, QPointF(0, m_next_y));
			Element *b = place(m_nozzles, QPointF(300, m_next_y));
			QVERIFY(a && b);
			auto *piece = new Conductor(a->terminals().at(1), b->terminals().at(0));
			piece->setProperties(whole->properties());
			m_folio->addItem(piece);
			QCOMPARE(piece->textItem()->toPlainText(), QStringLiteral("W01"));
			QCOMPARE(piece->textItem()->font().pointSize(), 14);
			QCOMPARE(piece->textItem()->color(), QColor(0, 0, 255));
		}

			// Saved as the text of the conductor, shown or not; read back
		void itIsSavedAsTheTextOfTheConductor()
		{
			useModel(QStringLiteral("tag_model;line;{fluid} {size}\n"));
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			setData(pipe, QStringLiteral("W01"), 100);
			QHash<Terminal *, int> ids;
			QDomDocument doc;
			QDomElement saved = pipe->toXml(doc, ids, {});
			ConductorProperties read;
			read.fromXml(saved);
			QVERIFY(read.m_show_text);
			QCOMPARE(read.text, QStringLiteral("W01 DN 100"));
		}
};

QET_APP_TEST_MAIN(tst_pipedesignation)
#include "tst_pipedesignation.moc"
