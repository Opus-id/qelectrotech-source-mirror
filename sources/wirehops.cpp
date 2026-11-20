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
#include "wirehops.h"

#include <QtMath>
#include <algorithm>

namespace {
		//Coordinates closer than this are the same; points are on a grid
	constexpr qreal tolerance = 0.01;

	bool isHorizontal(const QPointF &a, const QPointF &b) {
		return qAbs(a.y() - b.y()) < tolerance && qAbs(a.x() - b.x()) >= tolerance;
	}

	bool isVertical(const QPointF &a, const QPointF &b) {
		return qAbs(a.x() - b.x()) < tolerance && qAbs(a.y() - b.y()) >= tolerance;
	}

		//True if @a value is strictly between @a a and @a b, by more than @a margin
	bool strictlyBetween(qreal value, qreal a, qreal b, qreal margin) {
		return value > qMin(a, b) + margin && value < qMax(a, b) - margin;
	}

		//The hops of @a hops lying on the segment @a a - @a b, ordered from @a a
	QList<QPointF> hopsOnSegment(const QPointF &a, const QPointF &b,
								 const QList<QPointF> &hops, bool horizontal)
	{
		QList<QPointF> on_segment;
		for (const QPointF &hop : hops)
		{
			if (horizontal
				? (qAbs(hop.y() - a.y()) < tolerance && strictlyBetween(hop.x(), a.x(), b.x(), 0))
				: (qAbs(hop.x() - a.x()) < tolerance && strictlyBetween(hop.y(), a.y(), b.y(), 0)))
			{
				on_segment.append(hop);
			}
		}
		std::sort(on_segment.begin(), on_segment.end(), [&a](const QPointF &p, const QPointF &q) {
			return QLineF(a, p).length() < QLineF(a, q).length();
		});
		return on_segment;
	}

		//@a points without repeated points nor points where the wire goes
		//straight on: a conductor is drawn in segments which may be empty
		//or follow one another on one line, and a crossing on the joint of
		//two of them is still inside the straight run they make
	QVector<QPointF> straightRuns(const QVector<QPointF> &points)
	{
		QVector<QPointF> runs;
		for (const QPointF &p : points)
		{
			if (!runs.isEmpty() && QLineF(runs.constLast(), p).length() < tolerance) {
				continue;
			}
			if (runs.size() >= 2)
			{
				const QPointF &a = runs.at(runs.size() - 2), &b = runs.constLast();
				const bool along_x = qAbs(a.y() - b.y()) < tolerance && qAbs(b.y() - p.y()) < tolerance
						&& (b.x() - a.x()) * (p.x() - b.x()) > 0;
				const bool along_y = qAbs(a.x() - b.x()) < tolerance && qAbs(b.x() - p.x()) < tolerance
						&& (b.y() - a.y()) * (p.y() - b.y()) > 0;
				if (along_x || along_y) {
					runs.last() = p;
					continue;
				}
			}
			runs.append(p);
		}
		return runs;
	}
}

/**
	@brief WireHops::toString
	@return the value written in the project file for @a mode
*/
QString WireHops::toString(Mode mode)
{
	switch (mode) {
		case Mode::Horizontal: return QStringLiteral("horizontal");
		case Mode::Vertical:   return QStringLiteral("vertical");
		case Mode::Thinner:    return QStringLiteral("thinner");
		case Mode::None:       break;
	}
	return QStringLiteral("none");
}

/**
	@brief WireHops::fromString
	@return the mode written as @a string in a project file,
	Mode::None for anything else, so an unknown value draws no hop.
*/
WireHops::Mode WireHops::fromString(const QString &string)
{
	if (string == QLatin1String("horizontal")) return Mode::Horizontal;
	if (string == QLatin1String("vertical"))   return Mode::Vertical;
	if (string == QLatin1String("thinner"))    return Mode::Thinner;
	return Mode::None;
}

/**
	@brief WireHops::toString
	@return the value written in the project file for @a shape
*/
QString WireHops::toString(Shape shape)
{
	return shape == Shape::Gap ? QStringLiteral("gap") : QStringLiteral("arc");
}

