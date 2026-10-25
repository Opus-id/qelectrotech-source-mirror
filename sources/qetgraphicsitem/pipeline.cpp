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
#include "pipeline.h"

#include "../conductorrouter.h"

#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>

namespace
{
		/// Width of the line, in the units of the folio
	constexpr qreal pen_width = 1.0;
		/// Width of the area where a click selects the pipe line
	constexpr qreal click_width = 6.0;

		/// Name of the element a pipe line is written as
	const QString xml_tag = QStringLiteral("pipe");

		/// The letters the terminals use for their orientation in the files
	QString directionToString(Qet::Orientation o)
	{
		switch (o) {
			case Qet::North: return QStringLiteral("n");
			case Qet::East:  return QStringLiteral("e");
			case Qet::South: return QStringLiteral("s");
			case Qet::West:  return QStringLiteral("w");
		}
		return QStringLiteral("n");
	}

		/// Unlike Qet::orientationFromString(), which takes anything for
		/// north, only the four letters are accepted: a file with another
		/// one is not a pipe line this class wrote
	bool directionFromString(const QString &s, Qet::Orientation &o)
	{
		if (s == QLatin1String("n")) { o = Qet::North; return true; }
		if (s == QLatin1String("e")) { o = Qet::East;  return true; }
		if (s == QLatin1String("s")) { o = Qet::South; return true; }
		if (s == QLatin1String("w")) { o = Qet::West;  return true; }
		return false;
	}

		/// "nan" and "inf" read as numbers: a coordinate must be finite
	bool readCoordinate(const QDomElement &e, const QString &name, qreal &value)
	{
		bool ok = false;
		const qreal v = e.attribute(name).toDouble(&ok);
		if (!ok || !qIsFinite(v)) return false;
		value = v;
		return true;
	}

	bool readEnd(const QDomElement &e, const QString &prefix, PipeLine::End &end)
	{
		qreal x = 0, y = 0;
		Qet::Orientation direction = Qet::North;
		if (!readCoordinate(e, prefix + QStringLiteral("_x"), x)
			|| !readCoordinate(e, prefix + QStringLiteral("_y"), y)
			|| !directionFromString(
				e.attribute(prefix + QStringLiteral("_direction")), direction)) {
			return false;
		}
		end.point = QPointF(x, y);
		end.direction = direction;
		return true;
	}

	void writeEnd(QDomElement &e, const QString &prefix, const PipeLine::End &end)
	{
		e.setAttribute(prefix + QStringLiteral("_x"), QString::number(end.point.x()));
		e.setAttribute(prefix + QStringLiteral("_y"), QString::number(end.point.y()));
		e.setAttribute(prefix + QStringLiteral("_direction"),
					   directionToString(end.direction));
	}

	ConductorRouter::Direction routerDirection(Qet::Orientation o)
	{
		switch (o) {
			case Qet::North: return ConductorRouter::Direction::North;
			case Qet::East:  return ConductorRouter::Direction::East;
			case Qet::South: return ConductorRouter::Direction::South;
			case Qet::West:  return ConductorRouter::Direction::West;
		}
		return ConductorRouter::Direction::North;
	}
}

/**
	@brief PipeLine::PipeLine
	@param start the first end
	@param end the second end
	@param parent the parent item, if any
*/
PipeLine::PipeLine(const End &start, const End &end, QGraphicsItem *parent) :
	QGraphicsObject(parent),
	m_start(start),
	m_end(end),
	m_uuid(QUuid::createUuid())
{
		//Above the symbols, as the conductors are
	setZValue(11);
	setFlags(QGraphicsItem::ItemIsSelectable);
	updatePath();
}

/**
	@brief PipeLine::setProperties
	Set the properties of this pipe line. Nothing is passed on to any other
	pipe line, whatever it is connected to.
	@param properties the new properties
*/
void PipeLine::setProperties(const PipeProperties &properties)
{
	if (m_properties == properties) return;
	m_properties = properties;
	update();
	emit propertiesChange();
}

/**
	@brief PipeLine::setEnds
	Move the ends of this pipe line and draw the path again.
	@param start the first end
	@param end the second end
*/
void PipeLine::setEnds(const End &start, const End &end)
{
	m_start = start;
	m_end = end;
	updatePath();
}

