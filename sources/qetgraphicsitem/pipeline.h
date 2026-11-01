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

#include <QDomDocument>
#include <QDomElement>
#include <QGraphicsObject>
#include <QList>
#include <QPainterPath>
#include <QPen>
#include <QPointer>
#include <QPointF>
#include <QUuid>

class Terminal;

/**
	@brief The PipeLine class
	A pipe line of a P&ID drawing: a run of pipe drawn with horizontal and
	vertical segments between two ends.
	It is not a Conductor and shares nothing with the electrical wires but
	the routing of the path: no potential, no cable, no formula. Its data
	are those of PipeProperties and belong to this pipe line alone.

	An end is a point and a direction. It may also be attached to the
	nozzle of a symbol, a Terminal whose connection kind is
	TerminalData::Pipe: it then takes the place and the direction of the
	terminal, and follows it when the symbol is moved, turned or mirrored.
	A nozzle takes one pipe line. When the terminal goes, the end is left
	where it was, loose; nothing else is changed.

	A pipe line is drawn by dragging from a nozzle to another
	(Terminal::drawPipeTo()) and saved with its folio, in a <pipes> block
	(Diagram::toXml()): its <pipe> names the symbol and the terminal each
	attached end docks to.
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

			/// One of the two ends of a pipe line
		enum class Side { Start, End };

			/// The nozzle an end docks to, as a file names it: the uuid of
			/// the symbol and the uuid of its terminal
		struct Attachment
		{
			QUuid element;
			QUuid terminal;
			bool isNull() const { return element.isNull() || terminal.isNull(); }
		};

		PipeLine(const End &start, const End &end,
				 QGraphicsItem *parent = nullptr);
		~PipeLine() override;

			/// Qt tells the kinds of items apart by an integer which must be
			/// unique among the items of the application, and qgraphicsitem_cast
			/// relies on it. QET numbers its items by hand from UserType + 1000
			/// (Conductor 1001, Terminal 1002...). The P&ID items have a block
			/// of their own, from 4000, so that they never meet a number given
			/// later to an electrical item. PipeLine is the first of the block.
		enum { Type = UserType + 4001 };
		int type() const override { return Type; }

		QUuid uuid() const { return m_uuid; }
		void setUuid(const QUuid &uuid) { m_uuid = uuid; m_persist_uuid = true; }

		PipeProperties properties() const { return m_properties; }
		void setProperties(const PipeProperties &properties);

		End startEnd() const { return m_start; }
		End endEnd() const { return m_end; }
		void setEnds(const End &start, const End &end);

			/// Attaches the end @p side to the nozzle @p terminal; see
			/// canAttach() for what is refused, which leaves the end as it
			/// was. An end already attached elsewhere is detached first.
		bool attach(Side side, Terminal *terminal);
			/// Whether attach(@p side, @p terminal) would accept it
		bool canAttach(Side side, Terminal *terminal) const;
			/// Detaches the end @p side, which stays where it is
		void detach(Side side);
			/// Detaches whichever end is attached to @p terminal
		void detach(Terminal *terminal);
			/// The terminal the end @p side is attached to, if any
		Terminal *terminal(Side side) const;
			/// Puts the attached ends back on their terminals, which have
			/// moved: called by the terminals (Terminal::updateConductor())
		void updateFromTerminals();
			/// Frees the nozzles while the pipe line is off the folio, yet
			/// remembers them, as a removed conductor remembers its terminals:
			/// called by Diagram::removeItem()
		void releaseTerminals();
			/// Takes back the nozzles it remembers when it comes back on the
			/// folio; an end whose nozzle was taken meanwhile is left loose:
			/// called by Diagram::addItem()
		void claimTerminals();
			/// The nozzle fromXml() read for the end @p side, which is not
			/// attached to it (yet): the folio attaches it once its symbols
			/// are read. One that cannot be found -- symbol not loaded, say --
			/// is written back by toXml(), so a save loses nothing; attaching
			/// or detaching the end forgets it.
		Attachment pendingAttachment(Side side) const;

			/// The corners of the path, from the start to the end, both
			/// included
		QList<QPointF> pathPoints() const { return m_points; }
		QPainterPath path() const { return m_path; }

			/// Writes this pipe line as a <pipe> element. The output depends
			/// on the data alone: no date, no identifier made up on the way.
		QDomElement toXml(QDomDocument &document) const;
			/// Whether @p element is a <pipe> this class can read
		static bool valideXml(const QDomElement &element);
			/// Reads a <pipe> element. Returns false, and changes nothing,
			/// when valideXml() refuses it.
		bool fromXml(const QDomElement &element);

			/// How a kind of line is drawn, apart from its color: the width
			/// (strong, then medium, as in ISO 10628-1 5.3.1) and the style
			/// (dashed for the auxiliary system lines, 5.3.5)
		static QPen penFor(PipeProperties::LineKind kind);

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
			/// The nozzles the ends are attached to. QPointer, as a terminal
			/// goes with its symbol: it detaches itself first, this is only
			/// a safety net.
		QPointer<Terminal> m_start_terminal;
		QPointer<Terminal> m_end_terminal;
			/// See pendingAttachment()
		Attachment m_start_pending;
		Attachment m_end_pending;
		PipeProperties m_properties;
		QUuid m_uuid;
			/// false when m_uuid was made up by fromXml() because the file
			/// had none: toXml() must not write it, or every reload would
			/// save a different one (same reason as for the conductors)
		bool m_persist_uuid = true;
		QList<QPointF> m_points;
		QPainterPath m_path;
};

#endif
