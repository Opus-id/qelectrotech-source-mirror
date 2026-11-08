// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "pidcodification.h"
#include "factory/elementfactory.h"
#include "factory/propertieseditorfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "qetresult.h"
#include "ui/pipepropertieswidget.h"

#include <QComboBox>
#include <QDomDocument>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUndoStack>

// The page of a pipe in the properties panel. It is offered for pipes only,
// whatever the option of the panel of the wires says; a change is one undo
// step, reaches the field edited of the pipes selected and nothing else: no
// other field, no pipe connected to them.
class tst_pipepanel : public QObject
{
	Q_OBJECT

		// A symbol with two terminals N1 (west) and N2 (east), of the medium
		// @p medium ("" for none)
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
					QStringLiteral("import/panel"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/panel/") + file, &m_project);
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

		// What a user does in a text field: type, then leave it
	static void type(QLineEdit *edit, const QString &text)
	{
		edit->setText(text);
		edit->setModified(true);
		emit edit->editingFinished();
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("panel"), NamesList());
			QDomDocument a, b;
			m_electrical = add("electrical.elmt",
							   definition(a, "81111111-1111-1111-1111-111111111111", ""));
			m_nozzles = add("nozzles.elmt",
							definition(b, "82222222-2222-2222-2222-222222222222", "pipe"));
			QVERIFY(m_electrical.exist() && m_nozzles.exist());
			m_folio = m_project.addNewDiagram();
		}

		void onlyPipesGetThePageOfAPipe()
		{
			Conductor *pipe = draw(m_nozzles);
			Conductor *other = draw(m_nozzles);
			Conductor *wire = draw(m_electrical);
			QVERIFY(pipe && other && wire);

			PropertiesEditorWidget *editor = PropertiesEditorFactory::propertiesEditor(
						QList<QGraphicsItem *>{pipe}, nullptr, nullptr);
			auto *page = qobject_cast<PipePropertiesWidget *>(editor);
			QVERIFY(page);
			QCOMPARE(page->pipes(), QList<Conductor *>{pipe});

				// the same editor goes on with another selection
			QCOMPARE(PropertiesEditorFactory::propertiesEditor(QList<QGraphicsItem *>{pipe, other}, page, nullptr),
					 page);
			QCOMPARE(page->pipes().size(), 2);
			delete page;

				// a wire keeps what it had, and pipes and wires are not mixed
			PropertiesEditorWidget *for_wire = PropertiesEditorFactory::propertiesEditor(
						QList<QGraphicsItem *>{wire}, nullptr, nullptr);
			QVERIFY(!qobject_cast<PipePropertiesWidget *>(for_wire));
			delete for_wire;
			QVERIFY(!PropertiesEditorFactory::propertiesEditor(QList<QGraphicsItem *>{pipe, wire}, nullptr, nullptr));
		}

		void aChangeIsOneUndoStepAndTheFieldsFollow()
		{
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			PipePropertiesWidget page({pipe});
			auto *line = page.findChild<QLineEdit *>(QStringLiteral("line_number"));
			QVERIFY(line);
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();

			type(line, QStringLiteral("47121"));
			QCOMPARE(pipe->properties().m_pipe.line_number, QStringLiteral("47121"));
			QCOMPARE(pipe->medium(), TerminalData::Pipe);
			QCOMPARE(stack.count(), steps + 1);

				// leaving the field untouched is no change
			line->setModified(false);
			emit line->editingFinished();
			QCOMPARE(stack.count(), steps + 1);
				// neither is typing what it already holds
			type(line, QStringLiteral("47121"));
			QCOMPARE(stack.count(), steps + 1);

			stack.undo();
			QVERIFY(pipe->properties().m_pipe.line_number.isEmpty());
			QVERIFY(line->text().isEmpty());
			stack.redo();
			QCOMPARE(line->text(), QStringLiteral("47121"));
		}

