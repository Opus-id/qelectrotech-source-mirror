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
};

QET_APP_TEST_MAIN(tst_pendingelement)

#include "tst_pendingelement.moc"
