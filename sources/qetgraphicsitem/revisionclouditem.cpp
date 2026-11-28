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
#include "revisionclouditem.h"

#include "../PropertiesEditor/propertieseditordialog.h"
#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../createdxf.h"
#include "../diagram.h"
#include "../dxfpaintdevice.h"
#include "../properties/revisionlist.h"
#include "../qetproject.h"
#include "../ui/revisioncloudpropertieswidget.h"

#include <QApplication>
#include <QDate>
#include <QDomDocument>
#include <QFont>
#include <QGraphicsSceneMouseEvent>
#include <QLineF>
#include <QLocale>
#include <QPainter>
#include <QPainterPathStroker>
#include <QSettings>
#include <QStyleOptionGraphicsItem>
#include <QTextDocument>

#include <cmath>

namespace {
		/// A review note wider than this goes to the next line: 60 mm
	constexpr qreal MaxNoteWidth = 240;
		/// The outline turns by more than this at a corner: the arcs of
		/// the sides on both sides end there
	constexpr qreal CornerDegrees = 30;

		/// The point @p at along the polyline @p run
	QPointF pointAlong(const QPolygonF &run, qreal at)
	{
		for (int i = 1 ; i < run.size() ; ++i) {
			const QLineF segment(run.at(i - 1), run.at(i));
			const qreal length = segment.length();
			if (at <= length || i == run.size() - 1)
				return length > 0 ? segment.pointAt(qBound(0.0, at / length, 1.0)) : run.at(i);
			at -= length;
		}
		return run.isEmpty() ? QPointF() : run.last();
	}

		/// An arc of 120 degrees from @p p to @p q, bulging towards @p outward
	void addArc(QPainterPath &path, const QPointF &p, const QPointF &q, qreal side)
	{
		const QPointF chord = q - p;
		const qreal length = std::hypot(chord.x(), chord.y());
		if (length < 1e-9)
			return;
		const QPointF u = chord / length;
		const QPointF outward = side * QPointF(u.y(), -u.x());
			//A cubic as close as can be to a circular arc of 120 degrees:
			//handles of 4/3 tan(30°) R, R = c / (2 sin 60°), leaving at 60°
		const qreal handle = length * 4.0 / 9.0;
		const qreal along = 0.5, across = std::sqrt(3.0) / 2;
		path.cubicTo(p + handle * (along * u + across * outward),
					 q + handle * (-along * u + across * outward),
					 q);
	}
}

RevisionCloudItem::RevisionCloudItem(QPointF p1, QPointF p2, ShapeType type, Kind kind, QGraphicsItem *parent) :
	QetShapeItem(p1, p2, type == Polygon ? Polygon : Rectangle, parent),
	m_kind(kind)
{
	m_label = new RevisionCloudLabelItem(this);
	connect(m_label, &QGraphicsObject::xChanged, this, &RevisionCloudItem::labelPosChanged);
	connect(m_label, &QGraphicsObject::yChanged, this, &RevisionCloudItem::labelPosChanged);
	connect(this, &QetShapeItem::geometryChanged, this, &RevisionCloudItem::placeLabel);
		//A free outline is closed once drawn: its corner is known then
	connect(this, &QetShapeItem::closeChanged, this, &RevisionCloudItem::placeLabel);
		//Its text is drawn in the colour of its line
	connect(this, &QetShapeItem::penChanged, this, &RevisionCloudItem::refreshLabel);
	connect(this, &QetShapeItem::transformChanged, this, &RevisionCloudItem::keepLabelUpright);
	refreshLabel();
}

void RevisionCloudItem::setKind(Kind kind)
{
	if (m_kind == kind) return;
	m_kind = kind;
	changed();
}

void RevisionCloudItem::setRevision(const QString &revision)
{
	if (m_revision == revision) return;
	m_revision = revision;
	changed();
}

void RevisionCloudItem::setComment(const QString &comment)
{
	if (m_comment == comment) return;
	m_comment = comment;
	changed();
}

void RevisionCloudItem::setAuthor(const QString &author)
{
	if (m_author == author) return;
	m_author = author;
	changed();
}

void RevisionCloudItem::setDate(const QString &date)
{
	if (m_date == date) return;
	m_date = date;
	changed();
}

void RevisionCloudItem::setDone(bool done)
{
	if (m_done == done) return;
	m_done = done;
	changed();
}

