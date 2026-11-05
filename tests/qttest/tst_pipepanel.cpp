// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "factory/propertieseditorfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "ui/pipepropertieswidget.h"

#include <QComboBox>
#include <QDomDocument>
#include <QLabel>
#include <QLineEdit>
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

			type(edit("nominal_diameter"), QStringLiteral("80"));
			type(edit("flow"), QStringLiteral("120 m3/h"));
			type(edit("piping_class"), QStringLiteral("75HB13"));
			type(edit("nominal_pressure"), QStringLiteral("PN16"));
			type(edit("spool"), QStringLiteral("2"));
			type(edit("external_finish"), QStringLiteral("EA"));
			const PipeProperties data = pipe->properties().m_pipe;
			QCOMPARE(data.nominal_diameter, 80);
			QCOMPARE(data.flow, QStringLiteral("120 m3/h"));
			QCOMPARE(data.piping_class, QStringLiteral("75HB13"));
			QCOMPARE(data.nominal_pressure, QStringLiteral("PN16"));
			QCOMPARE(data.spool, QStringLiteral("2"));
			QCOMPARE(data.external_finish, QStringLiteral("EA"));

				// an empty diameter is no diameter, shown as nothing
			type(edit("nominal_diameter"), QString());
			QCOMPARE(pipe->properties().m_pipe.nominal_diameter, 0);
			QVERIFY(edit("nominal_diameter")->text().isEmpty());

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

		void severalPipesChangeOnlyTheFieldEdited()
		{
			Conductor *first = draw(m_nozzles);
			Conductor *second = draw(m_nozzles);
			QVERIFY(first && second);
			ConductorProperties p = first->properties();
			p.m_pipe.nominal_diameter = 80;
			p.m_pipe.material = QStringLiteral("M01");
			first->setProperties(p);
			p = second->properties();
			p.m_pipe.nominal_diameter = 100;
			p.m_pipe.material = QStringLiteral("M01");
			p.m_pipe.line_kind = PipeProperties::Secondary;
			second->setProperties(p);

			PipePropertiesWidget page({first, second});
			auto *dn = page.findChild<QLineEdit *>(QStringLiteral("nominal_diameter"));
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
			QCOMPARE(first->properties().m_pipe.nominal_diameter, 80);
			QCOMPARE(second->properties().m_pipe.nominal_diameter, 100);

				// a field edited reaches both, in one step, and no other field
			type(fluid->lineEdit(), QStringLiteral("W02"));
			QCOMPARE(stack.count(), steps + 1);
			QCOMPARE(first->properties().m_pipe.fluid, QStringLiteral("W02"));
			QCOMPARE(second->properties().m_pipe.fluid, QStringLiteral("W02"));
			QCOMPARE(first->properties().m_pipe.nominal_diameter, 80);
			QCOMPARE(second->properties().m_pipe.nominal_diameter, 100);
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
