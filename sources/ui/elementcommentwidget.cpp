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
#include "elementcommentwidget.h"

#include <diagram.h>
#include <diagramcontext.h>
#include <qetgraphicsitem/dynamicelementtextitem.h>
#include <qetgraphicsitem/element.h>
#include <qetgraphicsitem/elementtextitemgroup.h>
#include <qetinformation.h>
#include <undocommand/changeelementinformationcommand.h>

#include <qeticons.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

ElementCommentWidget::ElementCommentWidget(Element *element, QWidget *parent) :
	AbstractElementPropertiesEditorWidget(parent)
{
	auto *layout = new QHBoxLayout(this);
	m_comment = new QLineEdit(this);
	m_comment->setObjectName(QStringLiteral("comment"));
	const QString tip = tr("Shown on the folio by the symbol: where the fluid comes from or goes "
						   "to, for an arrow of inlet or outlet. The comment of the Informations tab.");
	m_comment->setToolTip(tip);
	layout->addWidget(m_comment, 1);
	m_several_lines = new QToolButton(this);
	m_several_lines->setObjectName(QStringLiteral("several_lines"));
	m_several_lines->setIcon(QET::Icons::EditText);
	m_several_lines->setToolTip(tr("Write it on several lines"));
	layout->addWidget(m_several_lines);
	connect(m_comment, &QLineEdit::editingFinished, this, [this]() {
		if (m_live_edit) {
			apply();
		}
	});
	connect(m_several_lines, &QToolButton::clicked, this, &ElementCommentWidget::editOnSeveralLines);
		//The line edit shows a line break as a space: the tooltip shows
		//the text as it is drawn
	connect(m_comment, &QLineEdit::textChanged, this, [this, tip](const QString &text) {
		m_comment->setToolTip(text.contains(QLatin1Char('\n')) ? text : tip);
	});
	setElement(element);
}

void ElementCommentWidget::setCommentText(const QString &text)
{
	m_comment->setText(text);
	if (m_live_edit) {
		apply();
	}
}

/**
	@brief ElementCommentWidget::editOnSeveralLines
	The comment in a box that takes line breaks, as the Informations tab
	offers it (ElementInfoPartWidget::editOnSeveralLines())
*/
void ElementCommentWidget::editOnSeveralLines()
{
	QDialog dialog(this);
	dialog.setWindowTitle(title());
	auto *edit = new QPlainTextEdit(&dialog);
	edit->setPlainText(m_comment->text());
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto *layout = new QVBoxLayout(&dialog);
	layout->addWidget(edit);
	layout->addWidget(buttons);
	edit->setFocus();
	if (dialog.exec() == QDialog::Accepted && edit->toPlainText() != m_comment->text()) {
		setCommentText(edit->toPlainText());
	}
}

/**
	@brief ElementCommentWidget::isWanted
	See the declaration
*/
bool ElementCommentWidget::isWanted(const Element *element)
{
	if (!element || element->pidFamily().isEmpty()) {
		return false;
	}
	QList<DynamicElementTextItem *> texts = element->dynamicTextItems();
	for (const ElementTextItemGroup *group : element->textGroups()) {
		texts << group->texts();
	}
	for (const DynamicElementTextItem *text : std::as_const(texts)) {
		if (text->textFrom() == DynamicElementTextItem::ElementInfo
			&& text->infoName() == QETInformation::ELMT_COMMENT) {
			return true;
		}
	}
	return false;
}

void ElementCommentWidget::setElement(Element *element)
{
	if (m_element) {
		disconnect(m_element, nullptr, this, nullptr);
	}
	m_element = element;
	if (m_element) {
			//Undone, redone, or changed in the Informations tab
		connect(m_element, &Element::elementInfoChange, this, &ElementCommentWidget::updateUi);
	}
	updateUi();
}

void ElementCommentWidget::updateUi()
{
	if (!m_element) {
		return;
	}
	const QSignalBlocker blocker(m_comment);
	m_comment->setText(m_element->elementInformations().value(QETInformation::ELMT_COMMENT).toString());
}

/**
	@brief ElementCommentWidget::associatedUndo
	@return the change of the comment of the element, or nullptr when the
	field holds the one it has
*/
QUndoCommand *ElementCommentWidget::associatedUndo() const
{
	if (!m_element) {
		return nullptr;
	}
	const DiagramContext old_information = m_element->elementInformations();
	DiagramContext new_information = old_information;
	const QString comment = m_comment->text().trimmed();
	if (comment.isEmpty() && !old_information.contains(QETInformation::ELMT_COMMENT)) {
		return nullptr;
	}
	new_information.addValue(QETInformation::ELMT_COMMENT, comment);
	if (new_information == old_information) {
		return nullptr;
	}
	auto *undo = new ChangeElementInformationCommand(m_element, old_information, new_information);
	undo->setText(tr("Change the comment of an element"));
	return undo;
}

void ElementCommentWidget::apply()
{
	if (!m_element || !m_element->diagram()) {
		return;
	}
	if (QUndoCommand *undo = associatedUndo()) {
		m_element->diagram()->undoStack().push(undo);
	}
}

bool ElementCommentWidget::setLiveEdit(bool live_edit)
{
	m_live_edit = live_edit;
	return true;
}

QString ElementCommentWidget::title() const
{
	return tr("Comment");
}