void RevisionCloudItem::setReply(const QString &reply)
{
	if (m_reply == reply) return;
	m_reply = reply;
	changed();
}

void RevisionCloudItem::setLabelSize(qreal size)
{
	if (qFuzzyCompare(m_label_size, size) || !(size > 0) || !qIsFinite(size))
		return;
	m_label_size = size;
	changed();
}

void RevisionCloudItem::setArcSize(qreal size)
{
	if (qFuzzyCompare(m_arc_size, size) || !(size > 0) || !qIsFinite(size))
		return;
	prepareGeometryChange();
	m_arc_size = size;
	changed();
}

/**
	@brief RevisionCloudItem::changed
	Everything a change of its data, its kind or its sizes draws again: the
	label, the grey of a done note, the tool tip.
*/
void RevisionCloudItem::changed()
{
	refreshLook();
	emit cloudChanged();
}

void RevisionCloudItem::refreshLook()
{
	refreshLabel();
		//A note done stays in place, greyed out; its colour is kept
	setOpacity(m_kind == Review && m_done ? 0.35 : 1.0);
	const QString tip = toolTipText();
	m_label->setToolTip(tip);
	if (!isSelected())
		setToolTip(tip);
	update();
}

bool RevisionCloudItem::isDrawn() const
{
	return m_kind == Revision || !diagram() || diagram()->drawReviewNotes();
}

QString RevisionCloudItem::labelText() const
{
	if (m_kind == Revision)
		return m_revision;
	QString text = m_comment;
	if (!m_reply.isEmpty()) {
		if (!text.isEmpty())
			text += QLatin1Char('\n');
		text += QStringLiteral("→ ") + m_reply;
	}
	return text;
}

QString RevisionCloudItem::toolTipText() const
{
	QStringList lines;
	if (m_kind == Revision)
	{
		lines << (m_revision.isEmpty() ? tr("Revision cloud")
									   : tr("Revision cloud %1").arg(m_revision));
			//The rest is the project's: a revision has no identifier, only
			//its index names it
		const QETProject *project = diagram() ? diagram()->project() : nullptr;
		if (project && !m_revision.isEmpty()) {
			for (const ::Revision &revision : project->revisions().revisions()) {
				if (revision.revision != m_revision)
					continue;
				if (!revision.date.isEmpty())
					lines << revision.date;
				if (!revision.description.isEmpty())
					lines << revision.description;
				break;
			}
		}
	}
	else
	{
		lines << tr("Review note");
		if (!m_author.isEmpty())
			lines << tr("Author: %1").arg(m_author);
		if (!m_date.isEmpty())
			lines << tr("Date: %1").arg(m_date);
		lines << (m_done ? tr("Done") : tr("To do"));
	}
	return lines.join(QLatin1Char('\n'));
}

QPolygonF RevisionCloudItem::baseOutline() const
{
	switch (shapeType())
	{
		case Rectangle: {
				//Its rounded corners, if any, are not drawn
			const QRectF r = rect().normalized();
			return QPolygonF({r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()});
		}
		case Polygon:
			return polygon();
		default: {
				//Made a curve (Convert to a curve), or anything else
			const QList<QPolygonF> subpaths = QetShapeItem::outline().toSubpathPolygons();
			return subpaths.isEmpty() ? QPolygonF() : subpaths.first();
		}
	}
}

