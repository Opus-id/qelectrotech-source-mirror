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
#include "qetgraphicsitem/dynamicelementtextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetproject.h"
#include "projectview.h"
#include "diagramview.h"

#include <QAction>
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

			//"Create similar" is a command of the editor with its own
			//shortcut, Ctrl+Alt+D, usable when one symbol is selected
		void createSimilarHasAShortcut()
		{
			QETDiagramEditor *editor = QetAppTest::editor();
			QVERIFY(editor);
			editor->resize(1400, 1000);
			editor->show();
			QVERIFY(QTest::qWaitForWindowExposed(editor));
			const QString copy = m_dir.filePath("similar_shortcut.qet");
			QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/every_feature.qet"), copy));
			QVERIFY(editor->openAndAddProject(copy, false));
			DiagramView *view = editor->currentProjectView()->currentDiagram();
			Diagram *diagram = view->diagram();
			QVERIFY(diagram->elements().size() >= 2);
			Element *model = diagram->elements().at(0);
			Element *other = diagram->elements().at(1);

			QAction *action = editor->findChild<QAction *>(QStringLiteral("createSimilar"));
			QVERIFY(action);
			QCOMPARE(action->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D));

				//nothing selected, then two symbols: nothing to copy
			diagram->clearSelection();
			QCoreApplication::processEvents();
			QVERIFY(!action->isEnabled());
			model->setSelected(true);
			other->setSelected(true);
			QCoreApplication::processEvents();
			QVERIFY(!action->isEnabled());
			QVERIFY(!view->similarSource());

				//one symbol: the command places a copy of it
			other->setSelected(false);
			QCoreApplication::processEvents();
			QCOMPARE(view->similarSource(), model);
			QVERIFY(action->isEnabled());
			action->trigger();
			Element *pending = diagram->pendingElement();
			QVERIFY(pending);
			QVERIFY(pending != model);
			QCOMPARE(pending->location(), model->location());

			view->setFocus();
			QTest::keyClick(view, Qt::Key_Escape);
			QVERIFY(!diagram->pendingElement());
		}

			//The order of the model, and the labels of the two copies dropped
			//after it: a model without an order yet gives numbered copies too
		void similarPlacementNumbersTheTag_data()
		{
			QTest::addColumn<QString>("model_order");
			QTest::addColumn<QString>("first");
			QTest::addColumn<QString>("second");
			QTest::addColumn<int>("next_after_undo");
			QTest::newRow("numbered") << QStringLiteral("1")
									  << QStringLiteral("U01 PP 002") << QStringLiteral("U01 PP 003") << 3;
			QTest::newRow("no order yet") << QString()
										  << QStringLiteral("U01 PP 001") << QStringLiteral("U01 PP 002") << 2;
		}

			//A P&ID symbol placed with "Create similar" takes the functional
			//unit and the train of its model, and the next order at each drop
		void similarPlacementNumbersTheTag()
		{
			QFETCH(QString, model_order);
			QFETCH(QString, first);
			QFETCH(QString, second);
			QFETCH(int, next_after_undo);

			QETDiagramEditor *editor = QetAppTest::editor();
			QVERIFY(editor);
			editor->resize(1400, 1000);
			editor->show();
			QVERIFY(QTest::qWaitForWindowExposed(editor));
			const QString copy = m_dir.filePath(
						QStringLiteral("similar_tag_%1.qet").arg(QString::fromLatin1(QTest::currentDataTag()).replace(' ', '_')));
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
				"<dynamic_text x=\"0\" y=\"15\" z=\"1\" text_width=\"-1\" Halignment=\"AlignHCenter\" Valignment=\"AlignTop\" "
				"frame=\"false\" rotation=\"0\" keep_visual_rotation=\"true\" text_from=\"ElementInfo\" "
				"uuid=\"{6a1f3c52-4444-4c2e-9a77-0b8d2f6e1a01}\" font=\"Liberation Sans,9,-1,5,50,0,0,0,0,0,Regular\">"
				"<text></text><info_name>label</info_name></dynamic_text>"
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
			information.addValue(PidTag::orderKey(), model_order);
			model->setElementInformations(model->withPidLabel(information));

			QVERIFY(view->startSimilarPlacement(model));
			Element *pending = diagram->pendingElement();
			QVERIFY(pending);
				//the symbol that follows the mouse has no tag: it would count
			QVERIFY(!PidTag::hasFields(pending->elementInformations()));

				//Each copy is told apart when it is dropped, not by where it
				//stands: the view may scroll between two clicks
			QList<Element *> dropped;
			auto dropAt = [&](const QPoint &where) {
				QTest::mouseMove(view->viewport(), where);
				QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, where);
				for (Element *e : diagram->elements()) {
					if (e != model && e != diagram->pendingElement()
							&& e->location() == model->location() && !dropped.contains(e)) {
						dropped << e;
					}
				}
			};
			const QPoint at = view->mapFromScene(pending->scenePos());
			dropAt(at);
			QCOMPARE(dropped.size(), 1);
			dropAt(at + QPoint(150, 0));
			QCOMPARE(dropped.size(), 2);
			QCOMPARE(dropped.at(0)->elementInformations().value(QStringLiteral("label")).toString(), first);
			QCOMPARE(dropped.at(1)->elementInformations().value(QStringLiteral("label")).toString(), second);
			QCOMPARE(dropped.at(1)->elementInformations().value(PidTag::trainKey()).toString(), QStringLiteral("0"));
				//the model keeps its own tag
			QCOMPARE(model->elementInformations().value(PidTag::orderKey()).toString(), model_order);
				//the label of a copy is centred under it, as the model's is
				//(it once grew to the right from where the empty text stood)
			auto labelCentre = [](Element *element) {
				const QList<DynamicElementTextItem *> texts = element->dynamicTextItems();
				return texts.isEmpty() ? qreal(-1000)
									   : texts.first()->pos().x() + texts.first()->boundingRect().width() / 2;
			};
			QVERIFY(qAbs(labelCentre(dropped.at(0)) - labelCentre(model)) < 1.0);
			QVERIFY(qAbs(labelCentre(dropped.at(1)) - labelCentre(model)) < 1.0);

			QTest::keyClick(view, Qt::Key_Escape);
			QVERIFY(!diagram->pendingElement());
				//one drop, one undo step
			diagram->undoStack().undo();
			QVERIFY(!dropped.at(1)->scene());
			QCOMPARE(PidTag::nextOrder(project, QStringLiteral("U01"), QStringLiteral("PP"), QStringLiteral("0")), next_after_undo);
		}
};

QET_APP_TEST_MAIN(tst_pendingelement)

#include "tst_pendingelement.moc"
