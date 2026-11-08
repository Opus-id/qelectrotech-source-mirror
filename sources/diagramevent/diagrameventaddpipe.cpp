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
#include "diagrameventaddpipe.h"

#include "../diagram.h"
#include "../qetapp.h"
#include "../qetdiagrameditor.h"
#include "../qetgraphicsitem/element.h"
#include "../qetgraphicsitem/terminal.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLineF>
#include <QPen>
#include <QStatusBar>
#include <QTimer>
#include <QUndoStack>

namespace {
		///Radius of the mark of a free nozzle, in pixels of the screen
	const qreal mark_radius = 6.0;
}

/**
	@brief DiagramEventAddPipe::DiagramEventAddPipe
	@param diagram : the folio the pipes are drawn on
*/
DiagramEventAddPipe::DiagramEventAddPipe(Diagram *diagram) :
	DiagramEventInterface(diagram)
{
		//Done here as well as in init(): Diagram::setEventInterface() calls
		//init() only when it replaces another tool.
	init();
	if (m_diagram) {
			//Undone or redone pipes free or take nozzles
		connect(&m_diagram->undoStack(), &QUndoStack::indexChanged,
				this, &DiagramEventAddPipe::refreshMarks);
	}
		//The tool which ran before is destroyed after this constructor
		//returns, and clears the status bar: the hint waits for it.
	QTimer::singleShot(0, this, [this]() { showHint(); });
}

/**
	@brief DiagramEventAddPipe::~DiagramEventAddPipe
*/
DiagramEventAddPipe::~DiagramEventAddPipe()
{
	hover(nullptr, QColor());
	clearMarks();
	if (m_diagram)
	{
		if (m_start) {
			m_diagram->setConductor(false);
		}
		for (QGraphicsView *view : m_diagram->views()) {
			view->setContextMenuPolicy(Qt::DefaultContextMenu);
		}
	}
	clearHint();
}

/**
	@brief DiagramEventAddPipe::init
	While the tool runs, the right button belongs to it alone.
*/
void DiagramEventAddPipe::init()
{
	m_running = true;
	if (m_diagram)
	{
		for (QGraphicsView *view : m_diagram->views()) {
			view->setContextMenuPolicy(Qt::NoContextMenu);
		}
	}
	refreshMarks();
}

/**
	@brief DiagramEventAddPipe::isFreeNozzle
	@param terminal
	@return true for a nozzle of a symbol which may still take a pipe
*/
bool DiagramEventAddPipe::isFreeNozzle(const Terminal *terminal)
{
	return terminal
			&& terminal->parentElement()
			&& terminal->medium() == TerminalData::Pipe
			&& terminal->hasRoomForWire();
}

/**
	@brief DiagramEventAddPipe::freeNozzleNear
	@param diagram : the folio
	@param scene_pos : where the mouse is, on the folio
	@param radius : how near, in units of the folio
	@return the free nozzle nearest to @p scene_pos within @p radius
*/
Terminal *DiagramEventAddPipe::freeNozzleNear(Diagram *diagram, const QPointF &scene_pos,
											  qreal radius)
{
	if (!diagram || radius <= 0) return nullptr;

	const QRectF area(scene_pos.x() - radius, scene_pos.y() - radius,
					  2 * radius, 2 * radius);
	Terminal *nearest = nullptr;
	qreal nearest_distance = 0;
	for (QGraphicsItem *item : diagram->items(area))
	{
		Terminal *terminal = qgraphicsitem_cast<Terminal *>(item);
		if (!isFreeNozzle(terminal)) continue;
		const qreal distance = QLineF(scene_pos, terminal->dockConductor()).length();
		if (distance > radius) continue;
		if (!nearest || distance < nearest_distance) {
			nearest = terminal;
			nearest_distance = distance;
		}
	}
	return nearest;
}

/**
	@brief DiagramEventAddPipe::mousePressEvent
	@param event
*/
void DiagramEventAddPipe::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	if (Q_UNLIKELY(!m_diagram || m_diagram->isReadOnly())) return;

		//Nothing is picked nor moved while the tool runs, whatever the button
	event->setAccepted(true);

	if (event->button() == Qt::RightButton)
	{
			//First right click takes the pipe being drawn back, a second one
			//leaves the tool. finish() destroys this tool: nothing after it.
		if (m_start) {
			reset();
		} else {
			m_running = false;
			emit finish();
		}
		return;
	}
	if (event->button() != Qt::LeftButton) return;

	const qreal radius = Terminal::magnetSceneRadius(event);

	if (!m_start)
	{
		Terminal *start = freeNozzleNear(m_diagram, event->scenePos(), radius);
		if (!start) {
			showHint(tr("No free nozzle here: click nearer to a nozzle.",
						"pipe drawing tool"));
			return;
		}
		m_start = start;
		m_diagram->setConductorStart(start->dockConductor());
		m_diagram->setConductorStop(event->scenePos());
		m_diagram->setConductor(true);
		hover(nullptr, QColor());
		showHint();
		return;
	}

	if (Terminal *target = m_start->magnetTarget(event->scenePos(), radius)) {
		finishAt(target);
	} else if (freeNozzleNear(m_diagram, event->scenePos(), radius) != m_start) {
		showHint(tr("No free nozzle of another symbol here: the pipe waits for "
					"its end. Right click or Esc: cancel.", "pipe drawing tool"));
	}
}

