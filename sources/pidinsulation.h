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
#ifndef PID_INSULATION_H
#define PID_INSULATION_H

#include <QList>
#include <QPainterPath>
#include <QPointF>

/**
	The drawing of an insulated or heat traced part of a pipe (step 7d),
	along a path of straight segments, in drawing units (4 by mm): an
	insulation is a band of two lines either side of the pipe, hatched
	across at 45°; a heat tracing a dashed line beside the pipe, inside the
	band when the pipe is insulated too. Drawn as short stubs where the
	insulated part of a line begins and ends, and as a mark in the middle of
	a pipe which asks for one (Conductor::insulationPaths()).
*/
namespace PidInsulation
{
		/// Half the width of the band, from the axis of the pipe: 1.5 mm
	constexpr qreal half_width = 6.0;
		/// The distance between two hatches, along the pipe: 1.25 mm
	constexpr qreal hatch_step = 5.0;
		/// The dashes of a heat tracing: 2 mm drawn, 1 mm blank
	constexpr qreal dash = 8.0;
	constexpr qreal dash_gap = 4.0;
		/// The width of the lines of the band, the hatches and the dashes:
		/// 0.25 mm
	constexpr qreal line_width = 1.0;

		/// The length of the path @p points
	qreal length(const QList<QPointF> &points);
		/// The part of the path @p points from the length @p from to the
		/// length @p to along it, its corners kept; none when @p to is not
		/// beyond @p from
	QList<QPointF> stretch(const QList<QPointF> &points, qreal from, qreal to);
		/// The path @p points moved by @p distance to its left (a negative
		/// one to its right), each corner at the meeting of its two sides
	QList<QPointF> offset(const QList<QPointF> &points, qreal distance);
		/// The path @p points moved by @p distance to the same side on the
		/// folio, whichever way it was drawn (step 7d-bis): below a
		/// horizontal step, right of a vertical one; each corner at the
		/// meeting of its two sides
	QList<QPointF> beside(const QList<QPointF> &points, qreal distance);
		/// Half the width of the band of an insulation around a jacket
		/// (step 8r-1): the jacket at half_width inside it, 3 mm
	constexpr qreal jacketed_half_width = 12.0;
		/// Where a heat tracing goes on a jacketed pipe: outside the
		/// jacket, inside the band when the pipe is insulated too, 2.25 mm
	constexpr qreal jacketed_tracing = 9.0;

		/// The band of an insulation along @p points: its two side lines,
		/// and its hatches, none closer to a corner or an end than they lean;
		/// wider around the jacket of a @p jacketed pipe, its hatches then
		/// only in the ring between the jacket and the band, on both sides
	QPainterPath band(const QList<QPointF> &points, bool jacketed = false);
		/// The line of a heat tracing along @p points, on the same side of
		/// the folio (beside()): beside the pipe (@p insulated false), or
		/// inside the band, half way; outside the jacket of a @p jacketed
		/// pipe (jacketed_tracing)
	QPainterPath tracing(const QList<QPointF> &points, bool insulated, bool jacketed = false);
		/// The two lines of the jacket of a double containment pipe along
		/// @p points (step 8r-1): either side of the pipe, half_width from
		/// its axis, as the sides of the band of an insulation, without
		/// hatches
	QPainterPath jacket(const QList<QPointF> &points);
}

#endif