/**
	@brief RevisionCloudItem::cloudPath
	The outline is cut at its corners, where it turns by more than 30
	degrees; each run between two corners is cut in equal chords, as near
	to @p arc_size as can be, and each chord becomes an arc bulging
	outwards. Outwards is found from the direction the outline goes round
	(the sign of its area), so a polygon drawn either way, a mirrored cloud
	or one made a curve keeps its arcs outside.
*/
QPainterPath RevisionCloudItem::cloudPath(const QPolygonF &base, qreal arc_size)
{
	QPolygonF points;
	for (const QPointF &point : base)
		if (points.isEmpty() || QLineF(points.last(), point).length() > 1e-6)
			points << point;
	while (points.size() > 1 && QLineF(points.first(), points.last()).length() <= 1e-6)
		points.removeLast();

	QPainterPath path;
	const int count = int(points.size());
	if (count < 3) {
		path.addPolygon(points);
		return path;
	}
	qreal twice_area = 0;
	for (int i = 0 ; i < count ; ++i) {
		const QPointF a = points.at(i), b = points.at((i + 1) % count);
		twice_area += a.x() * b.y() - b.x() * a.y();
	}
	if (qFuzzyIsNull(twice_area)) {
		path.addPolygon(points);
		path.closeSubpath();
		return path;
	}
	const qreal side = twice_area > 0 ? 1 : -1;
	if (!(arc_size > 0) || !qIsFinite(arc_size))
		arc_size = DefaultArcSize;

	QList<int> corners;
	for (int i = 0 ; i < count ; ++i) {
		const QLineF before(points.at((i - 1 + count) % count), points.at(i));
		const QLineF after(points.at(i), points.at((i + 1) % count));
		const qreal turn = before.angleTo(after);
		if (qMin(turn, 360 - turn) > CornerDegrees)
			corners << i;
	}
		//A smooth outline: one run all round
	if (corners.isEmpty())
		corners << 0;

	path.moveTo(points.at(corners.first()));
	for (int k = 0 ; k < corners.size() ; ++k)
	{
		const int start = corners.at(k);
		const int end = corners.at((k + 1) % corners.size());
		QPolygonF run;
		run << points.at(start);
		int i = start;
		do {
			i = (i + 1) % count;
			run << points.at(i);
		} while (i != end);

		qreal length = 0;
		for (int j = 1 ; j < run.size() ; ++j)
			length += QLineF(run.at(j - 1), run.at(j)).length();
		const int arcs = qMax(1, qRound(length / arc_size));
		QPointF from = run.first();
		for (int j = 1 ; j <= arcs ; ++j) {
			const QPointF to = j == arcs ? run.last() : pointAlong(run, length * j / arcs);
			addArc(path, from, to, side);
			from = to;
		}
	}
	path.closeSubpath();
	return path;
}

QPainterPath RevisionCloudItem::outline() const
{
	const QPolygonF base = baseOutline();
	if (base.size() < 3)
		return QetShapeItem::outline();
	return cloudPath(base, m_arc_size);
}

void RevisionCloudItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
	if (!isDrawn())
		return;
	QetShapeItem::paint(painter, option, widget);
}

/**
	@brief RevisionCloudItem::itemChange
	Selected, a shape's tool tip says what a click does; unselected, it says
	what the cloud is. Put on a folio, it reads the project's revisions.
*/
QVariant RevisionCloudItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
	const QVariant result = QetShapeItem::itemChange(change, value);
	if (change == ItemSelectedHasChanged && !isSelected())
		setToolTip(toolTipText());
	else if (change == ItemSceneHasChanged)
		refreshLook();
	return result;
}

/**
	@brief RevisionCloudItem::shape
	A thin outline is hard to point at, and the cloud's tool tip with it: a
	cloud is always as easy to point at as a hovered shape.
*/
QPainterPath RevisionCloudItem::shape() const
{
	QPainterPathStroker stroker;
	stroker.setWidth(pen().widthF() + 10);
	stroker.setJoinStyle(Qt::RoundJoin);
	return stroker.createStroke(outline());
}

/**
	@brief RevisionCloudItem::hoverEnterEvent
	QetShapeItem empties the tool tip of a shape not selected each time the
	cursor comes over it: the cloud's comes back.
*/
void RevisionCloudItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
	QetShapeItem::hoverEnterEvent(event);
	if (!isSelected())
		setToolTip(toolTipText());
}

/**
	@brief RevisionCloudItem::refreshLabel
	Its label says its index or its comment, in its colour and size. A
	cloud with nothing to say has no label to click.
*/
void RevisionCloudItem::refreshLabel()
{
	QFont font = m_label->font();
	font.setPointSizeF(m_label_size);
	m_label->setFont(font);
	m_label->setDefaultTextColor(pen().color());
	const QString text = labelText();
	m_label->setTextWidth(-1);
	m_label->setPlainText(text);
		//A long comment goes to the next line
	if (m_kind == Review && m_label->document()->idealWidth() > MaxNoteWidth)
		m_label->setTextWidth(MaxNoteWidth);
	m_label->setVisible(!text.isEmpty());
	m_label->refresh();
	placeLabel();
}

/**
	@brief RevisionCloudItem::placeLabel
	A label nobody has moved stands outside the cloud, at its top right
	corner; it stays there however the cloud is resized.
*/
void RevisionCloudItem::placeLabel()
{
	if (!m_label_moved)
		m_label->setPos(cornerLabelPos());
}

