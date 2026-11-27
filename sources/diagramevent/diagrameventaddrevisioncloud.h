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
#ifndef DIAGRAMEVENTADDREVISIONCLOUD_H
#define DIAGRAMEVENTADDREVISIONCLOUD_H

#include "diagrameventaddshape.h"
#include "../qetgraphicsitem/revisionclouditem.h"

/**
	@brief The DiagramEventAddRevisionCloud class
	Draws a revision cloud or a review note: a rectangle in two clicks, or a
	free outline point by point, closed by a double click. Space switches
	between the two, before the first click or right after it (the first
	point is kept). Reuses DiagramEventAddShape's mouse handling.
	A new cloud takes the colour set for its kind; a revision cloud, the
	last revision of the project; a review note, the user's name and
	today's date, and asks for its comment at once (cancelled: no note).
*/
class DiagramEventAddRevisionCloud : public DiagramEventAddShape
{
		Q_OBJECT

	public:
		DiagramEventAddRevisionCloud(Diagram *diagram, RevisionCloudItem::Kind kind);

		void mousePressEvent       (QGraphicsSceneMouseEvent *event) override;
		void mouseMoveEvent        (QGraphicsSceneMouseEvent *event) override;
		void mouseReleaseEvent     (QGraphicsSceneMouseEvent *event) override;
		void mouseDoubleClickEvent (QGraphicsSceneMouseEvent *event) override;
		void keyPressEvent         (QKeyEvent *event) override;
		bool handlesShortcutOverride(QKeyEvent *event) const override;

		RevisionCloudItem::Kind kind() const {return m_kind;}
			/// Whether the cloud drawn, or the next one, is a free outline, not a rectangle
		bool drawsFreeOutline() const {return m_shape_type == QetShapeItem::Polygon;}
			/// Whether Space can still switch the outline: before the first
			/// click, or with only the first point placed
		bool canSwitchOutline() const;
			/// What the status bar says
		QString hint() const;

			/// Asks for the comment of @p note; false when cancelled
		static bool askComment(RevisionCloudItem *note, QWidget *parent);

	protected:
		QetShapeItem *createShapeItem(QPointF p1, QPointF p2, QetShapeItem::ShapeType shape_type) override;
		bool keepShape(QetShapeItem *shape) override;

	private:
		void showHint() const;
		void styleCloud(QetShapeItem *shape) const;
		void switchOutline();

		RevisionCloudItem::Kind m_kind;
};

#endif // DIAGRAMEVENTADDREVISIONCLOUD_H
