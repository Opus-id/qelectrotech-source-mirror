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
#ifndef LEADER_LINE_H
#define LEADER_LINE_H

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>

/**
	@brief The LeaderLine namespace
	The thin line which ties the text of a conductor (a wire number, the
	designation of a pipe) to its conductor once the user moved the text
	away from it: from the edge of the text nearest to the conductor to the
	nearest point of the conductor. The project says from which distance it
	is drawn, none by default, and what ends it on the conductor.
*/
namespace LeaderLine
{
		/// What ends the leader line on the conductor: nothing by default, a
		/// dot being a junction on an electrical diagram
	enum class End { None, Dot, Arrow };

	QString toString(End end);
	End fromString(const QString &string);

		/// The distance from @p point to the nearest point of the line through
		/// @p points (two or more), and that point in @p nearest
	qreal distanceToLine(const QPointF &point, const QList<QPointF> &points,
						 QPointF *nearest = nullptr);

		/**
			The leader line from the text of bounding rectangle @p text to
			the conductor of points @p points (in the same coordinates),
			when the gap between them is more than @p from (in the same
			units; none when @p from is 0 or less): its line, then what
			ends it on the conductor, of size @p size. A plain end stops
			@p clearance short of the axis of the conductor (a little more
			than half its width), a dot or an arrow touches it. An empty
			path when the text is near enough.
		*/
	QPainterPath path(const QRectF &text, const QList<QPointF> &points,
					  qreal from, End end, qreal size, qreal clearance = 0);

		/// Drawing units by millimetre (README-PID: 4)
	constexpr qreal unitsByMillimetre = 4.0;
}

#endif
