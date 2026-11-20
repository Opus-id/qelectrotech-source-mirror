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
#include "leaderline.h"

#include <QLineF>
#include <QtMath>

#include <limits>

QString LeaderLine::toString(End end)
{
	switch (end) {
		case End::Dot:   return QStringLiteral("dot");
		case End::Arrow: return QStringLiteral("arrow");
		case End::None:  break;
	}
	return QStringLiteral("none");
}

LeaderLine::End LeaderLine::fromString(const QString &string)
{
	if (string == QLatin1String("dot"))   return End::Dot;
	if (string == QLatin1String("arrow")) return End::Arrow;
	return End::None;
}

qreal LeaderLine::distanceToLine(const QPointF &point, const QList<QPointF> &points,
								 QPointF *nearest)
{
	qreal best = std::numeric_limits<qreal>::max();
	QPointF best_point = points.isEmpty() ? point : points.first();
	for (int i = 0 ; i + 1 < points.size() ; ++i) {
		const QPointF a = points.at(i);
		const QPointF along = points.at(i + 1) - a;
		const qreal length2 = QPointF::dotProduct(along, along);
		const qreal t = length2 > 0
				? qBound(0.0, QPointF::dotProduct(point - a, along) / length2, 1.0)
				: 0.0;
		const QPointF on = a + t * along;
		const qreal distance = QLineF(point, on).length();
		if (distance < best) {
			best = distance;
			best_point = on;
		}
	}
	if (points.size() == 1) {
		best = QLineF(point, best_point).length();
	}
	if (nearest) {
		*nearest = best_point;
	}
	return best;
}

QPainterPath LeaderLine::path(const QRectF &text, const QList<QPointF> &points,
							  qreal from, End end, qreal size)
{
	QPainterPath leader;
	if (from <= 0 || points.isEmpty() || !text.isValid()) {
		return leader;
	}
		//The nearest points of the text and of the conductor, found in turn:
		//from the centre of the text to the conductor, then from the point of
		//the text nearest to it, twice, which is enough for straight segments
	auto clamped = [&text](const QPointF &point) {
		return QPointF(qBound(text.left(), point.x(), text.right()),
					   qBound(text.top(), point.y(), text.bottom()));
	};
	QPointF on_line;
	distanceToLine(text.center(), points, &on_line);
	QPointF on_text = clamped(on_line);
	for (int i = 0 ; i < 2 ; ++i) {
		distanceToLine(on_text, points, &on_line);
		on_text = clamped(on_line);
	}
	const QLineF line(on_text, on_line);
	if (line.length() <= from) {
		return leader;
	}

	leader.moveTo(on_text);
	leader.lineTo(on_line);
	if (end == End::Dot) {
		leader.addEllipse(on_line, size / 2, size / 2);
	} else if (end == End::Arrow) {
			//Two strokes back from the conductor, either side of the line
		QLineF back(on_line, on_text);
		back.setLength(size * 1.5);
		QLineF left = back;
		left.setAngle(back.angle() + 25);
		QLineF right = back;
		right.setAngle(back.angle() - 25);
		leader.moveTo(left.p2());
		leader.lineTo(on_line);
		leader.lineTo(right.p2());
	}
	return leader;
}