/**
	@brief RevisionCloudItem::keepLabelUpright
	A cloud turned, slanted or mirrored keeps its text readable: the label
	undoes what the cloud's transform does to it, and keeps its place.
*/
void RevisionCloudItem::keepLabelUpright()
{
	const QTransform t = transform();
	const QTransform linear(t.m11(), t.m12(), t.m21(), t.m22(), 0, 0);
	m_label->setTransform(linear.isInvertible() ? linear.inverted() : QTransform());
}

/**
	@return where a label nobody has moved stands: its bottom left corner
	beyond the arcs at the top right corner of the cloud
*/
QPointF RevisionCloudItem::cornerLabelPos() const
{
	const QRectF base = baseOutline().boundingRect();
	const qreal margin = m_arc_size * 0.4;
	const QRectF label = m_label->boundingRect();
	return base.topRight() + QPointF(margin, -margin) - QPointF(label.left(), label.bottom());
}

/**
	@brief RevisionCloudItem::labelPosChanged
	A label put back on the corner, by a drag or by an undo, follows the
	corner again and is saved as never moved.
*/
void RevisionCloudItem::labelPosChanged()
{
	m_label_moved = QLineF(m_label->pos(), cornerLabelPos()).length() > 0.01;
		//Its place is saved with the cloud (label_x, label_y)
	emit cloudChanged();
}

/**
	@return where the label stands, from the top right corner of the cloud
*/
QPointF RevisionCloudItem::labelOffset() const
{
	return m_label->pos() - baseOutline().boundingRect().topRight();
}

void RevisionCloudItem::setLabelOffset(const QPointF &offset)
{
	m_label->setPos(baseOutline().boundingRect().topRight() + offset);
}

bool RevisionCloudItem::isCloudXml(const QDomElement &e)
{
	return e.tagName() == QLatin1String("shape")
			&& !e.attribute(QStringLiteral("cloud")).isEmpty();
}

	//The geometry, pen, brush, uuid and transform are the shape's own
	//(QetShapeItem::fromXml()); the cloud adds its kind and its data. Nothing
	//missing is given a value: opening a file never changes it.
bool RevisionCloudItem::fromXml(const QDomElement &e)
{
	if (e.tagName() != QLatin1String("shape"))
		return false;
	prepareGeometryChange();
	m_kind = e.attribute(QStringLiteral("cloud")) == QLatin1String("review") ? Review : Revision;
	m_revision = e.attribute(QStringLiteral("revision"));
	m_comment = e.attribute(QStringLiteral("comment"));
	m_author = e.attribute(QStringLiteral("author"));
	m_date = e.attribute(QStringLiteral("date"));
	m_reply = e.attribute(QStringLiteral("reply"));
	m_done = e.attribute(QStringLiteral("done")) == QLatin1String("true");
	m_label_size = e.attribute(QStringLiteral("label_size"),
							   QString::number(DefaultLabelSize)).toDouble();
	if (!(m_label_size > 0) || !qIsFinite(m_label_size))
		m_label_size = DefaultLabelSize;
	m_arc_size = e.attribute(QStringLiteral("arc"),
							 QString::number(DefaultArcSize)).toDouble();
	if (!(m_arc_size > 0) || !qIsFinite(m_arc_size))
		m_arc_size = DefaultArcSize;

	if (!QetShapeItem::fromXml(e))
		return false;
	keepLabelUpright();
	refreshLabel();
		//Read into a cloud already on a folio (an undo), a label saved at the
		//corner goes back to the corner
	if (e.hasAttribute(QStringLiteral("label_x")))
		setLabelOffset(QPointF(e.attribute(QStringLiteral("label_x")).toDouble(),
							   e.attribute(QStringLiteral("label_y")).toDouble()));
	else
		m_label->setPos(cornerLabelPos());
	changed();
	return true;
}

