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
#ifndef WIREHOPS_H
#define WIREHOPS_H

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QVector>

/**
	@brief The WireHops namespace
	Hops (small arcs) drawn where two wires cross without being connected
	(issue #436). Only the drawing changes: no element is added and no wire
	is split. Kept free of any QGraphicsItem so the geometry can be tested
	on its own.

	Wires in QElectroTech are made of horizontal and vertical segments
	only, so a crossing is a segment of one orientation passing through a
	segment of the other, away from the ends of both. A wire that ends or
	bends on another one is a junction, not a crossing, and never hops.
*/
namespace WireHops
{
		///Which wire of a crossing draws the hop
	enum class Mode {
		None,       ///< no hop, the default: crossings are drawn as plain lines
		Horizontal, ///< the horizontal wire hops over the vertical one
		Vertical,   ///< the vertical wire hops over the horizontal one
		Thinner     ///< the thinner wire, the vertical one when both are as
					///< thick (ISO 10628-1 for process diagrams)
	};

		///How the wire which gives way is drawn at a crossing
	enum class Shape {
		Arc, ///< a half circle over the other wire, the default
		Gap  ///< the wire stops short of the other one and goes on past it
	};

		///Radius of a hop, in scene units (a grid step is 10)
	constexpr qreal radius = 4.0;
		///Room left by a gap between the end of the wire and the edge of
		///the other one, on each side, in scene units (1 mm)
	constexpr qreal gapClearance = 4.0;

	QString toString(Mode mode);
	Mode fromString(const QString &string);
	QString toString(Shape shape);
	Shape shapeFromString(const QString &string);

	QList<QPointF> crossings(const QVector<QPointF> &wire,
							 const QList<QVector<QPointF>> &others,
							 Mode mode);
	QPainterPath path(const QVector<QPointF> &wire,
					  const QList<QPointF> &hops,
					  Mode mode);

		///A wire as drawn: its points and the width of its line
	struct Line {
		QVector<QPointF> points;
		qreal width = 1.0;
	};

		///Where a wire is not drawn straight: from @a from to @a to, both
		///on one of its segments, in the order the wire runs. An arc hops
		///from one to the other; a gap leaves the wire undrawn between them.
	struct Interruption {
		QPointF from;
		QPointF to;
		bool arc = false;
	};

	QList<Interruption> interruptions(const Line &wire, const QList<Line> &others,
									  Mode mode, Shape shape, qreal cap_extension);
	QList<QPointF> crossingPoints(const QVector<QPointF> &wire,
								  const QList<QVector<QPointF>> &others);
	QPainterPath path(const QVector<QPointF> &wire,
					  const QList<Interruption> &interruptions);
}

#endif // WIREHOPS_H
