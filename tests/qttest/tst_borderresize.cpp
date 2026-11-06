// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "borderproperties.h"
#include "bordertitleblock.h"
#include "diagram.h"
#include "qetproject.h"
#include "ui/diagrampropertiesdialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

namespace {
	/// The button of the open dialog, null if it has none
	QPushButton *buttonOf(QWidget *dialog, QDialogButtonBox::StandardButton which)
	{
		const auto boxes = dialog->findChildren<QDialogButtonBox *>();
		for (QDialogButtonBox *box : boxes) {
			if (QPushButton *button = box->button(which)) {
				return button;
			}
		}
		return nullptr;
	}
}

// The frame of a folio and its title block follow a new size at once. Its
// sizes are read back from the project's store: the frame has to be worked
// out once they are stored, or it keeps the size it had.
class tst_borderresize : public QObject
{
	Q_OBJECT

	private slots:
		void theFrameFollowsTheNewSizeOfAFolio()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			BorderTitleBlock &border = folio->border_and_titleblock;

			BorderProperties properties = border.exportBorder();
			properties.columns_count = 77;
			properties.columns_width = 41;
			properties.rows_count = 19;
			properties.rows_height = 114;
			border.importBorder(properties);

			QCOMPARE(border.rowsCount(), 19);
			QCOMPARE(border.rowsHeight(), 114.0);
			const QRectF title_block = border.titleBlockRect();
			QCOMPARE(title_block.top(), Diagram::margin + border.columnsHeaderHeight() + 19 * 114);
			QCOMPARE(title_block.width(), border.rowsHeaderWidth() + 77 * 41.0);
		}

		void aSingleChangeIsFollowedToo()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			BorderTitleBlock &border = folio->border_and_titleblock;

			border.setRowsHeight(50);
			QCOMPARE(border.titleBlockRect().top(),
					 Diagram::margin + border.columnsHeaderHeight() + border.rowsCount() * 50);
			border.setColumnsWidth(30);
			QCOMPARE(border.titleBlockRect().width(),
					 border.rowsHeaderWidth() + border.columnsCount() * 30.0);
		}

			// Whatever goes wrong the dialog is closed at the end, or the
			// test would wait for ever: the checks come after it.
		void applyChangesTheFolioAndLeavesTheDialogOpen()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			const int steps = folio->undoStack().count();
			const int rows = folio->border_and_titleblock.rowsCount() + 3;

			int rows_after_apply = 0;
			int steps_after_apply = 0;
			int steps_after_second_apply = 0;
			bool still_open = false;

			QTimer::singleShot(0, this, [&] {
				QWidget *dialog = QApplication::activeModalWidget();
				if (!dialog) {
					return;
				}
				QSpinBox *rows_box = dialog->findChild<QSpinBox *>("m_rows_count_sp");
				QPushButton *apply = buttonOf(dialog, QDialogButtonBox::Apply);
				if (rows_box && apply) {
					rows_box->setValue(rows);
					apply->click();
					rows_after_apply = folio->border_and_titleblock.rowsCount();
					steps_after_apply = folio->undoStack().count();
					still_open = dialog->isVisible();
					apply->click();
					steps_after_second_apply = folio->undoStack().count();
				}
				buttonOf(dialog, QDialogButtonBox::Cancel)->click();
			});
			DiagramPropertiesDialog::diagramPropertiesDialog(folio);

			QCOMPARE(rows_after_apply, rows);
			QVERIFY(still_open);
			QCOMPARE(steps_after_apply, steps + 1);
			QCOMPARE(steps_after_second_apply, steps + 1);

				// Cancel keeps what was applied, and one undo takes it back
			QCOMPARE(folio->border_and_titleblock.rowsCount(), rows);
			folio->undoStack().undo();
			QCOMPARE(folio->border_and_titleblock.rowsCount(), rows - 3);
		}

		void aPaperSizeAppliedFromTheDialogGivesAFrameThatAddsUp_data()
		{
			QTest::addColumn<QString>("paper");
			QTest::addColumn<int>("page_width");
			QTest::addColumn<int>("page_height");
				// at 96 dots per inch, as the PDF export draws a folio
			QTest::newRow("A1") << "A1" << 3178 << 2245;
			QTest::newRow("A3") << "A3" << 1587 << 1122;
			QTest::newRow("A0") << "A0" << 4493 << 3178;
		}

		void aPaperSizeAppliedFromTheDialogGivesAFrameThatAddsUp()
		{
			QFETCH(QString, paper);
			QFETCH(int, page_width);
			QFETCH(int, page_height);

			QETProject project;
			Diagram *folio = project.addNewDiagram();
			BorderTitleBlock &border = folio->border_and_titleblock;
			bool applied = false;

			QTimer::singleShot(0, this, [&] {
				QWidget *dialog = QApplication::activeModalWidget();
				if (!dialog) {
					return;
				}
				auto *format = dialog->findChild<QComboBox *>("m_paper_cb");
				QPushButton *apply = buttonOf(dialog, QDialogButtonBox::Apply);
				const int index = format ? format->findText(paper) : -1;
				if (apply && index >= 0) {
					format->setCurrentIndex(index);
					emit format->activated(index);
					apply->click();
					applied = true;
				}
				buttonOf(dialog, QDialogButtonBox::Cancel)->click();
			});
			DiagramPropertiesDialog::diagramPropertiesDialog(folio);
			QVERIFY(applied);

				// The rows end where the title block starts, the columns where it ends
			const QRectF title_block = border.titleBlockRect();
			const qreal rows_end = Diagram::margin + border.columnsHeaderHeight()
					+ border.rowsCount() * border.rowsHeight();
			QCOMPARE(title_block.top(), rows_end);
			QCOMPARE(title_block.width(),
					 border.rowsHeaderWidth() + border.columnsCount() * border.columnsWidth());

				// And the whole folio has about the size of the paper, its line
				// included: whole cells do not always fill it, A1 from the
				// default folio is 4 pixels short
			const QRectF whole = border.borderAndTitleBlockRect();
			QVERIFY2(qAbs(whole.width() + 1 - page_width) <= 5,
					 qPrintable(QString::number(whole.width())));
			QVERIFY2(qAbs(whole.height() + 1 - page_height) <= 5,
					 qPrintable(QString::number(whole.height())));
		}

		void okAfterApplyAddsNothing()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			const int steps = folio->undoStack().count();
			const int rows = folio->border_and_titleblock.rowsCount() + 3;

			QTimer::singleShot(0, this, [&] {
				QWidget *dialog = QApplication::activeModalWidget();
				if (!dialog) {
					return;
				}
				QSpinBox *rows_box = dialog->findChild<QSpinBox *>("m_rows_count_sp");
				QPushButton *apply = buttonOf(dialog, QDialogButtonBox::Apply);
				if (rows_box && apply) {
					rows_box->setValue(rows);
					apply->click();
				}
				buttonOf(dialog, QDialogButtonBox::Ok)->click();
			});
			DiagramPropertiesDialog::diagramPropertiesDialog(folio);

			QCOMPARE(folio->border_and_titleblock.rowsCount(), rows);
			QCOMPARE(folio->undoStack().count(), steps + 1);
		}

		void okWithoutAnyChangeAddsNothing()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			const int steps = folio->undoStack().count();

			QTimer::singleShot(0, this, [&] {
				if (QWidget *dialog = QApplication::activeModalWidget()) {
					buttonOf(dialog, QDialogButtonBox::Ok)->click();
				}
			});
			DiagramPropertiesDialog::diagramPropertiesDialog(folio);

			QCOMPARE(folio->undoStack().count(), steps);
		}

		void aReadOnlyFolioHasNoApplyButton()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			project.setReadOnly(true);

			bool has_apply = true;
			QTimer::singleShot(0, this, [&] {
				QWidget *dialog = QApplication::activeModalWidget();
				if (!dialog) {
					return;
				}
				has_apply = buttonOf(dialog, QDialogButtonBox::Apply);
				buttonOf(dialog, QDialogButtonBox::Ok)->click();
			});
			DiagramPropertiesDialog::diagramPropertiesDialog(folio);

			QVERIFY(!has_apply);
		}
};

QET_APP_TEST_MAIN(tst_borderresize)
#include "tst_borderresize.moc"
