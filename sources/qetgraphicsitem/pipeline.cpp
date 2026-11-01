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
#include "pipeline.h"

#include "../conductorrouter.h"
#include "element.h"
#include "terminal.h"

#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>

namespace
{
		/// Widths in the units of the folio. ISO 10628-1 asks for 1 mm
		/// for the main flow lines and 0,5 mm for the others. A folio of
		/// QET at its default size is about 3,5 units to the millimetre
		/// (17 columns of 60 units on an A4 sheet): to be checked, and
		/// changed here alone, if the scale of a folio turns out otherwise.
	constexpr qreal main_width = 3.5;
	constexpr qreal medium_width = 1.75;
		/// Least width of the area where a click selects the pipe line
	constexpr qreal click_width = 6.0;

	qreal widthOf(PipeProperties::LineKind kind)
	{
		return kind == PipeProperties::Main ? main_width : medium_width;
	}

		/// Name of the element a pipe line is written as
	const QString xml_tag = QStringLiteral("pipe");

		/// The letters the terminals use for their orientation in the files
	QString directionToString(Qet::Orientation o)
	{
		switch (o) {
			case Qet::North: return QStringLiteral("n");
			case Qet::East:  return QStringLiteral("e");
			case Qet::South: return QStringLiteral("s");
			case Qet::West:  return QStringLiteral("w");
		}
		return QStringLiteral("n");
	}

		/// Unlike Qet::orientationFromString(), which takes anything for
		/// north, only the four letters are accepted: a file with another
		/// one is not a pipe line this class wrote
	bool directionFromString(const QString &s, Qet::Orientation &o)
	{
		if (s == QLatin1String("n")) { o = Qet::North; return true; }
		if (s == QLatin1String("e")) { o = Qet::East;  return true; }
		if (s == QLatin1String("s")) { o = Qet::South; return true; }
		if (s == QLatin1String("w")) { o = Qet::West;  return true; }
		return false;
	}

		/// "nan" and "inf" read as numbers: a coordinate must be finite
	bool readCoordinate(const QDomElement &e, const QString &name, qreal &value)
	{
		bool ok = false;
		const qreal v = e.attribute(name).toDouble(&ok);
		if (!ok || !qIsFinite(v)) return false;
		value = v;
		return true;
	}

	bool readEnd(const QDomElement &e, const QString &prefix, PipeLine::End &end)
	{
		qreal x = 0, y = 0;
		Qet::Orientation direction = Qet::North;
		if (!readCoordinate(e, prefix + QStringLiteral("_x"), x)
			|| !readCoordinate(e, prefix + QStringLiteral("_y"), y)
			|| !directionFromString(
				e.attribute(prefix + QStringLiteral("_direction")), direction)) {
			return false;
		}
		end.point = QPointF(x, y);
		end.direction = direction;
		return true;
	}

	void writeEnd(QDomElement &e, const QString &prefix, const PipeLine::End &end)
	{
		e.setAttribute(prefix + QStringLiteral("_x"), QString::number(end.point.x()));
		e.setAttribute(prefix + QStringLiteral("_y"), QString::number(end.point.y()));
		e.setAttribute(prefix + QStringLiteral("_direction"),
					   directionToString(end.direction));
	}

		/// Both uuids, or nothing: half a nozzle names none
	PipeLine::Attachment readAttachment(const QDomElement &e, const QString &prefix)
	{
		PipeLine::Attachment a;
		a.element = QUuid(e.attribute(prefix + QStringLiteral("_element")));
		a.terminal = QUuid(e.attribute(prefix + QStringLiteral("_terminal")));
		return a.isNull() ? PipeLine::Attachment() : a;
	}

	void writeAttachment(QDomElement &e, const QString &prefix,
						 const PipeLine::Attachment &a)
	{
		if (a.isNull()) return;
		e.setAttribute(prefix + QStringLiteral("_element"), a.element.toString());
		e.setAttribute(prefix + QStringLiteral("_terminal"), a.terminal.toString());
	}

		/// The nozzle @p terminal as a file names it, as a conductor names
		/// its terminals (Conductor::toXml())
	PipeLine::Attachment attachmentOf(const Terminal *terminal)
	{
		if (!terminal || !terminal->parentElement()) return {};
		return {terminal->parentElement()->uuid(), terminal->stableUuid()};
	}