QDomElement RevisionCloudItem::toXml(QDomDocument &document) const
{
	QDomElement result = QetShapeItem::toXml(document);
	result.setAttribute(QStringLiteral("cloud"), m_kind == Review ? QStringLiteral("review")
																   : QStringLiteral("revision"));
		//Only when filled, so that nothing is written for what is not
	const auto write = [&result](const char *name, const QString &value) {
		if (!value.isEmpty())
			result.setAttribute(QLatin1String(name), value);
	};
	write("revision", m_revision);
	write("comment", m_comment);
	write("author", m_author);
	write("date", m_date);
	write("reply", m_reply);
	if (m_done)
		result.setAttribute(QStringLiteral("done"), QStringLiteral("true"));
	if (m_label_moved) {
		const QPointF offset = labelOffset();
		result.setAttribute(QStringLiteral("label_x"), QString::number(offset.x(), 'f', 1));
		result.setAttribute(QStringLiteral("label_y"), QString::number(offset.y(), 'f', 1));
	}
	if (!qFuzzyCompare(m_label_size, DefaultLabelSize))
		result.setAttribute(QStringLiteral("label_size"), QString::number(m_label_size));
	if (!qFuzzyCompare(m_arc_size, DefaultArcSize))
		result.setAttribute(QStringLiteral("arc"), QString::number(m_arc_size));
	return result;
}

/**
	@brief RevisionCloudItem::toDXF
	The arcs, flattened into a closed polyline, and the label as it is
	drawn; nothing for a review note that is not drawn on paper.
*/
bool RevisionCloudItem::toDXF(const QString &filepath, const QPen &pen)
{
	if (!isDrawn())
		return true;
	const QList<QPolygonF> subpaths = outline().toSubpathPolygons();
	if (!subpaths.isEmpty())
		Createdxf::drawPolygon(filepath, mapToScene(subpaths.first()), Createdxf::dxfColor(pen));
	if (m_label->isVisible()) {
		DxfPaintDevice dxf_device(filepath);
		QPainter painter(&dxf_device);
		painter.setWorldTransform(m_label->sceneTransform());
		QStyleOptionGraphicsItem option;
		m_label->paint(&painter, &option, nullptr);
		painter.end();
	}
	return true;
}

void RevisionCloudItem::editProperty()
{
	if (diagram() && diagram()->isReadOnly())
		return;

	PropertiesEditorDialog ped(new RevisionCloudPropertiesWidget(this),
							   diagram() ? diagram()->views().value(0) : nullptr);
	ped.exec();
}

QString RevisionCloudItem::name() const
{
	return m_kind == Review ? tr("a review note") : tr("a revision cloud");
}

QColor RevisionCloudItem::newCloudColor(Kind kind)
{
	const QString fallback = kind == Review ? QStringLiteral("#ff0000") : QStringLiteral("#000000");
	const QColor color(QSettings().value(kind == Review ? QStringLiteral("diagrameditor/review_note_color")
														: QStringLiteral("diagrameditor/revision_cloud_color"),
										 fallback).toString());
	return color.isValid() ? color : QColor(fallback);
}

void RevisionCloudItem::setNewCloudColor(Kind kind, const QColor &color)
{
	QSettings().setValue(kind == Review ? QStringLiteral("diagrameditor/review_note_color")
										: QStringLiteral("diagrameditor/revision_cloud_color"),
						 color.name());
}

QString RevisionCloudItem::today()
{
	return QLocale::system().toString(QDate::currentDate(), QLocale::ShortFormat);
}

RevisionCloudLabelItem::RevisionCloudLabelItem(RevisionCloudItem *cloud) :
	DiagramTextItem(cloud),
	m_cloud(cloud)
{
	setFlag(QGraphicsItem::ItemIsSelectable, true);
	setFlag(QGraphicsItem::ItemIsMovable, true);
}

/**
	@brief RevisionCloudLabelItem::triangle
	The index of a revision stands in the middle of an equilateral triangle
	whose inscribed circle holds it.
*/
QPolygonF RevisionCloudLabelItem::triangle() const
{
	if (m_cloud->kind() != RevisionCloudItem::Revision)
		return QPolygonF();
		//The box of the text itself, not writtenRect(): that one reads
		//boundingRect(), which reads this triangle
	const qreal margin = document()->documentMargin();
	const QRectF text = QGraphicsTextItem::boundingRect().adjusted(margin, margin, -margin, -margin);
	const QPointF centre = text.center();
	const qreal radius = qMax(QLineF(text.topLeft(), text.bottomRight()).length() / 2 * 1.05, 4.0);
	const qreal side = radius * 2 * std::sqrt(3.0);
	const qreal height = radius * 3;
	return QPolygonF({QPointF(centre.x(), centre.y() - 2 * height / 3),
					  QPointF(centre.x() + side / 2, centre.y() + height / 3),
					  QPointF(centre.x() - side / 2, centre.y() + height / 3)});
}

