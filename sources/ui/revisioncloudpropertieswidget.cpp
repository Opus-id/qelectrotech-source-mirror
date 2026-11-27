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
#include "revisioncloudpropertieswidget.h"

#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../diagram.h"
#include "../properties/revisionlist.h"
#include "../qetgraphicsitem/revisionclouditem.h"
#include "../qetproject.h"

#ifdef BUILD_WITHOUT_KF
#	include "nokde/kcolorbutton.h"
#else
#	include <KColorButton>
#endif

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>

RevisionCloudPropertiesWidget::RevisionCloudPropertiesWidget(RevisionCloudItem *item, QWidget *parent) :
	PropertiesEditorWidget(parent),
	m_item(item)
{
	m_layout = new QFormLayout(this);

	m_kind = new QComboBox(this);
	m_kind->setObjectName(QStringLiteral("kind"));
	m_kind->addItem(tr("Revision cloud"), int(RevisionCloudItem::Revision));
	m_kind->addItem(tr("Review note"), int(RevisionCloudItem::Review));
	m_layout->addRow(tr("Kind"), m_kind);

		//The project's revisions, the last one at the bottom; any index can
		//be typed
	m_revision = new QComboBox(this);
	m_revision->setObjectName(QStringLiteral("revision"));
	m_revision->setEditable(true);
	m_revision->setInsertPolicy(QComboBox::NoInsert);
	if (m_item && m_item->diagram() && m_item->diagram()->project()) {
		for (const Revision &revision : m_item->diagram()->project()->revisions().revisions()) {
			const QString shown = revision.description.isEmpty()
					? revision.revision
					: QStringLiteral("%1 – %2").arg(revision.revision, revision.description);
			m_revision->addItem(shown, revision.revision);
		}
	}
	m_layout->addRow(tr("Revision"), m_revision);

	m_comment = new QPlainTextEdit(this);
	m_comment->setObjectName(QStringLiteral("comment"));
	m_comment->setTabChangesFocus(true);
	m_comment->setMaximumHeight(m_comment->fontMetrics().lineSpacing() * 5);
	m_comment->installEventFilter(this);
	m_layout->addRow(tr("Comment"), m_comment);
	m_author = new QLineEdit(this);
	m_author->setObjectName(QStringLiteral("author"));
	m_layout->addRow(tr("Author"), m_author);
	m_date = new QLineEdit(this);
	m_date->setObjectName(QStringLiteral("date"));
	m_date->setPlaceholderText(QStringLiteral("2026-11-27"));
	m_layout->addRow(tr("Date"), m_date);
	m_done = new QCheckBox(tr("Done"), this);
	m_done->setObjectName(QStringLiteral("done"));
	m_done->setToolTip(tr("A note done stays in place, greyed out, until it is deleted"));
	m_layout->addRow(QString(), m_done);
	m_reply = new QLineEdit(this);
	m_reply->setObjectName(QStringLiteral("reply"));
	m_reply->setToolTip(tr("Written under the comment, after an arrow"));
	m_layout->addRow(tr("Reply"), m_reply);

	m_color = new KColorButton(this);
	m_color->setObjectName(QStringLiteral("color"));
	m_layout->addRow(tr("Colour"), m_color);
	m_width = new QDoubleSpinBox(this);
	m_width->setObjectName(QStringLiteral("width"));
	m_width->setRange(0, 50);
	m_width->setDecimals(1);
	m_layout->addRow(tr("Line width"), m_width);
	m_style = new QComboBox(this);
	m_style->setObjectName(QStringLiteral("style"));
	m_style->addItem(tr("Solid"), int(Qt::SolidLine));
	m_style->addItem(tr("Dashed"), int(Qt::DashLine));
	m_style->addItem(tr("Dotted"), int(Qt::DotLine));
	m_style->addItem(tr("Dash dot"), int(Qt::DashDotLine));
	m_layout->addRow(tr("Line style"), m_style);
	m_label_size = new QDoubleSpinBox(this);
	m_label_size->setObjectName(QStringLiteral("labelSize"));
	m_label_size->setRange(1, 200);
	m_label_size->setDecimals(1);
	m_label_size->setSuffix(tr(" pt"));
	m_layout->addRow(tr("Size of the text"), m_label_size);
	m_arc_size = new QDoubleSpinBox(this);
	m_arc_size->setObjectName(QStringLiteral("arcSize"));
	m_arc_size->setRange(4, 400);
	m_arc_size->setDecimals(0);
	m_arc_size->setToolTip(tr("The length of an arc of the cloud"));
	m_layout->addRow(tr("Size of the arcs"), m_arc_size);

	connect(m_kind, qOverload<int>(&QComboBox::currentIndexChanged),
			this, &RevisionCloudPropertiesWidget::showRowsOfKind);
	if (m_item) {
			//An undo changes it behind the editor's back
		connect(m_item, &RevisionCloudItem::cloudChanged, this, &RevisionCloudPropertiesWidget::updateUi);
		connect(m_item, &QetShapeItem::penChanged, this, &RevisionCloudPropertiesWidget::updateUi);
	}

	updateUi();
}