/**
	@brief WireHops::shapeFromString
	@return the shape written as @a string in a project file,
	Shape::Arc for anything else
*/
WireHops::Shape WireHops::shapeFromString(const QString &string)
{
	return string == QLatin1String("gap") ? Shape::Gap : Shape::Arc;
}

/**
	@brief WireHops::crossings
	@param wire : the points of the wire, in scene coordinates
	@param others : the points of every other wire that may cross it,
	in scene coordinates
	@param mode : which wire of a crossing hops
	@return the points where @a wire hops, in the order of @a wire.

	A crossing is kept only if it lies strictly inside the segment of the
	other wire (a wire ending or bending on this one is a junction, not a
	crossing) and at least one hop radius from the ends of this wire's
	segment, so the arc fits. Of two crossings closer than one arc
	width, only the first hops.
*/
QList<QPointF> WireHops::crossings(const QVector<QPointF> &wire,
								   const QList<QVector<QPointF>> &others,
								   Mode mode)
{
	QList<QPointF> hops;
	if (mode == Mode::None || wire.size() < 2) {
		return hops;
	}

	const bool horizontal = mode == Mode::Horizontal;
	for (int i = 0 ; i < wire.size() - 1 ; ++i)
	{
		const QPointF a = wire.at(i);
		const QPointF b = wire.at(i + 1);
		if (horizontal ? !isHorizontal(a, b) : !isVertical(a, b)) {
			continue;
		}

		QList<QPointF> on_segment;
		for (const auto &other : others)
		{
			for (int j = 0 ; j < other.size() - 1 ; ++j)
			{
				const QPointF c = other.at(j);
				const QPointF d = other.at(j + 1);
				if (horizontal ? !isVertical(c, d) : !isHorizontal(c, d)) {
					continue;
				}

				const QPointF crossing = horizontal ? QPointF(c.x(), a.y())
													: QPointF(a.x(), c.y());
				const bool inside_wire = horizontal
						? strictlyBetween(crossing.x(), a.x(), b.x(), radius)
						: strictlyBetween(crossing.y(), a.y(), b.y(), radius);
				const bool inside_other = horizontal
						? strictlyBetween(crossing.y(), c.y(), d.y(), tolerance)
						: strictlyBetween(crossing.x(), c.x(), d.x(), tolerance);
				if (inside_wire && inside_other) {
					on_segment.append(crossing);
				}
			}
		}

		std::sort(on_segment.begin(), on_segment.end(), [&a](const QPointF &p, const QPointF &q) {
			return QLineF(a, p).length() < QLineF(a, q).length();
		});
		for (const QPointF &crossing : std::as_const(on_segment))
		{
			if (!hops.isEmpty()
				&& QLineF(hops.last(), crossing).length() < 2 * radius + tolerance) {
				continue;
			}
			hops.append(crossing);
		}
	}
	return hops;
}

/**
	@brief WireHops::path
	@param wire : the points of the wire
	@param hops : the points where the wire hops, from WireHops::crossings,
	in the same coordinates as @a wire
	@param mode : which wire of a crossing hops
	@return the path of the wire, with a half circle at each hop: above a
	horizontal wire, right of a vertical one. With no hop, the same path
	as the plain wire.
*/
QPainterPath WireHops::path(const QVector<QPointF> &wire,
							const QList<QPointF> &hops,
							Mode mode)
{
	QPainterPath path;
	if (wire.isEmpty()) {
		return path;
	}

	const bool horizontal = mode == Mode::Horizontal;
	path.moveTo(wire.first());
	for (int i = 0 ; i < wire.size() - 1 ; ++i)
	{
		const QPointF a = wire.at(i);
		const QPointF b = wire.at(i + 1);
		const bool hops_here = mode != Mode::None
				&& (horizontal ? isHorizontal(a, b) : isVertical(a, b));

		if (hops_here)
		{
				//1 when the wire runs towards +x (or +y), -1 otherwise
			const qreal direction = horizontal ? (b.x() > a.x() ? 1 : -1)
											   : (b.y() > a.y() ? 1 : -1);
			for (const QPointF &hop : hopsOnSegment(a, b, hops, horizontal))
			{
				const QRectF circle(hop.x() - radius, hop.y() - radius,
									2 * radius, 2 * radius);
				if (horizontal) {
						//Over the top: from the left end to the right one, or back
					path.lineTo(hop.x() - direction * radius, hop.y());
					path.arcTo(circle, direction > 0 ? 180 : 0, direction > 0 ? -180 : 180);
				} else {
						//On the right: from the top end to the bottom one, or back
					path.lineTo(hop.x(), hop.y() - direction * radius);
					path.arcTo(circle, direction > 0 ? 90 : 270, direction > 0 ? -180 : 180);
				}
			}
		}
		path.lineTo(b);
	}
	return path;
}

