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

#include "../conductorautonumerotation.h"
#include "../diagram.h"
#include "../factory/elementfactory.h"
#include "../pidcodification.h"
#include "../pipeconnection.h"
#include "../qetapp.h"
#include "../qetdiagrameditor.h"
#include "../qetgraphicsitem/conductor.h"
#include "../qetgraphicsitem/element.h"
#include "../qetgraphicsitem/terminal.h"
#include "../qetproject.h"
#include "../undocommand/addgraphicsobjectcommand.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLineF>
#include <QPen>
#include <QStatusBar>
#include <QTimer>
#include <QToolTip>
#include <QUndoStack>
#include <QtMath>

namespace {
		///Radius of the mark of a free nozzle, in pixels of the screen
	const qreal mark_radius = 6.0;

		///Ctrl held: the pipe goes where the mouse is, on the grid, without
		///snapping to the axis of its start (as the grid of the other tools)
	bool snaps(const QGraphicsSceneMouseEvent *event)
	{
		return !(event->modifiers() & Qt::ControlModifier);
	}
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
	delete m_tee_model;
	m_tee_model = nullptr;
	if (m_diagram)
	{
		if (hasStart()) {
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
	@brief DiagramEventAddPipe::nearestFreeNozzle
	@param items : the symbols looked at (other items are skipped)
	@param scene_pos : where the user pointed, on the folio
	@return the free nozzle of these symbols nearest to @p scene_pos
*/
Terminal *DiagramEventAddPipe::nearestFreeNozzle(const QList<QGraphicsItem *> &items,
												 const QPointF &scene_pos)
{
	Terminal *nearest = nullptr;
	qreal nearest_distance = 0;
	for (QGraphicsItem *item : items)
	{
		const Element *element = qgraphicsitem_cast<Element *>(item);
		if (!element) continue;
		const QList<Terminal *> terminals = element->terminals();
		for (Terminal *terminal : terminals)
		{
			if (!isFreeNozzle(terminal)) continue;
			const qreal distance = QLineF(scene_pos, terminal->dockConductor()).length();
			if (!nearest || distance < nearest_distance) {
				nearest = terminal;
				nearest_distance = distance;
			}
		}
	}
	return nearest;
}

/**
	@brief DiagramEventAddPipe::startFrom
	@param nozzle : where the pipe starts
*/
void DiagramEventAddPipe::startFrom(Terminal *nozzle)
{
	if (!m_diagram || m_diagram->isReadOnly() || !isFreeNozzle(nozzle)
			|| nozzle->diagram() != m_diagram) {
		return;
	}
	m_start = nozzle;
	m_start_free = false;
	m_diagram->setConductorStart(nozzle->dockConductor());
	m_diagram->setConductorStop(nozzle->dockConductor());
	m_diagram->setConductor(true);
	hover(nullptr, QColor());
	showHint();
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
		if (hasStart()) {
			reset();
		} else {
			m_running = false;
			emit finish();
		}
		return;
	}
	if (event->button() != Qt::LeftButton) return;

	const qreal radius = Terminal::magnetSceneRadius(event);
	const QPointF pos = event->scenePos();
	const bool snap = snaps(event);
		//Where the preview is drawn again for Space, before any move
	m_mouse_pos = pos;
	m_mouse_snap = snap;
	m_mouse_radius = radius;
		//A click in an empty place starts or ends the pipe on a free end
		//only when the project has a symbol for it
	const bool free_end = hasFreeEndSymbol();
		//A click on a pipe starts or ends the pipe on a tee there, when the
		//project has a symbol of tee; a click on a wire or a signal line
		//says why not
	const PidTee::Spot spot = PidTee::isSet(m_diagram) ? PidTee::spotAt(m_diagram, pos)
													   : PidTee::Spot();

	if (!hasStart())
	{
		if (Terminal *start = freeNozzleNear(m_diagram, pos, radius)) {
			startFrom(start);
			m_diagram->setConductorStop(pos);
		} else if (spot.isValid()) {
			startOnPipe(spot);
		} else if (spot.pipe) {
			refuse(event, spot.refusal);
		} else if (!free_end) {
			showHint(tr("No free nozzle here: click nearer to a nozzle.",
						"pipe drawing tool"));
		} else if (isEmptyPlace(pos)) {
			startAtPoint(pos);
		} else {
			showHint(tr("No free nozzle here: click nearer to a nozzle, or in an empty place.",
						"pipe drawing tool"));
		}
		return;
	}

	Terminal *near = freeNozzleNear(m_diagram, pos, radius);
	Terminal *target = m_start ? m_start->magnetTarget(pos, radius) : near;
	if (target) {
		finishAt(target, snap);
	} else if (m_start && near == m_start) {
			//On the nozzle it starts from: nothing
	} else if (spot.pipe && !spot.isValid()) {
		refuse(event, spot.refusal);
	} else if (spot.pipe) {
			//Two tees on one pipe keep their distance
		const PidTee::Spot end_spot = spotFacingStart(spot, snap);
		const QString clash = m_start_tee ? PidTee::clash(m_start_spot, end_spot, teeArm()) : QString();
		if (clash.isEmpty()) {
			finishOnPipe(end_spot);
		} else {
			refuse(event, clash);
		}
	} else if (!free_end) {
		showHint(tr("No free nozzle of another symbol here: the pipe waits for "
					"its end. Right click or Esc: cancel.", "pipe drawing tool"));
	} else if (isEmptyPlace(pos) && isEmptyPlace(endPoint(pos, snap))) {
		finishAtPoint(endPoint(pos, snap));
	} else {
		showHint(tr("No free nozzle of another symbol here, nor an empty place: the pipe "
					"waits for its end. Right click or Esc: cancel.", "pipe drawing tool"));
	}
}

/**
	@brief DiagramEventAddPipe::isEmptyPlace
	@param scene_pos
	@return true when no symbol, no pipe nor wire, lies at @p scene_pos: a
	free end may be placed there
*/
bool DiagramEventAddPipe::isEmptyPlace(const QPointF &scene_pos) const
{
	if (!m_diagram) return false;
	const QList<QGraphicsItem *> items = m_diagram->items(scene_pos);
	for (QGraphicsItem *item : items)
	{
		const QGraphicsItem *top = item->topLevelItem();
		if (qgraphicsitem_cast<const Element *>(top) || qgraphicsitem_cast<const Conductor *>(top)) {
			return false;
		}
	}
	return true;
}

/**
	@brief DiagramEventAddPipe::freeEndSymbol
	See the declaration
*/
ElementsLocation DiagramEventAddPipe::freeEndSymbol(const Diagram *diagram)
{
	QETProject *project = diagram ? diagram->project() : nullptr;
	if (!project) return ElementsLocation();
	const QString path = project->pidCodification().toolSymbol(PidCodification::freeEndRole());
	if (path.isEmpty()) return ElementsLocation();
		//A project given turns any path into one of its own collection
	const ElementsLocation location = path.startsWith(QLatin1String("embed://"))
			? ElementsLocation(path, project) : ElementsLocation(path);
	return location.isElement() && location.exist() ? location : ElementsLocation();
}

/**
	@brief DiagramEventAddPipe::freeEndFacing
	See the declaration
*/
Qet::Orientation DiagramEventAddPipe::freeEndFacing(const QPointF &from, Qet::Orientation from_facing,
													const QPointF &end, bool s_shape)
{
	const QPointF d = end - from;
	const bool horizontal = Qet::isHorizontal(from_facing);
		//How far in front of the way out, and how far aside
	const qreal ahead = horizontal ? (from_facing == Qet::East ? d.x() : -d.x())
								   : (from_facing == Qet::South ? d.y() : -d.y());
	const qreal aside = horizontal ? d.y() : d.x();
	constexpr qreal tolerance = 0.5;
	const auto opposite = [](Qet::Orientation o) { return Qet::Orientation((o + 2) % 4); };

	if (qAbs(aside) < tolerance) {
			//Straight on; right behind, round from below or from the right
		if (ahead > 0) return opposite(from_facing);
		return horizontal ? Qet::South : Qet::East;
	}
	if (ahead > tolerance) {
			//Two bends: the end faces back toward the start, the run across
			//halfway (Conductor::defaultRoute())
		if (s_shape && ahead >= sShapeRoom()) return opposite(from_facing);
			//One bend: the end faces back to the axis of the way out
		if (horizontal) return aside > 0 ? Qet::North : Qet::South;
		return aside > 0 ? Qet::West : Qet::East;
	}
		//Beside or behind: out, aside, then back to the end
	return from_facing;
}

/**
	@brief DiagramEventAddPipe::sShapeRoom
	See the declaration
*/
qreal DiagramEventAddPipe::sShapeRoom()
{
	return 2 * PipeConnection::pipeExit;
}

/**
	@brief DiagramEventAddPipe::facingOfFreeEnds
	See the declaration
*/
void DiagramEventAddPipe::facingOfFreeEnds(PipeEnd &start, PipeEnd &end, bool s_shape)
{
	if (!start.free && end.free) {
		end.facing = freeEndFacing(start.point, start.facing, end.point, s_shape);
	} else if (start.free && !end.free) {
		start.facing = freeEndFacing(end.point, end.facing, start.point, s_shape);
	} else if (start.free && end.free) {
		start.facing = dominantFacing(start.point, end.point);
		end.facing = freeEndFacing(start.point, start.facing, end.point, s_shape);
	}
}

/**
	@brief DiagramEventAddPipe::dominantFacing
	See the declaration
*/
Qet::Orientation DiagramEventAddPipe::dominantFacing(const QPointF &from, const QPointF &to)
{
	const QPointF d = to - from;
	if (qAbs(d.x()) >= qAbs(d.y())) {
		return d.x() >= 0 ? Qet::East : Qet::West;
	}
	return d.y() > 0 ? Qet::South : Qet::North;
}

/**
	@brief DiagramEventAddPipe::nearAxis
	See the declaration
*/
bool DiagramEventAddPipe::nearAxis(const QPointF &from, Qet::Orientation facing, const QPointF &to)
{
	static const qreal slope = qTan(qDegreesToRadians(axisAngle));
	const QPointF d = to - from;
	qreal ahead = 0, aside = 0;
	switch (facing) {
		case Qet::East:  ahead =  d.x(); aside = d.y(); break;
		case Qet::West:  ahead = -d.x(); aside = d.y(); break;
		case Qet::South: ahead =  d.y(); aside = d.x(); break;
		case Qet::North: ahead = -d.y(); aside = d.x(); break;
	}
	return ahead > 0 && qAbs(aside) <= ahead * slope;
}

/**
	@brief DiagramEventAddPipe::endPoint
	See the declaration
*/
QPointF DiagramEventAddPipe::endPoint(const QPointF &scene_pos, bool snap) const
{
	const QPointF grid = Diagram::snapToGrid(scene_pos);
	if (!snap || !hasStart()) return grid;

	const QPointF from = startScenePoint();
		//A nozzle or a tee: its way out, in front; an empty place: both
		//axes, both ways
	QList<Qet::Orientation> ways;
	if (m_start) {
		ways << m_start->orientation();
	} else if (m_start_tee) {
		ways << PidTee::branchFacing(m_start_spot, scene_pos);
	} else {
		ways << Qet::North << Qet::East << Qet::South << Qet::West;
	}
	for (const Qet::Orientation way : std::as_const(ways))
	{
		if (nearAxis(from, way, scene_pos)) {
				//Exactly on the axis, on the grid along it
			return Qet::isHorizontal(way) ? QPointF(grid.x(), from.y())
										  : QPointF(from.x(), grid.y());
		}
	}
	return grid;
}

/**
	@brief DiagramEventAddPipe::startPointFor
	See the declaration
*/
QPointF DiagramEventAddPipe::startPointFor(const Terminal *target, bool snap) const
{
	if (!m_start_free || !target || !snap) return m_start_point;
	const QPointF dock = target->dockConductor();
	const Qet::Orientation way = target->orientation();
	if (!nearAxis(dock, way, m_start_point)) return m_start_point;
	const QPointF aligned = Qet::isHorizontal(way) ? QPointF(m_start_point.x(), dock.y())
												   : QPointF(dock.x(), m_start_point.y());
		//Nothing is placed yet: the start may move, onto an empty place only
	return isEmptyPlace(aligned) ? aligned : m_start_point;
}

/**
	@brief DiagramEventAddPipe::spotFacingStart
	@return @p spot, or the spot of its pipe right in front of the start of
	the pipe being drawn, when the start lies nearly in front of @p spot
	(within axisAngle) and, for a nozzle or a tee, leaves toward it; not at
	a bend, nor when @p snap is false (Ctrl)
*/
PidTee::Spot DiagramEventAddPipe::spotFacingStart(const PidTee::Spot &spot, bool snap)
{
	if (!snap || !hasStart() || !spot.isValid() || spot.corner) return spot;

	const QPointF from = startScenePoint();
	const bool along_x = Qet::isHorizontal(spot.forward);
		//The way from the spot to the start, across the pipe
	const Qet::Orientation to_start = along_x
			? (from.y() < spot.point.y() ? Qet::North : Qet::South)
			: (from.x() < spot.point.x() ? Qet::West : Qet::East);
	if (!nearAxis(spot.point, to_start, from)) return spot;

		//A nozzle or a tee at the start must leave toward the spot
	const auto toward_spot = Qet::Orientation((int(to_start) + 2) % 4);
	if (m_start && m_start->orientation() != toward_spot) return spot;
	if (m_start_tee && PidTee::branchFacing(m_start_spot, spot.point) != toward_spot) return spot;

	const QPointF facing = along_x ? QPointF(from.x(), spot.point.y())
								   : QPointF(spot.point.x(), from.y());
	const PidTee::Spot moved = PidTee::spotOn(spot.pipe, facing, teeArm());
		//Kept only right in front, on the same straight part
	if (!moved.isValid() || moved.corner || QLineF(moved.point, facing).length() > 0.5) {
		return spot;
	}
	return moved;
}

/**
	@brief DiagramEventAddPipe::hasFreeEndSymbol
	@return true when the project has a symbol of free end set: a click in
	an empty place may start or end a pipe
*/
bool DiagramEventAddPipe::hasFreeEndSymbol() const
{
	return m_diagram && m_diagram->project()
			&& !m_diagram->project()->pidCodification().toolSymbol(PidCodification::freeEndRole()).isEmpty();
}

/**
	@brief DiagramEventAddPipe::startAtPoint
	Start the pipe in an empty place, at the point of the grid nearest to
	@p scene_pos: nothing is placed until it ends
*/
void DiagramEventAddPipe::startAtPoint(const QPointF &scene_pos)
{
	if (!freeEndSymbol(m_diagram).isElement()) {
		showHint(tr("The symbol of free end of the project is not found: choose another one "
					"(properties of the project, P&ID, Connections).", "pipe drawing tool"));
		return;
	}
	m_start.clear();
	m_start_free = true;
	m_start_point = Diagram::snapToGrid(scene_pos);
	m_diagram->setConductorStart(m_start_point);
	m_diagram->setConductorStop(scene_pos);
	m_diagram->setConductor(true);
	hover(nullptr, QColor());
	showHint();
}

/**
	@brief DiagramEventAddPipe::finishAtPoint
	End the pipe in an empty place, at @p end, as endPoint() gives it
*/
void DiagramEventAddPipe::finishAtPoint(const QPointF &end)
{
	if (QLineF(startScenePoint(), end).length() < 0.5) {
		showHint(tr("Too short: the pipe would end where it starts. Right click or Esc: cancel.",
					"pipe drawing tool"));
		return;
	}
	if (m_start_tee && !m_start_spot.pipe) {
		reset();
		return;
	}
	Terminal *start = m_start;
	const QPointF start_point = m_start_point;
	const PidTee::Spot start_spot = m_start_tee ? m_start_spot : PidTee::Spot();
	reset();
	drawPipe(start, start_point, start_spot, nullptr, end, PidTee::Spot());
	refreshMarks();
}

/**
	@brief DiagramEventAddPipe::startOnPipe
	Start the pipe on a pipe, at @p spot: the tee is placed at the end
*/
void DiagramEventAddPipe::startOnPipe(const PidTee::Spot &spot)
{
	if (!spot.isValid()) return;
	m_start.clear();
	m_start_free = false;
	m_start_tee = true;
	m_start_spot = spot;
	m_diagram->setConductorStart(spot.point);
	m_diagram->setConductorStop(spot.point);
	m_diagram->setConductor(true);
	hover(nullptr, QColor());
	showHint();
}

/**
	@brief DiagramEventAddPipe::finishOnPipe
	End the pipe on a pipe, at @p spot, on a tee
*/
void DiagramEventAddPipe::finishOnPipe(const PidTee::Spot &spot)
{
	if (!spot.isValid() || (m_start_tee && !m_start_spot.pipe)) {
		reset();
		return;
	}
	Terminal *start = m_start;
	const QPointF start_point = m_start_point;
	const PidTee::Spot start_spot = m_start_tee ? m_start_spot : PidTee::Spot();
	reset();
	drawPipe(start, start_point, start_spot, nullptr, QPointF(), spot);
	refreshMarks();
}

/**
	@brief DiagramEventAddPipe::startScenePoint
	@return where the pipe being drawn starts, on the folio
*/
QPointF DiagramEventAddPipe::startScenePoint() const
{
	if (m_start) return m_start->dockConductor();
	if (m_start_tee) return m_start_spot.point;
	return m_start_point;
}

/**
	@brief DiagramEventAddPipe::teeArm
	@return the arms of the tee of the project, measured once per pipe
*/
qreal DiagramEventAddPipe::teeArm()
{
	if (m_tee_arm < 0) {
		m_tee_arm = PidTee::armOf(m_diagram);
	}
	return m_tee_arm;
}

/**
	@brief DiagramEventAddPipe::refuse
	Say @p why in a tooltip at the mouse, and in the status bar
*/
void DiagramEventAddPipe::refuse(const QGraphicsSceneMouseEvent *event, const QString &why) const
{
	if (why.isEmpty()) return;
	QToolTip::showText(event->screenPos(), why);
	showHint(why);
}

/**
	@brief DiagramEventAddPipe::drawPipe
	The pipe from @p start, or else from a tee at @p start_spot when it has
	a pipe, or else from a free end at @p start_point, to @p target, or else
	to a tee at @p end_spot, or else to a free end at @p end_point. A tee
	turns its way out toward the other end; the tees placed, the pipes they
	cut, the free ends placed and the pipe are one undo step. Nothing when
	a symbol of the project cannot be placed.
*/
void DiagramEventAddPipe::drawPipe(Terminal *start, const QPointF &start_point,
								   const PidTee::Spot &start_spot, Terminal *target,
								   const QPointF &end_point, const PidTee::Spot &end_spot)
{
	if (!m_diagram || !m_diagram->project()) return;
	const bool start_tee = !start && start_spot.pipe;
	const bool end_tee = !target && end_spot.pipe;
	if (start && target) {
		start->connectTo(target);
		return;
	}

		//The tees first: each faces the other end with its way out
	QList<Element *> tees;
	auto teeOn = [&](const PidTee::Spot &spot, const QPointF &toward) -> Terminal * {
		QString error;
		Element *tee = PidTee::build(m_diagram, &error);
		if (!tee) {
			showHint(error);
			return nullptr;
		}
		tees << tee;
		Terminal *out = PidTee::fit(tee, spot, PidTee::branchFacing(spot, toward));
		if (!out) {
			showHint(PidTee::notATee());
		}
		return out;
	};
	const QPointF start_at = start ? start->dockConductor()
								   : (start_tee ? start_spot.point : start_point);
	const QPointF end_at = target ? target->dockConductor()
								  : (end_tee ? end_spot.point : end_point);
	Terminal *from = start, *to = target;
	if (start_tee && !(from = teeOn(start_spot, end_at))) {
		qDeleteAll(tees);
		return;
	}
	if (end_tee && !(to = teeOn(end_spot, from ? from->dockConductor() : start_at))) {
		qDeleteAll(tees);
		return;
	}

		//Which way the nozzle of each free end faces, as the preview shows
	PipeEnd start_end{from ? from->dockConductor() : start_point,
					  from ? from->orientation() : Qet::East, !from};
	PipeEnd end_end{to ? to->dockConductor() : end_point,
					to ? to->orientation() : Qet::West, !to};
	facingOfFreeEnds(start_end, end_end, m_s_shape);
	const Qet::Orientation start_facing = start_end.facing;
	const Qet::Orientation end_facing = end_end.facing;

	ElementsLocation imported;
	if (!from || !to)
	{
		const ElementsLocation symbol = freeEndSymbol(m_diagram);
		if (!symbol.isElement()) {
			qDeleteAll(tees);
			showHint(tr("The symbol of free end of the project is not found: choose another one "
						"(properties of the project, P&ID, Connections).", "pipe drawing tool"));
			return;
		}
		imported = symbol;
		if (!(symbol.isProject() && symbol.project() == m_diagram->project())) {
			imported = m_diagram->project()->importElement(imported);
		}
		if (!imported.exist()) {
			qDeleteAll(tees);
			return;
		}
	}

		//A free end turned so that its nozzle faces @p facing, its nozzle at
		//@p dock; nullptr when the symbol has no nozzle
	QList<Element *> built;
	auto freeEnd = [&](const QPointF &dock, Qet::Orientation facing) -> Terminal * {
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(imported, nullptr, &state);
		if (!element || state) {
			delete element;
			return nullptr;
		}
		Terminal *nozzle = nullptr;
		for (Terminal *t : element->terminals()) {
			if (t->medium() == TerminalData::Pipe) {
				nozzle = t;
				break;
			}
		}
		if (!nozzle) {
			delete element;
			return nullptr;
		}
		element->takeLabelSize(m_diagram->project()->pidCodification());
		const int turns = (int(facing) - int(nozzle->orientation()) + 4) % 4;
		element->setRotation(90.0 * turns);
		element->setPos(QPointF());
		element->setPos(dock - nozzle->dockConductor());
		built << element;
		return nozzle;
	};

	if (!from) {
		from = freeEnd(start_point, start_facing);
	}
	if (!to) {
		to = from ? freeEnd(end_point, end_facing) : nullptr;
	}
	if (!from || !to) {
		qDeleteAll(built);
		qDeleteAll(tees);
		showHint(tr("The symbol of free end of the project has no nozzle: choose another one "
					"(properties of the project, P&ID, Connections).", "pipe drawing tool"));
		return;
	}

		//On the folio before the pipe, which takes the size of its nozzles
		//there: the tees cutting their pipes (once for both on one pipe),
		//then the free ends
	auto *undo = new QUndoCommand();
	if (start_tee && end_tee && start_spot.pipe == end_spot.pipe) {
		PidTee::cut(m_diagram, start_spot.pipe, {start_spot, end_spot}, tees, undo);
	} else {
		int i = 0;
		if (start_tee) {
			PidTee::cut(m_diagram, start_spot.pipe, {start_spot}, {tees.at(i++)}, undo);
		}
		if (end_tee) {
			PidTee::cut(m_diagram, end_spot.pipe, {end_spot}, {tees.at(i++)}, undo);
		}
	}
	for (Element *element : std::as_const(built)) {
		m_diagram->addItem(element);
		new AddGraphicsObjectCommand(element, m_diagram, element->pos(), undo);
	}
	auto *pipe = new Conductor(from, to);
	auto *add_pipe = new AddGraphicsObjectCommand(pipe, m_diagram, QPointF(), undo);
	undo->setText(add_pipe->text());
	ConductorAutoNumerotation numbering(pipe, m_diagram, undo);
	numbering.numerate();
	m_diagram->undoStack().push(undo);
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

	if (!hasStart()) {
		hover(freeNozzleNear(m_diagram, event->scenePos(), radius), Terminal::allowedColor);
		return;
	}
	updatePreview(event->scenePos(), snaps(event), radius);
}

/**
	@brief DiagramEventAddPipe::updatePreview
	See the declaration
*/
void DiagramEventAddPipe::updatePreview(const QPointF &scene_pos, bool snap, qreal radius)
{
	if (!m_diagram || !hasStart()) return;
	m_mouse_pos = scene_pos;
	m_mouse_snap = snap;
	m_mouse_radius = radius;

	Terminal *target = m_start ? m_start->magnetTarget(scene_pos, radius)
							   : freeNozzleNear(m_diagram, scene_pos, radius);
		//Started in an empty place: moved onto the axis of the nozzle
		//which would catch the pipe, back when it no longer would
	const QPointF start_point = m_start_free ? startPointFor(target, snap) : m_start_point;
	if (m_start_free) {
		m_diagram->setConductorStart(start_point);
	}
		//The preview ends exactly where a click would end the pipe
	const bool free_end = !target && hasFreeEndSymbol();
	const QPointF end_point = free_end ? endPoint(scene_pos, snap) : scene_pos;
	QPointF stop = target ? target->dockConductor() : end_point;
		//Over a pipe, the preview ends where the tee would go
	PidTee::Spot spot;
	if (!target && PidTee::isSet(m_diagram) && teeArm() > 0)
	{
		Conductor *pipe = PidTee::conductorAt(m_diagram, scene_pos);
		if (pipe && pipe->medium() == TerminalData::Pipe) {
			spot = spotFacingStart(PidTee::spotOn(pipe, scene_pos, teeArm()), snap);
			if (spot.isValid()) stop = spot.point;
		}
	}
	m_diagram->setConductorStop(stop);
		//The real route, bends included, when the end is known
	m_diagram->setConductorRoute(previewRoute(start_point, target, spot, end_point, free_end));
	hover(target, Terminal::allowedColor);
}

/**
	@brief DiagramEventAddPipe::teeWayOut
	See the declaration
*/
bool DiagramEventAddPipe::teeWayOut(const PidTee::Spot &spot, const QPointF &toward, PipeEnd *end)
{
	if (!end || !spot.isValid()) return false;
	if (!m_tee_model) {
		m_tee_model = PidTee::build(m_diagram, nullptr, false);
	}
	if (!m_tee_model) return false;
	const Terminal *out = PidTee::fit(m_tee_model, spot, PidTee::branchFacing(spot, toward));
	if (!out) return false;
	*end = PipeEnd{out->dockConductor(), out->orientation(), false};
	return true;
}

/**
	@brief DiagramEventAddPipe::previewRoute
	See the declaration. The same ends as drawPipe() gives the pipe.
*/
QList<QPointF> DiagramEventAddPipe::previewRoute(const QPointF &start_point, Terminal *target,
												 const PidTee::Spot &end_spot,
												 const QPointF &end_point, bool free_end)
{
	const bool end_tee = !target && end_spot.isValid();
	if (!target && !end_tee && !free_end) return {};
	if (m_start_tee && !m_start_spot.pipe) return {};

		//Where the other end is, for a tee to face it
	const QPointF end_at = target ? target->dockConductor()
								  : (end_tee ? end_spot.point : end_point);
	PipeEnd start;
	if (m_start) {
		start = PipeEnd{m_start->dockConductor(), m_start->orientation(), false};
	} else if (m_start_tee) {
		if (!teeWayOut(m_start_spot, end_at, &start)) return {};
	} else {
		start = PipeEnd{start_point, Qet::East, true};
	}

	PipeEnd end;
	if (target) {
		end = PipeEnd{target->dockConductor(), target->orientation(), false};
	} else if (end_tee) {
		if (!teeWayOut(end_spot, start.point, &end)) return {};
	} else {
		end = PipeEnd{end_point, Qet::West, true};
	}
	if (QLineF(start.point, end.point).length() < 0.5) return {};

	facingOfFreeEnds(start, end, m_s_shape);
	return Conductor::defaultRoute(start.point, start.facing, end.point, end.facing,
								   PipeConnection::pipeExit);
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
	if (event->button() != Qt::LeftButton || !hasStart()) return;

	const qreal radius = Terminal::magnetSceneRadius(event);
		//Released on the empty place it starts from: it waits for the end
	Terminal *target = m_start ? m_start->magnetTarget(event->scenePos(), radius)
							   : freeNozzleNear(m_diagram, event->scenePos(), radius);
	if (target) {
		finishAt(target, snaps(event));
		return;
	}
		//A drag from a nozzle released on a pipe: a tee there, as the
		//release of a drag outside the tool does
	if (m_start && PidTee::isSet(m_diagram))
	{
		const PidTee::Spot spot = PidTee::spotAt(m_diagram, event->scenePos());
		if (spot.isValid()) {
			finishOnPipe(spotFacingStart(spot, snaps(event)));
		}
	}
}

/**
	@brief DiagramEventAddPipe::keyPressEvent
	Esc takes back the pipe being drawn, a second Esc leaves the tool.
	Space, while a pipe is drawn, turns its route in S or back in L.
	@param event
*/
void DiagramEventAddPipe::keyPressEvent(QKeyEvent *event)
{
		//Space: the route in S rather than in L, or back; never a rotation
		//of the selection while the tool runs (DiagramView::event())
	if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier)
	{
		if (hasStart()) {
			m_s_shape = !m_s_shape;
			updatePreview(m_mouse_pos, m_mouse_snap, m_mouse_radius);
			showHint();
		}
		event->setAccepted(true);
		return;
	}
	if (event->key() == Qt::Key_Escape && hasStart())
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
	@param snap : false with Ctrl: a start in an empty place stays where it is
*/
void DiagramEventAddPipe::finishAt(Terminal *target, bool snap)
{
	Terminal *start = m_start;
	const bool start_free = m_start_free;
	const QPointF start_point = startPointFor(target, snap);
	const bool start_tee = m_start_tee && m_start_spot.pipe;
	const PidTee::Spot start_spot = start_tee ? m_start_spot : PidTee::Spot();
	reset();
	if (target && (start || start_free || start_tee)) {
		drawPipe(start, start_point, start_spot, target, QPointF(), PidTee::Spot());
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
	if (m_diagram && hasStart()) {
		m_diagram->setConductor(false);
	}
	m_start.clear();
	m_start_free = false;
	m_start_point = QPointF();
	m_start_tee = false;
	m_start_spot = PidTee::Spot();
		//Measured again for the next pipe: the project may have changed
	m_tee_arm = -1;
	delete m_tee_model;
	m_tee_model = nullptr;
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

	const bool free_end = hasFreeEndSymbol();
	const bool tee = PidTee::isSet(m_diagram);
	QString text;
	if (!message.isEmpty()) {
		text = (message);
	} else if (hasStart() && tee && free_end) {
		text = (tr(
			"Click or release near another free nozzle, click on a pipe (tee) or in an empty "
			"place: end the pipe; Ctrl: no snapping to the axis; right click or Esc: cancel",
			"pipe drawing tool"));
	} else if (hasStart() && tee) {
		text = (tr(
			"Click or release near another free nozzle, or click on a pipe (tee): end the "
			"pipe; Ctrl: no snapping to the axis; right click or Esc: cancel", "pipe drawing tool"));
	} else if (!hasStart() && tee && free_end) {
		text = (tr(
			"Click near a free nozzle, on a pipe (tee) or in an empty place: start a pipe; "
			"right click or Esc: quit the tool", "pipe drawing tool"));
	} else if (!hasStart() && tee) {
		text = (tr(
			"Click near a free nozzle or on a pipe (tee): start a pipe; "
			"right click or Esc: quit the tool", "pipe drawing tool"));
	} else if (hasStart() && free_end) {
		text = (tr(
			"Click or release near another free nozzle, or click in an empty place: end "
			"the pipe; Ctrl: no snapping to the axis; right click or Esc: cancel",
			"pipe drawing tool"));
	} else if (hasStart()) {
		text = (tr(
			"Click or release near another free nozzle: end the pipe; "
			"right click or Esc: cancel", "pipe drawing tool"));
	} else if (free_end) {
		text = (tr(
			"Click near a free nozzle or in an empty place: start a pipe; "
			"right click or Esc: quit the tool", "pipe drawing tool"));
	} else {
		text = (tr(
			"Click near a free nozzle: start a pipe; "
			"right click or Esc: quit the tool", "pipe drawing tool"));
	}
		//A free end may end the pipe: Space turns its route the other way
	if (message.isEmpty() && hasStart() && free_end) {
		text += QStringLiteral("; ") + (m_s_shape ? tr("Space: route in L", "pipe drawing tool")
												  : tr("Space: route in S", "pipe drawing tool"));
	}
	editor->statusBar()->showMessage(text);
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
