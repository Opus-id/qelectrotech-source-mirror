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
#include "../qetgraphicsitem/terminal.h"

#include <QColor>
#include <QList>
#include <QPointF>
#include <QPointer>

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
		Terminal *startNozzle() const { return m_start; }
			/// How many free nozzles are marked
		int markCount() const { return m_marks.size(); }

	private:
		void reset();
		void refreshMarks();
		void clearMarks();
		void hover(Terminal *terminal, const QColor &color);
		void finishAt(Terminal *target);
		void showHint(const QString &message = QString()) const;
		void clearHint() const;

	private:
		QPointer<Terminal> m_start;
		QPointer<Terminal> m_hovered;
		QList<QGraphicsEllipseItem *> m_marks;
};

#endif // DIAGRAMEVENTADDPIPE_H