/**
	@brief WireHops::interruptions
	@param wire : the wire, its points in scene coordinates
	@param others : every other wire that may cross it, in scene coordinates
	@param mode : which wire of a crossing gives way; with Mode::Thinner,
	this one when its line is thinner than the other's, or as thick and
	vertical there
	@param shape : an arc or a gap
	@param cap_extension : how far the pen of @a wire draws past the end of
	a line (half its width for a square cap, 0 for a flat one), so that
	what is seen of a gap is what gapClearance says
	@return where @a wire gives way, in the order of @a wire. A crossing is
	one as for crossings(): strictly inside the segment of the other wire,
	and with room for the arc or the gap on this wire's segment. A gap
	leaves gapClearance on each side of the other line; an arc clears it
	by half as much, never smaller than the hop of crossings(). Two
	crossings whose arcs or gaps overlap make one, which spans both.
*/
QList<WireHops::Interruption> WireHops::interruptions(const Line &given_wire, const QList<Line> &given_others,
													  Mode mode, Shape shape, qreal cap_extension)
{
	QList<Interruption> result;
	const Line wire{straightRuns(given_wire.points), given_wire.width};
	if (mode == Mode::None || wire.points.size() < 2) {
		return result;
	}
	QList<Line> others;
	for (const Line &other : given_others) {
		others.append({straightRuns(other.points), other.width});
	}

	for (int i = 0 ; i < wire.points.size() - 1 ; ++i)
	{
		const QPointF a = wire.points.at(i);
		const QPointF b = wire.points.at(i + 1);
		const bool horizontal = isHorizontal(a, b);
		if ((!horizontal && !isVertical(a, b))
			|| (mode == Mode::Horizontal && !horizontal)
			|| (mode == Mode::Vertical && horizontal)) {
			continue;
		}
		const qreal a_along = horizontal ? a.x() : a.y();
		const qreal b_along = horizontal ? b.x() : b.y();
		const qreal across = horizontal ? a.y() : a.x();

			//What this segment leaves undrawn, as spans along it
		QList<QPair<qreal, qreal>> spans;
		for (const Line &other : others)
		{
			const qreal other_width = other.width;
			if (mode == Mode::Thinner
				&& !(wire.width < other_width - tolerance
					 || (qAbs(wire.width - other_width) <= tolerance && !horizontal))) {
				continue;
			}
			const qreal reach = shape == Shape::Gap
					? other_width / 2 + gapClearance + cap_extension
					: qMax(radius, other_width / 2 + gapClearance / 2 + wire.width / 2);
			for (int j = 0 ; j < other.points.size() - 1 ; ++j)
			{
				const QPointF c = other.points.at(j);
				const QPointF d = other.points.at(j + 1);
				if (horizontal ? !isVertical(c, d) : !isHorizontal(c, d)) {
					continue;
				}
				const qreal along = horizontal ? c.x() : c.y();
				const bool inside_other = horizontal
						? strictlyBetween(across, c.y(), d.y(), tolerance)
						: strictlyBetween(across, c.x(), d.x(), tolerance);
				if (inside_other && strictlyBetween(along, a_along, b_along, reach)) {
					spans.append({along - reach, along + reach});
				}
			}
		}
		if (spans.isEmpty()) {
			continue;
		}

		std::sort(spans.begin(), spans.end());
		QList<QPair<qreal, qreal>> merged{spans.first()};
		for (int k = 1 ; k < spans.size() ; ++k) {
			if (spans.at(k).first <= merged.last().second + tolerance) {
				merged.last().second = qMax(merged.last().second, spans.at(k).second);
			} else {
				merged.append(spans.at(k));
			}
		}
		if (b_along < a_along) {
			std::reverse(merged.begin(), merged.end());
		}
		const auto point = [&](qreal along) {
			return horizontal ? QPointF(along, across) : QPointF(across, along);
		};
		for (const auto &span : std::as_const(merged)) {
			Interruption cut;
			cut.from = point(b_along > a_along ? span.first : span.second);
			cut.to = point(b_along > a_along ? span.second : span.first);
			cut.arc = shape == Shape::Arc;
			result.append(cut);
		}
	}
	return result;
}