QString RevisionCloudPropertiesWidget::title() const
{
	return m_item && m_item->kind() == RevisionCloudItem::Review
			? tr("Edit a review note")
			: tr("Edit a revision cloud");
}

QString RevisionCloudPropertiesWidget::revisionText() const
{
	const int index = m_revision->currentIndex();
	if (index >= 0 && m_revision->currentText() == m_revision->itemText(index))
		return m_revision->itemData(index).toString();
	return m_revision->currentText().trimmed();
}

/**
	@brief RevisionCloudPropertiesWidget::showRowsOfKind
	A revision cloud has an index; a review note, a comment, its author,
	date, state and reply.
*/
void RevisionCloudPropertiesWidget::showRowsOfKind()
{
	const bool review = m_kind->currentData().toInt() == RevisionCloudItem::Review;
	const auto show = [this](QWidget *field, bool visible) {
		field->setVisible(visible);
		if (QWidget *label = m_layout->labelForField(field))
			label->setVisible(visible);
	};
	show(m_revision, !review);
	for (QWidget *field : {static_cast<QWidget *>(m_comment), static_cast<QWidget *>(m_author),
						   static_cast<QWidget *>(m_date), static_cast<QWidget *>(m_done),
						   static_cast<QWidget *>(m_reply)})
		show(field, review);
}

void RevisionCloudPropertiesWidget::apply()
{
	if (!m_item || m_updating)
		return;

		//One undo step for all that changed, as the other item editors do
	QUndoCommand *undo = nullptr;
	const auto parent = [&]() {
		if (!undo)
			undo = new QUndoCommand(m_item->kind() == RevisionCloudItem::Review
									? tr("Modify a review note")
									: tr("Modify a revision cloud"));
		return undo;
	};
	const auto change = [&](const char *property, const QVariant &old_value, const QVariant &new_value) {
		if (old_value == new_value) return;
		new QPropertyUndoCommand(m_item.data(), property, old_value, new_value, parent());
	};
	const auto kind = RevisionCloudItem::Kind(m_kind->currentData().toInt());
	change("kind", QVariant::fromValue(m_item->kind()), QVariant::fromValue(kind));
	change("revision", m_item->revision(), revisionText());
	change("comment", m_item->comment(), m_comment->toPlainText().trimmed());
	change("author", m_item->author(), m_author->text().trimmed());
	change("date", m_item->date(), m_date->text().trimmed());
	change("done", m_item->isDone(), m_done->isChecked());
	change("reply", m_item->reply(), m_reply->text().trimmed());
		//Only a size the user typed: a stored size the box cannot show is
		//not changed by another edit
	if (!qFuzzyCompare(m_label_size_shown, m_label_size->value())) {
		change("labelSize", m_item->labelSize(), m_label_size->value());
		m_label_size_shown = m_label_size->value();
	}
	if (!qFuzzyCompare(m_arc_size_shown, m_arc_size->value())) {
		change("arcSize", m_item->arcSize(), m_arc_size->value());
		m_arc_size_shown = m_arc_size->value();
	}
	QPen pen = m_item->pen();
	pen.setColor(m_color->color());
	pen.setStyle(Qt::PenStyle(m_style->currentData().toInt()));
	if (!qFuzzyCompare(m_width_shown + 1, m_width->value() + 1)) {
		pen.setWidthF(m_width->value());
		m_width_shown = m_width->value();
	}
	if (pen != m_item->pen())
		change("pen", QVariant::fromValue(m_item->pen()), QVariant::fromValue(pen));

	if (!undo) return;
	if (m_item->diagram())
		m_item->diagram()->undoStack().push(undo);
	else {
		undo->redo();
		delete undo;
	}
}

