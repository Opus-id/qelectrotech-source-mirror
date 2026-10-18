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
