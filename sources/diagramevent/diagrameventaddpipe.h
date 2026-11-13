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
#ifndef DIAGRAMEVENTADDPIPE_H
#define DIAGRAMEVENTADDPIPE_H

#include "diagrameventinterface.h"
#include "../ElementsCollection/elementslocation.h"
#include "../pidtee.h"
#include "../qet.h"
#include "../qetgraphicsitem/terminal.h"

#include <QColor>
#include <QList>
#include <QPointF>
#include <QPointer>

class Element;
class QGraphicsEllipseItem;
class QGraphicsItem;

/**
	@brief The DiagramEventAddPipe class: the tool which draws pipes.

	At a low zoom a nozzle is hard to hit, and a click near one picks or
	moves its symbol. While this tool runs a click never picks nor moves
	anything: it starts a pipe at the free nozzle nearest to it, within
	Terminal::magnetRadius on screen, and a second click -- or the release
	of a drag -- near another free nozzle ends it there, in one undo step.
	The free nozzles are marked while the tool runs, and the tool stays on
	after each pipe. A right click or Esc takes back the pipe being drawn,
	a second one leaves the tool. Wires are not drawn with it: it starts
	at nozzles only.

	When the project has a symbol of free end (PidCodification::tool_symbols),
	a click in an empty place -- no free nozzle near, no symbol nor pipe
	under it -- starts or ends the pipe there, on the grid: the symbol is
	placed at the end, turned so that its nozzle faces the pipe, along the
	simplest way (straight when aligned, else one bend). Nothing is placed
	before the end: the free ends and the pipe are one undo step.

	When the project has a symbol of tee, a click on a pipe starts or ends
	the pipe there, on a tee (PidTee): the pipe is cut through it at the
	end, in the same undo step.

	After the first click, an end nearly in the axis of the start (within
	axisAngle) snaps onto it, in the preview as at the click: no small
	bends for a pipe meant straight. A start in an empty place moves onto
	the axis of a nozzle nearly in front of it; a tee at the end goes right
	in front of the start. Ctrl held: no snapping, the grid only.

	The preview shows the route the pipe will take, bends included. Where
	a free end decides it, the route is in L (one bend) or, after Space, in
	S (two bends); the choice stays while the tool runs.
*/
class DiagramEventAddPipe : public DiagramEventInterface
{
		Q_OBJECT

	public:
		explicit DiagramEventAddPipe(Diagram *diagram);
		~DiagramEventAddPipe() override;

		void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
		void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
		void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
		void keyPressEvent(QKeyEvent *event) override;
		void init() override;

			/// The free nozzle of @p diagram nearest to @p scene_pos within
			/// @p radius, nullptr for none
		static Terminal *freeNozzleNear(Diagram *diagram, const QPointF &scene_pos,
										qreal radius);
			/// A nozzle which may still take a pipe
		static bool isFreeNozzle(const Terminal *terminal);
			/// The free nozzle of the symbols among @p items nearest to
			/// @p scene_pos, however far; nullptr for none
		static Terminal *nearestFreeNozzle(const QList<QGraphicsItem *> &items,
										   const QPointF &scene_pos);
			/// Start the pipe at @p nozzle, as a first click near it does;
			/// nothing when it is not a free nozzle of this folio
		void startFrom(Terminal *nozzle);
			/// Where the pipe being drawn starts, nullptr before the first click
			/// and for a pipe started in an empty place
		Terminal *startNozzle() const { return m_start; }
			/// Whether the pipe being drawn starts in an empty place
		bool startsInEmptyPlace() const { return m_start_free; }
			/// Where it starts then, on the grid
		QPointF startPoint() const { return m_start_point; }
			/// Whether the pipe being drawn starts on a pipe, at a tee placed
			/// at the end
		bool startsOnPipe() const { return m_start_tee; }
			/// Where on which pipe it starts then
		PidTee::Spot startSpot() const { return m_start_spot; }
			/// How many free nozzles are marked
		int markCount() const { return m_marks.size(); }

			/// The symbol of free end of the project of @p diagram, a null
			/// location when it has none or when it is not found
		static ElementsLocation freeEndSymbol(const Diagram *diagram);
			/// Which way the nozzle of a free end placed at @p end faces, for
			/// a pipe from @p from, which leaves it facing @p from_facing:
			/// straight in front, back along it; else the simplest way, one
			/// bend in front (a route in L), two beside or behind. With
			/// @p s_shape, in front and aside: back toward @p from, two bends
			/// (a route in S), when there is room for it (sShapeRoom)
		static Qet::Orientation freeEndFacing(const QPointF &from, Qet::Orientation from_facing,
											  const QPointF &end, bool s_shape = false);
			/// The way from @p from to @p to, horizontal or vertical, whichever
			/// is longer
		static Qet::Orientation dominantFacing(const QPointF &from, const QPointF &to);
			/// How far in front of its start a free end must be for a route
			/// in S: room for the straight run leaving each end
			/// (PipeConnection::pipeExit) before the bends; nearer, in L
		static qreal sShapeRoom();

