// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "configdialog.h"
#include "diagram.h"
#include "editor/elementscene.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidstatus.h"
#include "pidtooltip.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "qetresult.h"
#include "ui/configpage/pidconfigpage.h"
#include "ui/elementoptionswidget.h"
#include "ui/pipepropertieswidget.h"
#include "utils/qetsettings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDomDocument>
#include <QFile>
#include <QHeaderView>
#include <QImage>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QStyleOptionGraphicsItem>
#include <QUndoStack>

// The colours of a P&ID: set for a project, by family of fluid, of object and
// by status. The code knows none: what has none is drawn as before. On a pipe,
// a colour of its own comes before its fluid, its fluid before its family of
// object, and a wire is not concerned.
class tst_pidcolors : public QObject
{
	Q_OBJECT

	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &medium)
	{
		const QString attr = medium.isEmpty()
				? QString()
				: QStringLiteral(" medium=\"%1\"").arg(medium);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%2/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%2/>"
			"</description></definition>").arg(uuid, attr));
		return doc.documentElement();
	}

		// A symbol in three parts: a square of ink, a square of ink masked
		// in white, a square of a colour of its own
	static QDomElement paintedSymbol(QDomDocument &doc, const QString &uuid, const QString &family)
	{
		const QString rect = QStringLiteral(
			"<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%3\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:none;filling:%4;color:black\"/>");
		const QString attr = family.isEmpty() ? QString()
											  : QStringLiteral(" pid_family=\"%1\"").arg(family);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\"%2 "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">P</name></names><description>").arg(uuid, attr)
			+ rect.arg(-15).arg(-15).arg(10).arg(QStringLiteral("black"))
			+ rect.arg(5).arg(-15).arg(10).arg(QStringLiteral("black"))
			+ rect.arg(7).arg(-13).arg(6).arg(QStringLiteral("white"))
			+ rect.arg(-15).arg(5).arg(10).arg(QStringLiteral("red"))
			+ QStringLiteral("</description></definition>"));
		return doc.documentElement();
	}

		// A tee: a square of ink, and three nozzles the file gives out of
		// order
	static QDomElement fittingSymbol(QDomDocument &doc, const QString &uuid)
	{
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" pid_family=\"pipe\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">T</name></names><description>"
			"<rect x=\"-15\" y=\"-15\" width=\"10\" height=\"10\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:none;filling:black;color:black\"/>"
			"<terminal uuid=\"{a3333333-3333-3333-3333-333333333333}\" name=\"N3\" x=\"0\" y=\"10\" "
			"orientation=\"s\" type=\"Generic\" medium=\"pipe\"/>"
			"<terminal uuid=\"{a1111111-1111-1111-1111-111111111111}\" name=\"N1\" x=\"-10\" y=\"0\" "
			"orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
			"<terminal uuid=\"{a2222222-2222-2222-2222-222222222222}\" name=\"N2\" x=\"10\" y=\"0\" "
			"orientation=\"e\" type=\"Generic\" medium=\"pipe\"/>"
			"</description></definition>").arg(uuid));
		return doc.documentElement();
	}

	static Terminal *nozzle(Element *element, const QString &name)
	{
		for (Terminal *terminal : element->terminals()) {
			if (terminal->baseName() == name) return terminal;
		}
		return nullptr;
	}

		// The element drawn four times its size, the hotspot in the middle
	static QImage render(Element *element)
	{
		QImage image(160, 160, QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		painter.scale(4, 4);
		painter.translate(20, 20);
		element->paint(&painter, nullptr, nullptr);
		painter.end();
		return image;
	}

		// The colour at the point @p x, @p y of the symbol
	static QColor at(const QImage &image, int x, int y)
	{
		return image.pixelColor((x + 20) * 4, (y + 20) * 4);
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_electrical, m_nozzles, m_valve, m_plain, m_fitting;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/colors"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/colors/") + file, &m_project);
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

	static void setFluid(Conductor *pipe, const QString &fluid)
	{
		ConductorProperties p = pipe->properties();
		p.m_pipe.fluid = fluid;
		pipe->setProperties(p);
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("colors"), NamesList());
			QDomDocument a, b, c, d, e;
			m_fitting = add("fitting.elmt", fittingSymbol(e, "95555555-5555-5555-5555-555555555555"));
			m_valve = add("valve.elmt", paintedSymbol(c, "93333333-3333-3333-3333-333333333333", "valve"));
			m_plain = add("plain.elmt", paintedSymbol(d, "94444444-4444-4444-4444-444444444444", ""));
			m_electrical = add("electrical.elmt",
							   definition(a, "91111111-1111-1111-1111-111111111111", ""));
			m_nozzles = add("nozzles.elmt",
							definition(b, "92222222-2222-2222-2222-222222222222", "pipe"));
			QVERIFY(m_electrical.exist() && m_nozzles.exist());
			m_folio = m_project.addNewDiagram();
		}

		void theColorsAreReadFromACsvFile()
		{
			int skipped = -1;
			const PidCodification c = PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\n"
				"fluid_color;E;#0000ff\n"
				"couleur_fluide;B;brown\n"
				"object_color;equipment;#800000\n"
				"couleur_objet;valve;#808000\n"
				"status_color;existing;#808080\n"
				"couleur_statut;demolished;red\n"
				"fluid_color;X;not a color\n"
				"fluid_color;E;#00ff00\n"), &skipped);
			QCOMPARE(c.fluid_family_colors.value(QStringLiteral("E")), QColor(0, 0, 255));
			QVERIFY(c.fluid_family_colors.value(QStringLiteral("B")).isValid());
			QCOMPARE(c.object_colors.value(QStringLiteral("equipment")), QColor(128, 0, 0));
			QCOMPARE(c.object_colors.size(), 2);
			QCOMPARE(c.status_colors.value(QStringLiteral("demolished")), QColor(Qt::red));
				// a colour that cannot be read, a key twice
			QCOMPARE(skipped, 2);

			QCOMPARE(c.fluidColor(QStringLiteral("W01")), QColor(0, 0, 255));
			QVERIFY(!c.fluidColor(QStringLiteral("free text")).isValid());
		}

		void aFileWithoutColorsKeepsThoseOfTheProject()
		{
			const PidCodification project = PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\nfluid_color;E;#0000ff\n"));
			const PidCodification codes = PidCodification::fromCsv(QStringLiteral(
				"fluid;W02;Clarified water;E\n"));
			const PidCodification with_codes = codes.completedWith(project);
			QCOMPARE(with_codes.fluids.first().code, QStringLiteral("W02"));
			QVERIFY(with_codes.hasColors());

				// and a file of colours keeps the codes
			const PidCodification colors = PidCodification::fromCsv(QStringLiteral(
				"object_color;pipe;#123456\n"));
			QVERIFY(!colors.isEmpty());
			const PidCodification with_colors = colors.completedWith(project);
			QCOMPARE(with_colors.fluids.first().code, QStringLiteral("W01"));
			QCOMPARE(with_colors.object_colors.value(QStringLiteral("pipe")), QColor(0x12, 0x34, 0x56));
			QVERIFY(with_colors.fluid_family_colors.isEmpty());
		}

		void theColorsAreSavedWithTheProject_data()
		{
			QTest::addColumn<QString>("suffix");
			QTest::newRow(".qet") << QStringLiteral(".qet");
			QTest::newRow(".qetz") << QStringLiteral(".qetz");
			QTest::newRow(".qetdb") << QStringLiteral(".qetdb");
		}

		void theColorsAreSavedWithTheProject()
		{
			QFETCH(QString, suffix);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("colors") + suffix);
			const PidCodification c = PidCodification::fromCsv(QStringLiteral(
				"fluid_color;E;#0000ff\nobject_color;pipe;#808000\nstatus_color;existing;#808080\n"));
			{
				QETProject project;
				project.addNewDiagram();
				project.setPidCodification(c);
				project.setFilePath(path);
				QVERIFY(project.write().isOk());
			}
			QETProject back(path);
			QVERIFY(back.pidCodification() == c);
		}

		void aPipeFollowsItsColorThenItsFluidThenItsFamily()
		{
			Conductor *pipe = draw(m_nozzles);
			Conductor *wire = draw(m_electrical);
			QVERIFY(pipe && wire);
			const QColor wire_before = wire->drawnColor();
			const QColor pipe_before = pipe->drawnColor();

			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\n"
				"fluid;W02;Other water;\n"
				"fluid_color;E;#0000ff\n"
				"object_color;pipe;#808000\n")));

				// no fluid: the colour of the family of object
			QCOMPARE(pipe->drawnColor(), QColor(0x80, 0x80, 0));
				// a fluid of a family with a colour
			setFluid(pipe, QStringLiteral("W01"));
			QCOMPARE(pipe->drawnColor(), QColor(0, 0, 255));
				// a fluid without a family, free text: the family of object
			setFluid(pipe, QStringLiteral("W02"));
			QCOMPARE(pipe->drawnColor(), QColor(0x80, 0x80, 0));
			setFluid(pipe, QStringLiteral("free text"));
			QCOMPARE(pipe->drawnColor(), QColor(0x80, 0x80, 0));

				// a colour of its own comes first
			setFluid(pipe, QStringLiteral("W01"));
			ConductorProperties p = pipe->properties();
			p.m_pipe.color = QColor(1, 2, 3);
			pipe->setProperties(p);
			QCOMPARE(pipe->drawnColor(), QColor(1, 2, 3));

				// a wire knows nothing of it
			QCOMPARE(wire->drawnColor(), wire_before);

				// no colour set: drawn as before
			p.m_pipe.color = QColor();
			pipe->setProperties(p);
			m_project.setPidCodification(PidCodification());
			QCOMPARE(pipe->drawnColor(), pipe_before);
		}

		void aPipeFollowsTheZoomInsteadOfSwelling()
		{
				// how many dark pixels a line is wide at the zoom @p scale
			auto width = [](Conductor *conductor, qreal scale) {
				const QRectF bounds = conductor->sceneBoundingRect();
				QImage image(int(bounds.width() * scale) + 40, int(bounds.height() * scale) + 40,
							 QImage::Format_ARGB32);
				image.fill(Qt::white);
				QPainter painter(&image);
				painter.translate(20, 20);
				painter.scale(scale, scale);
				painter.translate(-bounds.topLeft() + conductor->pos());
				QStyleOptionGraphicsItem option;
				conductor->paint(&painter, &option, nullptr);
				painter.end();
				int dark = 0;
				for (int y = 0 ; y < image.height() ; ++y) {
					if (image.pixelColor(image.width() / 2, y).lightness() < 128) {
						++dark;
					}
				}
				return dark;
			};

			Conductor *pipe = draw(m_nozzles);
			Conductor *wire = draw(m_electrical);
			QVERIFY(pipe && wire);
				// 4 units at full size
			QCOMPARE(width(pipe, 1.0), 4);
				// less than half the size: thinner, never gone
			QVERIFY(width(pipe, 0.4) <= 2);
			QVERIFY(width(pipe, 0.4) >= 1);
			QVERIFY(width(pipe, 0.1) >= 1);
			QVERIFY(width(pipe, 0.1) <= 2);
				// a wire stays one pixel wide, as before
			QCOMPARE(width(wire, 0.4), 1);
		}

		void aColorSetOnAPipeIsSavedOnlyWhenThereIsOne()
		{
			ConductorProperties p;
			p.m_medium = TerminalData::Pipe;
			QDomDocument doc;
			QDomElement e = doc.createElement(QStringLiteral("conductor"));
			p.toXml(e);
			QVERIFY(!e.hasAttribute(QStringLiteral("pid_color")));

			p.m_pipe.color = QColor(0x12, 0x34, 0x56);
			QDomElement f = doc.createElement(QStringLiteral("conductor"));
			p.toXml(f);
			QCOMPARE(f.attribute(QStringLiteral("pid_color")), QStringLiteral("#123456"));
			ConductorProperties back;
			back.fromXml(f);
			QCOMPARE(back.m_pipe.color, QColor(0x12, 0x34, 0x56));
		}

		void thePageOfAPipeSetsAndClearsItsColor()
		{
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			PipePropertiesWidget page({pipe});
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();

			page.setManualColor(QColor(10, 20, 30));
			QCOMPARE(pipe->properties().m_pipe.color, QColor(10, 20, 30));
			QCOMPARE(stack.count(), steps + 1);
			auto *button = page.findChild<QPushButton *>(QStringLiteral("color"));
			QVERIFY(button);
			QCOMPARE(button->text(), QColor(10, 20, 30).name());

			page.setManualColor(QColor());
			QVERIFY(!pipe->properties().m_pipe.color.isValid());
			stack.undo();
			QCOMPARE(pipe->properties().m_pipe.color, QColor(10, 20, 30));
		}

		void aClickInAColorAsksForIt()
		{
			PidConfigPage page(&m_project);
			page.resize(800, 600);
			page.show();
			auto *table = page.findChild<QTableWidget *>(QStringLiteral("color_table"));
			QVERIFY(table);
			int asked = 0;
			page.setColorAsker([&asked](const QColor &) { ++asked; return QColor(4, 4, 4); });
			auto cell = [table](int row, int column) {
				return table->visualRect(table->model()->index(row, column)).center();
			};
			auto *viewport = table->viewport();

				// one line
			QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, cell(0, 2));
			QCOMPARE(asked, 1);
			QCOMPARE(table->item(0, 2)->text(), QColor(4, 4, 4).name());

				// Ctrl selects without asking
			QTest::mouseClick(viewport, Qt::LeftButton, Qt::ControlModifier, cell(2, 2));
			QTest::mouseClick(viewport, Qt::LeftButton, Qt::ControlModifier, cell(3, 2));
			QCOMPARE(asked, 1);
			QCOMPARE(table->selectionModel()->selectedRows().size(), 3);

				// a click in the colour of one of them asks for all of them
			page.setColorAsker([&asked](const QColor &) { ++asked; return QColor(5, 5, 5); });
			QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, cell(2, 2));
			QCOMPARE(asked, 2);
			for (int row : {0, 2, 3}) {
				QCOMPARE(table->item(row, 2)->text(), QColor(5, 5, 5).name());
			}
			QCOMPARE(table->selectionModel()->selectedRows().size(), 3);

				// a click in the colour of another line is for that line alone
			page.setColorAsker([&asked](const QColor &) { ++asked; return QColor(6, 6, 6); });
			QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, cell(4, 2));
			QCOMPARE(asked, 3);
			QCOMPARE(table->item(4, 2)->text(), QColor(6, 6, 6).name());
			QCOMPARE(table->item(0, 2)->text(), QColor(5, 5, 5).name());
		}

		void aSymbolTakesTheColorOfItsFamily()
		{
			m_project.setPidCodification(PidCodification());
			m_next_y += 100;
			Element *valve = place(m_valve, QPointF(0, m_next_y));
			Element *plain = place(m_plain, QPointF(100, m_next_y));
			QVERIFY(valve && plain);
			const QColor black(Qt::black), white(Qt::white), red(Qt::red), olive(0x80, 0x80, 0);

				// no colour set: as before
			QCOMPARE(at(render(valve), -10, -10), black);

				// the ink changes at once, not the white that masks, nor a
				// colour chosen in the symbol
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"object_color;valve;#808000\n")));
			const QImage inked = render(valve);
			QCOMPARE(at(inked, -10, -10), olive);
			QCOMPARE(at(inked, 6, -10), olive);
			QCOMPARE(at(inked, 10, -10), white);
			QCOMPARE(at(inked, -10, 10), red);

				// a symbol without a family stays black
			QCOMPARE(at(render(plain), -10, -10), black);

				// placed afterwards, it has it too
			Element *next = place(m_valve, QPointF(200, m_next_y));
			QVERIFY(next);
			QCOMPARE(at(render(next), -10, -10), olive);

				// a folio drawn in black, to print it so
			m_folio->setDrawColoredConductors(false);
			QCOMPARE(at(render(valve), -10, -10), black);
			m_folio->setDrawColoredConductors(true);
			QCOMPARE(at(render(valve), -10, -10), olive);

				// no colour any more
			m_project.setPidCodification(PidCodification());
			QCOMPARE(at(render(valve), -10, -10), black);
		}

		void aStatusComesFirst()
		{
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\n"
				"fluid_color;E;#0000ff\n"
				"object_color;valve;#808000\n"
				"status_color;existing;#808080\n"
				"status_color;demolished;#ff0000\n")));
			m_next_y += 100;
			Element *valve = place(m_valve, QPointF(0, m_next_y));
			Conductor *pipe = draw(m_nozzles);
			Conductor *wire = draw(m_electrical);
			QVERIFY(valve && pipe && wire);
			setFluid(pipe, QStringLiteral("W01"));
			ConductorProperties p = pipe->properties();
			p.m_pipe.color = QColor(1, 2, 3);
			pipe->setProperties(p);
			QVERIFY(PidStatus::applies(valve) && PidStatus::applies(pipe));
			QVERIFY(!PidStatus::applies(wire));

				// one step for all, what has no status left alone
			const QColor wire_before = wire->drawnColor();
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.index();
			stack.push(PidStatus::change({valve, pipe, wire}, QStringLiteral("demolished")));
			QCOMPARE(stack.index(), steps + 1);
			QCOMPARE(pipe->drawnColor(), QColor(Qt::red));
			QCOMPARE(at(render(valve), -10, -10), QColor(Qt::red));
			QCOMPARE(wire->drawnColor(), wire_before);

				// nothing to change, no step
			QVERIFY(!PidStatus::change({valve, pipe}, QStringLiteral("demolished")));

			stack.undo();
			QCOMPARE(pipe->drawnColor(), QColor(1, 2, 3));
			QCOMPARE(at(render(valve), -10, -10), QColor(0x80, 0x80, 0));

				// a status without a colour falls back on what comes next
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"object_color;valve;#808000\n")));
			stack.redo();
			QCOMPARE(pipe->drawnColor(), QColor(1, 2, 3));
			QCOMPARE(at(render(valve), -10, -10), QColor(0x80, 0x80, 0));
			stack.undo();
			m_project.setPidCodification(PidCodification());
		}

		void aFittingFollowsItsPipe()
		{
			const QColor black(Qt::black), olive(0x80, 0x80, 0), blue(0, 0, 255),
					lime(0, 255, 0), red(Qt::red), gray(0x80, 0x80, 0x80), own(1, 2, 3);
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\n"
				"fluid;SL1;Sludge;B\n"
				"fluid_color;E;#0000ff\n"
				"fluid_color;B;#00ff00\n"
				"object_color;pipe;#808000\n"
				"object_color;valve;#ff00ff\n"
				"status_color;existing;#808080\n"
				"status_color;demolished;#ff0000\n")));
			m_next_y += 100;
			Element *tee = place(m_fitting, QPointF(0, m_next_y));
			Element *a = place(m_nozzles, QPointF(200, m_next_y));
			Element *b = place(m_nozzles, QPointF(300, m_next_y));
			Element *c = place(m_nozzles, QPointF(400, m_next_y));
			QVERIFY(tee && a && b && c);
			QVERIFY(tee->followsPipes() && !a->followsPipes());
			auto ink = [tee]() { return at(render(tee), -10, -10); };

				// without a pipe, the colour of its family
			QCOMPARE(ink(), olive);

				// the branch while it is alone, and the changes of its fluid
			auto *branch = new Conductor(nozzle(tee, QStringLiteral("N3")), a->terminals().at(0));
			m_folio->addItem(branch);
			setFluid(branch, QStringLiteral("SL1"));
			QCOMPARE(ink(), lime);

				// the run goes first, N1 before N2, as named and not as listed
			auto *out = new Conductor(nozzle(tee, QStringLiteral("N2")), b->terminals().at(0));
			m_folio->addItem(out);
			QCOMPARE(ink(), olive);
			setFluid(out, QStringLiteral("W01"));
			QCOMPARE(ink(), blue);
			auto *in = new Conductor(c->terminals().at(1), nozzle(tee, QStringLiteral("N1")));
			m_folio->addItem(in);
			setFluid(in, QStringLiteral("SL1"));
			QCOMPARE(ink(), lime);

				// the status of the pipe, undone
			QUndoStack &stack = m_folio->undoStack();
			stack.push(PidStatus::change({in}, QStringLiteral("demolished")));
			QCOMPARE(ink(), red);
			stack.undo();
			QCOMPARE(ink(), lime);

				// a status of its own comes first
			stack.push(PidStatus::change({tee}, QStringLiteral("existing")));
			QCOMPARE(ink(), gray);
			stack.undo();
			QCOMPARE(ink(), lime);

				// the pipe leaves and comes back
			m_folio->removeItem(in);
			QCOMPARE(ink(), blue);
			m_folio->addItem(in);
			QCOMPARE(ink(), lime);

				// nothing of this is kept on the element
			QVERIFY(tee->optionChoices().isEmpty());

				// a folio drawn in black, also for a colour only set on the
				// pipe, in a project without colours
			m_folio->setDrawColoredConductors(false);
			QCOMPARE(ink(), black);
			m_folio->setDrawColoredConductors(true);
			QCOMPARE(ink(), lime);
			m_project.setPidCodification(PidCodification());
			QCOMPARE(ink(), black);
			ConductorProperties p = in->properties();
			p.m_pipe.color = own;
			in->setProperties(p);
			QCOMPARE(ink(), own);
			m_folio->setDrawColoredConductors(false);
			QCOMPARE(ink(), black);
			m_folio->setDrawColoredConductors(true);
			QCOMPARE(ink(), own);

				// a valve keeps the colour of its family
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"object_color;valve;#ff00ff\n")));
			Element *valve = place(m_valve, QPointF(500, m_next_y));
			QVERIFY(valve);
			QCOMPARE(at(render(valve), -10, -10), QColor(0xff, 0, 0xff));

				// gone with its pipes, which leave the others as they were
			delete tee;
			QVERIFY(a->terminals().at(0)->conductors().isEmpty());
			m_project.setPidCodification(PidCodification());
		}

		void aFreeNozzleOfAFittingIsMarkedOnTheScreen()
		{
			m_next_y += 100;
			Element *tee = place(m_fitting, QPointF(0, m_next_y));
			Element *end = place(m_nozzles, QPointF(200, m_next_y));
			QVERIFY(tee && end);
				// the bottom of a ring around the point @p x, @p y
			auto ringAround = [](Element *element, int x, int y) {
				const QImage image = render(element);
				for (int i = (x - 2 + 20) * 4; i <= (x + 2 + 20) * 4; ++i) {
					for (int j = (y + 4 + 20) * 4; j <= (y + 8 + 20) * 4; ++j) {
						if (image.pixelColor(i, j) != QColor(Qt::white)) return true;
					}
				}
				return false;
			};

				// N3, free, then not on what is exported or printed
			QVERIFY(ringAround(tee, 0, 10));
			m_folio->setDrawTerminals(false);
			QVERIFY(!ringAround(tee, 0, 10));
			m_folio->setDrawTerminals(true);

				// gone with its pipe, back when the pipe leaves
			auto *branch = new Conductor(nozzle(tee, QStringLiteral("N3")), end->terminals().at(0));
			m_folio->addItem(branch);
			QVERIFY(!ringAround(tee, 0, 10));
			m_folio->removeItem(branch);
			QVERIFY(ringAround(tee, 0, 10));
			delete branch;

				// a symbol that is not a fitting shows its nozzles as before
			QVERIFY(!ringAround(end, -10, 0));
		}

		void aFittingIsDrawnAfterItsPipeOnceReopened_data()
		{
			QTest::addColumn<QString>("suffix");
			QTest::newRow(".qet") << QStringLiteral(".qet");
			QTest::newRow(".qetz") << QStringLiteral(".qetz");
			QTest::newRow(".qetdb") << QStringLiteral(".qetdb");
		}

		void aFittingIsDrawnAfterItsPipeOnceReopened()
		{
			QFETCH(QString, suffix);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("fitting") + suffix);
			QUuid tee_uuid;
			{
				QETProject project;
				project.embeddedElementCollection()->createDir(
							QStringLiteral("import"), QStringLiteral("colors"), NamesList());
				QDomDocument a;
				project.embeddedElementCollection()->addElementDefinition(
							QStringLiteral("import/colors"), QStringLiteral("fitting.elmt"),
							fittingSymbol(a, "96666666-6666-6666-6666-666666666666"));
				project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
					"fluid;W01;Raw water;E\nfluid_color;E;#0000ff\n")));
				Diagram *folio = project.addNewDiagram();
				int state = 0;
				const ElementsLocation fitting(QStringLiteral("embed://import/colors/fitting.elmt"), &project);
				Element *tee = ElementFactory::Instance()->createElement(fitting, nullptr, &state);
				Element *end = ElementFactory::Instance()->createElement(fitting, nullptr, &state);
				QVERIFY(tee && end && !state);
				folio->addItem(tee);
				folio->addItem(end);
				end->setPos(200, 0);
				auto *pipe = new Conductor(nozzle(tee, QStringLiteral("N2")),
										   nozzle(end, QStringLiteral("N1")));
				folio->addItem(pipe);
				setFluid(pipe, QStringLiteral("W01"));
				tee_uuid = tee->uuid();
				project.setFilePath(path);
				QVERIFY(project.write().isOk());
			}
			QETProject back(path);
			QCOMPARE(back.diagrams().size(), 1);
				// a .qetz builds its folios when they are shown
			back.diagrams().first()->ensureLoaded();
			Element *tee = nullptr;
			for (QGraphicsItem *item : back.diagrams().first()->items()) {
				if (auto *element = qgraphicsitem_cast<Element *>(item)) {
					if (element->uuid() == tee_uuid) tee = element;
				}
			}
			QVERIFY(tee);
			QCOMPARE(at(render(tee), -10, -10), QColor(0, 0, 255));
		}

		void theStatusIsChosenOnThePagesAndSaved()
		{
			m_next_y += 100;
			Element *valve = place(m_valve, QPointF(0, m_next_y));
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(valve && pipe);

			PipePropertiesWidget page({pipe});
			auto *status = page.findChild<QComboBox *>(QStringLiteral("status"));
			QVERIFY(status);
			QCOMPARE(status->count(), 3);
			QCOMPARE(status->currentIndex(), 0);
			status->setCurrentIndex(1);
			emit status->activated(1);
			QCOMPARE(pipe->properties().m_pipe.status, QStringLiteral("existing"));

			QDomDocument doc;
			QDomElement e = doc.createElement(QStringLiteral("conductor"));
			pipe->properties().toXml(e);
			QCOMPARE(e.attribute(QStringLiteral("pid_status")), QStringLiteral("existing"));

				// a symbol of a family offers it, an electrical symbol does not
			QVERIFY(ElementOptionsWidget::hasOptions(valve));
			Element *plain = place(m_plain, QPointF(100, m_next_y));
			QVERIFY(plain && !ElementOptionsWidget::hasOptions(plain));
			ElementOptionsWidget tab(valve);
			tab.setLiveEdit(true);
			auto *list = tab.findChild<QComboBox *>(QStringLiteral("status"));
			QVERIFY(list);
			list->setCurrentIndex(2);
			QCOMPARE(valve->optionChoices().value(PidStatus::option()), QStringLiteral("demolished"));
			m_folio->undoStack().undo();
			QVERIFY(valve->optionChoices().isEmpty());
			QCOMPARE(list->currentIndex(), 0);
		}

		void theEditorKeepsTheFamilyOfASymbol()
		{
			QDomDocument doc;
			QDomDocument source;
			paintedSymbol(source, "95555555-5555-5555-5555-555555555555", "equipment");
			ElementScene scene(nullptr);
			scene.fromXml(source, QPointF(), true);
			doc = scene.toXml();
			QCOMPARE(doc.documentElement().attribute(QStringLiteral("pid_family")),
					 QStringLiteral("equipment"));
		}

		void aDialogAppliesAndStaysOpen()
		{
			ConfigDialog dialog;
			auto *page = new PidConfigPage(&m_project);
			dialog.addPage(page);
			dialog.show();
			auto *box = dialog.findChild<QDialogButtonBox *>();
			QVERIFY(box);
			QPushButton *apply = box->button(QDialogButtonBox::Apply);
			QVERIFY(apply);

			page->setColor(PidConfigPage::ObjectFamily, QStringLiteral("pipe"), QColor(7, 7, 7));
			QVERIFY(!m_project.pidCodification().hasColors());
			apply->click();
			QCOMPARE(m_project.pidCodification().object_colors.value(QStringLiteral("pipe")),
					 QColor(7, 7, 7));
			QVERIFY(dialog.isVisible());

				// a second time changes nothing
			const PidCodification once = m_project.pidCodification();
			apply->click();
			QVERIFY(m_project.pidCodification() == once);
			QVERIFY(dialog.isVisible());
			m_project.setPidCodification(PidCodification());
		}

			// The icons of the pages of a dialog are one above the other, in
			// the middle of their list, whatever the width of their titles
		void thePagesOfADialogAreCenteredInItsList()
		{
			class TitledPage : public ConfigPage
			{
				public:
					explicit TitledPage(const QString &title) : ConfigPage(nullptr), m_title(title) {}
					void applyConf() override {}
					QString title() const override { return m_title; }
					QIcon icon() const override {
						QPixmap pixmap(64, 64);
						pixmap.fill(Qt::darkGray);
						return QIcon(pixmap);
					}
				private:
					QString m_title;
			};
			ConfigDialog dialog;
			dialog.addPage(new TitledPage(QStringLiteral("P&ID")));
			dialog.addPage(new TitledPage(QStringLiteral("Automatic numbering")));
			dialog.addPage(new TitledPage(QStringLiteral("General")));
			dialog.show();
			QCoreApplication::processEvents();
			auto *list = dialog.findChild<QListWidget *>();
			QVERIFY(list);
			QCOMPARE(list->count(), 3);
			const int middle = list->viewport()->rect().center().x();
			for (int row = 0 ; row < list->count() ; ++row) {
				const QRect rect = list->visualItemRect(list->item(row));
				QCOMPARE(rect.width(), list->visualItemRect(list->item(0)).width());
				QVERIFY2(qAbs(rect.center().x() - middle) <= 3, qPrintable(list->item(row)->text()));
					// one under the other, as before
				if (row > 0) {
					QVERIFY(rect.top() > list->visualItemRect(list->item(row - 1)).bottom());
				}
			}
		}

		void theConfigPageSetsTheColorsOfTheProject()
		{
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\nfluid;SL1;Sludge;B\n")));
			{
				PidConfigPage page(&m_project);
				auto *table = page.findChild<QTableWidget *>(QStringLiteral("color_table"));
				QVERIFY(table);
					// two families of fluid, five of object, two statuses
				QCOMPARE(table->rowCount(), 9);

				page.setColor(PidConfigPage::FluidFamily, QStringLiteral("E"), QColor(0, 0, 255));
				page.setColor(PidConfigPage::ObjectFamily, QStringLiteral("pipe"), QColor(1, 1, 1));
				page.setColor(PidConfigPage::Status, QStringLiteral("existing"), QColor(2, 2, 2));
				page.setColor(PidConfigPage::ObjectFamily, QStringLiteral("pipe"), QColor());
					// nothing is applied before the page is validated
				QVERIFY(!m_project.pidCodification().hasColors());
				page.applyProjectConf();

					// several lines at once, whatever their group
				table->clearSelection();
				table->selectRow(0);
				table->setSelectionMode(QAbstractItemView::MultiSelection);
				table->selectRow(1);
				table->selectRow(8);
				page.setColorOfSelection(QColor(3, 3, 3));
				QCOMPARE(table->item(0, 2)->text(), QColor(3, 3, 3).name());
				QCOMPARE(table->item(1, 2)->text(), QColor(3, 3, 3).name());
				QCOMPARE(table->item(8, 2)->text(), QColor(3, 3, 3).name());
					// the selection stays, for what comes next
				QCOMPARE(table->selectionModel()->selectedRows().size(), 3);
				page.setColorOfSelection(QColor());
				QVERIFY(table->item(0, 2)->text().isEmpty());
				QVERIFY(table->item(8, 2)->text().isEmpty());
				page.setColor(PidConfigPage::FluidFamily, QStringLiteral("E"), QColor(0, 0, 255));
				page.setColor(PidConfigPage::Status, QStringLiteral("existing"), QColor(2, 2, 2));
				page.applyProjectConf();
			}
			const PidCodification c = m_project.pidCodification();
			QCOMPARE(c.fluids.size(), 2);
			QCOMPARE(c.fluid_family_colors.value(QStringLiteral("E")), QColor(0, 0, 255));
			QVERIFY(c.object_colors.isEmpty());
			QCOMPARE(c.status_colors.value(QStringLiteral("existing")), QColor(2, 2, 2));
			m_project.setPidCodification(PidCodification());
		}

			// Step 5l-1v: a file of presentation alone says so, not "0 fluids"
		void anImportSaysWhatItRead()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("presentation.csv"));
			{
				QFile file(path);
				QVERIFY(file.open(QIODevice::WriteOnly));
				file.write("kind;key;value\nobject_color;valve;#808000\n"
						   "text_option;drawn_connections;flange\n");
			}
			QETProject project;
			PidConfigPage page(&project);
			QVERIFY(page.importCsv(path));
			QVERIFY2(page.codeMessage().contains(QStringLiteral("presentation")),
					 qPrintable(page.codeMessage()));
			QVERIFY(!page.codeMessage().contains(QStringLiteral("fluids")));
			page.applyProjectConf();
			QVERIFY(project.pidCodification().drawsConnection(PipeConnection::Flange));
		}

			// Step 5l-1v: the long tool tips of the page are cut on several lines
		void theToolTipsOfThePageWrap()
		{
			QETProject project;
			PidConfigPage page(&project);
			for (const QString &name : {QStringLiteral("save_presentation"), QStringLiteral("save_codification")}) {
				auto *button = page.findChild<QPushButton *>(name);
				QVERIFY(button);
				QVERIFY2(Qt::mightBeRichText(button->toolTip()), qPrintable(name));
			}
		}

			// Step 5l-1w: the long tool tips of the P&ID wrap, the short ones
			// stay as they are, line breaks kept
		void aLongToolTipWraps()
		{
			QCOMPARE(PidToolTip::wrapped(QStringLiteral("Flange")), QStringLiteral("Flange"));
			QCOMPARE(PidToolTip::wrapped(QString()), QString());
			const QString two_short = QStringLiteral("Plain end\nNozzle Ø 110, pipe DN 100");
			QCOMPARE(PidToolTip::wrapped(two_short), two_short);
			const QString long_line = QStringLiteral(
				"A sentence long enough to run past the screen <one> when it is not cut on several lines");
			const QString tip = PidToolTip::wrapped(QStringLiteral("Socket\n") + long_line);
			QVERIFY(Qt::mightBeRichText(tip));
			QVERIFY(tip.contains(QStringLiteral("Socket<br>")));
			QVERIFY(tip.contains(QStringLiteral("&lt;one&gt;")));
		}

		void aNewProjectStartsFromTheDefaultPresentation()
		{
			QETDiagramEditor *editor = QetAppTest::editor();
			QVERIFY(editor);
			const QColor olive(0x80, 0x80, 0), grey(0x80, 0x80, 0x80), blue(0, 0, 255);

				// nothing saved: as before
			QVERIFY(QetSettings::pidPresentation().isEmpty());
			QVERIFY(editor->newProject());
			QVERIFY(editor->currentProject()->pidCodification().isEmpty());

				// the colours of the page, not its codes
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\n")));
			{
				PidConfigPage page(&m_project);
				page.setColor(PidConfigPage::FluidFamily, QStringLiteral("E"), blue);
				page.setColor(PidConfigPage::ObjectFamily, QStringLiteral("valve"), olive);
				page.setColor(PidConfigPage::Status, QStringLiteral("existing"), grey);
				auto *save = page.findChild<QPushButton *>(QStringLiteral("save_presentation"));
				QVERIFY(save);
					// step 5l-1v: it saves more than colours, the presentation
				QCOMPARE(save->text(), QStringLiteral("Save as default presentation"));
				QVERIFY(save->toolTip().contains(QStringLiteral("Connections")));
				save->click();
			}
				// saving changes neither this project nor another one
			QVERIFY(!m_project.pidCodification().hasColors());

			QVERIFY(editor->newProject());
			QETProject *project = editor->currentProject();
			const PidCodification c = project->pidCodification();
			QCOMPARE(c.fluid_family_colors.value(QStringLiteral("E")), blue);
			QCOMPARE(c.object_colors.value(QStringLiteral("valve")), olive);
			QCOMPARE(c.status_colors.value(QStringLiteral("existing")), grey);
			QVERIFY(!c.hasCodes());
				// nothing to save yet, and the palette is its own copy
			QVERIFY(!project->projectOptionsWereModified());
			QetSettings::setPidPresentation(QString());
			QVERIFY(project->pidCodification().hasColors());

				// a page without colours takes the palette away
			{
				PidConfigPage page(&m_project);
				page.setColor(PidConfigPage::FluidFamily, QStringLiteral("E"), QColor());
				page.saveAsDefaultPresentation();
			}
			QVERIFY(QetSettings::pidPresentation().isEmpty());
			m_project.setPidCodification(PidCodification());
		}

		void aNewProjectStartsFromTheDefaultCodification()
		{
			QETDiagramEditor *editor = QetAppTest::editor();
			QVERIFY(editor);
			const QString codes = QStringLiteral(
						"fluid;W01;Raw water;E\n"
						"material;M05;HDPE;od\n"
						"size;M05;90;80\n"
						"size;M05;110;100\n"
						"size;M05;125;100\n"
						"dn_equal;60;65\n"
						"unit;U10;Intake\n");
			const PidCodification expected = PidCodification::fromCsv(codes);
			QVERIFY(!expected.materials.isEmpty());
			QCOMPARE(expected.materials.first().sizes.size(), 3);
			QCOMPARE(expected.equal_nominals.size(), 1);

				// nothing saved: as before
			QVERIFY(QetSettings::pidDefaultCodification().isEmpty());

				// the codes of the page, not its colours, kept apart
			m_project.setPidCodification(PidCodification::fromCsv(codes
																  + QStringLiteral("fluid_color;E;#0000ff\n")));
			{
				PidConfigPage page(&m_project);
				auto *save = page.findChild<QPushButton *>(QStringLiteral("save_codification"));
				QVERIFY(save);
				save->click();
			}
			QVERIFY(!QetSettings::pidDefaultCodification().isEmpty());
			QVERIFY(QetSettings::pidPresentation().isEmpty());

			QVERIFY(editor->newProject());
			QETProject *project = editor->currentProject();
			QVERIFY(project->pidCodification() == expected);
				// nothing to save yet
			QVERIFY(!project->projectOptionsWereModified());

				// with a palette too: a project takes both
			QetSettings::setPidPresentation(QStringLiteral("fluid_color;E;#0000ff\n"));
			QVERIFY(editor->newProject());
			const PidCodification both = editor->currentProject()->pidCodification();
			QCOMPARE(both.fluids.size(), 1);
			QCOMPARE(both.fluidColor(QStringLiteral("W01")), QColor(0, 0, 255));
			QetSettings::setPidPresentation(QString());

				// a page without codes takes the codification away
			m_project.setPidCodification(PidCodification());
			{
				PidConfigPage page(&m_project);
				page.saveAsDefaultCodification();
			}
			QVERIFY(QetSettings::pidDefaultCodification().isEmpty());
		}

			// Undo takes back the colours just applied, not the last thing drawn
		void theColorsOfThePageAreUndone()
		{
			m_project.setPidCodification(PidCodification());
			m_next_y += 100;
			Element *valve = place(m_valve, QPointF(0, m_next_y));
			QVERIFY(valve);
			const QColor black(Qt::black), olive(0x80, 0x80, 0);
			QUndoStack *stack = m_project.undoStack();
			const int steps = stack->count();

			PidConfigPage page(&m_project);
			page.setColor(PidConfigPage::ObjectFamily, QStringLiteral("valve"), olive);
			page.applyProjectConf();
			QCOMPARE(stack->count(), steps + 1);
			QCOMPARE(at(render(valve), -10, -10), olive);

				// applied a second time, or OK after Apply: no new step
			page.applyProjectConf();
			QCOMPARE(stack->count(), steps + 1);

			stack->undo();
			QVERIFY(!m_project.pidCodification().hasColors());
			QCOMPARE(at(render(valve), -10, -10), black);
			QVERIFY(m_folio->elements().contains(valve));

			stack->redo();
			QCOMPARE(m_project.pidCodification().object_colors.value(QStringLiteral("valve")), olive);
			QCOMPARE(at(render(valve), -10, -10), olive);

			stack->undo();
			m_project.setPidCodification(PidCodification());
		}
};

QET_APP_TEST_MAIN(tst_pidcolors)
#include "tst_pidcolors.moc"
