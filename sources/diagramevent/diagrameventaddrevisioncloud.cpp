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
#include "diagrameventaddrevisioncloud.h"

#include "../diagram.h"
#include "../projectlock.h"
#include "../properties/revisionlist.h"
#include "../qetapp.h"
#include "../qetdiagrameditor.h"
#include "../qetproject.h"
#include "../undocommand/addgraphicsobjectcommand.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

DiagramEventAddRevisionCloud::DiagramEventAddRevisionCloud(Diagram *diagram, RevisionCloudItem::Kind kind) :
	DiagramEventAddShape(diagram, QetShapeItem::Rectangle),
	m_kind(kind)
{
		//After the hint of the shapes, shown the same way (see
		//DiagramEventAddShape::DiagramEventAddShape())
	QTimer::singleShot(0, this, [this]() { showHint(); });
}

QetShapeItem *DiagramEventAddRevisionCloud::createShapeItem(QPointF p1, QPointF p2, QetShapeItem::ShapeType shape_type)
{
	auto cloud = new RevisionCloudItem(p1, p2, shape_type, m_kind);
	styleCloud(cloud);
		//Given when drawn only, never when a file is read
	if (m_kind == RevisionCloudItem::Revision) {
		if (m_diagram && m_diagram->project())
			cloud->setRevision(m_diagram->project()->revisions().latest().revision);
	} else {
		const QString user = ProjectLock::thisUser();
		if (user != QLatin1String("unknown"))
			cloud->setAuthor(user);
		cloud->setDate(RevisionCloudItem::today());
	}
	return cloud;
}

/**
	@brief DiagramEventAddRevisionCloud::styleCloud
	The colour set for its kind, a plain line, no filling: not the style
	last given to an ordinary shape, which DiagramEventAddShape applies
	once the shape is created.
*/
void DiagramEventAddRevisionCloud::styleCloud(QetShapeItem *shape) const
{
	QPen pen;
	pen.setColor(RevisionCloudItem::newCloudColor(m_kind));
	shape->setPen(pen);
	shape->setBrush(Qt::NoBrush);
}

/**
	@brief DiagramEventAddRevisionCloud::keepShape
	A review note asks for its comment at once: cancelled, no note.
*/
bool DiagramEventAddRevisionCloud::keepShape(QetShapeItem *shape)
{
	auto cloud = qobject_cast<RevisionCloudItem *>(shape);
	if (!cloud || cloud->kind() != RevisionCloudItem::Review)
		return true;
	const auto views = m_diagram->views();
	return askComment(cloud, views.isEmpty() ? nullptr : views.first());
}

bool DiagramEventAddRevisionCloud::askComment(RevisionCloudItem *note, QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("askReviewNote"));
	dialog.setWindowTitle(tr("New review note"));
	auto layout = new QVBoxLayout(&dialog);
	layout->addWidget(new QLabel(tr("Comment"), &dialog));
	auto comment = new QPlainTextEdit(note->comment(), &dialog);
	comment->setObjectName(QStringLiteral("comment"));
	comment->setTabChangesFocus(true);
	layout->addWidget(comment);
	auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	comment->setFocus();

	if (dialog.exec() != QDialog::Accepted)
		return false;
	note->setComment(comment->toPlainText().trimmed());
	return true;
}

void DiagramEventAddRevisionCloud::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	const bool creating = !m_shape_item && event->button() == Qt::LeftButton;
	DiagramEventAddShape::mousePressEvent(event);
		//DiagramEventAddShape gives a new shape the style last used
	if (creating && m_shape_item)
		styleCloud(m_shape_item);
	showHint();
}

void DiagramEventAddRevisionCloud::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	DiagramEventAddShape::mouseMoveEvent(event);
	showHint();
}

void DiagramEventAddRevisionCloud::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	const bool ends = !m_shape_item && event->button() == Qt::RightButton;
	DiagramEventAddShape::mouseReleaseEvent(event);
		//A right click with nothing drawn ends the tool
	if (!ends)
		showHint();
}

/**
	@brief DiagramEventAddRevisionCloud::mouseDoubleClickEvent
	A double click closes a free outline: a cloud always is closed, with
	three points at least. With fewer, nothing is drawn.
*/
void DiagramEventAddRevisionCloud::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
	if (!(m_shape_item && m_shape_type == QetShapeItem::Polygon && event->button() == Qt::LeftButton))
	{
		DiagramEventAddShape::mouseDoubleClickEvent(event);
		return;
	}
	event->setAccepted(true);
		//The second click of a double click added a point under the first
	m_shape_item->removePoints();
		//Ended on the first point: that point once
	const QPolygonF polygon = m_shape_item->polygon();
	if (polygon.size() > 3 && polygon.first() == polygon.last())
		m_shape_item->removePoints();

	QetShapeItem *shape = m_shape_item;
	m_shape_item = nullptr;
	if (shape->pointsCount() >= 3) {
		shape->setClosed(true);
		shapeDrawn(shape);
		if (keepShape(shape)) {
			m_diagram->undoStack().push(new AddGraphicsObjectCommand(shape, m_diagram));
			shape = nullptr;
		}
	}
	if (shape) {
		m_diagram->removeItem(shape);
		delete shape;
	}
	showHint();
}

/**
	@brief DiagramEventAddRevisionCloud::keyPressEvent
	Space, before the first click: a rectangle or a free outline. Never a
	rotation of the selection while the tool runs (handlesShortcutOverride()).
*/
void DiagramEventAddRevisionCloud::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier)
	{
		if (!m_shape_item) {
			m_shape_type = drawsFreeOutline() ? QetShapeItem::Rectangle : QetShapeItem::Polygon;
			showHint();
		}
		event->setAccepted(true);
		return;
	}
	DiagramEventAddShape::keyPressEvent(event);
}

bool DiagramEventAddRevisionCloud::handlesShortcutOverride(QKeyEvent *event) const
{
	return event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier;
}

QString DiagramEventAddRevisionCloud::hint() const
{
	const QString what = m_kind == RevisionCloudItem::Review ? tr("Review note")
															 : tr("Revision cloud");
	if (drawsFreeOutline())
		return m_shape_item
				? tr("%1, free outline: left click: next point; double click: close the cloud; "
					 "right click: undo the last point").arg(what)
				: tr("%1, free outline: left click: first point (Ctrl = free position); "
					 "Space: rectangle").arg(what);
	return m_shape_item
			? tr("%1, rectangle: left click: opposite corner (Ctrl = from center); "
				 "right click: cancel").arg(what)
			: tr("%1, rectangle: left click: first corner (Ctrl = free position); "
				 "Space: free outline").arg(what);
}

void DiagramEventAddRevisionCloud::showHint() const
{
	if (!m_diagram || m_diagram->views().isEmpty())
		return;
	if (auto *editor = QETApp::diagramEditorAncestorOf(m_diagram->views().constFirst()))
		editor->statusBar()->showMessage(hint());
}
