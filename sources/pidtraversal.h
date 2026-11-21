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
#ifndef PIDTRAVERSAL_H
#define PIDTRAVERSAL_H

	//elementpicturefactory.h holds hashes of these, only declared there
#include <QPicture>
#include <QPixmap>
#include <QUuid>

#include "factory/elementpicturefactory.h"

#include <QList>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <QVector>

/**
	@brief The PidTraversal namespace
	Where a pipe or a signal line runs over the drawing of a symbol it is
	not docked to there (step 7c-bis). It is said, never refused nor
	corrected: the part of the line over the symbol is drawn orange on the
	screen, and its tool tip names the symbol (Conductor::traversalNote()).
	Kept free of any QGraphicsItem, so the geometry can be tested on its own.

	What counts is all a symbol draws, its texts left out: the inside of
	its closed shapes (a circle, a rectangle, a closed polygon) and its
	lines alone (a stem, an open polygon, an arc). A stretch of the line
	which touches one of its two ends is never a traversal: the nozzles the
	line leaves and reaches, and a line ending inside a closed basin, on a
	pump set in it. Only a line which goes into a symbol and out of it is.
*/
namespace PidTraversal
{
		/// Raised whenever a symbol moves, turns, is mirrored, is drawn
		/// anew (options, change of type), enters or leaves a folio, is
		/// shown or hidden: the traversals of every line may then change
	void symbolsChanged();
	quint64 symbolsGeneration();

		/// What a symbol draws, on the folio: the closed shapes, whose
		/// inside counts, and every line of the drawing, the edges of the
		/// closed shapes included
	struct Outline
	{
		QList<QPolygonF> areas;
		QList<QPolygonF> strokes;
		QRectF rect;
		bool isEmpty() const { return strokes.isEmpty(); }
	};
		/// The outline of the symbol drawn as @p drawing, placed on the
		/// folio by @p to_folio (its scene transform: place, turn, mirrors)
	Outline outline(const ElementPictureFactory::primitives &drawing,
					const QTransform &to_folio);

		/// A stretch of a line, by its distance along it from its first point
	struct Span
	{
		qreal from = 0;
		qreal to = 0;
	};
		/// The length of the polyline @p line
	qreal length(const QVector<QPointF> &line);
		/// Where the polyline @p line runs over @p outline: inside a closed
		/// shape, along a line of it, or across one (a span of no length),
		/// the stretches closer than half a unit joined. The stretches
		/// which touch an end of @p line are left out
	QList<Span> traversals(const QVector<QPointF> &line, const Outline &outline);
		/// The parts of @p line along @p spans, each one made at least
		/// @p min_length long about its middle, within the line
	QPainterPath pathAlong(const QVector<QPointF> &line, const QList<Span> &spans,
						   qreal min_length);
}

#endif // PIDTRAVERSAL_H
