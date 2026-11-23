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

#include <QEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

ElementCommentWidget::ElementCommentWidget(Element *element, QWidget *parent) :
	AbstractElementPropertiesEditorWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	m_comment = new QPlainTextEdit(this);
	m_comment->setObjectName(QStringLiteral("comment"));
	m_comment->setToolTip(tr("Shown on the folio by the symbol: where the fluid comes from or goes "
							 "to, for an arrow of inlet or outlet. The comment of the Informations tab."));
		//Enter goes to the next line, Tab to the next field
	m_comment->setTabChangesFocus(true);
	m_comment->setLineWrapMode(QPlainTextEdit::NoWrap);
		//Three lines seen, as the text of an arrow seldom takes more
	const QFontMetrics metrics(m_comment->font());
	m_comment->setFixedHeight(metrics.lineSpacing() * 3
							  + 2 * (m_comment->frameWidth() + qRound(m_comment->document()->documentMargin())));
		//A text box says it grows in height, fixed or not: the P&ID tab
		//then shared its room left between this section and its end, and
		//the field stood in the middle of a tall section
	m_comment->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	layout->addWidget(m_comment);
		//Applied when it is left, as a line edit is when its editing ends
	m_comment->installEventFilter(this);
		//The tag on the other side, for a symbol that writes it (an off-page
		//connector, a battery limit: "to B01")
	if (showsInformation(element, QETInformation::ELMT_NOTES)) {
		layout->addWidget(new QLabel(tr("Tag on the other side"), this));
		m_notes = new QLineEdit(this);
		m_notes->setObjectName(QStringLiteral("notes"));
		m_notes->setToolTip(tr("Shown on the folio by the symbol: the equipment the line goes to "
							   "or comes from. The notes of the Informations tab."));
		layout->addWidget(m_notes);
		connect(m_notes, &QLineEdit::editingFinished, this, [this]() {
			if (m_live_edit) {
				apply();
			}
		});
	}
	setElement(element);
}

bool ElementCommentWidget::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == m_comment && event->type() == QEvent::FocusOut && m_live_edit) {
		apply();
	}
	return AbstractElementPropertiesEditorWidget::eventFilter(watched, event);
}

/**
	@brief ElementCommentWidget::isWanted
	See the declaration
*/
bool ElementCommentWidget::isWanted(const Element *element)
{
	return element && !element->pidFamily().isEmpty()
		&& showsInformation(element, QETInformation::ELMT_COMMENT);
}

/**
	@brief ElementCommentWidget::showsInformation
	See the declaration
*/
bool ElementCommentWidget::showsInformation(const Element *element, const QString &key)
{
	if (!element) {
		return false;
	}
	QList<DynamicElementTextItem *> texts = element->dynamicTextItems();
	for (const ElementTextItemGroup *group : element->textGroups()) {
		texts << group->texts();
	}
	const QString variable = QStringLiteral("%{") + key + QLatin1Char('}');
	for (const DynamicElementTextItem *text : std::as_const(texts)) {
		if ((text->textFrom() == DynamicElementTextItem::ElementInfo && text->infoName() == key)
			|| (text->textFrom() == DynamicElementTextItem::CompositeText
				&& text->compositeText().contains(variable))) {
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
	const QString comment = m_element->elementInformations().value(QETInformation::ELMT_COMMENT).toString();
		//Set again only when it differs, the cursor staying where it is
	if (comment != m_comment->toPlainText()) {
		const QSignalBlocker blocker(m_comment);
		m_comment->setPlainText(comment);
	}
	if (m_notes) {
		const QString notes = m_element->elementInformations().value(QETInformation::ELMT_NOTES).toString();
		if (notes != m_notes->text()) {
			const QSignalBlocker blocker(m_notes);
			m_notes->setText(notes);
		}
	}
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
		//A field left empty adds no information the symbol did not have
	const auto put = [&](const QString &key, const QString &value) {
		if (!value.isEmpty() || old_information.contains(key)) {
			new_information.addValue(key, value);
		}
	};
	put(QETInformation::ELMT_COMMENT, m_comment->toPlainText().trimmed());
	if (m_notes) {
		put(QETInformation::ELMT_NOTES, m_notes->text().trimmed());
	}
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