void RevisionCloudPropertiesWidget::reset()
{
	updateUi();
}

void RevisionCloudPropertiesWidget::updateUi()
{
	if (!m_item)
		return;
	m_updating = true;
	m_kind->setCurrentIndex(m_kind->findData(int(m_item->kind())));
	const int revision = m_revision->findData(m_item->revision());
	if (revision >= 0)
		m_revision->setCurrentIndex(revision);
	else {
		m_revision->setCurrentIndex(-1);
		m_revision->setEditText(m_item->revision());
	}
	if (m_comment->toPlainText() != m_item->comment())
		m_comment->setPlainText(m_item->comment());
	m_author->setText(m_item->author());
	m_date->setText(m_item->date());
	m_done->setChecked(m_item->isDone());
	m_reply->setText(m_item->reply());
	m_color->setColor(m_item->pen().color());
	m_width->setValue(m_item->pen().widthF());
	m_width_shown = m_width->value();
	const int style = m_style->findData(int(m_item->pen().style()));
	m_style->setCurrentIndex(style >= 0 ? style : 0);
	m_label_size->setValue(m_item->labelSize());
	m_label_size_shown = m_label_size->value();
	m_arc_size->setValue(m_item->arcSize());
	m_arc_size_shown = m_arc_size->value();
	showRowsOfKind();
	m_updating = false;
}

/**
	@brief RevisionCloudPropertiesWidget::eventFilter
	In the Selection properties panel, a comment is applied when it is
	left, as a line is.
*/
bool RevisionCloudPropertiesWidget::eventFilter(QObject *watched, QEvent *event)
{
	if (m_live_edit && watched == m_comment && event->type() == QEvent::FocusOut)
		apply();
	return PropertiesEditorWidget::eventFilter(watched, event);
}

/**
	@brief RevisionCloudPropertiesWidget::setLiveEdit
	In the Selection properties panel there is no Apply button: a value is
	applied when its field is left, or when it is picked.
	@param live_edit
	@return always true
*/
bool RevisionCloudPropertiesWidget::setLiveEdit(bool live_edit)
{
	if (live_edit == m_live_edit)
		return true;
	m_live_edit = live_edit;

	for (const auto &connection : std::as_const(m_edit_connections))
		disconnect(connection);
	m_edit_connections.clear();

	if (m_live_edit) {
		for (QLineEdit *edit : {m_author, m_date, m_reply})
			m_edit_connections << connect(edit, &QLineEdit::editingFinished,
										  this, &RevisionCloudPropertiesWidget::apply);
		m_edit_connections << connect(m_revision->lineEdit(), &QLineEdit::editingFinished,
									  this, &RevisionCloudPropertiesWidget::apply);
		m_edit_connections << connect(m_revision, qOverload<int>(&QComboBox::activated),
									  this, &RevisionCloudPropertiesWidget::apply);
		for (QComboBox *combo : {m_kind, m_style})
			m_edit_connections << connect(combo, qOverload<int>(&QComboBox::activated),
										  this, &RevisionCloudPropertiesWidget::apply);
		m_edit_connections << connect(m_done, &QCheckBox::clicked,
									  this, &RevisionCloudPropertiesWidget::apply);
		for (QDoubleSpinBox *spin : {m_width, m_label_size, m_arc_size})
			m_edit_connections << connect(spin, &QDoubleSpinBox::editingFinished,
										  this, &RevisionCloudPropertiesWidget::apply);
		m_edit_connections << connect(m_color, &KColorButton::changed,
									  this, &RevisionCloudPropertiesWidget::apply);
	}
	return true;
}
