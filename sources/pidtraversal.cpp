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
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pidtraversal.h"

#include <QLineF>
#include <QtMath>

#include <algorithm>

namespace {
		//0 is never current, so a fresh cache is always rebuilt
	quint64 s_symbols_generation = 1;

		//Stretches closer than this are one; a stretch closer than this to
		//an end of the line touches it
	constexpr qreal join = 0.5;

	qreal cross(const QPointF &a, const QPointF &b)
	{
		return a.x() * b.y() - a.y() * b.x();
	}
	qreal dot(const QPointF &a, const QPointF &b)
	{
		return a.x() * b.x() + a.y() * b.y();
	}

		//A closed shape of @p path (an ellipse, a whole circle) as polygons
		//on the folio: an area and its edge
	void addClosed(PidTraversal::Outline &outline, const QPainterPath &path,
				   const QTransform &to_folio)
	{
		for (const QPolygonF &polygon : path.toSubpathPolygons(to_folio)) {
			outline.areas << polygon;
			outline.strokes << polygon;
		}
	}

		//The point at @p at along the segment @p a @p b, which begins at
		//@p start along its line and is @p length long
	QPointF pointAt(const QPointF &a, const QPointF &b, qreal start, qreal length, qreal at)
	{
		if (length <= 0) {
			return a;
		}
		return a + (b - a) * ((at - start) / length);
	}
}

void PidTraversal::symbolsChanged()
{
	++s_symbols_generation;
}

quint64 PidTraversal::symbolsGeneration()
{
	return s_symbols_generation;
}

/**
	@brief PidTraversal::outline
	See the declaration. The rounded corners of a rectangle are taken
	square: a line which only cuts one of them is rare enough.
*/
PidTraversal::Outline PidTraversal::outline(const ElementPictureFactory::primitives &drawing,
											const QTransform &to_folio)
{
	Outline outline;
	for (const QLineF &line : drawing.m_lines) {
		outline.strokes << QPolygonF({to_folio.map(line.p1()), to_folio.map(line.p2())});
	}
	for (const QRectF &rect : drawing.m_rectangles) {
		const QPolygonF polygon = to_folio.map(QPolygonF(rect));
		outline.areas << polygon;
		outline.strokes << polygon;
	}
	for (const QRectF &rect : drawing.m_circles) {
		QPainterPath path;
		path.addEllipse(rect);
		addClosed(outline, path, to_folio);
	}
	for (const QVector<QPointF> &points : drawing.m_polygons)
	{
		if (points.size() < 2) {
			continue;
		}
		const QPolygonF polygon = to_folio.map(QPolygonF(points));
			//A closed polygon has its first point again at its end
			//(ElementPictureFactory::parsePolygon())
		if (points.size() > 3 && points.first() == points.last()) {
			outline.areas << polygon;
		}
		outline.strokes << polygon;
	}
	for (const QVector<qreal> &arc : drawing.m_arcs)
	{
		if (arc.size() < 6) {
			continue;
		}
		const QRectF rect(arc.at(0), arc.at(1), arc.at(2), arc.at(3));
		QPainterPath path;
		if (qAbs(arc.at(5)) >= 360) {
			path.addEllipse(rect);
			addClosed(outline, path, to_folio);
		} else {
			path.arcMoveTo(rect, arc.at(4));
			path.arcTo(rect, arc.at(4), arc.at(5));
			outline.strokes << path.toSubpathPolygons(to_folio);
		}
	}
	for (const QPolygonF &stroke : std::as_const(outline.strokes)) {
		outline.rect = outline.rect.united(stroke.boundingRect());
	}
	return outline;
}

qreal PidTraversal::length(const QVector<QPointF> &line)
{
	qreal total = 0;
	for (int i = 1 ; i < line.size() ; ++i) {
		total += QLineF(line.at(i - 1), line.at(i)).length();
	}
	return total;
}

