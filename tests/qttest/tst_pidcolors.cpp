// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "qetresult.h"
#include "ui/configpage/pidconfigpage.h"
#include "ui/pipepropertieswidget.h"

#include <QDomDocument>
#include <QPushButton>
#include <QTemporaryDir>
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

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_electrical, m_nozzles;
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
			QDomDocument a, b;
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

		void aColorSetOnAPipeIsSavedOnlyWhenThereIsOne()
		{
			ConductorProperties p;
			p.m_medium = TerminalData::Pipe;
			QDomDocument doc;
			QDomElement e = doc.createElement(QStringLiteral("conductor"));
			p.toXml(e);
			QVERIFY(!e.hasAttribute(QStringLiteral("pipe_color")));

			p.m_pipe.color = QColor(0x12, 0x34, 0x56);
			QDomElement f = doc.createElement(QStringLiteral("conductor"));
			p.toXml(f);
			QCOMPARE(f.attribute(QStringLiteral("pipe_color")), QStringLiteral("#123456"));
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

		void theConfigPageSetsTheColorsOfTheProject()
		{
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\nfluid;SL1;Sludge;B\n")));
			{
				PidConfigPage page(&m_project);
				auto *table = page.findChild<QTableWidget *>();
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
};

QET_APP_TEST_MAIN(tst_pidcolors)
#include "tst_pidcolors.moc"
