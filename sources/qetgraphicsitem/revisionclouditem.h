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
#ifndef REVISIONCLOUDITEM_H
#define REVISIONCLOUDITEM_H

#include "diagramtextitem.h"
#include "qetshapeitem.h"

#include <QColor>
#include <QPointF>

class RevisionCloudLabelItem;

/**
	@brief The RevisionCloudItem class
	A cloud drawn around a part of a folio, of one of two kinds:
	- a revision cloud: what changed since the previous index (as in
	  Revit or AutoCAD), with that index written in a small triangle;
	- a review note: a remark of a reviewer, written next to the cloud,
	  with its author, its date, whether it is done and the answer of the
	  draughtsman.
	The cloud is a rectangle or a closed polygon whose outline is drawn as
	arcs bulging outwards. It is a QetShapeItem subclass and keeps its
	type(): cut, copy, group, align, delete, the grid and the DXF export
	treat it as any shape.

	Saved as a <shape> carrying a cloud attribute ("revision" or "review")
	and its data, written only when filled, so every path that keeps shapes
	(the .qet, the project database and the .qetdb, folios not built, copy
	and paste) keeps a cloud too; an older QElectroTech reads it as a plain
	rectangle or polygon.

	A review note is drawn on paper only when the options of rendering ask
	for it (Diagram::drawReviewNotes()); a revision cloud always is.
*/
class RevisionCloudItem : public QetShapeItem
{
	friend class RevisionCloudLabelItem;
	Q_OBJECT
	Q_PROPERTY(Kind kind READ kind WRITE setKind NOTIFY cloudChanged)
	Q_PROPERTY(QString revision READ revision WRITE setRevision NOTIFY cloudChanged)
	Q_PROPERTY(QString comment READ comment WRITE setComment NOTIFY cloudChanged)
	Q_PROPERTY(QString author READ author WRITE setAuthor NOTIFY cloudChanged)
	Q_PROPERTY(QString date READ date WRITE setDate NOTIFY cloudChanged)
	Q_PROPERTY(bool done READ isDone WRITE setDone NOTIFY cloudChanged)
	Q_PROPERTY(QString reply READ reply WRITE setReply NOTIFY cloudChanged)
	Q_PROPERTY(qreal labelSize READ labelSize WRITE setLabelSize NOTIFY cloudChanged)
	Q_PROPERTY(qreal arcSize READ arcSize WRITE setArcSize NOTIFY cloudChanged)

	public:
		enum Kind {Revision, Review};
		Q_ENUM(Kind)

			/// The length of an arc unless set: 5 mm on paper
		static constexpr qreal DefaultArcSize = 20;
		static constexpr qreal DefaultLabelSize = 8;

		explicit RevisionCloudItem(QPointF p1, QPointF p2 = QPointF(0, 0),
								   ShapeType type = Rectangle, Kind kind = Revision,
								   QGraphicsItem *parent = nullptr);
		~RevisionCloudItem() override = default;

		Kind kind() const {return m_kind;}
		void setKind(Kind kind);
		QString revision() const {return m_revision;}
		void setRevision(const QString &revision);
		QString comment() const {return m_comment;}
		void setComment(const QString &comment);
		QString author() const {return m_author;}
		void setAuthor(const QString &author);
		QString date() const {return m_date;}
		void setDate(const QString &date);
		bool isDone() const {return m_done;}
		void setDone(bool done);
		QString reply() const {return m_reply;}
		void setReply(const QString &reply);
		qreal labelSize() const {return m_label_size;}
		void setLabelSize(qreal size);
		qreal arcSize() const {return m_arc_size;}
		void setArcSize(qreal size);

			/// Its index or its comment: a text of its own, which can stand anywhere
		RevisionCloudLabelItem *label() const {return m_label;}
		QPointF labelOffset() const;
		void setLabelOffset(const QPointF &offset);
		bool labelMoved() const {return m_label_moved;}

			/// Whether it is drawn: always on the screen; on paper, a review
			/// note only when the options of rendering ask for it
		bool isDrawn() const;
			/// What its label says: the index of a revision; the comment of a
			/// review note, then "→ reply" on a line of its own
		QString labelText() const;
			/// What the tool tip says: index, date and description of a
			/// revision, read in the project's list; author, date and state of
			/// a review note
		QString toolTipText() const;
			/// The outline it is drawn around, before the arcs: its rectangle,
			/// its polygon, or its flattened curve
		QPolygonF baseOutline() const;

		bool fromXml(const QDomElement &e) override;
		QDomElement toXml(QDomDocument &document) const override;
		bool toDXF(const QString &filepath, const QPen &pen) override;

		void editProperty() override;
		QString name() const override;

			/// The colour new clouds of @p kind are drawn in: black for a
			/// revision, red for a review note unless set
		static QColor newCloudColor(Kind kind);
		static void setNewCloudColor(Kind kind, const QColor &color);

			/// Whether @p e, a <shape>, is a cloud
		static bool isCloudXml(const QDomElement &e);
			/// The outline of arcs bulging outwards along the closed polygon
			/// @p base, each about @p arc_size long
		static QPainterPath cloudPath(const QPolygonF &base, qreal arc_size);
			/// Today, as a cloud writes it: ISO 8601
		static QString today();

			/// As wide as the shape of a hovered QetShapeItem, hovered or not
		QPainterPath shape() const override;

	signals:
			/// One of its data, its kind, its sizes changed, by an edit or an undo
		void cloudChanged();

	protected:
		QPainterPath outline() const override;
		void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
				   QWidget *widget) override;
		QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
		void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;

	private:
		void changed();
		void refreshLook();
		void refreshLabel();
		void placeLabel();
		void keepLabelUpright();
		QPointF cornerLabelPos() const;
		void labelPosChanged();

		Kind m_kind = Revision;
		QString m_revision, m_comment, m_author, m_date, m_reply;
		bool m_done = false;
		qreal m_label_size = DefaultLabelSize;
		qreal m_arc_size = DefaultArcSize;
		RevisionCloudLabelItem *m_label = nullptr;
			/// Not at the corner: it stays where it is put. Worked out from
			/// where it stands, so undoing a move puts it back to the corner.
		bool m_label_moved = false;
};

/**
	@brief The RevisionCloudLabelItem class
	What a cloud says: the index of a revision, in a triangle, or the
	comment of a review note and the reply. Selected and dragged on its
	own, wherever it is wanted, in one undo step. A double click edits the
	cloud.
*/
class RevisionCloudLabelItem : public DiagramTextItem
{
	friend class RevisionCloudItem;
	Q_OBJECT

	public:
		enum { Type = UserType + 1018 };
		explicit RevisionCloudLabelItem(RevisionCloudItem *cloud);
		int type() const override { return Type; }
		void fromXml(const QDomElement &) override {}

		QRectF boundingRect() const override;
		QPainterPath shape() const override;
			/// The triangle around the index of a revision, in its own
			/// coordinates; empty for a review note
		QPolygonF triangle() const;
			/// Its text, its kind or its sizes changed: its box too
		void refresh();

	protected:
		void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
				   QWidget *widget) override;
		void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
		void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
		void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
		void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

	private:
		RevisionCloudItem *m_cloud;
		QPointF m_grab;
		QPointF m_start;
		bool m_dragging = false;      ///< past the drag distance: a click is not a move
		bool m_move_cloud = false;    ///< part of a selection with its cloud: the cloud drives
};

#endif // REVISIONCLOUDITEM_H
