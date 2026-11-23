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

#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

ElementCommentWidget::ElementCommentWidget(Element *element, QWidget *parent) :
	AbstractElementPropertiesEditorWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	m_comment = new QLineEdit(this);
	m_comment->setObjectName(QStringLiteral("comment"));
	m_comment->setToolTip(tr("Shown on the folio by the symbol: where the fluid comes from or goes "
							 "to, for an arrow of inlet or outlet. The comment of the Informations tab."));
	layout->addWidget(m_comment);
	connect(m_comment, &QLineEdit::editingFinished, this, [this]() {
		if (m_live_edit) {
			apply();
		}
	});
	setElement(element);
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