	ConductorRouter::Direction routerDirection(Qet::Orientation o)
	{
		switch (o) {
			case Qet::North: return ConductorRouter::Direction::North;
			case Qet::East:  return ConductorRouter::Direction::East;
			case Qet::South: return ConductorRouter::Direction::South;
			case Qet::West:  return ConductorRouter::Direction::West;
		}
		return ConductorRouter::Direction::North;
	}
}

/**
	@brief PipeLine::PipeLine
	@param start the first end
	@param end the second end
	@param parent the parent item, if any
*/
PipeLine::PipeLine(const End &start, const End &end, QGraphicsItem *parent) :
	QGraphicsObject(parent),
	m_start(start),
	m_end(end),
	m_uuid(QUuid::createUuid())
{
		//Above the symbols, as the conductors are
	setZValue(11);
	setFlags(QGraphicsItem::ItemIsSelectable);
	updatePath();
}

/**
	@brief PipeLine::~PipeLine
	Frees the nozzles the ends were attached to.
*/
PipeLine::~PipeLine()
{
	detach(Side::Start);
	detach(Side::End);
}

/**
	@brief PipeLine::canAttach
	@param side the end to attach
	@param terminal the terminal to attach it to
	@return true if the end @p side may be attached to @p terminal: a nozzle
	(connection kind TerminalData::Pipe) which has no other pipe line, and
	which is not the one the other end is attached to. An end may be
	"attached again" to the terminal it already is.
*/
bool PipeLine::canAttach(Side side, Terminal *terminal) const
{
	if (!terminal || terminal->connectionKind() != TerminalData::Pipe) {
		return false;
	}
	if (this->terminal(side) == terminal) {
		return true;
	}
	const Side other = side == Side::Start ? Side::End : Side::Start;
	if (this->terminal(other) == terminal) {
		return false;
	}
	return terminal->hasRoomForPipe();
}

/**
	@brief PipeLine::attach
	Attach the end @p side to the nozzle @p terminal: the end takes the
	place and the direction of the terminal and follows it from now on.
	@param side the end to attach
	@param terminal the nozzle
	@return true if the end is attached to @p terminal; false, with nothing
	changed, if canAttach() refuses it
*/
bool PipeLine::attach(Side side, Terminal *terminal)
{
	if (!canAttach(side, terminal)) {
		return false;
	}
	if (this->terminal(side) == terminal) {
		return true;
	}
	detach(side);
	if (!terminal->addPipe(this)) {
		return false;
	}
	(side == Side::Start ? m_start_terminal : m_end_terminal) = terminal;
	(side == Side::Start ? m_start_pending : m_end_pending) = Attachment();
	updateFromTerminals();
	return true;
}

/**
	@brief PipeLine::detach
	Detach the end @p side from its nozzle, or from the one a file named for
	it (pendingAttachment()). The end stays where it is.
	@param side the end to detach
*/
void PipeLine::detach(Side side)
{
	(side == Side::Start ? m_start_pending : m_end_pending) = Attachment();
	QPointer<Terminal> &attached = side == Side::Start ? m_start_terminal
							   : m_end_terminal;
	if (Terminal *terminal = attached.data()) {
		attached.clear();
		terminal->removePipe(this);
	}
}

/**
	@brief PipeLine::detach
	Detach whichever end is attached to @p terminal, which is going: the
	end stays where it is.
	@param terminal the terminal
*/
void PipeLine::detach(Terminal *terminal)
{
	if (!terminal) return;
	if (m_start_terminal == terminal) detach(Side::Start);
	if (m_end_terminal == terminal) detach(Side::End);
}

/**
	@brief PipeLine::terminal
	@param side the end
	@return the nozzle the end @p side is attached to, or nullptr
*/
Terminal *PipeLine::terminal(Side side) const
{
	return side == Side::Start ? m_start_terminal.data() : m_end_terminal.data();
}