/**
	@brief PidTraversal::traversals
	See the declaration
*/
QList<PidTraversal::Span> PidTraversal::traversals(const QVector<QPointF> &line,
												   const Outline &outline)
{
	QList<Span> hits;
	qreal at = 0;
	for (int k = 1 ; k < line.size() ; ++k)
	{
		const QPointF a = line.at(k - 1);
		const QPointF b = line.at(k);
		const QPointF d = b - a;
		const qreal len = QLineF(a, b).length();
		if (len < 1e-9) {
			continue;
		}
			//Where the segment meets the lines of the symbol, as a fraction
			//of it: the stretches between are wholly inside a closed shape
			//or wholly outside
		QList<qreal> cuts({0.0, 1.0});
		for (const QPolygonF &stroke : outline.strokes)
		{
			for (int i = 1 ; i < stroke.size() ; ++i)
			{
				const QPointF c = stroke.at(i - 1);
				const QPointF e = stroke.at(i) - c;
				const qreal e_len = qSqrt(dot(e, e));
				if (e_len < 1e-9) {
					continue;
				}
				const qreal denom = cross(d, e);
				if (qAbs(denom) < 1e-9 * len * e_len)
				{
						//Parallel: along it when close enough
					if (qAbs(cross(c - a, d)) / len >= join) {
						continue;
					}
					const qreal t0 = dot(c - a, d) / (len * len);
					const qreal t1 = dot(c + e - a, d) / (len * len);
					const qreal lo = qMax(0.0, qMin(t0, t1));
					const qreal hi = qMin(1.0, qMax(t0, t1));
					if (lo <= hi) {
						hits << Span{at + lo * len, at + hi * len};
						cuts << lo << hi;
					}
					continue;
				}
				const qreal t = cross(c - a, e) / denom;
				const qreal u = cross(c - a, d) / denom;
				constexpr qreal eps = 1e-9;
				if (t >= -eps && t <= 1 + eps && u >= -eps && u <= 1 + eps) {
					const qreal on = qBound(0.0, t, 1.0);
					hits << Span{at + on * len, at + on * len};
					cuts << on;
				}
			}
		}
		std::sort(cuts.begin(), cuts.end());
		for (int i = 1 ; i < cuts.size() ; ++i)
		{
			const qreal t0 = cuts.at(i - 1);
			const qreal t1 = cuts.at(i);
			if ((t1 - t0) * len < 1e-6) {
				continue;
			}
			const QPointF middle = a + d * ((t0 + t1) / 2);
			for (const QPolygonF &area : outline.areas) {
				if (area.containsPoint(middle, Qt::OddEvenFill)) {
					hits << Span{at + t0 * len, at + t1 * len};
					break;
				}
			}
		}
		at += len;
	}

	std::sort(hits.begin(), hits.end(), [](const Span &x, const Span &y) {
		return x.from < y.from;
	});
	QList<Span> joined;
	for (const Span &hit : std::as_const(hits))
	{
		if (!joined.isEmpty() && hit.from <= joined.last().to + join) {
			joined.last().to = qMax(joined.last().to, hit.to);
		} else {
			joined << hit;
		}
	}
		//What touches an end of the line: its nozzles, or a basin it ends in
	QList<Span> kept;
	for (const Span &span : std::as_const(joined)) {
		if (span.from > join && span.to < at - join) {
			kept << span;
		}
	}
	return kept;
}

/**
	@brief PidTraversal::pathAlong
	See the declaration
*/
QPainterPath PidTraversal::pathAlong(const QVector<QPointF> &line, const QList<Span> &spans,
									 qreal min_length)
{
	QPainterPath path;
	const qreal total = length(line);
	for (const Span &span : spans)
	{
		qreal from = span.from;
		qreal to = span.to;
		if (to - from < min_length) {
			const qreal middle = (from + to) / 2;
			from = middle - min_length / 2;
			to = middle + min_length / 2;
		}
		from = qMax(0.0, from);
		to = qMin(total, to);
		bool started = false;
		qreal at = 0;
		for (int k = 1 ; k < line.size() ; ++k)
		{
			const QPointF a = line.at(k - 1);
			const QPointF b = line.at(k);
			const qreal len = QLineF(a, b).length();
			const qreal start = at;
			at += len;
			if (at < from) {
				continue;
			}
			if (start > to) {
				break;
			}
			if (!started) {
				path.moveTo(pointAt(a, b, start, len, qMax(from, start)));
				started = true;
			}
			path.lineTo(pointAt(a, b, start, len, qMin(to, at)));
		}
	}
	return path;
}