/**
	@brief PipeLine::toXml
	Write this pipe line as a <pipe> element: its identifier, its two ends
	and its properties, all as attributes. Nothing else goes in: the path
	is worked out again from the ends when the element is read, so a change
	in the routing never makes a file out of date.
	@param document the document the element is created in
	@return the element
*/
QDomElement PipeLine::toXml(QDomDocument &document) const
{
	QDomElement element = document.createElement(xml_tag);
	if (m_persist_uuid) {
		element.setAttribute(QStringLiteral("uuid"), m_uuid.toString());
	}
	writeEnd(element, QStringLiteral("start"), m_start);
	writeEnd(element, QStringLiteral("end"), m_end);
	m_properties.toXml(element);
	return element;
}

/**
	@brief PipeLine::valideXml
	@param element the element to check
	@return true if it is a <pipe> with both ends readable. The identifier
	and the properties are optional.
*/
bool PipeLine::valideXml(const QDomElement &element)
{
	if (element.tagName() != xml_tag) return false;
	End start, end;
	return readEnd(element, QStringLiteral("start"), start)
		&& readEnd(element, QStringLiteral("end"), end);
}

/**
	@brief PipeLine::fromXml
	Read a <pipe> element. An attribute this class does not know is
	ignored. A missing or malformed identifier is replaced by a new one
	which is not written back by toXml().
	@param element the element to read
	@return true if it was read; false, with this pipe line unchanged,
	if valideXml() refuses it
*/
bool PipeLine::fromXml(const QDomElement &element)
{
	if (!valideXml(element)) return false;

	End start, end;
	readEnd(element, QStringLiteral("start"), start);
	readEnd(element, QStringLiteral("end"), end);

	const QUuid uuid(element.attribute(QStringLiteral("uuid")));
	m_persist_uuid = !uuid.isNull();
	m_uuid = uuid.isNull() ? QUuid::createUuid() : uuid;

	PipeProperties properties;
	properties.fromXml(element);
	setProperties(properties);
	setEnds(start, end);
	return true;
}

/**
	@brief PipeLine::updatePath
	Work out the path between the two ends. The routing is the one of the
	conductors (ConductorRouter), without obstacles. Despite its name it is
	plain geometry -- horizontal and vertical segments between two points,
	each leaving in a direction -- and knows no wire, terminal or electrical
	rule. This is the only place where PipeLine depends on it. When it finds no path,
	for example when the two ends are the same point, the path is made of
	one corner at most.
*/
void PipeLine::updatePath()
{
	ConductorRouter::Request request;
	request.start = m_start.point;
	request.start_direction = routerDirection(m_start.direction);
	request.end = m_end.point;
	request.end_direction = routerDirection(m_end.direction);

	QList<QPointF> points = ConductorRouter::route(request).points;
	if (points.size() < 2) {
		points.clear();
		points << m_start.point;
		if (m_start.point.x() != m_end.point.x()
			&& m_start.point.y() != m_end.point.y()) {
			points << QPointF(m_end.point.x(), m_start.point.y());
		}
		points << m_end.point;
	}

	QPainterPath path;
	path.moveTo(points.first());
	for (int i = 1; i < points.size(); ++i) {
		path.lineTo(points.at(i));
	}

	prepareGeometryChange();
	m_points = points;
	m_path = path;
	update();
}

/**
	@brief PipeLine::boundingRect
	@return the area drawn by this pipe line
*/
QRectF PipeLine::boundingRect() const
{
	const qreal margin = pen_width + 2;
	return m_path.boundingRect().adjusted(-margin, -margin, margin, margin);
}

/**
	@brief PipeLine::shape
	@return the area where a click selects this pipe line
*/
QPainterPath PipeLine::shape() const
{
	QPainterPathStroker stroker;
	stroker.setWidth(click_width);
	return stroker.createStroke(m_path);
}

/**
	@brief PipeLine::paint
	Draw the pipe line without antialiasing, red when it is selected.
*/
void PipeLine::paint(QPainter *painter,
					 const QStyleOptionGraphicsItem *option,
					 QWidget *widget)
{
	Q_UNUSED(widget)
	Q_UNUSED(option)
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, false);

	QPen pen(isSelected() ? QColor(Qt::red) : QColor(Qt::black));
	pen.setWidthF(pen_width);
	pen.setJoinStyle(Qt::MiterJoin);
	pen.setCapStyle(Qt::SquareCap);
	painter->setPen(pen);
	painter->setBrush(Qt::NoBrush);
	painter->drawPath(m_path);

	painter->restore();
}