/**
	@brief PipeLine::updateFromTerminals
	Put each attached end back on its nozzle: the docking point of the
	terminal, the way conductors dock, and the direction it points to on the
	folio, which takes the turn and the mirror of the symbol into account.
	A loose end does not move.
*/
void PipeLine::updateFromTerminals()
{
	if (!m_start_terminal && !m_end_terminal) {
		return;
	}
	End start = m_start;
	End end = m_end;
	if (Terminal *t = m_start_terminal.data()) {
		start.point = mapFromScene(t->dockConductor());
		start.direction = t->orientation();
	}
	if (Terminal *t = m_end_terminal.data()) {
		end.point = mapFromScene(t->dockConductor());
		end.direction = t->orientation();
	}
	setEnds(start, end);
}

/**
	@brief PipeLine::releaseTerminals
	The pipe line leaves the folio, deleted with undo in view: its nozzles
	are freed for another pipe line, but it remembers them, so that undoing
	puts it back where it was (see claimTerminals()).
*/
void PipeLine::releaseTerminals()
{
	if (Terminal *t = m_start_terminal.data()) t->removePipe(this);
	if (Terminal *t = m_end_terminal.data())   t->removePipe(this);
}

/**
	@brief PipeLine::claimTerminals
	The pipe line comes back on the folio: it takes back the nozzles it
	remembers. An end whose nozzle has another pipe line by now is left
	loose where it is: a nozzle takes one pipe line.
*/
void PipeLine::claimTerminals()
{
	for (QPointer<Terminal> *attached : {&m_start_terminal, &m_end_terminal}) {
		if (Terminal *t = attached->data()) {
			if (!t->addPipe(this)) {
				attached->clear();
			}
		}
	}
	updateFromTerminals();
}

/**
	@brief PipeLine::pendingAttachment
	@param side the end
	@return the nozzle fromXml() read for the end @p side and which it is
	not attached to; a null one if the file named none, or once the end is
	attached or detached
*/
PipeLine::Attachment PipeLine::pendingAttachment(Side side) const
{
	return side == Side::Start ? m_start_pending : m_end_pending;
}

/**
	@brief PipeLine::setProperties
	Set the properties of this pipe line. Nothing is passed on to any other
	pipe line, whatever it is connected to.
	@param properties the new properties
*/
void PipeLine::setProperties(const PipeProperties &properties)
{
	if (m_properties == properties) return;
		// A wider line is a larger area to draw
	if (m_properties.line_kind != properties.line_kind) {
		prepareGeometryChange();
	}
	m_properties = properties;
	update();
	emit propertiesChange();
}

/**
	@brief PipeLine::setEnds
	Move the ends of this pipe line and draw the path again.
	@param start the first end
	@param end the second end
*/
void PipeLine::setEnds(const End &start, const End &end)
{
	m_start = start;
	m_end = end;
	updatePath();
}

/**
	@brief PipeLine::toXml
	Write this pipe line as a <pipe> element: its identifier, its two ends,
	the nozzle each end docks to (start_element and start_terminal...) and
	its properties, all as attributes. Nothing else goes in: the path is
	worked out again from the ends when the element is read, so a change in
	the routing never makes a file out of date.
	@param document the document the element is created in
	@return the element
*/
QDomElement PipeLine::toXml(QDomDocument &document) const
{
	QDomElement element = document.createElement(xml_tag);
	if (m_persist_uuid) {
		element.setAttribute(QStringLiteral("uuid"), m_uuid.toString());
	}
	writeEnd(element, QStringLiteral("start"), m_start);
	writeEnd(element, QStringLiteral("end"), m_end);
	writeAttachment(element, QStringLiteral("start"),
					m_start_terminal ? attachmentOf(m_start_terminal) : m_start_pending);
	writeAttachment(element, QStringLiteral("end"),
					m_end_terminal ? attachmentOf(m_end_terminal) : m_end_pending);
	m_properties.toXml(element);
	return element;
}

/**
	@brief PipeLine::valideXml
	@param element the element to check
	@return true if it is a <pipe> with both ends readable. The identifier
	and the properties are optional.
*/
bool PipeLine::valideXml(const QDomElement &element)
{
	if (element.tagName() != xml_tag) return false;
	End start, end;
	return readEnd(element, QStringLiteral("start"), start)
		&& readEnd(element, QStringLiteral("end"), end);
}

