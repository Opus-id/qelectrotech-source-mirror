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
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>
#include <QValidator>

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
		// The one size the panel shows of a pipe: the one its material
		// names it by, of its DN and its outside diameter
	PipeDiameter written(const Conductor *pipe)
	{
		const PipeProperties p = pipe->properties().m_pipe;
		return p.writtenSize(m_project.pidCodification().outsideDiameterFor(p.material));
	}

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
			QCOMPARE(data.nominal, PipeDiameter::nominal(80));
			QVERIFY(!data.outside.isSet());
			QCOMPARE(data.flow, QStringLiteral("120 m3/h"));
			QCOMPARE(data.piping_class, QStringLiteral("75HB13"));
			QCOMPARE(data.nominal_pressure, QStringLiteral("PN16"));
			QCOMPARE(data.spool, QStringLiteral("2"));
			QCOMPARE(data.external_finish, QStringLiteral("EA"));

				// an empty diameter is no diameter, shown as nothing
			type(edit("diameter"), QString());
			QVERIFY(!pipe->properties().m_pipe.hasSize());
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

			// Each material has its sizes, which the field offers. A pipe has
			// a DN, the size of the line, and a plastic one an outside
			// diameter too: one field takes either and finds the other in
			// the sizes of the material. Without a material, a DN.
		void theSizesAreThoseOfTheMaterial()
		{
			PidCodification codification;
				// Ø 110 and Ø 125 are both DN 100; Ø 450 has no DN
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.addSize(1100, 100);
			plastic.addSize(1250, 100);
			plastic.addSize(1600, 150);
			plastic.addSize(4500, 0);
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.addSize(0, 80);
			steel.addSize(0, 100);
			steel.addSize(0, 125);
			PidCodification::Entry bare;
			bare.code = QStringLiteral("M03");
			PidCodification::Entry stainless;
			stainless.code = QStringLiteral("M04");
			stainless.outside_diameter = true;
			stainless.addSize(889, 0);
			stainless.addSize(1143, 100);
				// another plastic, whose Ø 125 is DN 125
			PidCodification::Entry other_plastic;
			other_plastic.code = QStringLiteral("M05");
			other_plastic.outside_diameter = true;
			other_plastic.addSize(1250, 125);
			codification.materials = {plastic, steel, bare, stainless, other_plastic};
			codification.decimal = PidCodification::Decimal::Comma;
			m_project.setPidCodification(codification);

			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			auto setMaterial = [pipe](const QString &material) {
				ConductorProperties p = pipe->properties();
				p.m_pipe.material = material;
				pipe->setProperties(p);
			};
			auto diameter = [this, pipe]() { return written(pipe); };
			setMaterial(QStringLiteral("M01"));
			PipePropertiesWidget page({pipe});
			auto *list = page.findChild<QComboBox *>(QStringLiteral("diameter_list"));
			QVERIFY(list);
			QLineEdit *edit = list->lineEdit();
			auto dn = [pipe]() { return pipe->properties().m_pipe.nominal; };
			auto od = [pipe]() { return pipe->properties().m_pipe.outside; };
			int asked = 0;
			int answer = -1;
			page.setChooser([&asked, &answer](const QString &, const QStringList &) {
				++asked;
				return answer;
			});

				// a plastic: its sizes, and a number alone is an outside diameter
			QVERIFY(list->isEnabled());
			QCOMPARE(list->count(), 5);
			QCOMPARE(list->itemText(1), QStringLiteral("Ø 110"));
			type(edit, QStringLiteral("110"));
			QCOMPARE(diameter(), PipeDiameter::outside(110));
			QCOMPARE(edit->text(), QStringLiteral("Ø 110"));
			QVERIFY(page.diameterMessage().isEmpty());
				// with it, the DN the material has for it: the size of the line
			QCOMPARE(dn(), PipeDiameter::nominal(100));

				// a size the material has not: the nearest one, and said; the
				// greater of two as near
			type(edit, QStringLiteral("123"));
			QCOMPARE(diameter(), PipeDiameter::outside(125));
			QCOMPARE(edit->text(), QStringLiteral("Ø 125"));
				// an outside diameter has one DN in its material, whatever
				// another material says of it: nothing is asked
			QCOMPARE(dn(), PipeDiameter::nominal(100));
			QCOMPARE(asked, 0);
			QVERIFY(page.diameterMessage().contains(QStringLiteral("123")));
			type(edit, QStringLiteral("112"));
			QCOMPARE(diameter(), PipeDiameter::outside(110));
			type(edit, QStringLiteral("117,5"));
			QCOMPARE(diameter(), PipeDiameter::outside(125));

				// a DN typed on a plastic is its DN: the outside diameter it
				// had stays when the material has it for this DN
			type(edit, QStringLiteral("DN 100"));
			QCOMPARE(dn(), PipeDiameter::nominal(100));
			QCOMPARE(od(), PipeDiameter::outside(125));
			QCOMPARE(edit->text(), QStringLiteral("Ø 125"));
			QCOMPARE(asked, 0);
				// the only one the material has for it is taken
			type(edit, QStringLiteral("dn150"));
			QCOMPARE(dn(), PipeDiameter::nominal(150));
			QCOMPARE(od(), PipeDiameter::outside(160));
			QCOMPARE(asked, 0);
				// among several, the user chooses, in the same step
			type(edit, QString());
			QVERIFY(!pipe->properties().m_pipe.hasSize());
			QUndoStack &stack = m_folio->undoStack();
			int steps = stack.count();
			answer = 0;
			type(edit, QStringLiteral("DN 100"));
			QCOMPARE(asked, 1);
			QCOMPARE(dn(), PipeDiameter::nominal(100));
			QCOMPARE(od(), PipeDiameter::outside(110));
			QCOMPARE(stack.count(), steps + 1);
			QVERIFY(page.diameterMessage().isEmpty());
				// or leaves it empty: the DN is written meanwhile, and said
			type(edit, QString());
			answer = -1;
			type(edit, QStringLiteral("DN 100"));
			QCOMPARE(asked, 2);
			QCOMPARE(dn(), PipeDiameter::nominal(100));
			QVERIFY(!od().isSet());
			QCOMPARE(edit->text(), QStringLiteral("DN 100"));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("DN 100")));
				// a DN the material has no size for: the nearest
			answer = 1;
			type(edit, QStringLiteral("DN 110"));
			QCOMPARE(dn(), PipeDiameter::nominal(100));
			QCOMPARE(od(), PipeDiameter::outside(125));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("DN 110")));

				// an outside diameter on a pipe without size: its DN in the
				// material, without a question
			type(edit, QString());
			asked = 0;
			type(edit, QStringLiteral("125"));
			QCOMPARE(asked, 0);
			QCOMPARE(od(), PipeDiameter::outside(125));
			QCOMPARE(dn(), PipeDiameter::nominal(100));
				// one the material gives no DN: none is guessed, and it is said
			type(edit, QString());
			type(edit, QStringLiteral("450"));
			QCOMPARE(asked, 0);
			QCOMPARE(od(), PipeDiameter::outside(450));
			QVERIFY(!dn().isSet());
			QVERIFY(page.diameterMessage().contains(QStringLiteral("DN")));
			type(edit, QStringLiteral("110"));
			QCOMPARE(dn(), PipeDiameter::nominal(100));

				// to tenths, with the separator of the project
			setMaterial(QStringLiteral("M04"));
			QCOMPARE(list->itemText(1), QStringLiteral("Ø 88,9"));
			type(edit, QStringLiteral("88.9"));
			QCOMPARE(diameter(), PipeDiameter::outside(88.9));
			QCOMPARE(edit->text(), QStringLiteral("Ø 88,9"));
				// which the material gives no DN: none is kept, nor guessed,
				// and it is said
			QVERIFY(!dn().isSet());
			QVERIFY(!page.diameterMessage().isEmpty());

				// a material without sizes: nothing to choose, and said
			setMaterial(QStringLiteral("M03"));
			QVERIFY(!list->isEnabled());
			QVERIFY(page.diameterMessage().contains(QStringLiteral("M03")));
			QCOMPARE(diameter(), PipeDiameter::outside(88.9));

				// a steel: its sizes; an outside diameter typed gives the DN
				// the other materials have for it, and a steel pipe has no
				// other size
			setMaterial(QStringLiteral("M02"));
			QVERIFY(list->isEnabled());
			QCOMPARE(list->itemText(1), QStringLiteral("DN 80"));
			type(edit, QStringLiteral("Ø 110"));
			QCOMPARE(dn(), PipeDiameter::nominal(100));
			QVERIFY(!od().isSet());
			QCOMPARE(edit->text(), QStringLiteral("DN 100"));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("Ø 110")));
				// which no material has: refused
			type(edit, QStringLiteral("Ø 90"));
			QCOMPARE(dn(), PipeDiameter::nominal(100));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("Ø 90")));
				// two plastics do not give it the same DN: asked; no answer,
				// nothing changed
			asked = 0;
			answer = -1;
			steps = stack.count();
			type(edit, QStringLiteral("Ø 125"));
			QCOMPARE(asked, 1);
			QCOMPARE(dn(), PipeDiameter::nominal(100));
			QCOMPARE(stack.count(), steps);
			answer = 1;
			type(edit, QStringLiteral("Ø 125"));
			QCOMPARE(dn(), PipeDiameter::nominal(125));
			QVERIFY(!od().isSet());

				// no material, or one the codification has not: a DN, among
				// those of all the materials of the project
			for (const QString &none : {QStringLiteral("XYZ"), QString()})
			{
				setMaterial(none);
				QVERIFY(list->isEnabled());
				QCOMPARE(list->count(), 5);
				QCOMPARE(list->itemText(1), QStringLiteral("DN 80"));
				type(edit, QStringLiteral("80"));
				QCOMPARE(dn(), PipeDiameter::nominal(80));
				QVERIFY(!od().isSet());
				QCOMPARE(edit->text(), QStringLiteral("DN 80"));
				QVERIFY(page.diameterMessage().isEmpty());
					// an outside diameter typed: its DN only, and said
				type(edit, QStringLiteral("Ø 110"));
				QCOMPARE(dn(), PipeDiameter::nominal(100));
				QVERIFY(!od().isSet());
				QVERIFY(page.diameterMessage().contains(QStringLiteral("Ø 110")));
					// out of the list: the nearest one, the greater of two
				type(edit, QStringLiteral("90"));
				QCOMPARE(dn(), PipeDiameter::nominal(100));
				type(edit, QStringLiteral("60"));
				QCOMPARE(dn(), PipeDiameter::nominal(80));
					// what is no size of a pipe is refused
				type(edit, QStringLiteral("G 1/2"));
				QCOMPARE(dn(), PipeDiameter::nominal(80));
				QVERIFY(!page.diameterMessage().isEmpty());
			}

				// several materials: a DN for all of them
				// (severalMaterialsTakeOneDn()), unless one has no sizes
			setMaterial(QStringLiteral("M01"));
			Conductor *other = draw(m_nozzles);
			ConductorProperties p = other->properties();
			p.m_pipe.material = QStringLiteral("M02");
			other->setProperties(p);
			PipePropertiesWidget both({pipe, other});
			QVERIFY(both.findChild<QComboBox *>(QStringLiteral("diameter_list"))->isEnabled());
			p.m_pipe.material = QStringLiteral("M03");
			other->setProperties(p);
			both.updateUi();
			QVERIFY(!both.findChild<QComboBox *>(QStringLiteral("diameter_list"))->isEnabled());
			QVERIFY(both.diameterMessage().contains(QStringLiteral("M03")));

				// a project which lists no material offers the DN of ISO 6708
			m_project.setPidCodification(PidCodification());
			QVERIFY(list->isEnabled());
			QCOMPARE(list->count(), int(PipeProperties::preferredDiameters().size()) + 1);
			type(edit, QStringLiteral("100"));
			QCOMPARE(diameter(), PipeDiameter::nominal(100));
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(100));
			QVERIFY(!pipe->properties().m_pipe.outside.isSet());
		}

			// Pipes of several materials selected: the DN of the project
			// offered, one given to all of them, each with the outside
			// diameter of its material, asked once for a material and a DN
		void severalMaterialsTakeOneDn()
		{
			PidCodification codification;
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.addSize(900, 80);
			plastic.addSize(1100, 100);
			plastic.addSize(1250, 100);
			plastic.addSize(1600, 150);
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.addSize(0, 80);
			steel.addSize(0, 100);
			steel.addSize(0, 150);
			codification.materials = {plastic, steel};
			m_project.setPidCodification(codification);

			QList<Conductor *> pipes;
			for (const QString &code : {QStringLiteral("M01"), QStringLiteral("M02"),
										QStringLiteral("M01")}) {
				Conductor *pipe = draw(m_nozzles);
				QVERIFY(pipe);
				ConductorProperties p = pipe->properties();
				p.m_pipe.material = code;
				pipe->setProperties(p);
				pipes << pipe;
			}
			auto sizes = [&pipes](int i) { return pipes.at(i)->properties().m_pipe; };
			PipePropertiesWidget page(pipes);
			auto *list = page.findChild<QComboBox *>(QStringLiteral("diameter_list"));
			QVERIFY(list && list->isEnabled());
			QCOMPARE(list->count(), 4);
			QCOMPARE(list->itemText(1), QStringLiteral("DN 80"));
			int asked = 0;
			page.setChooser([&asked](const QString &, const QStringList &) {
				++asked;
				return 1;
			});

				// a number alone is a DN; one question for the two plastic pipes
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();
			type(list->lineEdit(), QStringLiteral("100"));
			QCOMPARE(asked, 1);
			for (int i = 0 ; i < 3 ; ++i) {
				QCOMPARE(sizes(i).nominal, PipeDiameter::nominal(100));
			}
			QCOMPARE(sizes(0).outside, PipeDiameter::outside(125));
			QCOMPARE(sizes(2).outside, PipeDiameter::outside(125));
			QVERIFY(!sizes(1).outside.isSet());
			QCOMPARE(stack.count(), steps + 1);
				// the DN they share is shown
			QCOMPARE(list->lineEdit()->text(), QStringLiteral("DN 100"));

				// an outside diameter typed gives the DN the materials have
				// for it, and each its own outside diameter
			type(list->lineEdit(), QStringLiteral("Ø 160"));
			QCOMPARE(asked, 1);
			QCOMPARE(sizes(1).nominal, PipeDiameter::nominal(150));
			QCOMPARE(sizes(0).outside, PipeDiameter::outside(160));
			QVERIFY(!sizes(1).outside.isSet());
			stack.undo();
			QCOMPARE(sizes(1).nominal, PipeDiameter::nominal(100));
			QCOMPARE(sizes(2).outside, PipeDiameter::outside(125));
				// redone: the stack, shared with the next tests, keeps counting
			stack.redo();
			m_project.setPidCodification(PidCodification());
		}

			// The outside diameter chosen for a DN which has several may be
			// remembered: it becomes the default one of this DN in the
			// material, in the step of the pipe, and is no longer asked
		void theOutsideDiameterChosenMayBeRemembered()
		{
			PidCodification codification;
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.addSize(1100, 100);
			plastic.addSize(1250, 100);
			codification.materials = {plastic};
			m_project.setPidCodification(codification);
			auto usual = [this]() {
				return m_project.pidCodification().defaultOutside(PipeDiameter::nominal(100),
																   QStringLiteral("M01"));
			};

			QList<Conductor *> pipes;
			for (int i = 0 ; i < 3 ; ++i) {
				Conductor *pipe = draw(m_nozzles);
				QVERIFY(pipe);
				ConductorProperties p = pipe->properties();
				p.m_pipe.material = QStringLiteral("M01");
				pipe->setProperties(p);
				pipes << pipe;
			}
			int asked = 0;
			bool remember = false;
			QString box;
			auto chooser = [&asked](const QString &, const QStringList &) {
				++asked;
				return 1;
			};
			auto rememberer = [&remember, &box](const QString &text) {
				box = text;
				return remember;
			};

				// not remembered: nothing changes in the project
			{
				PipePropertiesWidget page({pipes.at(0)});
				page.setChooser(chooser);
				page.setRememberer(rememberer);
				auto *list = page.findChild<QComboBox *>(QStringLiteral("diameter_list"));
				QVERIFY(list);
				type(list->lineEdit(), QStringLiteral("DN 100"));
				QCOMPARE(asked, 1);
				QCOMPARE(pipes.at(0)->properties().m_pipe.outside, PipeDiameter::outside(125));
				QVERIFY(box.contains(QStringLiteral("DN 100")) && box.contains(QStringLiteral("M01")));
				QVERIFY(!usual().isSet());
			}

				// remembered: the default one of the material, in the same step
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();
			remember = true;
			{
				PipePropertiesWidget page({pipes.at(1)});
				page.setChooser(chooser);
				page.setRememberer(rememberer);
				auto *list = page.findChild<QComboBox *>(QStringLiteral("diameter_list"));
				QVERIFY(list);
				type(list->lineEdit(), QStringLiteral("DN 100"));
				QCOMPARE(asked, 2);
				QCOMPARE(pipes.at(1)->properties().m_pipe.outside, PipeDiameter::outside(125));
				QCOMPARE(usual(), PipeDiameter::outside(125));
				QCOMPARE(stack.count(), steps + 1);
				QVERIFY(page.diameterMessage().contains(QStringLiteral("Codification")));
			}
			stack.undo();
			QVERIFY(!usual().isSet());
			QVERIFY(!pipes.at(1)->properties().m_pipe.outside.isSet());
			stack.redo();
			QCOMPARE(usual(), PipeDiameter::outside(125));

				// then it is taken without a question
			{
				PipePropertiesWidget page({pipes.at(2)});
				page.setChooser(chooser);
				page.setRememberer(rememberer);
				auto *list = page.findChild<QComboBox *>(QStringLiteral("diameter_list"));
				QVERIFY(list);
				type(list->lineEdit(), QStringLiteral("DN 100"));
				QCOMPARE(asked, 2);
				QCOMPARE(pipes.at(2)->properties().m_pipe.outside, PipeDiameter::outside(125));
			}
			m_project.setPidCodification(PidCodification());
		}

			// A change of material keeps the DN, the size of the line, in the
			// same step; the outside diameter is the one of the new material,
			// which the user chooses among several
		void aChangeOfMaterialKeepsTheDn()
		{
			PidCodification codification;
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.addSize(1100, 100);
			plastic.addSize(1250, 100);
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.addSize(0, 80);
			steel.addSize(0, 100);
			steel.addSize(0, 125);
				// another plastic, whose Ø 125 is DN 125
			PidCodification::Entry other_plastic;
			other_plastic.code = QStringLiteral("M03");
			other_plastic.outside_diameter = true;
			other_plastic.addSize(1250, 125);
			codification.materials = {plastic, steel, other_plastic};
			m_project.setPidCodification(codification);

			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			ConductorProperties p = pipe->properties();
			p.m_pipe.material = QStringLiteral("M01");
				// an outside diameter alone, as a pipe drawn before both
				// sizes were kept
			p.m_pipe.outside = PipeDiameter::outside(110);
			pipe->setProperties(p);
			auto diameter = [this, pipe]() { return written(pipe); };
			auto sizes = [pipe]() { return pipe->properties().m_pipe; };
			PipePropertiesWidget page({pipe});
			auto *material = page.findChild<QComboBox *>(QStringLiteral("material"));
			QVERIFY(material);
			int asked = 0;
			int answer = -1;
			page.setChooser([&asked, &answer](const QString &, const QStringList &items) {
				++asked;
				return items.size() > 1 ? answer : -1;
			});

				// an outside diameter alone gets the DN its material has for
				// it, without a question, in one step
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();
			type(material->lineEdit(), QStringLiteral("M02"));
			QCOMPARE(pipe->properties().m_pipe.material, QStringLiteral("M02"));
			QCOMPARE(diameter(), PipeDiameter::nominal(100));
				// a steel pipe has a DN, and no outside diameter
			QVERIFY(!sizes().outside.isSet());
			QCOMPARE(asked, 0);
			QCOMPARE(stack.count(), steps + 1);
			stack.undo();
			QCOMPARE(pipe->properties().m_pipe.material, QStringLiteral("M01"));
			QCOMPARE(diameter(), PipeDiameter::outside(110));
			QVERIFY(!sizes().nominal.isSet());
			stack.redo();

				// back to the plastic: Ø 110 and Ø 125 are both DN 100,
				// which stays the size of the line
			answer = 1;
			type(material->lineEdit(), QStringLiteral("M01"));
			QCOMPARE(asked, 1);
			QCOMPARE(diameter(), PipeDiameter::outside(125));
			QCOMPARE(sizes().nominal, PipeDiameter::nominal(100));

				// to the steel again: the DN is there, nothing is asked, and
				// the outside diameter goes
			type(material->lineEdit(), QStringLiteral("M02"));
			QCOMPARE(asked, 1);
			QCOMPARE(diameter(), PipeDiameter::nominal(100));
			QVERIFY(!sizes().outside.isSet());
			QVERIFY(page.diameterMessage().isEmpty());

				// an outside diameter alone on a pipe without material, which
				// two plastics do not give the same DN, and no answer: the
				// size stays, said not to suit the material
			p = pipe->properties();
			p.m_pipe.material = QString();
			p.m_pipe.nominal = PipeDiameter();
			p.m_pipe.outside = PipeDiameter::outside(125);
			pipe->setProperties(p);
			answer = -1;
			type(material->lineEdit(), QStringLiteral("M02"));
			QCOMPARE(asked, 2);
			QCOMPARE(diameter(), PipeDiameter::outside(125));
			QVERIFY(!sizes().nominal.isSet());
			QVERIFY(page.diameterMessage().contains(QStringLiteral("M02")));
				// an answer: the DN chosen, and no outside diameter
			answer = 1;
			type(material->lineEdit(), QStringLiteral("M01"));
			type(material->lineEdit(), QStringLiteral("M02"));
			QCOMPARE(sizes().nominal, PipeDiameter::nominal(125));
			QVERIFY(!sizes().outside.isSet());

				// no outside diameter chosen for the plastic: the DN is
				// written meanwhile, and said
			p = pipe->properties();
			p.m_pipe.nominal = PipeDiameter::nominal(100);
			pipe->setProperties(p);
			answer = -1;
			type(material->lineEdit(), QStringLiteral("M01"));
			QCOMPARE(sizes().nominal, PipeDiameter::nominal(100));
			QVERIFY(!sizes().outside.isSet());
			QCOMPARE(diameter(), PipeDiameter::nominal(100));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("M01")));

				// no material: the DN stays, alone
			p = pipe->properties();
			p.m_pipe.outside = PipeDiameter::outside(110);
			pipe->setProperties(p);
			type(material->lineEdit(), QString());
			QVERIFY(pipe->properties().m_pipe.material.isEmpty());
			QCOMPARE(sizes().nominal, PipeDiameter::nominal(100));
			QVERIFY(!sizes().outside.isSet());
			QVERIFY(page.diameterMessage().isEmpty());
			m_project.setPidCodification(PidCodification());
		}

			// A DN which has several outside diameters in a material, one of
			// them its default one: taken without a question, and said
		void aDefaultOutsideDiameterIsTakenWithoutAQuestion()
		{
			PidCodification codification;
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.addSize(1100, 100);
			plastic.addSize(1250, 100, true);
				// DN 150 has two too, and no default one
			plastic.addSize(1600, 150);
			plastic.addSize(1800, 150);
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.addSize(0, 100);
			steel.addSize(0, 150);
			codification.materials = {plastic, steel};
			m_project.setPidCodification(codification);

			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			ConductorProperties p = pipe->properties();
			p.m_pipe.material = QStringLiteral("M01");
			pipe->setProperties(p);
			PipePropertiesWidget page({pipe});
			QLineEdit *edit = page.findChild<QComboBox *>(QStringLiteral("diameter_list"))->lineEdit();
			auto *material = page.findChild<QComboBox *>(QStringLiteral("material"));
			QVERIFY(edit && material);
			auto dn = [pipe]() { return pipe->properties().m_pipe.nominal; };
			auto od = [pipe]() { return pipe->properties().m_pipe.outside; };
			int asked = 0;
			page.setChooser([&asked](const QString &, const QStringList &) {
				++asked;
				return 0;
			});

				// a DN typed: its default outside diameter, in the same step
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();
			type(edit, QStringLiteral("DN 100"));
			QCOMPARE(asked, 0);
			QCOMPARE(dn(), PipeDiameter::nominal(100));
			QCOMPARE(od(), PipeDiameter::outside(125));
			QCOMPARE(stack.count(), steps + 1);
			QVERIFY(page.diameterMessage().contains(QStringLiteral("125")));
				// the other one of this DN stays when the pipe has it
			type(edit, QStringLiteral("110"));
			type(edit, QStringLiteral("DN 100"));
			QCOMPARE(od(), PipeDiameter::outside(110));
			QCOMPARE(asked, 0);
				// no default one: the question, as before
			type(edit, QStringLiteral("DN 150"));
			QCOMPARE(asked, 1);
			QCOMPARE(od(), PipeDiameter::outside(160));

				// a change of material: the default one again, and said
			type(material->lineEdit(), QStringLiteral("M02"));
			type(edit, QStringLiteral("100"));
			QVERIFY(!od().isSet());
			type(material->lineEdit(), QStringLiteral("M01"));
			QCOMPARE(asked, 1);
			QCOMPARE(dn(), PipeDiameter::nominal(100));
			QCOMPARE(od(), PipeDiameter::outside(125));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("125")));
			m_project.setPidCodification(PidCodification());
		}

			// Several pipes get a material at once: one question for a DN,
			// whatever their number
		void aQuestionIsAskedOnceForADn()
		{
			PidCodification codification;
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.addSize(900, 80);
			plastic.addSize(1100, 100);
			plastic.addSize(1250, 100);
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.addSize(0, 80);
			steel.addSize(0, 100);
			codification.materials = {plastic, steel};
			m_project.setPidCodification(codification);

			QList<Conductor *> pipes;
			for (int dn : {100, 100, 80, 100}) {
				Conductor *pipe = draw(m_nozzles);
				QVERIFY(pipe);
				ConductorProperties p = pipe->properties();
				p.m_pipe.material = QStringLiteral("M02");
				p.m_pipe.nominal = PipeDiameter::nominal(dn);
				pipe->setProperties(p);
				pipes << pipe;
			}
			PipePropertiesWidget page(pipes);
			QStringList questions;
			page.setChooser([&questions](const QString &question, const QStringList &items) {
				questions << question;
				return int(items.size()) - 1;
			});
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();
			type(page.findChild<QComboBox *>(QStringLiteral("material"))->lineEdit(), QStringLiteral("M01"));
			QCOMPARE(questions.size(), 1);
			QVERIFY(questions.first().contains(QStringLiteral("DN 100")));
			QCOMPARE(stack.count(), steps + 1);
			for (Conductor *pipe : std::as_const(pipes)) {
				const PipeProperties data = pipe->properties().m_pipe;
				QCOMPARE(data.material, QStringLiteral("M01"));
				QCOMPARE(data.outside, data.nominal == PipeDiameter::nominal(80)
						 ? PipeDiameter::outside(90) : PipeDiameter::outside(125));
			}
			stack.undo();
			for (Conductor *pipe : std::as_const(pipes)) {
				QCOMPARE(pipe->properties().m_pipe.material, QStringLiteral("M02"));
				QVERIFY(!pipe->properties().m_pipe.outside.isSet());
			}
			m_project.setPidCodification(PidCodification());
		}

			// From a material to another of the same kind: an outside diameter
			// the new material has for this DN stays, else it is the one it
			// has (a plastic Ø 110 is a stainless Ø 114,3); a DN stays even
			// when the new material has it not, and it is said, unless the
			// project says it equal to one the material has (DN 65, DN 60)
		void aChangeOfMaterialOfTheSameKind()
		{
			PidCodification codification;
			PidCodification::Entry plastic;
			plastic.code = QStringLiteral("M01");
			plastic.outside_diameter = true;
			plastic.addSize(1100, 100);
			plastic.addSize(1250, 0);
			PidCodification::Entry stainless;
			stainless.code = QStringLiteral("M02");
			stainless.outside_diameter = true;
			stainless.addSize(889, 0);
			stainless.addSize(1143, 100);
			PidCodification::Entry galvanised;
			galvanised.code = QStringLiteral("M03");
			galvanised.addSize(0, 50);
			galvanised.addSize(0, 65);
			PidCodification::Entry iron;
			iron.code = QStringLiteral("M04");
			iron.addSize(0, 60);
			iron.addSize(0, 80);
			PidCodification::Entry other_plastic;
			other_plastic.code = QStringLiteral("M05");
			other_plastic.outside_diameter = true;
			other_plastic.addSize(1100, 100);
			other_plastic.addSize(1600, 150);
			codification.materials = {plastic, stainless, galvanised, iron, other_plastic};
			codification.decimal = PidCodification::Decimal::Comma;
			m_project.setPidCodification(codification);

			Conductor *pipe = draw(m_nozzles);
			ConductorProperties p = pipe->properties();
			p.m_pipe.material = QStringLiteral("M01");
			p.m_pipe.nominal = PipeDiameter::nominal(100);
			p.m_pipe.outside = PipeDiameter::outside(110);
			pipe->setProperties(p);
			auto diameter = [this, pipe]() { return written(pipe); };
			PipePropertiesWidget page({pipe});
			int asked = 0;
			page.setChooser([&asked](const QString &, const QStringList &) {
				++asked;
				return -1;
			});
			auto *material = page.findChild<QComboBox *>(QStringLiteral("material"));

				// another plastic which has this outside diameter: it stays,
				// and nothing is said
			type(material->lineEdit(), QStringLiteral("M05"));
			QCOMPARE(pipe->properties().m_pipe.material, QStringLiteral("M05"));
			QCOMPARE(diameter(), PipeDiameter::outside(110));
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(100));
			QVERIFY(page.diameterMessage().isEmpty());

				// one which has it not: the one it has for the DN
			type(material->lineEdit(), QStringLiteral("M02"));
			QCOMPARE(diameter(), PipeDiameter::outside(114.3));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("Ø 114,3")));
				// the DN of the line stays
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(100));
			QCOMPARE(asked, 0);

				// two metals: the DN stays, even when the new one has it not,
				// which is said; never the nearest one
			p = pipe->properties();
			p.m_pipe.material = QStringLiteral("M03");
			p.m_pipe.nominal = PipeDiameter::nominal(65);
			p.m_pipe.outside = PipeDiameter();
			pipe->setProperties(p);
			type(material->lineEdit(), QStringLiteral("M04"));
			QCOMPARE(pipe->properties().m_pipe.material, QStringLiteral("M04"));
			QCOMPARE(diameter(), PipeDiameter::nominal(65));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("DN 65")));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("M04")));

				// to a plastic which has no size of this DN: the DN stays,
				// without an outside diameter, and it is said
			type(material->lineEdit(), QStringLiteral("M01"));
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(65));
			QVERIFY(!pipe->properties().m_pipe.outside.isSet());
			QVERIFY(page.diameterMessage().contains(QStringLiteral("DN 65")));
			QCOMPARE(asked, 0);

				// the project says DN 60 and DN 65 the same size of line: a
				// line takes the one its new material has, and it is said
			codification.addEqualNominals(60, 65);
			m_project.setPidCodification(codification);
			type(material->lineEdit(), QStringLiteral("M03"));
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(65));
			QVERIFY(page.diameterMessage().isEmpty());
			type(material->lineEdit(), QStringLiteral("M04"));
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(60));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("DN 60")));
			type(material->lineEdit(), QStringLiteral("M03"));
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(65));
				// and so does a DN typed
			type(material->lineEdit(), QStringLiteral("M04"));
			auto *size = page.findChild<QComboBox *>(QStringLiteral("diameter_list"));
			type(size->lineEdit(), QStringLiteral("80"));
			type(size->lineEdit(), QStringLiteral("65"));
			QCOMPARE(pipe->properties().m_pipe.nominal, PipeDiameter::nominal(60));
			QVERIFY(page.diameterMessage().contains(QStringLiteral("DN 65")));
			QCOMPARE(asked, 0);
			m_project.setPidCodification(PidCodification());
		}

			// DN 60 and DN 65, which the project says the same size of line,
			// are one choice for a pipe without material, written "DN 60/65":
			// its material chooses later
		void equalDnAreOneChoiceWithoutMaterial()
		{
			PidCodification codification;
			PidCodification::Entry galvanised;
			galvanised.code = QStringLiteral("M03");
			galvanised.addSize(0, 50);
			galvanised.addSize(0, 65);
			PidCodification::Entry iron;
			iron.code = QStringLiteral("M04");
			iron.addSize(0, 60);
			iron.addSize(0, 80);
			codification.materials = {galvanised, iron};
			codification.addEqualNominals(60, 65);
			m_project.setPidCodification(codification);

			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			ConductorProperties p = pipe->properties();
			p.m_pipe = PipeProperties();
			pipe->setProperties(p);
			auto dn = [pipe]() { return pipe->properties().m_pipe.nominal; };
			PipePropertiesWidget page({pipe});
			auto *list = page.findChild<QComboBox *>(QStringLiteral("diameter_list"));
			auto *material = page.findChild<QComboBox *>(QStringLiteral("material"));
			QVERIFY(list && material);
			QLineEdit *edit = list->lineEdit();

				// nothing, DN 50, DN 60/65, DN 80
			QCOMPARE(list->count(), 4);
			QCOMPARE(list->itemText(2), QStringLiteral("DN 60/65"));
				// either typed, or both, is the same, and nothing is said; the
				// pipe keeps the usual one, DN 60 being of cast iron only
			for (const QString &typed : {QStringLiteral("65"), QStringLiteral("DN 60"),
										 QStringLiteral("DN 60/65"), QStringLiteral("65/60")})
			{
				type(edit, QStringLiteral("80"));
				type(edit, typed);
				QCOMPARE(dn(), PipeDiameter::nominal(65));
				QCOMPARE(edit->text(), QStringLiteral("DN 60/65"));
				QVERIFY2(page.diameterMessage().isEmpty(), qPrintable(typed));
			}
				// the field takes it as the list writes it
			int position = 0;
			QString written = QStringLiteral("DN 60/65");
			QCOMPARE(list->validator()->validate(written, position), QValidator::Acceptable);
				// DN which are not said equal are no size
			type(edit, QStringLiteral("60/80"));
			QCOMPARE(dn(), PipeDiameter::nominal(65));
			QVERIFY(!page.diameterMessage().isEmpty());

				// its material chooses: the one it has
			type(material->lineEdit(), QStringLiteral("M03"));
			QCOMPARE(dn(), PipeDiameter::nominal(65));
			QCOMPARE(edit->text(), QStringLiteral("DN 65"));
			type(material->lineEdit(), QStringLiteral("M04"));
			QCOMPARE(dn(), PipeDiameter::nominal(60));
			QCOMPARE(edit->text(), QStringLiteral("DN 60"));
				// and both again without it, whichever it had
			type(material->lineEdit(), QString());
			QCOMPARE(dn(), PipeDiameter::nominal(65));
			QCOMPARE(edit->text(), QStringLiteral("DN 60/65"));
			QVERIFY(page.diameterMessage().isEmpty());
			m_project.setPidCodification(PidCodification());
		}

		void severalPipesChangeOnlyTheFieldEdited()
		{
			Conductor *first = draw(m_nozzles);
			Conductor *second = draw(m_nozzles);
			QVERIFY(first && second);
			ConductorProperties p = first->properties();
			p.m_pipe.nominal = PipeDiameter::nominal(80);
			p.m_pipe.material = QStringLiteral("M01");
			first->setProperties(p);
			p = second->properties();
			p.m_pipe.nominal = PipeDiameter::nominal(100);
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
			QCOMPARE(first->properties().m_pipe.nominal.value(), 80.0);
			QCOMPARE(second->properties().m_pipe.nominal.value(), 100.0);

				// a field edited reaches both, in one step, and no other field
			type(fluid->lineEdit(), QStringLiteral("W02"));
			QCOMPARE(stack.count(), steps + 1);
			QCOMPARE(first->properties().m_pipe.fluid, QStringLiteral("W02"));
			QCOMPARE(second->properties().m_pipe.fluid, QStringLiteral("W02"));
			QCOMPARE(first->properties().m_pipe.nominal.value(), 80.0);
			QCOMPARE(second->properties().m_pipe.nominal.value(), 100.0);
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

			// ---- the colour ----
			// One button shows the colour drawn and where it comes from (the
			// order of Conductor::pipeColor()); a small one gives the
			// automatic colour back, enabled when there is a colour to clear.
		void theColorButtonSaysWhereTheColorComesFrom()
		{
			const PidCodification saved = m_project.pidCodification();
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\n"
				"fluid_color;E;#0000ff\n"
				"object_color;pipe;#808000\n"
				"status_color;existing;#808080\n")));
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(pipe);
			PipePropertiesWidget page({pipe});
			auto *color = page.findChild<QPushButton *>(QStringLiteral("color"));
			auto *automatic = page.findChild<QToolButton *>(QStringLiteral("automatic_color"));
			QVERIFY(color && automatic);
			auto set = [pipe](const std::function<void(PipeProperties &)> &change) {
				ConductorProperties p = pipe->properties();
				change(p.m_pipe);
				pipe->setProperties(p);
			};

				// no fluid: the family of the pipes; nothing to clear
			QCOMPARE(color->text(), QStringLiteral("Automatic (pipe lines)"));
			QVERIFY(!color->icon().isNull());
			QVERIFY(!automatic->isEnabled());
				// a fluid of a family with a colour
			set([](PipeProperties &p) { p.fluid = QStringLiteral("W01"); });
			QCOMPARE(color->text(), QStringLiteral("Automatic (fluid)"));

				// a colour chosen: its code, one step, and it may be cleared
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.index();
			page.setManualColor(QColor(1, 2, 3));
			QCOMPARE(stack.index(), steps + 1);
			QCOMPARE(color->text(), QStringLiteral("#010203"));
			QVERIFY(automatic->isEnabled());

				// the status comes first, the colour chosen is kept and said
			set([](PipeProperties &p) { p.status = QStringLiteral("existing"); });
			QCOMPARE(pipe->drawnColor(), QColor(0x80, 0x80, 0x80));
			QCOMPARE(color->text(), QStringLiteral("Status of works"));
			QVERIFY(color->toolTip().contains(QStringLiteral("#010203")));
			QVERIFY(automatic->isEnabled());

				// the small button clears the colour chosen, in one step
			set([](PipeProperties &p) { p.status.clear(); });
			const int before_clear = stack.index();
			automatic->click();
			QCOMPARE(stack.index(), before_clear + 1);
			QVERIFY(!pipe->properties().m_pipe.color.isValid());
			QCOMPARE(color->text(), QStringLiteral("Automatic (fluid)"));
			QVERIFY(!automatic->isEnabled());
			stack.undo();
			QCOMPARE(pipe->properties().m_pipe.color, QColor(1, 2, 3));
			QCOMPARE(color->text(), QStringLiteral("#010203"));
			QVERIFY(automatic->isEnabled());
			stack.redo();

				// no colour in the project: black
			m_project.setPidCodification(PidCodification());
			QCOMPARE(color->text(), QStringLiteral("Automatic (black)"));
			m_project.setPidCodification(saved);
		}

		void severalPipesShowOneColorOrNone()
		{
			const PidCodification saved = m_project.pidCodification();
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"fluid;W01;Raw water;E\n"
				"fluid_color;E;#0000ff\n"
				"object_color;pipe;#808000\n")));
			Conductor *a = draw(m_nozzles);
			Conductor *b = draw(m_nozzles);
			QVERIFY(a && b);
			PipePropertiesWidget page({a, b});
			auto *color = page.findChild<QPushButton *>(QStringLiteral("color"));
			auto *automatic = page.findChild<QToolButton *>(QStringLiteral("automatic_color"));
			QVERIFY(color && automatic);

				// the same: shown as for one
			QCOMPARE(color->text(), QStringLiteral("Automatic (pipe lines)"));
			QVERIFY(!color->icon().isNull());

				// both automatic, from different sources: no swatch
			ConductorProperties p = a->properties();
			p.m_pipe.fluid = QStringLiteral("W01");
			a->setProperties(p);
			QCOMPARE(color->text(), QStringLiteral("Automatic"));
			QVERIFY(color->icon().isNull());
			QVERIFY(!automatic->isEnabled());

				// colours of their own which differ
			p = a->properties();
			p.m_pipe.color = QColor(1, 2, 3);
			a->setProperties(p);
			p = b->properties();
			p.m_pipe.color = QColor(4, 5, 6);
			b->setProperties(p);
			QCOMPARE(color->text(), QStringLiteral("(several values)"));
			QVERIFY(color->icon().isNull());
			QVERIFY(automatic->isEnabled());

				// cleared for both in one step
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.index();
			automatic->click();
			QCOMPARE(stack.index(), steps + 1);
			QVERIFY(!a->properties().m_pipe.color.isValid());
			QVERIFY(!b->properties().m_pipe.color.isValid());
			stack.undo();
			QCOMPARE(b->properties().m_pipe.color, QColor(4, 5, 6));
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
