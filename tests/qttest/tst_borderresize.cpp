// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "borderproperties.h"
#include "bordertitleblock.h"
#include "diagram.h"
#include "qetproject.h"

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
};

QET_APP_TEST_MAIN(tst_borderresize)
#include "tst_borderresize.moc"