/**
	@brief WireHops::crossingPoints
	@return the points where @a wire crosses one of @a others, both lines
	going on past it (strictly inside a segment of each), whichever gives
	way there: what a mark drawn along @a wire keeps clear of
*/
QList<QPointF> WireHops::crossingPoints(const QVector<QPointF> &given_wire,
										const QList<QVector<QPointF>> &given_others)
{
	QList<QPointF> points;
	const QVector<QPointF> wire = straightRuns(given_wire);
	QList<QVector<QPointF>> others;
	for (const auto &other : given_others) {
		others.append(straightRuns(other));
	}
	for (int i = 0 ; i < wire.size() - 1 ; ++i)
	{
		const QPointF a = wire.at(i);
		const QPointF b = wire.at(i + 1);
		const bool horizontal = isHorizontal(a, b);
		if (!horizontal && !isVertical(a, b)) {
			continue;
		}
		for (const auto &other : others) {
			for (int j = 0 ; j < other.size() - 1 ; ++j)
			{
				const QPointF c = other.at(j);
				const QPointF d = other.at(j + 1);
				if (horizontal ? !isVertical(c, d) : !isHorizontal(c, d)) {
					continue;
				}
				const QPointF crossing = horizontal ? QPointF(c.x(), a.y()) : QPointF(a.x(), c.y());
				const bool inside_wire = horizontal
						? strictlyBetween(crossing.x(), a.x(), b.x(), tolerance)
						: strictlyBetween(crossing.y(), a.y(), b.y(), tolerance);
				const bool inside_other = horizontal
						? strictlyBetween(crossing.y(), c.y(), d.y(), tolerance)
						: strictlyBetween(crossing.x(), c.x(), d.x(), tolerance);
				if (inside_wire && inside_other) {
					points.append(crossing);
				}
			}
		}
	}
	return points;
}

/**
	@brief WireHops::solidAtCrossings
	@param wire : the points of a wire drawn dashed
	@param interruptions : where it gives way, from interruptions()
	@param crossings : every point where it crosses another wire, from
	crossingPoints()
	@param dash : the length of one dash of its pattern
	@return what is drawn solid over the dashes, so that the wire crosses
	on a dash, never on a blank (ISO 128-20): a dash on each side of each
	interruption (and its arc), and a dash over each crossing where it
	goes on, of @a dash or more so that it runs past the other line. Kept
	on the straight run it lies on. Empty with no crossing.
*/
QPainterPath WireHops::solidAtCrossings(const QVector<QPointF> &given_wire,
										const QList<Interruption> &interruptions,
										const QList<QPointF> &crossings, qreal dash)
{
	QPainterPath solid;
	const QVector<QPointF> wire = straightRuns(given_wire);
	if (wire.size() < 2 || dash <= 0 || (interruptions.isEmpty() && crossings.isEmpty())) {
		return solid;
	}
		//The run @a p lies on: its ends
	auto runOf = [&](const QPointF &p, QPointF *a, QPointF *b) {
		for (int i = 0 ; i + 1 < wire.size() ; ++i) {
			const QPointF s = wire.at(i), e = wire.at(i + 1);
			const bool on = isHorizontal(s, e)
					? qAbs(p.y() - s.y()) < tolerance && p.x() >= qMin(s.x(), e.x()) - tolerance
					  && p.x() <= qMax(s.x(), e.x()) + tolerance
					: isVertical(s, e) && qAbs(p.x() - s.x()) < tolerance
					  && p.y() >= qMin(s.y(), e.y()) - tolerance && p.y() <= qMax(s.y(), e.y()) + tolerance;
			if (on) {
				*a = s;
				*b = e;
				return true;
			}
		}
		return false;
	};
		//@a p moved by @a length towards @a to, no further than @a to
	auto towards = [](const QPointF &p, const QPointF &to, qreal length) {
		const qreal room = QLineF(p, to).length();
		return room <= length ? to : p + (to - p) * (length / room);
	};

	for (const Interruption &cut : interruptions)
	{
		QPointF a, b;
		if (!runOf(cut.from, &a, &b)) {
			continue;
		}
			//a the end of the run on the side of from
		if (QLineF(a, cut.from).length() > QLineF(a, cut.to).length()) {
			std::swap(a, b);
		}
		const QVector<QPointF> piece{towards(cut.from, a, dash), towards(cut.to, b, dash)};
		solid.addPath(path(piece, {cut}));
	}
	for (const QPointF &crossing : crossings)
	{
		bool given_way = false;
		for (const Interruption &cut : interruptions) {
			const QRectF span = QRectF(cut.from, cut.to).normalized().adjusted(-tolerance, -tolerance,
																			   tolerance, tolerance);
			if (span.contains(crossing)) {
				given_way = true;
				break;
			}
		}
		QPointF a, b;
		if (given_way || !runOf(crossing, &a, &b)) {
			continue;
		}
			//Past a main pipe (1 mm) by 1 mm on each side at least
		const qreal half = qMax(dash, 3 * gapClearance) / 2;
		solid.moveTo(towards(crossing, a, half));
		solid.lineTo(towards(crossing, b, half));
	}
	return solid;
}

