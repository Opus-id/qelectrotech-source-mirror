/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "qetapptest.h"
#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidtag.h"
#include "qetgraphicsitem/element.h"
#include "qetproject.h"
#include "projectview.h"
#include "diagramview.h"

/**
	While a symbol is placed, the folio knows which one follows the mouse,
	so the crosshair can leave it out, and Space turns it.
*/
class tst_pendingelement : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

	private slots:
		void pendingWhilePlacing()
		{
			QETDiagramEditor *editor = QetAppTest::editor();
			QVERIFY(editor);
			editor->resize(1400, 1000);
			editor->show();
			QVERIFY(QTest::qWaitForWindowExposed(editor));
			const QString copy = m_dir.filePath("p.qet");
			QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/every_feature.qet"), copy));
			QVERIFY(editor->openAndAddProject(copy, false));
			DiagramView *view = editor->currentProjectView()->currentDiagram();
			Diagram *diagram = view->diagram();
			QVERIFY(!diagram->pendingElement());

			QVERIFY(!diagram->elements().isEmpty());
			Element *model = diagram->elements().first();
			QVERIFY(view->startElementPlacement(model->location(), model->scenePos() + QPointF(200, 200)));
			Element *pending = diagram->pendingElement();
			QVERIFY(pending);
			QVERIFY(pending != model);

			const qreal rotation = pending->rotation();
			view->setFocus();
			QTest::keyClick(view, Qt::Key_Space);
			QCOMPARE(pending->rotation(), rotation + 90);

			QTest::keyClick(view, Qt::Key_Escape);
			QVERIFY(!diagram->pendingElement());
		}

		void similarPlacementTakesTheOptionsAndTheRotation()
		{
			QETDiagramEditor *editor = QetAppTest::editor();
			QVERIFY(editor);
			editor->resize(1400, 1000);
			editor->show();
			QVERIFY(QTest::qWaitForWindowExposed(editor));
			const QString copy = m_dir.filePath("similar.qet");
			QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/every_feature.qet"), copy));
			QVERIFY(editor->openAndAddProject(copy, false));
			DiagramView *view = editor->currentProjectView()->currentDiagram();
			Diagram *diagram = view->diagram();
			QVERIFY(!diagram->elements().isEmpty());
			Element *model = diagram->elements().first();
			ElementOptions::Choices choices;
			choices.insert("motor", "variable");
			choices.insert("pid_status", "existing");
			model->setOptionChoices(choices);
			model->setRotation(90);
			const int before = diagram->elements().size();

			QVERIFY(view->startSimilarPlacement(model));
			Element *pending = diagram->pendingElement();
			QVERIFY(pending);
			QVERIFY(pending != model);
			QCOMPARE(pending->optionChoices(), choices);
			QCOMPARE(pending->rotation(), 90.0);

				//Drop one copy: it keeps the options, the model is untouched
			const QPoint at = view->mapFromScene(pending->scenePos());
			QTest::mouseMove(view->viewport(), at);
			QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, at);
			QVERIFY(diagram->elements().size() > before);
			Element *dropped = nullptr;
			for (Element *e : diagram->elements()) {
				if (e != model && e->location() == model->location() && e->optionChoices() == choices) {
					dropped = e;
				}
			}
			QVERIFY(dropped);
			QCOMPARE(dropped->rotation(), 90.0);
			QCOMPARE(model->optionChoices(), choices);

			QTest::keyClick(view, Qt::Key_Escape);
			QVERIFY(!diagram->pendingElement());
		}

			//A P&ID symbol placed with "Create similar" takes the functional
			//unit and the train of its model, and the next order at each drop
		void similarPlacementNumbersTheTag()
		{
			QETDiagramEditor *editor = QetAppTest::editor();
			QVERIFY(editor);
			editor->resize(1400, 1000);
			editor->show();
			QVERIFY(QTest::qWaitForWindowExposed(editor));
			const QString copy = m_dir.filePath("similar_tag.qet");
			QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/every_feature.qet"), copy));
			QVERIFY(editor->openAndAddProject(copy, false));
			DiagramView *view = editor->currentProjectView()->currentDiagram();
			Diagram *diagram = view->diagram();
			QETProject *project = diagram->project();

			project->embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("pidtest"), NamesList());
			QDomDocument definition;
			QVERIFY(definition.setContent(QStringLiteral(
				"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
				"pid_family=\"equipment\" prefix=\"PP\" width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
				"<uuid uuid=\"{6a1f3c52-1111-4c2e-9a77-0b8d2f6e1a01}\"/><names><name lang=\"en\">Pump</name></names>"
				"<description><ellipse x=\"-5\" y=\"-5\" width=\"10\" height=\"10\" style=\"line-style:normal;"
				"line-weight:normal;filling:none;color:black\" antialias=\"false\"/>"
				"<terminal uuid=\"{6a1f3c52-2222-4c2e-9a77-0b8d2f6e1a01}\" name=\"N1\" x=\"-10\" y=\"0\" "
				"orientation=\"w\" type=\"Generic\" medium=\"pipe\"/>"
				"<terminal uuid=\"{6a1f3c52-3333-4c2e-9a77-0b8d2f6e1a01}\" name=\"N2\" x=\"10\" y=\"0\" "
				"orientation=\"e\" type=\"Generic\" medium=\"pipe\"/>"
				"</description></definition>")));
			project->embeddedElementCollection()->addElementDefinition(
						QStringLiteral("import/pidtest"), QStringLiteral("pump.elmt"), definition.documentElement());
			const ElementsLocation location(QStringLiteral("embed://import/pidtest/pump.elmt"), project);
			QVERIFY(location.exist());
			PidCodification codification;
			codification.tag_models.insert(QStringLiteral("equipment"), QStringLiteral("{unit} {id} {train}{order:2}"));
			project->setPidCodification(codification);

				//The model: numbered before the train was required
			int state = 0;
			Element *model = ElementFactory::Instance()->createElement(location, nullptr, &state);
			QVERIFY(model && !state);
			diagram->addItem(model);
			model->setPos(-400, -400);
			DiagramContext information = model->elementInformations();
			information.addValue(PidTag::unitKey(), QStringLiteral("U01"));
			information.addValue(PidTag::orderKey(), QStringLiteral("1"));
			model->setElementInformations(model->withPidLabel(information));

			QVERIFY(view->startSimilarPlacement(model));
			Element *pending = diagram->pendingElement();
			QVERIFY(pending);
				//the symbol that follows the mouse has no tag: it would count
			QVERIFY(!PidTag::hasFields(pending->elementInformations()));

			const QPoint at = view->mapFromScene(pending->scenePos());
			QTest::mouseMove(view->viewport(), at);
			QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, at);
			const QPoint further = at + QPoint(150, 0);
			QTest::mouseMove(view->viewport(), further);
			QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, further);

			QList<Element *> dropped;
			for (Element *e : diagram->elements()) {
				if (e != model && e != diagram->pendingElement() && e->location() == model->location()) {
					dropped << e;
				}
			}
			QCOMPARE(dropped.size(), 2);
			std::sort(dropped.begin(), dropped.end(), [](Element *a, Element *b) { return a->pos().x() < b->pos().x(); });
			QCOMPARE(dropped.at(0)->elementInformations().value(QStringLiteral("label")).toString(), QStringLiteral("U01 PP 002"));
			QCOMPARE(dropped.at(1)->elementInformations().value(QStringLiteral("label")).toString(), QStringLiteral("U01 PP 003"));
			QCOMPARE(dropped.at(1)->elementInformations().value(PidTag::trainKey()).toString(), QStringLiteral("0"));
				//the model keeps its own tag
			QCOMPARE(model->elementInformations().value(PidTag::orderKey()).toString(), QStringLiteral("1"));

			QTest::keyClick(view, Qt::Key_Escape);
			QVERIFY(!diagram->pendingElement());
				//one drop, one undo step
			diagram->undoStack().undo();
			QVERIFY(!dropped.at(1)->scene());
			QCOMPARE(PidTag::nextOrder(project, QStringLiteral("U01"), QStringLiteral("PP"), QStringLiteral("0")), 3);
		}
};

QET_APP_TEST_MAIN(tst_pendingelement)

#include "tst_pendingelement.moc"