			/// One end of a pipe: where it docks and which way it faces;
			/// @c free for a free end, whose way facingOfFreeEnds() gives
		struct PipeEnd
		{
			QPointF point;
			Qet::Orientation facing = Qet::East;
			bool free = false;
		};
			/// Turn the free ends among @p start and @p end the way their
			/// nozzles face, for a pipe from @p start to @p end: what the tool
			/// places and what it shows before (one rule for both). A nozzle
			/// or a tee keeps its way. @p s_shape: a route in S where a free
			/// end decides it, else in L
		static void facingOfFreeEnds(PipeEnd &start, PipeEnd &end, bool s_shape);

			/// Whether the next pipes take a route in S (two bends) rather than
			/// in L (one bend) where a free end decides it; kept while the tool
			/// runs, in L again the next time
		bool sShape() const { return m_s_shape; }
		void setSShape(bool s_shape) { m_s_shape = s_shape; }

			/// Within this angle of an axis of its start, in degrees, the end
			/// of the pipe being drawn snaps onto it (5l-1p-bis)
		static constexpr qreal axisAngle = 10.0;
			/// Whether @p to lies in front of @p from along @p facing, within
			/// axisAngle of that axis
		static bool nearAxis(const QPointF &from, Qet::Orientation facing, const QPointF &to);
			/// Where the pipe being drawn ends, in an empty place, for the
			/// mouse at @p scene_pos: on the grid; exactly on the axis of its
			/// start when nearly on it (a nozzle or a tee: its way out, in
			/// front; an empty place: both axes), unless @p snap is false (Ctrl)
		QPointF endPoint(const QPointF &scene_pos, bool snap) const;
			/// Where the pipe being drawn starts when it ends at @p target:
			/// started in an empty place nearly in front of @p target, moved
			/// onto its axis when that place is empty too; unless @p snap is
			/// false (Ctrl). Nothing is placed before the end.
		QPointF startPointFor(const Terminal *target, bool snap) const;

	private:
		void reset();
		void refreshMarks();
		void clearMarks();
		void hover(Terminal *terminal, const QColor &color);
		void finishAt(Terminal *target, bool snap = true);
		void startAtPoint(const QPointF &scene_pos);
		void finishAtPoint(const QPointF &end);
		PidTee::Spot spotFacingStart(const PidTee::Spot &spot, bool snap);
		bool hasFreeEndSymbol() const;
		void startOnPipe(const PidTee::Spot &spot);
		void finishOnPipe(const PidTee::Spot &spot);
		void drawPipe(Terminal *start, const QPointF &start_point, const PidTee::Spot &start_spot,
					  Terminal *target, const QPointF &end_point, const PidTee::Spot &end_spot);
		bool isEmptyPlace(const QPointF &scene_pos) const;
			/// Where the nozzle of a tee at @p spot, facing @p toward as
			/// drawPipe() turns it, docks and which way it faces; false
			/// without a symbol of tee. The tee is built once per pipe,
			/// never placed (m_tee_model). @p middle, when given, takes
			/// where its ways meet (PidTee::middleOf()).
		bool teeWayOut(const PidTee::Spot &spot, const QPointF &toward, PipeEnd *end,
					   QPointF *middle = nullptr);
			/// The route the pipe being drawn would take, from its start (at
			/// @p start_point when it starts in an empty place) to @p target,
			/// else to a tee at @p end_spot (valid), else to a free end at
			/// @p end_point when @p free_end: the points of the pipe placed
			/// on a click, carried on to the middle of a tee at either end
			/// (the tee is not drawn: the line reaches the pipe it cuts);
			/// empty when the end is not known
		QList<QPointF> previewRoute(const QPointF &start_point, Terminal *target,
									const PidTee::Spot &end_spot, const QPointF &end_point,
									bool free_end);
			/// The preview for the mouse at @p scene_pos: where it ends, its
			/// route, the nozzle lit; kept to be shown again (m_mouse_*)
		void updatePreview(const QPointF &scene_pos, bool snap, qreal radius);
		bool hasStart() const { return m_start || m_start_free || m_start_tee; }
		QPointF startScenePoint() const;
		qreal teeArm();
		void refuse(const QGraphicsSceneMouseEvent *event, const QString &why) const;
		void showHint(const QString &message = QString()) const;
		void clearHint() const;

	private:
		QPointer<Terminal> m_start;
		bool m_start_free = false;
		QPointF m_start_point;
		bool m_start_tee = false;
		PidTee::Spot m_start_spot;
			/// The arms of the tee of the project while the tool runs, for
			/// the preview: -1 until measured
		qreal m_tee_arm = -1;
			/// A route in S rather than in L (sShape())
		bool m_s_shape = false;
			/// The symbol of tee of the project, never placed: where its
			/// nozzles would go, for the preview (teeWayOut()); built once
			/// per pipe
		Element *m_tee_model = nullptr;
			/// The last mouse move: where, with or without snapping, how
			/// near a nozzle catches the pipe
		QPointF m_mouse_pos;
		bool m_mouse_snap = true;
		qreal m_mouse_radius = 0;
		QPointer<Terminal> m_hovered;
		QList<QGraphicsEllipseItem *> m_marks;
};

#endif // DIAGRAMEVENTADDPIPE_H
