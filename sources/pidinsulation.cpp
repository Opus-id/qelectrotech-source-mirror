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

namespace {
		/// @p points moved by @p distance along the side @p side gives each
		/// of its steps (a unit vector across it), each corner at the
		/// meeting of its two sides
	template <typename Side>
	QList<QPointF> moved(const QList<QPointF> &points, qreal distance, Side side)
	{
		const QList<QPointF> path = cleaned(points);
		QList<QPointF> result;
		if (path.size() < 2) {
			return result;
		}
		auto shifted = [distance, &side](const QLineF &line) {
			return line.translated(side(direction(line)) * distance);
		};
		QLineF previous = shifted(QLineF(path.at(0), path.at(1)));
		result << previous.p1();
		for (int i = 1 ; i + 1 < path.size() ; ++i)
		{
			const QLineF next = shifted(QLineF(path.at(i), path.at(i + 1)));
			QPointF corner;
			if (previous.intersects(next, &corner) == QLineF::NoIntersection) {
					//Going on straight, or back: the same point
				corner = next.p1();
			}
			result << corner;
			previous = next;
		}
		result << previous.p2();
		return result;
	}
}

QList<QPointF> PidInsulation::beside(const QList<QPointF> &points, qreal distance)
{
		//Across a step (dx, dy), y going down on a folio: (-dy, dx) or
		//(dy, -dx), whichever goes down, or right on a vertical step;
		//on a slant, the one going down
	return moved(points, distance, [](const QPointF &d) {
		const QPointF across(-d.y(), d.x());
		const bool flip = qAbs(across.y()) > 1e-6 ? across.y() < 0 : across.x() < 0;
		return flip ? -across : across;
	});
}

QList<QPointF> PidInsulation::offset(const QList<QPointF> &points, qreal distance)
{
		//The left of a step (dx, dy), y going down on a folio: (dy, -dx)
	return moved(points, distance, [](const QPointF &d) {
		return QPointF(d.y(), -d.x());
	});
}

QPainterPath PidInsulation::band(const QList<QPointF> &points, bool jacketed)
{
	QPainterPath path;
	const QList<QPointF> axis = cleaned(points);
	if (axis.size() < 2) {
		return path;
	}
	const qreal half_width = jacketed ? jacketed_half_width : PidInsulation::half_width;
	for (const qreal side : {half_width, -half_width}) {
		const QList<QPointF> line = offset(axis, side);
		path.moveTo(line.first());
		for (int i = 1 ; i < line.size() ; ++i) {
			path.lineTo(line.at(i));
		}
	}
		//The hatches, at 45°, each as far along as across: none within
		//that much of a corner or an end, where it would stick out. Across
		//the whole band; around a jacket (choice of the user, 25/11/2026),
		//only within the ring between the jacket and the band, on both
		//sides, neither over the jacket nor over the pipe: shorter, so they
		//go from the start of the stub to its end
	const qreal inner = jacketed ? PidInsulation::half_width : -half_width;
	const qreal lean = (half_width - inner) / 2;
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
			path.moveTo(c + left * inner - d * lean);
			path.lineTo(c + left * half_width + d * lean);
			if (jacketed) {
					//The same slant in the ring on the other side
				path.moveTo(c - left * half_width - d * lean);
				path.lineTo(c - left * inner + d * lean);
			}
		}
	}
	return path;
}

QPainterPath PidInsulation::tracing(const QList<QPointF> &points, bool insulated, bool jacketed)
{
	QPainterPath path;
	const qreal distance = jacketed ? jacketed_tracing : insulated ? half_width / 2 : half_width;
	const QList<QPointF> line = beside(points, distance);
	if (line.size() < 2) {
		return path;
	}
	path.moveTo(line.first());
	for (int i = 1 ; i < line.size() ; ++i) {
		path.lineTo(line.at(i));
	}
	return path;
}

QPainterPath PidInsulation::jacket(const QList<QPointF> &points)
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
	return path;
}