		void everyFieldReachesItsData()
		{
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			PipePropertiesWidget page({pipe});
			auto edit = [&page](const char *name) {
				return page.findChild<QLineEdit *>(QLatin1String(name));
			};

			type(edit("diameter"), QStringLiteral("80"));
			type(edit("flow"), QStringLiteral("120 m3/h"));
			type(edit("piping_class"), QStringLiteral("75HB13"));
			type(edit("nominal_pressure"), QStringLiteral("PN16"));
			type(edit("spool"), QStringLiteral("2"));
			type(edit("external_finish"), QStringLiteral("EA"));
			const PipeProperties data = pipe->properties().m_pipe;
			QCOMPARE(data.diameter.value(), 80.0);
			QCOMPARE(data.flow, QStringLiteral("120 m3/h"));
			QCOMPARE(data.piping_class, QStringLiteral("75HB13"));
			QCOMPARE(data.nominal_pressure, QStringLiteral("PN16"));
			QCOMPARE(data.spool, QStringLiteral("2"));
			QCOMPARE(data.external_finish, QStringLiteral("EA"));

				// an empty diameter is no diameter, shown as nothing
			type(edit("diameter"), QString());
			QCOMPARE(pipe->properties().m_pipe.diameter.value(), 0.0);
			QVERIFY(edit("diameter")->text().isEmpty());

				// the lists offer entries and take free text
			auto *fluid = page.findChild<QComboBox *>(QStringLiteral("fluid"));
			QVERIFY(fluid && fluid->count() > 0);
			fluid->setCurrentIndex(1);
			emit fluid->activated(1);
			QCOMPARE(pipe->properties().m_pipe.fluid, fluid->itemText(1));
			type(fluid->lineEdit(), QStringLiteral("W01"));
			QCOMPARE(pipe->properties().m_pipe.fluid, QStringLiteral("W01"));
			auto *material = page.findChild<QComboBox *>(QStringLiteral("material"));
			QVERIFY(material);
			type(material->lineEdit(), QStringLiteral("M01"));
			QCOMPARE(pipe->properties().m_pipe.material, QStringLiteral("M01"));

				// the kind of line sets how the pipe is drawn
			auto *kind = page.findChild<QComboBox *>(QStringLiteral("line_kind"));
			QVERIFY(kind);
			QCOMPARE(pipe->properties().lineWidth(), 4.0);
			kind->setCurrentIndex(2);
			emit kind->activated(2);
			QCOMPARE(pipe->properties().m_pipe.line_kind, PipeProperties::Auxiliary);
			QCOMPARE(pipe->properties().lineWidth(), 2.0);
			QCOMPARE(pipe->properties().lineStyle(), Qt::DashLine);
		}