/**
	@brief DiagramEventAddPipe::mouseMoveEvent
	Before the first click, the nozzle a click would start from is lit; then
	the pipe follows the mouse and jumps to the nozzle that would catch it.
	@param event
*/
void DiagramEventAddPipe::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	if (!m_diagram) return;
	event->setAccepted(true);
	const qreal radius = Terminal::magnetSceneRadius(event);

	if (!m_start) {
		hover(freeNozzleNear(m_diagram, event->scenePos(), radius), Terminal::allowedColor);
		return;
	}

	Terminal *target = m_start->magnetTarget(event->scenePos(), radius);
	m_diagram->setConductorStop(target ? target->dockConductor() : event->scenePos());
	hover(target, Terminal::allowedColor);
}

/**
	@brief DiagramEventAddPipe::mouseReleaseEvent
	The release of a drag near another free nozzle ends the pipe there;
	anywhere else the pipe waits for a second click.
	@param event
*/
void DiagramEventAddPipe::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	if (!m_diagram) return;
	event->setAccepted(true);
	if (event->button() != Qt::LeftButton || !m_start) return;

	if (Terminal *target = m_start->magnetTarget(event->scenePos(),
												 Terminal::magnetSceneRadius(event))) {
		finishAt(target);
	}
}

/**
	@brief DiagramEventAddPipe::keyPressEvent
	Esc takes back the pipe being drawn, a second Esc leaves the tool.
	@param event
*/
void DiagramEventAddPipe::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape && m_start)
	{
		reset();
		event->setAccepted(true);
		return;
	}
	DiagramEventInterface::keyPressEvent(event);
}

/**
	@brief DiagramEventAddPipe::finishAt
	The pipe from the start nozzle to @p target, in one undo step; the tool
	then waits for the next one.
	@param target
*/
void DiagramEventAddPipe::finishAt(Terminal *target)
{
	Terminal *start = m_start;
	reset();
	if (start && target) {
		start->connectTo(target);
	}
	refreshMarks();
}

/**
	@brief DiagramEventAddPipe::reset
	Back to before the first click: no pipe being drawn.
*/
void DiagramEventAddPipe::reset()
{
	hover(nullptr, QColor());
	if (m_diagram && m_start) {
		m_diagram->setConductor(false);
	}
	m_start.clear();
	showHint();
}

/**
	@brief DiagramEventAddPipe::hover
	Light @p terminal alone, nullptr for none.
*/
void DiagramEventAddPipe::hover(Terminal *terminal, const QColor &color)
{
	if (m_hovered && m_hovered != terminal) {
		m_hovered->showHover(false);
	}
	m_hovered = terminal;
	if (terminal) {
		terminal->showHover(true, color);
	}
}

/**
	@brief DiagramEventAddPipe::clearMarks
*/
void DiagramEventAddPipe::clearMarks()
{
	for (QGraphicsEllipseItem *mark : std::as_const(m_marks))
	{
		if (m_diagram && mark->scene() == m_diagram) {
			m_diagram->removeItem(mark);
		}
		delete mark;
	}
	m_marks.clear();
}

/**
	@brief DiagramEventAddPipe::refreshMarks
	A ring, the same size on screen at any zoom, around every free nozzle.
*/
void DiagramEventAddPipe::refreshMarks()
{
	clearMarks();
	if (!m_diagram) return;

	QPen pen(Terminal::stemColor(TerminalData::Pipe));
	pen.setCosmetic(true);
	pen.setWidthF(1.5);

	const QList<QGraphicsItem *> items = m_diagram->items();
	for (QGraphicsItem *item : items)
	{
		Terminal *terminal = qgraphicsitem_cast<Terminal *>(item);
		if (!isFreeNozzle(terminal)) continue;

		auto *mark = new QGraphicsEllipseItem(-mark_radius, -mark_radius,
											  2 * mark_radius, 2 * mark_radius);
		mark->setFlag(QGraphicsItem::ItemIgnoresTransformations);
		mark->setPen(pen);
		mark->setBrush(Qt::NoBrush);
		mark->setAcceptedMouseButtons(Qt::NoButton);
		mark->setZValue(1000.0);
		mark->setPos(terminal->dockConductor());
		m_diagram->addItem(mark);
		m_marks << mark;
	}
}

/**
	@brief DiagramEventAddPipe::showHint
	@param message : shown instead of the usual hint when not empty
*/
void DiagramEventAddPipe::showHint(const QString &message) const
{
	if (!m_diagram || m_diagram->views().isEmpty()) return;
	auto *editor = QETApp::diagramEditorAncestorOf(m_diagram->views().constFirst());
	if (!editor) return;

	if (!message.isEmpty()) {
		editor->statusBar()->showMessage(message);
	} else if (m_start) {
		editor->statusBar()->showMessage(tr(
			"Click or release near another free nozzle: end the pipe; "
			"right click or Esc: cancel", "pipe drawing tool"));
	} else {
		editor->statusBar()->showMessage(tr(
			"Click near a free nozzle: start a pipe; "
			"right click or Esc: quit the tool", "pipe drawing tool"));
	}
}

/**
	@brief DiagramEventAddPipe::clearHint
*/
void DiagramEventAddPipe::clearHint() const
{
	if (!m_diagram || m_diagram->views().isEmpty()) return;
	if (auto *editor = QETApp::diagramEditorAncestorOf(m_diagram->views().constFirst())) {
		editor->statusBar()->clearMessage();
	}
}