/**
	@brief PipeLine::fromXml
	Read a <pipe> element. An attribute this class does not know is
	ignored. A missing or malformed identifier is replaced by a new one
	which is not written back by toXml(). The ends are left loose: the
	nozzles the element names are kept as pendingAttachment(), for the
	folio to attach them (Diagram::fromXml()); attached ends are detached.
	@param element the element to read
	@return true if it was read; false, with this pipe line unchanged,
	if valideXml() refuses it
*/
bool PipeLine::fromXml(const QDomElement &element)
{
	if (!valideXml(element)) return false;

	End start, end;
	readEnd(element, QStringLiteral("start"), start);
	readEnd(element, QStringLiteral("end"), end);

	const QUuid uuid(element.attribute(QStringLiteral("uuid")));
	m_persist_uuid = !uuid.isNull();
	m_uuid = uuid.isNull() ? QUuid::createUuid() : uuid;

	detach(Side::Start);
	detach(Side::End);
	m_start_pending = readAttachment(element, QStringLiteral("start"));
	m_end_pending = readAttachment(element, QStringLiteral("end"));

	PipeProperties properties;
	properties.fromXml(element);
	setProperties(properties);
	setEnds(start, end);
	return true;
}

/**
	@brief PipeLine::updatePath
	Work out the path between the two ends. The routing is the one of the
	conductors (ConductorRouter), without obstacles. Despite its name it is
	plain geometry -- horizontal and vertical segments between two points,
	each leaving in a direction -- and knows no wire, terminal or electrical
	rule. This is the only place where PipeLine depends on it. When it finds no path,
	for example when the two ends are the same point, the path is made of
	one corner at most.
*/
void PipeLine::updatePath()
{
	ConductorRouter::Request request;
	request.start = m_start.point;
	request.start_direction = routerDirection(m_start.direction);
	request.end = m_end.point;
	request.end_direction = routerDirection(m_end.direction);

	QList<QPointF> points = ConductorRouter::route(request).points;
	if (points.size() < 2) {
		points.clear();
		points << m_start.point;
		if (m_start.point.x() != m_end.point.x()
			&& m_start.point.y() != m_end.point.y()) {
			points << QPointF(m_end.point.x(), m_start.point.y());
		}
		points << m_end.point;
	}

	QPainterPath path;
	path.moveTo(points.first());
	for (int i = 1; i < points.size(); ++i) {
		path.lineTo(points.at(i));
	}

	prepareGeometryChange();
	m_points = points;
	m_path = path;
	update();
}

/**
	@brief PipeLine::boundingRect
	@return the area drawn by this pipe line
*/
QRectF PipeLine::boundingRect() const
{
	const qreal margin = widthOf(m_properties.line_kind) + 2;
	return m_path.boundingRect().adjusted(-margin, -margin, margin, margin);
}

/**
	@brief PipeLine::penFor
	@param kind the kind of line
	@return the pen for this kind, in black: the main lines are the widest,
	the others medium, and the auxiliary ones dashed
*/
QPen PipeLine::penFor(PipeProperties::LineKind kind)
{
	QPen pen(Qt::black);
	pen.setWidthF(widthOf(kind));
	pen.setStyle(kind == PipeProperties::Auxiliary ? Qt::DashLine : Qt::SolidLine);
	pen.setJoinStyle(Qt::MiterJoin);
	pen.setCapStyle(Qt::FlatCap);
	return pen;
}

/**
	@brief PipeLine::shape
	@return the area where a click selects this pipe line
*/
QPainterPath PipeLine::shape() const
{
	QPainterPathStroker stroker;
	stroker.setWidth(qMax(click_width, widthOf(m_properties.line_kind) + 2));
	return stroker.createStroke(m_path);
}

/**
	@brief PipeLine::paint
	Draw the pipe line without antialiasing, red when it is selected.
*/
void PipeLine::paint(QPainter *painter,
					 const QStyleOptionGraphicsItem *option,
					 QWidget *widget)
{
	Q_UNUSED(widget)
	Q_UNUSED(option)
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, false);

	QPen pen = penFor(m_properties.line_kind);
	if (isSelected()) pen.setColor(Qt::red);
	painter->setPen(pen);
	painter->setBrush(Qt::NoBrush);
	painter->drawPath(m_path);

	painter->restore();
}