			// Each material has its series: the field offers it, and only it;
			// without a material of the codification, nothing to choose
		void theSizesAreTheSeriesOfTheMaterial()
		{
			PidCodification codification;
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.series = {1100, 1250};
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.series = {800, 1000, 1250};
			PidCodification::Entry bare;
			bare.code = QStringLiteral("M03");
			PidCodification::Entry stainless;
			stainless.code = QStringLiteral("M04");
			stainless.outside_diameter = true;
			stainless.series = {889, 1143};
			codification.materials = {plastic, steel, bare, stainless};
			codification.outside_diameters.insert(1100, {100});
			codification.outside_diameters.insert(1250, {100, 125});
			codification.decimal = PidCodification::Decimal::Comma;
			m_project.setPidCodification(codification);

			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			auto setMaterial = [pipe](const QString &material) {
				ConductorProperties p = pipe->properties();
				p.m_pipe.material = material;
				pipe->setProperties(p);
			};
			auto diameter = [pipe]() { return pipe->properties().m_pipe.diameter; };
			setMaterial(QStringLiteral("M01"));
			PipePropertiesWidget page({pipe});
			auto *list = page.findChild<QComboBox *>(QStringLiteral("diameter_list"));
			QVERIFY(list);
			QLineEdit *edit = list->lineEdit();

				// a plastic: its series, and a number alone is an outside diameter
			QVERIFY(list->isEnabled());
			QCOMPARE(list->count(), 3);
			QCOMPARE(list->itemText(1), QStringLiteral("Ø 110"));
			type(edit, QStringLiteral("110"));
			QCOMPARE(diameter(), PipeDiameter::outside(110));
			QCOMPARE(edit->text(), QStringLiteral("Ø 110"));
			QVERIFY(page.diameterMessage().isEmpty());

				// a size out of its series: the nearest one, and said; the
				// greater of two as near
			type(edit, QStringLiteral("123"));
			QCOMPARE(diameter(), PipeDiameter::outside(125));
			QCOMPARE(edit->text(), QStringLiteral("Ø 125"));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("123")));
			type(edit, QStringLiteral("112"));
			QCOMPARE(diameter(), PipeDiameter::outside(110));
			type(edit, QStringLiteral("117,5"));
			QCOMPARE(diameter(), PipeDiameter::outside(125));
				// a DN typed on a plastic is refused
			type(edit, QStringLiteral("DN 100"));
			QCOMPARE(diameter(), PipeDiameter::outside(125));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("DN 100")));
			type(edit, QStringLiteral("110"));

				// to tenths, with the separator of the project
			setMaterial(QStringLiteral("M04"));
			QCOMPARE(list->itemText(1), QStringLiteral("Ø 88,9"));
			type(edit, QStringLiteral("88.9"));
			QCOMPARE(diameter(), PipeDiameter::outside(88.9));
			QCOMPARE(edit->text(), QStringLiteral("Ø 88,9"));

				// a material without series, none of the codification, or no
				// material: nothing to choose, and said
			setMaterial(QStringLiteral("M03"));
			QVERIFY(!list->isEnabled());
			QVERIFY(page.diameterMessage().contains(QStringLiteral("M03")));
			QCOMPARE(diameter(), PipeDiameter::outside(88.9));
			setMaterial(QStringLiteral("XYZ"));
			QVERIFY(!list->isEnabled());
			QVERIFY(!page.diameterMessage().isEmpty());
			setMaterial(QString());
			QVERIFY(!list->isEnabled());

				// several materials: nothing to choose either
			setMaterial(QStringLiteral("M01"));
			Conductor *other = draw(m_nozzles);
			ConductorProperties p = other->properties();
			p.m_pipe.material = QStringLiteral("M02");
			other->setProperties(p);
			PipePropertiesWidget both({pipe, other});
			QVERIFY(!both.findChild<QComboBox *>(QStringLiteral("diameter_list"))->isEnabled());

				// a project which lists no material offers the DN of ISO 6708
			m_project.setPidCodification(PidCodification());
			QVERIFY(list->isEnabled());
			QCOMPARE(list->count(), int(PipeProperties::preferredDiameters().size()) + 1);
			type(edit, QStringLiteral("100"));
			QCOMPARE(diameter(), PipeDiameter::nominal(100));
		}

			// A material of the other kind: the table gives the size, in the
			// same step; several sizes, the user chooses
		void aChangeOfMaterialConvertsTheSize()
		{
			PidCodification codification;
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.series = {1100, 1250};
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.series = {800, 1000, 1250};
			codification.materials = {plastic, steel};
			codification.outside_diameters.insert(1100, {100});
			codification.outside_diameters.insert(1250, {100, 125});
			m_project.setPidCodification(codification);

			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			ConductorProperties p = pipe->properties();
			p.m_pipe.material = QStringLiteral("M01");
			p.m_pipe.diameter = PipeDiameter::outside(110);
			pipe->setProperties(p);
			auto diameter = [pipe]() { return pipe->properties().m_pipe.diameter; };
			PipePropertiesWidget page({pipe});
			auto *material = page.findChild<QComboBox *>(QStringLiteral("material"));
			QVERIFY(material);
			int asked = 0;
			int answer = -1;
			page.setChooser([&asked, &answer](const QString &, const QStringList &items) {
				++asked;
				return items.size() > 1 ? answer : -1;
			});

				// one size: taken without a question, in one step
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();
			type(material->lineEdit(), QStringLiteral("M02"));
			QCOMPARE(pipe->properties().m_pipe.material, QStringLiteral("M02"));
			QCOMPARE(diameter(), PipeDiameter::nominal(100));
			QCOMPARE(asked, 0);
			QCOMPARE(stack.count(), steps + 1);
			stack.undo();
			QCOMPARE(pipe->properties().m_pipe.material, QStringLiteral("M01"));
			QCOMPARE(diameter(), PipeDiameter::outside(110));
			stack.redo();

				// back to the plastic: Ø 110 and Ø 125 are joined to DN 100
			answer = 1;
			type(material->lineEdit(), QStringLiteral("M01"));
			QCOMPARE(asked, 1);
			QCOMPARE(diameter(), PipeDiameter::outside(125));

				// no answer: the size stays, said not to suit the material
			answer = -1;
			type(material->lineEdit(), QStringLiteral("M02"));
			QCOMPARE(asked, 2);
			QCOMPARE(diameter(), PipeDiameter::outside(125));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("M02")));
			m_project.setPidCodification(PidCodification());
		}

			// The same kind: through the DN the table joins (a plastic Ø 110
			// is a stainless Ø 114,3), else the nearest size of the series
		void aChangeOfMaterialOfTheSameKind()
		{
			PidCodification codification;
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.series = {1100, 1250};
			PidCodification::Entry stainless;
			stainless.code = QStringLiteral("M02");
			stainless.outside_diameter = true;
			stainless.series = {889, 1143};
			PidCodification::Entry galvanised;
			galvanised.code = QStringLiteral("M03");
			galvanised.series = {500, 650};
			PidCodification::Entry iron;
			iron.code = QStringLiteral("M04");
			iron.series = {600, 800};
			codification.materials = {plastic, stainless, galvanised, iron};
			codification.outside_diameters.insert(1100, {100});
			codification.outside_diameters.insert(1143, {100});
			codification.decimal = PidCodification::Decimal::Comma;
			m_project.setPidCodification(codification);

			Conductor *pipe = draw(m_nozzles);
			ConductorProperties p = pipe->properties();
			p.m_pipe.material = QStringLiteral("M01");
			p.m_pipe.diameter = PipeDiameter::outside(110);
			pipe->setProperties(p);
			auto diameter = [pipe]() { return pipe->properties().m_pipe.diameter; };
			PipePropertiesWidget page({pipe});
			page.setChooser([](const QString &, const QStringList &) { return -1; });
			auto *material = page.findChild<QComboBox *>(QStringLiteral("material"));

			type(material->lineEdit(), QStringLiteral("M02"));
			QCOMPARE(diameter(), PipeDiameter::outside(114.3));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("Ø 114,3")));

				// two DN without table: the nearest one
			p = pipe->properties();
			p.m_pipe.material = QStringLiteral("M03");
			p.m_pipe.diameter = PipeDiameter::nominal(65);
			pipe->setProperties(p);
			type(material->lineEdit(), QStringLiteral("M04"));
			QCOMPARE(diameter(), PipeDiameter::nominal(60));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("DN 60")));
			m_project.setPidCodification(PidCodification());
		}

		void severalPipesChangeOnlyTheFieldEdited()
		{
			Conductor *first = draw(m_nozzles);
			Conductor *second = draw(m_nozzles);
			QVERIFY(first && second);
			ConductorProperties p = first->properties();
			p.m_pipe.diameter = PipeDiameter::nominal(80);
			p.m_pipe.material = QStringLiteral("M01");
			first->setProperties(p);
			p = second->properties();
			p.m_pipe.diameter = PipeDiameter::nominal(100);
			p.m_pipe.material = QStringLiteral("M01");
			p.m_pipe.line_kind = PipeProperties::Secondary;
			second->setProperties(p);

			PipePropertiesWidget page({first, second});
			auto *dn = page.findChild<QLineEdit *>(QStringLiteral("diameter"));
			auto *material = page.findChild<QComboBox *>(QStringLiteral("material"));
			auto *kind = page.findChild<QComboBox *>(QStringLiteral("line_kind"));
			auto *fluid = page.findChild<QComboBox *>(QStringLiteral("fluid"));
			QVERIFY(dn && material && kind && fluid);

				// what differs shows nothing, what they share shows
			QVERIFY(dn->text().isEmpty());
			QVERIFY(!dn->placeholderText().isEmpty());
			QCOMPARE(material->currentText(), QStringLiteral("M01"));
			QCOMPARE(kind->currentIndex(), -1);

				// leaving a field of several values changes nothing
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();
			emit dn->editingFinished();
			QCOMPARE(stack.count(), steps);
			QCOMPARE(first->properties().m_pipe.diameter.value(), 80.0);
			QCOMPARE(second->properties().m_pipe.diameter.value(), 100.0);

				// a field edited reaches both, in one step, and no other field
			type(fluid->lineEdit(), QStringLiteral("W02"));
			QCOMPARE(stack.count(), steps + 1);
			QCOMPARE(first->properties().m_pipe.fluid, QStringLiteral("W02"));
			QCOMPARE(second->properties().m_pipe.fluid, QStringLiteral("W02"));
			QCOMPARE(first->properties().m_pipe.diameter.value(), 80.0);
			QCOMPARE(second->properties().m_pipe.diameter.value(), 100.0);
			QCOMPARE(second->properties().m_pipe.line_kind, PipeProperties::Secondary);

			stack.undo();
			QVERIFY(first->properties().m_pipe.fluid.isEmpty());
			QVERIFY(second->properties().m_pipe.fluid.isEmpty());
		}

		void nothingIsPassedOnToAConnectedPipe()
		{
				// two pipes on the two nozzles of a symbol
			m_next_y += 100;
			Element *left = place(m_nozzles, QPointF(0, m_next_y));
			Element *middle = place(m_nozzles, QPointF(200, m_next_y));
			Element *right = place(m_nozzles, QPointF(400, m_next_y));
			QVERIFY(left && middle && right);
			auto *edited = new Conductor(left->terminals().at(1), middle->terminals().at(0));
			auto *next = new Conductor(middle->terminals().at(1), right->terminals().at(0));
			m_folio->addItem(edited);
			m_folio->addItem(next);

			PipePropertiesWidget page({edited});
			type(page.findChild<QLineEdit *>(QStringLiteral("line_number")), QStringLiteral("47121"));
			QCOMPARE(edited->properties().m_pipe.line_number, QStringLiteral("47121"));
			QVERIFY(next->properties().m_pipe.line_number.isEmpty());
		}

			// ---- the codification of the project ----
		void aCodificationIsReadFromACsvFile()
		{
			int skipped = -1;
			const PidCodification c = PidCodification::fromCsv(QStringLiteral(
				"\xFEFF" "kind;code;name;family\r\n"
				"fluid;W01;Raw water;E\r\n"
				"fluide;SL1;\"Thickened \"\"sludge\"\"\";B\n"
				"material\tM04\tHigh density polyethylene\n"
				"matériau,M03,Stainless steel\n"
				"\n"
				"fluid;W01;Twice;E\n"
				"pump;P;Pump\n"
				"fluid;;No code\n"), &skipped);
			QCOMPARE(c.fluids.size(), 2);
			QCOMPARE(c.materials.size(), 2);
			QCOMPARE(c.fluids.at(0).code, QStringLiteral("W01"));
			QCOMPARE(c.fluids.at(0).family, QStringLiteral("E"));
			QCOMPARE(c.fluids.at(1).name, QStringLiteral("Thickened \"sludge\""));
			QCOMPARE(c.material(QStringLiteral("M03"))->name, QStringLiteral("Stainless steel"));
			QVERIFY(!c.fluid(QStringLiteral("M01")));
				// the header is no line left out; a code twice, a line of
				// another kind, a line without a code are
			QCOMPARE(skipped, 3);
			QVERIFY(PidCodification::fromCsv(QStringLiteral("nothing to read")).isEmpty());
		}

		void theCodificationIsSavedWithTheProject_data()
		{
			QTest::addColumn<QString>("suffix");
			QTest::newRow(".qet") << QStringLiteral(".qet");
			QTest::newRow(".qetz") << QStringLiteral(".qetz");
			QTest::newRow(".qetdb") << QStringLiteral(".qetdb");
		}

		void theCodificationIsSavedWithTheProject()
		{
			QFETCH(QString, suffix);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("codes") + suffix);
			const PidCodification c = PidCodification::fromCsv(QStringLiteral(
				"fluid;W02;Clarified water;E\nmaterial;M02;Galvanised steel\n"));
			{
				QETProject project;
				project.addNewDiagram();
					// none: no block
				QVERIFY(project.toXml().documentElement()
						.firstChildElement(QStringLiteral("pid_codification")).isNull());
				QSignalSpy changed(&project, &QETProject::pidCodificationChanged);
				project.setPidCodification(c);
				QCOMPARE(changed.count(), 1);
				project.setFilePath(path);
				QVERIFY(project.write().isOk());
			}
			QETProject back(path);
			QVERIFY(back.pidCodification() == c);

				// a second one, imported after opening, replaces it, once
			const PidCodification other = PidCodification::fromCsv(QStringLiteral("fluid;W03;Other\n"));
			back.setPidCodification(other);
			QVERIFY(back.write().isOk());
			QETProject again(path);
			QVERIFY(again.pidCodification() == other);
			const QDomDocument xml = again.toXml();
			const QDomElement block = xml.documentElement()
					.firstChildElement(QStringLiteral("pid_codification"));
			QVERIFY(!block.isNull());
			QVERIFY(block.nextSiblingElement(QStringLiteral("pid_codification")).isNull());
		}

		void thePageOffersTheCodesOfTheProject()
		{
			const PidCodification saved = m_project.pidCodification();
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\nfluid;SL1;Sludge;B\nmaterial;M01;Steel\n")));
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			PipePropertiesWidget page({pipe});
			auto *fluid = page.findChild<QComboBox *>(QStringLiteral("fluid"));
			auto *material = page.findChild<QComboBox *>(QStringLiteral("material"));
			QVERIFY(fluid && material);
			QCOMPARE(fluid->count(), 3);
			QCOMPARE(material->count(), 2);

				// the pipe keeps the code, the page shows it with its name
			fluid->setCurrentIndex(2);
			emit fluid->activated(2);
			QCOMPARE(pipe->properties().m_pipe.fluid, QStringLiteral("SL1"));
			QVERIFY(fluid->currentText().startsWith(QStringLiteral("SL1")));
			QVERIFY(fluid->currentText().contains(QStringLiteral("Sludge")));

				// a code typed is that code, free text stays free text
			type(material->lineEdit(), QStringLiteral("M01"));
			QCOMPARE(pipe->properties().m_pipe.material, QStringLiteral("M01"));
			type(fluid->lineEdit(), QStringLiteral("Something else"));
			QCOMPARE(pipe->properties().m_pipe.fluid, QStringLiteral("Something else"));

				// a new codification reaches the open page, the pipe keeps its code
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Water;E\n")));
			QCOMPARE(fluid->count(), 2);
			QCOMPARE(pipe->properties().m_pipe.fluid, QStringLiteral("Something else"));

			m_project.setPidCodification(saved);
			QVERIFY(fluid->count() > 2);
		}

		void noneIsOfferedFirstInTheLists()
		{
			const PidCodification saved = m_project.pidCodification();
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\nmaterial;M01;Steel\n")));
			Conductor *a = draw(m_nozzles);
			Conductor *b = draw(m_nozzles);
			QVERIFY(a && b);
			for (Conductor *pipe : {a, b}) {
				ConductorProperties p = pipe->properties();
				p.m_pipe.fluid = QStringLiteral("W01");
				p.m_pipe.material = QStringLiteral("M01");
				pipe->setProperties(p);
			}
			PipePropertiesWidget page({a, b});
			auto *fluid = page.findChild<QComboBox *>(QStringLiteral("fluid"));
			auto *material = page.findChild<QComboBox *>(QStringLiteral("material"));
			QVERIFY(fluid && material);
			QVERIFY(fluid->itemData(0).toString().isEmpty());
			QVERIFY(material->itemData(0).toString().isEmpty());

				// it clears the fluid of all the pipes, in one step, and
				// leaves the material alone
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.index();
			fluid->setCurrentIndex(0);
			emit fluid->activated(0);
			QCOMPARE(stack.index(), steps + 1);
			QVERIFY(a->properties().m_pipe.fluid.isEmpty());
			QVERIFY(b->properties().m_pipe.fluid.isEmpty());
			QCOMPARE(a->properties().m_pipe.material, QStringLiteral("M01"));
			QVERIFY(fluid->currentText().isEmpty());

				// on an empty field it changes nothing, and is not left written
			fluid->setCurrentIndex(0);
			emit fluid->activated(0);
			QCOMPARE(stack.index(), steps + 1);
			QVERIFY(fluid->currentText().isEmpty());

			stack.undo();
			QCOMPARE(a->properties().m_pipe.fluid, QStringLiteral("W01"));
			QVERIFY(fluid->currentText().startsWith(QStringLiteral("W01")));
			m_project.setPidCodification(saved);
		}

		void aPipeRemovedFromTheFolioLeavesThePageUsable()
		{
			Conductor *stays = draw(m_nozzles);
			Conductor *goes = draw(m_nozzles);
			QVERIFY(stays && goes);
			PipePropertiesWidget page({stays, goes});
			m_folio->removeItem(goes);
			delete goes;
			QCOMPARE(page.pipes(), QList<Conductor *>{stays});
			type(page.findChild<QLineEdit *>(QStringLiteral("spool")), QStringLiteral("3"));
			QCOMPARE(stays->properties().m_pipe.spool, QStringLiteral("3"));
		}
};

QET_APP_TEST_MAIN(tst_pipepanel)
#include "tst_pipepanel.moc"