/**
	@brief WireHops::path
	@param wire : the points of the wire
	@param interruptions : where it is not drawn straight, from
	interruptions(), in the same coordinates as @a wire
	@return the path of the wire: an arc above a horizontal segment and
	right of a vertical one for an arc, nothing for a gap. With no
	interruption, the same path as the plain wire.
*/
QPainterPath WireHops::path(const QVector<QPointF> &given_wire,
							const QList<Interruption> &interruptions)
{
	QPainterPath path;
		//The same line, on the straight runs interruptions() worked on
	const QVector<QPointF> wire = interruptions.isEmpty() ? given_wire : straightRuns(given_wire);
	if (wire.isEmpty()) {
		return path;
	}
	path.moveTo(wire.first());
	for (int i = 0 ; i < wire.size() - 1 ; ++i)
	{
		const QPointF a = wire.at(i);
		const QPointF b = wire.at(i + 1);
		const bool horizontal = isHorizontal(a, b);
		const bool vertical = isVertical(a, b);
		if (horizontal || vertical)
		{
			const auto onSegment = [&](const QPointF &p) {
				return horizontal
						? qAbs(p.y() - a.y()) < tolerance && strictlyBetween(p.x(), a.x(), b.x(), 0)
						: qAbs(p.x() - a.x()) < tolerance && strictlyBetween(p.y(), a.y(), b.y(), 0);
			};
			QList<Interruption> here;
			for (const Interruption &cut : interruptions) {
				if (onSegment(cut.from) && onSegment(cut.to)) {
					here.append(cut);
				}
			}
			std::sort(here.begin(), here.end(), [&a](const Interruption &p, const Interruption &q) {
				return QLineF(a, p.from).length() < QLineF(a, q.from).length();
			});
			for (const Interruption &cut : std::as_const(here))
			{
				path.lineTo(cut.from);
				if (!cut.arc) {
					path.moveTo(cut.to);
					continue;
				}
				const QPointF centre = (cut.from + cut.to) / 2.0;
				const qreal half = QLineF(cut.from, cut.to).length() / 2.0;
					//Two crossings in one arc: no taller than a hop and a half
				const qreal height = qMin(half, 3 * radius);
				if (horizontal) {
					const QRectF ellipse(centre.x() - half, centre.y() - height, 2 * half, 2 * height);
					const bool forward = cut.to.x() > cut.from.x();
					path.arcTo(ellipse, forward ? 180 : 0, forward ? -180 : 180);
				} else {
					const QRectF ellipse(centre.x() - height, centre.y() - half, 2 * height, 2 * half);
					const bool forward = cut.to.y() > cut.from.y();
					path.arcTo(ellipse, forward ? 90 : 270, forward ? -180 : 180);
				}
			}
		}
		path.lineTo(b);
	}
	return path;
}
