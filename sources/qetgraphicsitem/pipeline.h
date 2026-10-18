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
#ifndef PIPELINE_H
#define PIPELINE_H

#include "../pipeproperties.h"
#include "../qet.h"

#include <QGraphicsObject>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QUuid>

/**
	@brief The PipeLine class
	A pipe line of a P&ID drawing: a run of pipe drawn with horizontal and
	vertical segments between two ends.
	It is not a Conductor and shares nothing with the electrical wires but
	the routing of the path: no potential, no cable, no formula. Its data
	are those of PipeProperties and belong to this pipe line alone.

	For now an end is a point and a direction rather than a Terminal, which
	only knows Conductor objects: the pipe line can be built and tested
	on its own. Nothing creates one yet and a diagram does not know about
	this class.
*/
class PipeLine : public QGraphicsObject
{
	Q_OBJECT

	public:
			/// Where a pipe line docks and which way it leaves
		struct End
		{
				/// Docking point, in the coordinates of the item
			QPointF point;
				/// Direction the end points to, outward from its symbol
			Qet::Orientation direction = Qet::North;
		};

		PipeLine(const End &start, const End &end,
				 QGraphicsItem *parent = nullptr);

			/// Qt tells the kinds of items apart by an integer which must be
			/// unique among the items of the application, and qgraphicsitem_cast
			/// relies on it. QET numbers its items by hand from UserType + 1000
			/// (Conductor 1001, Terminal 1002...). The P&ID items have a block
			/// of their own, from 4000, so that they never meet a number given
			/// later to an electrical item. PipeLine is the first of the block.
		enum { Type = UserType + 4001 };
		int type() const override { return Type; }

		QUuid uuid() const { return m_uuid; }
		void setUuid(const QUuid &uuid) { m_uuid = uuid; }

		PipeProperties properties() const { return m_properties; }
		void setProperties(const PipeProperties &properties);

		End startEnd() const { return m_start; }
		End endEnd() const { return m_end; }
		void setEnds(const End &start, const End &end);

			/// The corners of the path, from the start to the end, both
			/// included
		QList<QPointF> pathPoints() const { return m_points; }
		QPainterPath path() const { return m_path; }

		QRectF boundingRect() const override;
		QPainterPath shape() const override;
		void paint(QPainter *painter,
				   const QStyleOptionGraphicsItem *option,
				   QWidget *widget = nullptr) override;

	signals:
		void propertiesChange();

	private:
		void updatePath();

		End m_start;
		End m_end;
		PipeProperties m_properties;
		QUuid m_uuid;
		QList<QPointF> m_points;
		QPainterPath m_path;
};

#endif
