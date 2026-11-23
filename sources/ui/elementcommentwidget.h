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
#ifndef ELEMENTCOMMENTWIDGET_H
#define ELEMENTCOMMENTWIDGET_H

#include "abstractelementpropertieseditorwidget.h"

class QPlainTextEdit;

/**
	@brief The ElementCommentWidget class
	A section of the P&ID tab of a placed symbol which shows its comment on
	the folio (a text of the symbol taken from the element information
	"comment"): the arrows of inlet and outlet of the fluids write there
	where the fluid comes from or goes to (step 8b). The same field as the
	comment of the Informations tab, typed here without looking for it among
	the electrical ones. A field of a few lines: Enter goes to the next line,
	Tab to the next field. Live, it is applied when it is left, with undo.
*/
class ElementCommentWidget : public AbstractElementPropertiesEditorWidget
{
		Q_OBJECT
	public:
		explicit ElementCommentWidget(Element *element, QWidget *parent = nullptr);

			/// Whether @p element is a P&ID symbol which shows its comment
		static bool isWanted(const Element *element);

		void setElement(Element *element) override;
		void apply() override;
		QUndoCommand *associatedUndo() const override;
		QString title() const override;
		void updateUi() override;
		bool setLiveEdit(bool live_edit) override;

		QPlainTextEdit *commentField() const { return m_comment; }

	protected:
		bool eventFilter(QObject *watched, QEvent *event) override;

	private:
		QPlainTextEdit *m_comment = nullptr;
};

#endif // ELEMENTCOMMENTWIDGET_H