void RevisionCloudLabelItem::refresh()
{
	prepareGeometryChange();
	update();
}

QRectF RevisionCloudLabelItem::boundingRect() const
{
	QRectF rect = DiagramTextItem::boundingRect();
	const QPolygonF tri = triangle();
	if (!tri.isEmpty()) {
		const qreal pen = qMax(m_cloud->pen().widthF(), 1.0);
		rect |= tri.boundingRect().adjusted(-pen, -pen, pen, pen);
	}
	return rect;
}

QPainterPath RevisionCloudLabelItem::shape() const
{
	QPainterPath path = DiagramTextItem::shape();
	const QPolygonF tri = triangle();
	if (!tri.isEmpty()) {
		QPainterPath triangle_path;
		triangle_path.addPolygon(tri);
		triangle_path.closeSubpath();
		path = path.united(triangle_path);
	}
	return path;
}

void RevisionCloudLabelItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
	if (!m_cloud->isDrawn())
		return;
	const QPolygonF tri = triangle();
	if (!tri.isEmpty()) {
		painter->save();
		QPen pen = m_cloud->pen();
		pen.setStyle(Qt::SolidLine);
		pen.setJoinStyle(Qt::MiterJoin);
		painter->setPen(pen);
		painter->setBrush(Qt::NoBrush);
		painter->setRenderHint(QPainter::Antialiasing, true);
		painter->drawPolygon(tri);
		painter->restore();
	}
	DiagramTextItem::paint(painter, option, widget);
}

/**
	@brief RevisionCloudLabelItem::mousePressEvent
	Selected with its cloud, a drag moves the whole selection, as a drag of
	the cloud would; Ctrl still adds or takes the label alone. Else it
	moves the label alone.
*/
void RevisionCloudLabelItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	m_dragging = false;
	m_move_cloud = event->button() == Qt::LeftButton
			&& !(event->modifiers() & Qt::ControlModifier)
			&& isSelected() && m_cloud->isSelected();
	if (m_move_cloud) {
		m_cloud->mousePressEvent(event);
		return;
	}
	DiagramTextItem::mousePressEvent(event);
	m_start = pos();
	m_grab = pos() - parentItem()->mapFromScene(event->scenePos());
}

void RevisionCloudLabelItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	if (m_move_cloud) {
		m_cloud->mouseMoveEvent(event);
		return;
	}
	if (!(event->buttons() & Qt::LeftButton)) {
		event->ignore();
		return;
	}
	event->accept();
		//A locked cloud keeps its text where it is
	if (!m_cloud->isMovable())
		return;
	if (!m_dragging) {
		const QPointF dragged =
				event->scenePos() - event->buttonDownScenePos(Qt::LeftButton);
		if (dragged.manhattanLength() < QApplication::startDragDistance())
			return;
		m_dragging = true;
	}
		//On the text grid like every other text; Ctrl places it freely
	const QPointF wanted = parentItem()->mapToScene(
			parentItem()->mapFromScene(event->scenePos()) + m_grab);
	setPos(parentItem()->mapFromScene(Diagram::snapToTextGrid(wanted, this)));
}

void RevisionCloudLabelItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	if (m_move_cloud) {
		m_move_cloud = false;
		m_cloud->mouseReleaseEvent(event);
		return;
	}
		//The corner is off the text grid: dropped next to it, the text goes
		//back to it and follows it again
	if (m_dragging
		&& QLineF(pos(), m_cloud->cornerLabelPos()).length() < 4)
		setPos(m_cloud->cornerLabelPos());
	const bool moved = m_dragging && pos() != m_start;
	m_dragging = false;
	if (event->button() == Qt::LeftButton && moved) {
		if (Diagram *folio = diagram()) {
				//A parent command: two drags are two undo steps, never
				//merged as the steps of one QPropertyUndoCommand are
			auto *undo = new QUndoCommand(tr("Move the text of a cloud"));
			new QPropertyUndoCommand(this, "pos", m_start, pos(), undo);
			folio->undoStack().push(undo);
		}
		event->accept();
		return;
	}
	QGraphicsTextItem::mouseReleaseEvent(event);
}

void RevisionCloudLabelItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
	event->accept();
	m_cloud->editProperty();
}
