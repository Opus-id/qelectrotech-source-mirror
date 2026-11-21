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
#include "pidinsulation.h"

#include <QLineF>
#include <QtMath>

namespace {
		/// The unit vector of @p line, none for a line of no length
	QPointF direction(const QLineF &line)
	{
		const qreal l = line.length();
		return l < 1e-9 ? QPointF() : (line.p2() - line.p1()) / l;
	}

		/// The path @p points without its steps of no length, and without
		/// the points between two steps going on in the same direction: a
		/// pipe often has such a point, and a straight run must stay one
		/// segment (its hatches are spread over each segment)
	QList<QPointF> cleaned(const QList<QPointF> &points)
	{
		QList<QPointF> kept;
		for (const QPointF &p : points)
		{
			if (!kept.isEmpty() && QLineF(kept.last(), p).length() <= 1e-6) {
				continue;
			}
			if (kept.size() >= 2) {
				const QPointF a = direction(QLineF(kept.at(kept.size() - 2), kept.last()));
				const QPointF b = direction(QLineF(kept.last(), p));
				const qreal cross = a.x() * b.y() - a.y() * b.x();
				const qreal dot = a.x() * b.x() + a.y() * b.y();
				if (qAbs(cross) < 1e-6 && dot > 0) {
					kept.last() = p;
					continue;
				}
			}
			kept << p;
		}
		return kept;
	}
}

qreal PidInsulation::length(const QList<QPointF> &points)
{
	qreal total = 0;
	for (int i = 0 ; i + 1 < points.size() ; ++i) {
		total += QLineF(points.at(i), points.at(i + 1)).length();
	}
	return total;
}

QList<QPointF> PidInsulation::stretch(const QList<QPointF> &points, qreal from, qreal to)
{
	QList<QPointF> part;
	if (to <= from || points.size() < 2) {
		return part;
	}
	qreal at = 0;
	for (int i = 0 ; i + 1 < points.size() ; ++i)
	{
		const QLineF segment(points.at(i), points.at(i + 1));
		const qreal l = segment.length();
		const qreal start = at, end = at + l;
		at = end;
		if (l < 1e-9 || end < from || start > to) {
			continue;
		}
		const qreal a = qMax(from, start) - start;
		const qreal b = qMin(to, end) - start;
		const QPointF first = segment.pointAt(a / l);
		if (part.isEmpty() || QLineF(part.last(), first).length() > 1e-6) {
			part << first;
		}
		part << segment.pointAt(b / l);
	}
	return cleaned(part);
}

QList<QPointF> PidInsulation::offset(const QList<QPointF> &points, qreal distance)
{
	const QList<QPointF> path = cleaned(points);
	QList<QPointF> moved;
	if (path.size() < 2) {
		return moved;
	}
		//The left of a step (dx, dy), y going down on a folio: (dy, -dx)
	auto shifted = [distance](const QLineF &line) {
		const QPointF d = direction(line);
		const QPointF left(d.y(), -d.x());
		return line.translated(left * distance);
	};
	QLineF previous = shifted(QLineF(path.at(0), path.at(1)));
	moved << previous.p1();
	for (int i = 1 ; i + 1 < path.size() ; ++i)
	{
		const QLineF next = shifted(QLineF(path.at(i), path.at(i + 1)));
		QPointF corner;
		if (previous.intersects(next, &corner) == QLineF::NoIntersection) {
				//Going on straight: the same point
			corner = next.p1();
		}
		moved << corner;
		previous = next;
	}
	moved << previous.p2();
	return moved;
}

QPainterPath PidInsulation::band(const QList<QPointF> &points)
{
	QPainterPath path;
	const QList<QPointF> axis = cleaned(points);
	if (axis.size() < 2) {
		return path;
	}
	for (const qreal side : {half_width, -half_width}) {
		const QList<QPointF> line = offset(axis, side);
		path.moveTo(line.first());
		for (int i = 1 ; i < line.size() ; ++i) {
			path.lineTo(line.at(i));
		}
	}
		//The hatches, at 45°, each as far along as across: none within
		//that much of a corner or an end, where it would stick out
	const qreal lean = half_width;
	for (int i = 0 ; i + 1 < axis.size() ; ++i)
	{
		const QLineF segment(axis.at(i), axis.at(i + 1));
		const qreal l = segment.length();
		const QPointF d = direction(segment);
		const QPointF left(d.y(), -d.x());
			//Centred on the segment
		const int count = int(qFloor((l - 2 * lean) / hatch_step)) + 1;
		if (count < 1) {
			continue;
		}
		const qreal first = (l - (count - 1) * hatch_step) / 2;
		for (int k = 0 ; k < count ; ++k)
		{
			const QPointF c = segment.p1() + d * (first + k * hatch_step);
			path.moveTo(c - left * half_width - d * lean);
			path.lineTo(c + left * half_width + d * lean);
		}
	}
	return path;
}

QPainterPath PidInsulation::tracing(const QList<QPointF> &points, bool insulated)
{
	QPainterPath path;
	const QList<QPointF> line = offset(points, insulated ? half_width / 2 : half_width);
	if (line.size() < 2) {
		return path;
	}
	path.moveTo(line.first());
	for (int i = 1 ; i < line.size() ; ++i) {
		path.lineTo(line.at(i));
	}
	return path;
}
