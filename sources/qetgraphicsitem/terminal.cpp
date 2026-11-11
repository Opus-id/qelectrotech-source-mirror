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
#include <qetgraphicsitem/terminal.h>
#include <ElementsCollection/terminaluuids.h>
#include <qet.h>
#include <qetproject.h>
#include <dataBase/projectdatabase.h>
#include <conductorautonumerotation.h>
#include <diagram.h>
#include <diagramview.h>
#include <undocommand/addgraphicsobjectcommand.h>
#include <properties/terminaldata.h>
#include <qetgraphicsitem/conductor.h>
#include <qetgraphicsitem/element.h>
#include "conductortextitem.h"
#include <wiringrules.h>
#include <autoconnect.h>

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QSettings>
#include <QToolTip>

#include <QtCore/qnumeric.h>
#include <utility>

QColor Terminal::neutralColor      = QColor(Qt::blue);
QColor Terminal::allowedColor      = QColor(Qt::darkGreen);
QColor Terminal::warningColor      = QColor("#ff8000");
QColor Terminal::forbiddenColor    = QColor(Qt::red);
const qreal Terminal::terminalSize = 4.0;
const qreal Terminal::magnetRadius = 20.0;

/**
	@brief Terminal::magnetSceneRadius
	@param e : a mouse event of the folio
	@return Terminal::magnetRadius in units of the folio, at the zoom of
	the view @p e comes from: the catch stays the same on screen whatever
	the zoom. Without a view (a test), in the folio's units.
*/
qreal Terminal::magnetSceneRadius(const QGraphicsSceneMouseEvent *e)
{
	qreal scale = 1.0;
	if (e && e->widget()) {
		if (auto *view = qobject_cast<QGraphicsView *>(e->widget()->parentWidget())) {
			scale = qAbs(view->transform().m11());
		}
	}
	return magnetRadius / qMax(scale, 0.01);
}

/**
	@brief Terminal::stemColor
	The stem of a terminal, the short line to its docking point, tells the
	medium it connects, on the folio and in the element editor: red for an
	electrical terminal, as it always was, and a color of its own for each
	other medium. None of them is green or orange, which tell while a wire
	is drawn whether it may go to a terminal. The terminals are not printed.
	@param medium the medium the terminal connects
	@return the color of its stem
*/
QColor Terminal::stemColor(TerminalData::Medium medium)
{
	switch (medium) {
		case TerminalData::Electrical: return QColor(Qt::red);
		case TerminalData::Pipe:       return QColor(0x00, 0x80, 0x80); // teal
		case TerminalData::Signal:     return QColor(0xc0, 0x00, 0xc0); // magenta
		case TerminalData::Drive:      return QColor(0x8b, 0x45, 0x13); // brown
	}
	return QColor(Qt::red);
}

/**
	@brief Terminal::mediumName
	@param medium
	@return the name of @p medium, to be read in a sentence ("electrical",
	"pipe"...)
*/
QString Terminal::mediumName(TerminalData::Medium medium)
{
	switch (medium) {
		case TerminalData::Electrical: return tr("electrical", "medium of a terminal, in a sentence");
		case TerminalData::Pipe:       return tr("pipe", "medium of a terminal, in a sentence");
		case TerminalData::Signal:     return tr("signal", "medium of a terminal, in a sentence");
		case TerminalData::Drive:      return tr("drive", "medium of a terminal, in a sentence");
	}
	return tr("electrical", "medium of a terminal, in a sentence");
}

namespace {
	/// See Terminal::takesConductorColor(); -1 until read from the settings
	int s_takes_conductor_color = -1;
}

/**
	@brief Terminal::takesConductorColor
	@return true if a connected terminal is drawn in the color of its
	(first) conductor rather than in red, so it does not stand out within
	a wire of another color. Off by default.
*/
bool Terminal::takesConductorColor()
{
	if (s_takes_conductor_color < 0) {
		QSettings settings;
		s_takes_conductor_color = settings.value(
			QStringLiteral("diagrameditor/terminal_takes_conductor_color"), false).toBool();
	}
	return s_takes_conductor_color;
}

/**
	@brief Terminal::setTakesConductorColor
	@param takes : see takesConductorColor(); kept in the settings
*/
void Terminal::setTakesConductorColor(bool takes)
{
	s_takes_conductor_color = takes;
	QSettings settings;
	settings.setValue(QStringLiteral("diagrameditor/terminal_takes_conductor_color"), takes);
}
const qreal Terminal::Z = 1000;

/**
	@brief Terminal::init
	Methode privee pour initialiser la borne.
	Private method to initialize the terminal.
	@param number of terminal
	@param name of terminal
	@param hiddenName
*/
void Terminal::init()
{
		//Calculate the docking point of the element
		//m_pos of d is the docking point of conductor
	dock_elmt_ = d->m_pos;
	switch(d->m_orientation) {
		case Qet::North: dock_elmt_ += QPointF(0, Terminal::terminalSize);  break;
		case Qet::East : dock_elmt_ += QPointF(-Terminal::terminalSize, 0); break;
		case Qet::West : dock_elmt_ += QPointF(Terminal::terminalSize, 0);  break;
		case Qet::South: dock_elmt_ += QPointF(0, -Terminal::terminalSize); break;
	}

		//Calculate the bounding rect
	qreal dcx = d->m_pos.x();
	qreal dcy = d->m_pos.y();
	qreal dex = dock_elmt_.x();
	qreal dey = dock_elmt_.y();
	QPointF origin = (dcx <= dex && dcy <= dey ? d->m_pos : dock_elmt_);
	origin += QPointF(-3.0, -3.0);
	qreal w = qAbs(dcx - dex) + 7;
	qreal h = qAbs(dcy - dey) + 7;
	m_br = QRectF(origin, QSizeF(w, h));

	setAcceptHoverEvents(true);
	setAcceptedMouseButtons(Qt::LeftButton);
	setToolTip(QObject::tr("Terminal", "tooltip"));
		//A nozzle says how a pipe is joined to it
	if (d->m_medium == TerminalData::Pipe)
		setToolTip(PipeConnection::name(d->m_connection));
	setZValue(Z);
}

Terminal::Terminal(TerminalData* data, Element* e) :
	QGraphicsObject(e),
	d(data),
	parent_element_(e)
{
	d->setParent(this);
	init();
}

/**
 * @brief Terminal::~Terminal
 * Destruction of the terminal, and also docked conductor
 */
Terminal::~Terminal() {
	// Each conductor's destructor calls removeConductor() on both its
	// terminals, which mutates m_conductors_list while qDeleteAll() is
	// still iterating it. Delete from a snapshot so the live list can
	// change underneath without affecting the iteration.
	const QList<Conductor *> conductors_to_delete = m_conductors_list;
	qDeleteAll(conductors_to_delete);
	delete d;
}

/**
	Permet de connaitre l'orientation de la borne. Si le parent de la borne
	est bien un Element, cette fonction renvoie l'orientation par rapport a
	la scene de la borne, en tenant compte du fait que l'element ait pu etre
	pivote. Sinon elle renvoie son sens normal.
	Used to find out the orientation of the terminal. If the terminal's parent
	is in fact an Element, this function returns the orientation of the
	terminal with respect to the scene, taking into account the angle of
	rotation. scene, taking into account the fact that the element may have
	been rotated. Otherwise it returns its normal direction.
	@return L'orientation actuelle de la Terminal.
*/
Qet::Orientation Terminal::orientation() const
{
	if (Element *elt = qgraphicsitem_cast<Element *>(parentItem())) {
		// orientations actuelle et par defaut de l'element
		// current and default element orientations
		int ori_cur = elt -> orientation();
		// The mirrors of an element are about its own axes, before it is
		// rotated (Element::setMirror()): a horizontal one swaps east and
		// west, a vertical one north and south
		Qet::Orientation ori_elmt = d->m_orientation;
		if (Qet::isHorizontal(ori_elmt) ? elt -> hasHorizontalMirror()
										: elt -> hasVerticalMirror())
			ori_elmt = (Qet::Orientation)((ori_elmt + 2) % 4);
	if (ori_cur == 0) return(ori_elmt);
		else {
			// calcul l'angle de rotation implique par l'orientation de l'element parent
			// angle de rotation de la borne sur la scene, divise par 90
			// calculates the angle of rotation implied by the orientation of the parent
			// element angle of rotation of the terminal on the scene, divided by 90
			int angle = ori_cur + ori_elmt;
			while (angle >= 4) angle -= 4;
			return((Qet::Orientation)angle);
		}
	} else return(d->m_orientation);
}

/**
	@brief Terminal::addConductor
	Add a conductor to this terminal
	@param conductor : the conductor to add.
	@return true if the conductor was successfully added
*/
bool Terminal::addConductor(Conductor *conductor)
{
	if (!conductor) return(false);

	Q_ASSERT_X(((conductor -> terminal1 == this) ^ (conductor -> terminal2 == this)),
		   "Terminal::addConductor",
		   "The conductor must be linked exactly once to this terminal");

	//Get the other terminal where the conductor must be linked
	Terminal *other_terminal = (conductor -> terminal1 == this)
			? conductor->terminal2 : conductor->terminal1;

	//Check if this terminal isn't already linked with other_terminal
	foreach (Conductor* cond, m_conductors_list)
		if (cond -> terminal1 == other_terminal || cond -> terminal2 == other_terminal)
			return false; //They already a conductor linked to this and other_terminal

	m_conductors_list.append(conductor);
		//A nozzle joined to one on the same point draws no mark
	if (takesConductorColor() || d->m_medium == TerminalData::Pipe) update();
	emit conductorWasAdded(conductor);
	return(true);
}

/**
	@brief Terminal::removeConductor
	Remove a conductor from this terminal
	@param conductor : conductor to remove
*/
void Terminal::removeConductor(Conductor *conductor)
{
	int index = m_conductors_list.indexOf(conductor);
	if (index == -1) return;
	m_conductors_list.removeAt(index);
	if (takesConductorColor() || d->m_medium == TerminalData::Pipe) update();
	emit conductorWasRemoved(conductor);
}

/**
	@brief Terminal::paint
	Fonction de dessin des bornes
	@param painter Le QPainter a utiliser
	@param options Les options de dessin
*/
void Terminal::paint(
		QPainter *painter,
		const QStyleOptionGraphicsItem *options,
		QWidget *)
{
	// en dessous d'un certain zoom, les bornes ne sont plus dessinees
	// below a certain zoom level, the terminals are no longer drawn
	// A terminal of a P&ID keeps its help lines: a sheet of that size is
	// often worked on below that zoom
	const bool low_zoom = options
			&& options->levelOfDetailFromTransform(painter->worldTransform()) < 0.5;
	const bool electrical = d->m_medium == TerminalData::Electrical;
	if (low_zoom && electrical)
		return;
	painter -> save();

	// annulation des renderhints
	// cancel renderhints
	painter -> setRenderHint(QPainter::Antialiasing,          false);
	painter -> setRenderHint(QPainter::TextAntialiasing,      false);
	painter -> setRenderHint(QPainter::SmoothPixmapTransform, false);

	// on travaille avec les coordonnees de l'element parent
	// work with the coordinates of the parent element
	QPointF c = mapFromParent(d->m_pos);
	QPointF e = mapFromParent(dock_elmt_);

	QPen t;
	t.setWidthF(1.0);

	if (options && options->levelOfDetailFromTransform(painter->worldTransform()) < 1.0)
	{
		t.setCosmetic(true);
	}

	// dessin de la borne en rouge
	// draw the terminal in red, or in the color of its medium
	if (!low_zoom && (!diagram() || diagram()->drawTerminals())) {
		t.setColor(takesConductorColor() && !m_conductors_list.isEmpty()
				   ? m_conductors_list.first()->drawnColor()
				   : stemColor(d->m_medium));
		painter -> setPen(t);
		painter -> drawLine(c, e);

		// dessin du point d'amarrage au conducteur en bleu
		// draw the docking point to the conductor in blue
		t.setColor(m_hovered_color);
		painter -> setPen(t);
		painter -> setBrush(m_hovered_color);
		if (m_hovered) {
			painter -> setRenderHint(QPainter::Antialiasing, true);
			painter -> drawEllipse(QRectF(c.x() - 2.5, c.y() - 2.5, 5.0, 5.0));
		} else painter -> drawPoint(c);
	}

		//The mark of the connection of a nozzle: part of the drawing, at any
		//zoom and on paper
	if (drawsOwnMark()) {
		drawConnectionMark(painter, options ? options->levelOfDetailFromTransform(painter->worldTransform())
											: 1.0);
	}

		//Two sizes where this nozzle is placed against another one: the
		//orange diamond a pipe draws at its ends, on the screen only
	if (diagram() && diagram()->drawTerminals() && marksContactGap()) {
		const QColor orange(255, 140, 0);
		QPen pen(orange);
		pen.setCosmetic(true);
		painter->setPen(pen);
		painter->setBrush(orange);
		painter->setRenderHint(QPainter::Antialiasing, true);
		painter->drawPolygon(QPolygonF({c + QPointF(0, -6), c + QPointF(6, 0),
										c + QPointF(0, 6), c + QPointF(-6, 0)}));
		painter->setBrush(Qt::NoBrush);
	}

	//Draw help line if needed,
	if (diagram() && m_draw_help_line)
	{
		//Draw the help line with same orientation of terminal
		//Only if there isn't docked conductor
		if (conductors().isEmpty())
		{
			if (!m_help_line)
				m_help_line = new QGraphicsLineItem(this);
			QPen pen;
			pen.setColor(Qt::darkBlue);
			pen.setCosmetic(!electrical); // one pixel at any zoom

			QLineF line(HelpLine());

			if (diagram() -> project() -> autoConductor())
			{
				Terminal *t = alignedWithTerminal();
				if (t)
				{
					line.setP2(t -> dockConductor());
					pen.setColor(Qt::darkGreen);
				}
			}

			//Map the line (in scene coordinate) to m_help_line coordinate
			line.setP1(m_help_line -> mapFromScene(line.p1()));
			line.setP2(m_help_line -> mapFromScene(line.p2()));
			m_help_line -> setPen(pen);
			m_help_line -> setLine(line);
		}

		//Draw the help line perpendicular to the terminal. Not with the
		//crosshair: it already crosses the folio through the terminal
		//nearest the mouse, and lines through the other terminals beside
		//it only blur where the symbol lands
		if (DiagramView::crosshairShown())
		{
			delete m_help_line_a;
			m_help_line_a = nullptr;
		}
		else
		{
		if (!m_help_line_a)
		{
			m_help_line_a = new QGraphicsLineItem(this);
			QPen pen;
			pen.setColor(Diagram::background_color == Qt::darkGray ? Qt::lightGray : Qt::darkGray);
			pen.setCosmetic(!electrical);
			m_help_line_a -> setPen(pen);
		}

		QRectF rect = diagram() -> border_and_titleblock.insideBorderRect();
		QLineF line;

		if (Qet::isHorizontal(orientation()))
		{
			line.setP1(QPointF(dockConductor().x(), rect.topLeft().y()));
			line.setP2(QPointF(dockConductor().x(), rect.bottomLeft().y()));
		}
		else
		{
			line.setP1(QPointF(rect.topLeft().x(), dockConductor().y()));
			line.setP2(QPointF(rect.topRight().x(), dockConductor().y()));
		}

			//Map the line (in scene coordinate) to m_help_line_a coordinate
		line.setP1(m_help_line_a -> mapFromScene(line.p1()));
		line.setP2(m_help_line_a -> mapFromScene(line.p2()));
		m_help_line_a -> setLine(line);
		}
	}

	// Draw label if show_name is enabled and terminal names are allowed
	const QString display_name = labelText();
		// A small image of a folio keeps the texts its paper would have
	const bool on_screen = painter->device()
			&& painter->device()->devType() == QInternal::Widget;
	if ((!low_zoom || !on_screen) && d->m_show_name && !display_name.isEmpty()
		&& labelIsDrawn()) {
		painter->setRenderHint(QPainter::Antialiasing, true);
		painter->setRenderHint(QPainter::TextAntialiasing, true);
		const QFont label_font = labelFont();
		painter->setFont(label_font);
		painter->setPen(d->m_label_color);

		QPointF label_pos = d->m_pos + d->m_label_pos;
		QFontMetrics fm(label_font);
		QSizeF text_size = TerminalData::labelSize(fm, display_name);

			// When the element mirrors its symbol, or turns it in a project
			// that keeps symbol texts horizontal, undo that on the name
			// about the centre of its box, in the frame it is drawn in
			// (turned by label_rotation): its box stays where the element
			// puts it, but it reads as in the symbol (Element::keepReadable())
		const Element *element = qgraphicsitem_cast<Element *>(parentItem());
		const QTransform texts_transform = element ? element->symbolTextsTransform()
												   : QTransform();
		auto keep_readable = [painter, &texts_transform](const QRectF &rect, qreal label_rotation) {
			if (texts_transform.isIdentity()) return;
			const QTransform undo = QTransform().rotate(label_rotation)
									* texts_transform.inverted()
									* QTransform().rotate(-label_rotation);
			const QPointF c = rect.center();
			painter->setTransform(QTransform::fromTranslate(-c.x(), -c.y())
								  * undo
								  * QTransform::fromTranslate(c.x(), c.y()), true);
		};

		if (!qFuzzyIsNull(d->m_label_rotation)) {
			painter->save();
			painter->translate(label_pos);
			painter->rotate(d->m_label_rotation);

			qreal rx = 0, ry = 0;
			if (d->m_label_halignment & Qt::AlignLeft) rx = 0;
			else if (d->m_label_halignment & Qt::AlignHCenter) rx = -text_size.width() / 2.0;
			else if (d->m_label_halignment & Qt::AlignRight) rx = -text_size.width();

			if (d->m_label_valignment & Qt::AlignTop) ry = 0;
			else if (d->m_label_valignment & Qt::AlignVCenter) ry = -text_size.height() / 2.0;
			else if (d->m_label_valignment & Qt::AlignBottom) ry = -text_size.height();

			QRectF text_rect(QPointF(rx, ry), text_size);
			keep_readable(text_rect, d->m_label_rotation);
			painter->drawText(text_rect, static_cast<int>(d->m_label_halignment | d->m_label_valignment), display_name);
			painter->restore();
		} else {
			qreal dx = 0, dy = 0;
			if (d->m_label_halignment & Qt::AlignLeft) dx = 0;
			else if (d->m_label_halignment & Qt::AlignHCenter) dx = -text_size.width() / 2.0;
			else if (d->m_label_halignment & Qt::AlignRight) dx = -text_size.width();

			if (d->m_label_valignment & Qt::AlignTop) dy = 0;
			else if (d->m_label_valignment & Qt::AlignVCenter) dy = -text_size.height() / 2.0;
			else if (d->m_label_valignment & Qt::AlignBottom) dy = -text_size.height();

			QRectF text_rect(label_pos + QPointF(dx, dy), text_size);
			keep_readable(text_rect, 0);
			if (d->m_label_frame) {
				painter->drawRect(text_rect.adjusted(-1, -1, 1, 1));
			}
			painter->drawText(text_rect, static_cast<int>(d->m_label_halignment | d->m_label_valignment), display_name);
		}
	}

	painter -> restore();
}

/**
	@brief Terminal::drawHelpLine
	@param draw : true, display the help line
	false, hide it.
*/
void Terminal::drawHelpLine(bool draw)
{
	if (draw && !DiagramView::terminalHelpLinesShown()) draw = false;
	if (m_draw_help_line == draw) return;

	m_draw_help_line = draw;

	if (!draw)
	{
		if (m_help_line)
		{
			delete m_help_line;
			m_help_line = nullptr;
		}
		if (m_help_line_a)
		{
			delete m_help_line_a;
			m_help_line_a = nullptr;
		}
	}
}

/**
	@brief Terminal::HelpLine
	@return a line with coordinate P1 the dock point of conductor
	and P2 the border of diagram, according to the orientation of terminal
	The line is in scene coordinate;
*/
QLineF Terminal::HelpLine() const
{
	QPointF scene_dock = dockConductor();
	QRectF  rect       = diagram() -> border_and_titleblock.insideBorderRect();

	QLineF line(scene_dock , QPointF());

		//Set the second point of line to the edge of diagram,
		//according with the orientation of this terminal
	switch (orientation())
	{
		case Qet::North:
			line.setP2(QPointF(scene_dock.x(), rect.top()));
			break;
		case Qet::East:
			line.setP2(QPointF(rect.right() , scene_dock.y()));
			break;
		case Qet::South:
			line.setP2(QPointF(scene_dock.x(), rect.bottom()));
			break;
		case Qet::West:
			line.setP2(QPointF(rect.left(), scene_dock.y()));
			break;
	}

	return line;
}

/**
	@brief Terminal::boundingRect
	@return Le rectangle (en precision flottante) delimitant la borne et ses alentours.
	The room of a nozzle holds the mark of any connection, drawn or not: the
	room does not change when the project draws another one.
*/
QRectF Terminal::boundingRect() const
{
	const QRectF own = ownRect();
	if (d->m_medium != TerminalData::Pipe) {
		return own;
	}
		//The largest mark, with its pen; behind the point, the diamond of
		//two sizes at a contact
	const qreal h = PipeConnection::markHalfHeight + 3;
	return own.united(markTransform().mapRect(QRectF(-7, -h, 25, 2 * h)));
}

/**
	@brief Terminal::shape
	See the declaration: for any other terminal, its room, as before
*/
QPainterPath Terminal::shape() const
{
	QPainterPath path;
	path.addRect(ownRect());
	return path;
}

/**
	@brief Terminal::ownRect
	See the declaration
*/
QRectF Terminal::ownRect() const {
		//A nozzle keeps room for the DN under its name: that DN comes and
		//goes with its pipe, which does not tell the scene
	const QString display_name = d->m_medium == TerminalData::Pipe
			? TerminalData::labelText(name(), PipeDiameter::outside(99999)) : name();
	if (!d->m_show_name || display_name.isEmpty()) {
		return m_br;
	}

	QFontMetrics fm(labelFont());
	QSizeF text_size = TerminalData::labelSize(fm, display_name);
	if (d->m_medium == TerminalData::Pipe)
	{
			//Or for several DN a project says equal, "DN 60/65"
		const QString several = QStringLiteral("DN 00/000");
		const qreal width = TerminalData::labelSize(
					fm, name().isEmpty() ? several : name() + QLatin1Char('\n') + several).width();
		text_size.setWidth(qMax(text_size.width(), width));
	}
	QPointF label_pos = d->m_pos + d->m_label_pos;

	qreal dx = 0, dy = 0;
	if (d->m_label_halignment & Qt::AlignLeft) dx = 0;
	else if (d->m_label_halignment & Qt::AlignHCenter) dx = -text_size.width() / 2.0;
	else if (d->m_label_halignment & Qt::AlignRight) dx = -text_size.width();

	if (d->m_label_valignment & Qt::AlignTop) dy = 0;
	else if (d->m_label_valignment & Qt::AlignVCenter) dy = -text_size.height() / 2.0;
	else if (d->m_label_valignment & Qt::AlignBottom) dy = -text_size.height();

	QRectF label_rect(label_pos + QPointF(dx, dy), text_size);
	label_rect = label_rect.adjusted(-2, -2, 2, 2);

	return m_br.united(label_rect);
}

/**
	@brief Terminal::alignedWithTerminal
	If this terminal is aligned with an other terminal
	and is orientation is opposed return the other terminal
	else return nullptr
	@return
*/
Terminal* Terminal::alignedWithTerminal() const
{
	QLineF line(HelpLine());

	QPainterPath path;
	path.moveTo(line.p1());
	path.lineTo(line.p2());

	//Get all QGraphicsItem in the alignement of this terminal
	QList<QGraphicsItem *> qgi_list = diagram() -> items(path);

	//Remove all terminals of the parent element
	foreach (Terminal *t, parent_element_ -> terminals())
		qgi_list.removeAll(t);

	if (qgi_list.isEmpty()) return nullptr;

	//Get terminals only if orientation is opposed with this terminal
	QList <Terminal *>  available_terminals;
	foreach (QGraphicsItem *qgi, qgi_list)
	{
		if (Terminal *tt = qgraphicsitem_cast <Terminal *> (qgi))
		{
			//Call QET::lineContainsPoint to be sure the line intersect
			//the dock point and not an other part of terminal
			if (Qet::isOpposed(orientation(), tt -> orientation()) &&
				QET::lineContainsPoint(line, tt -> dockConductor()))
			{
				available_terminals << tt;
			}
		}
	}

	if (available_terminals.isEmpty())   return nullptr;
	if (available_terminals.size() == 1) return (available_terminals.first());

	//Available_terminals have several terminals, we get the nearest terminal
	line.setP2(available_terminals.first() -> dockConductor());
	qreal     current_lenght   = line.length();
	Terminal *nearest_terminal = available_terminals.takeFirst();

	//Search the nearest terminal to this one
	foreach (Terminal *terminal, available_terminals)
	{
		line.setP2(terminal -> dockConductor());
		if (line.length() < current_lenght)
		{
			current_lenght   = line.length();
			nearest_terminal = terminal;
		}
	}

	return nearest_terminal;
}

/**
	@brief Terminal::hoverEnterEvent
	Gere l'entree de la souris sur la zone de la Borne.
*/
void Terminal::hoverEnterEvent(QGraphicsSceneHoverEvent *)
{
	m_hovered = true;
	update();
}

/**
	@brief Terminal::hoverMoveEvent
	Gere les mouvements de la souris sur la zone de la Borne.
*/
void Terminal::hoverMoveEvent(QGraphicsSceneHoverEvent *) {}

/**
	@brief Terminal::hoverLeaveEvent
	Gere le fait que la souris sorte de la zone de la Borne.
*/
void Terminal::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
	m_hovered = false;
	update();
}

namespace {
		//With live automatic connections, AutoConnect lays the wires: the
		//user steers them with the symbols, a corner, a T-junction or an
		//interruption point, and draws none of their own
	bool wiredAutomatically(const Terminal *terminal)
	{
		return AutoConnect::isOn(terminal->diagram());
	}
}

/**
	@brief Terminal::mousePressEvent
	Gere le fait qu'on enfonce un bouton de la souris sur la Borne.
	@param e L'evenement souris correspondant
*/
void Terminal::mousePressEvent(QGraphicsSceneMouseEvent *e)
{
	if (wiredAutomatically(this)) {
		e->ignore();
		return;
	}
	if (Diagram *diag = diagram()) {
		diag -> setConductorStart(mapToScene(QPointF(d->m_pos)));
		diag -> setConductorStop(e -> scenePos());
		diag -> setConductor(true);
		//setCursor(Qt::CrossCursor);
	}
}

/**
	@brief Terminal::mouseMoveEvent
	Gere le fait qu'on bouge la souris sur la Borne.
	@param e L'evenement souris correspondant
*/
void Terminal::mouseMoveEvent(QGraphicsSceneMouseEvent *e)
{
	// pendant la pose d'un conducteur, on adopte un autre curseur
	//setCursor(Qt::CrossCursor);

	// d'un mouvement a l'autre, il faut retirer l'effet hover de la borne precedente
	if (m_previous_terminal) {
		if (m_previous_terminal == this) m_hovered = true;
		else m_previous_terminal -> m_hovered = false;
		m_previous_terminal -> m_hovered_color = m_previous_terminal -> neutralColor;
		m_previous_terminal -> update();
	}

	Diagram *diag = diagram();
	if (!diag) return;
	// si la scene est un Diagram, on actualise le poseur de conducteur
	diag -> setConductorStop(e -> scenePos());

	// on recupere la liste des qgi sous le pointeur
	QList<QGraphicsItem *> qgis = diag -> items(e -> scenePos());

	/* le qgi le plus haut
	   = le poseur de conductor
	   = le premier element de la liste
	   = la liste ne peut etre vide
	   = on prend le deuxieme element de la liste
	*/
	Q_ASSERT_X(!(qgis.isEmpty()), "Terminal::mouseMoveEvent", "La liste d'items ne devrait pas etre vide");

	// sinon on prend le deuxieme element de la liste et on verifie s'il s'agit d'une borne
	Terminal *other_terminal = qgis.size() > 1
			? qgraphicsitem_cast<Terminal *>(qgis.at(1))
			: nullptr;
	if (!other_terminal)
	{
			//Not on a terminal: a pipe is caught by a free nozzle nearby,
			//and the preview shows where it goes
		other_terminal = magnetTarget(e->scenePos(), magnetSceneRadius(e));
		if (!other_terminal) return;
		diag->setConductorStop(other_terminal->dockConductor());
	}
	m_previous_terminal = other_terminal;

	// s'il s'agit d'une borne, on lui applique l'effet hover approprie
	if (!canBeLinkedTo(other_terminal) || wiredAutomatically(other_terminal)) {
		other_terminal -> m_hovered_color = forbiddenColor;
	} else if (other_terminal -> conductorsCount() || !sameMediumAs(other_terminal)) {
			//orange: allowed, but worth a look (the terminal is already
			//wired, or it connects another medium)
		other_terminal -> m_hovered_color = warningColor;
	} else {
		other_terminal -> m_hovered_color = allowedColor;
	}

	other_terminal -> m_hovered = true;
	other_terminal -> update();
}


/**
	@brief Terminal::mouseReleaseEvent
	@param e
*/
void Terminal::mouseReleaseEvent(QGraphicsSceneMouseEvent *e)
{
	m_previous_terminal = nullptr;
	m_hovered_color     = neutralColor;

	if (!diagram()) return;

	//Stop conductor preview
	diagram() -> setConductor(false);

	//Get item under cursor: it must be a terminal
	QGraphicsItem *qgi = diagram() -> itemAt(e -> scenePos(), QTransform());
	Terminal *other_terminal = qgi ? qgraphicsitem_cast<Terminal *>(qgi) : nullptr;
		//else a pipe is caught by a free nozzle nearby (magnetTarget())
	if (!other_terminal) {
		other_terminal = magnetTarget(e->scenePos(), magnetSceneRadius(e));
	}
	if (!other_terminal) return;

	other_terminal -> m_hovered_color = neutralColor;
	other_terminal -> m_hovered       = false;

	if (wiredAutomatically(other_terminal))
		return;

	//We stop her if we can't link this terminal with other terminal
	if (!canBeLinkedTo(other_terminal))
	{
			//Say why when it is the wire limit (discussion #1158): a red
			//terminal alone does not tell the user what to do instead.
		const Terminal *full = !hasRoomForWire() ? this
							 : !other_terminal->hasRoomForWire() ? other_terminal
							 : nullptr;
		if (full && full->medium() == TerminalData::Pipe) {
			QToolTip::showText(e->screenPos(),
							   tr("This nozzle is already connected: a nozzle takes one pipe. "
								  "Draw a branch on the pipe instead.",
								  "pipe refused by a nozzle already connected"));
		} else if (full) {
			QToolTip::showText(e->screenPos(),
							   tr("This terminal already has %n conductors, the project's "
								  "limit. Add a terminal to connect one more conductor.",
								  "wire refused by the wires-per-terminal limit",
								  full->wireLimit()));
		} else if (refusesMediumOf(other_terminal)) {
			QToolTip::showText(e->screenPos(),
							   tr("These terminals connect different media (%1, %2): the "
								  "project's wiring rules do not join them.",
								  "connection refused between terminals of different media")
							   .arg(mediumName(medium()), mediumName(other_terminal->medium())));
		}
		return;
	}

		//Different media, and the project does not refuse it: joined, but
		//say so, since it is seldom meant
	if (!sameMediumAs(other_terminal)) {
		QToolTip::showText(e->screenPos(),
						   tr("These terminals connect different media (%1, %2).",
							  "connection between terminals of different media")
						   .arg(mediumName(medium()), mediumName(other_terminal->medium())));
	}

	connectTo(other_terminal);
}

/**
	@brief Terminal::connectTo
	What a release of the mouse on @p other_terminal does once it may be
	linked to this terminal: a conductor between them, with the properties
	of its potential or a number of its own, in one undo step. Shared by
	the drag of the folio and the pipe drawing tool.
	@param other_terminal : where the conductor ends
	@return the new conductor, nullptr when they may not be linked
*/
Conductor *Terminal::connectTo(Terminal *other_terminal)
{
	if (!diagram() || !other_terminal || !canBeLinkedTo(other_terminal)) {
		return nullptr;
	}

	//Create conductor
	Conductor *new_conductor = new Conductor(this, other_terminal);

	//Get all conductors at the same potential of new conductors
	QSet <Conductor *> conductors_list = new_conductor->relatedPotentialConductors();

	//Compare the properties of every conductors stored in conductors_list,
	//if every conductors properties is equal, we use this properties for the new conductor.
	ConductorProperties others_properties;
	bool use_properties = false;
	if (!conductors_list.isEmpty())
	{
		use_properties = true;
		others_properties = (*conductors_list.begin())->properties();
		foreach (Conductor *conductor, conductors_list) {
			if (conductor->properties() != others_properties)
				use_properties = false;
		}
	}


	QUndoCommand *undo = new QUndoCommand();
	QUndoCommand *aic = new AddGraphicsObjectCommand(new_conductor, diagram(), QPointF(), undo);
	undo->setText(aic->text());

	if (use_properties)
	{
		Conductor *other = conductors_list.values().first();
		new_conductor->replaceSequenceNum(other->sequenceNum());
		new_conductor->setProperties(others_properties);
	}
	else
	{
		//Autonum it
		ConductorAutoNumerotation can (new_conductor, diagram(), undo);
		can.numerate();
	}
	//Add undo command to the parent diagram
	diagram() -> undoStack().push(undo);
	if (use_properties)
	{
		Conductor *other = conductors_list.values().first();
		new_conductor->setProperties(other->properties());
	}
	return new_conductor;
}

/**
	@brief Terminal::showHover
	@param hovered : whether the terminal is drawn as hovered
	@param color : the color of the effect
*/
void Terminal::showHover(bool hovered, const QColor &color)
{
	m_hovered = hovered;
	m_hovered_color = color;
	update();
}

/**
	@brief Terminal::updateConductor
	Update the path of conductor docked to this terminal
*/
void Terminal::updateConductor()
{
	foreach (Conductor *conductor, m_conductors_list)
		conductor->updatePath();
}

/**
	@brief Terminal::isLinkedTo
	@param other_terminal Autre borne
	@return true si cette borne est reliee a other_terminal, false sion
*/
bool Terminal::isLinkedTo(Terminal *other_terminal) {
	if (other_terminal == this) return(false);

	bool already_linked = false;
	foreach (Conductor *c, m_conductors_list) {
		if (c -> terminal1 == other_terminal || c -> terminal2 == other_terminal) {
			already_linked = true;
			break;
		}
	}
	return(already_linked);
}

/**
	@brief Terminal::canBeLinkedTo
	Checking if the terminal can be linked to \p other_terminal or not
	Reasons for not linable:
	 - \p other_terminal is this terminal
	 - this terminal is already connected to \p other_terminal
	 - either terminal already has as many wires as the project allows
	 - the terminals connect different media and the project refuses it
	@param other_terminal
	@return true if this terminal can be linked to other_terminal,
	otherwise false
*/
bool Terminal::canBeLinkedTo(Terminal *other_terminal)
{
	if (other_terminal == this || isLinkedTo(other_terminal))
		return false;

	if (!hasRoomForWire() || !other_terminal->hasRoomForWire())
		return false;

	if (refusesMediumOf(other_terminal))
		return false;

	return true;
}

/**
	@brief Terminal::sameMediumAs
	@param other
	@return true if this terminal and @p other connect the same medium
	(TerminalData::Medium): two electrical terminals, two nozzles...
*/
bool Terminal::sameMediumAs(const Terminal *other) const
{
	return other && d->m_medium == other->d->m_medium;
}

/**
	@brief Terminal::refusesMediumOf
	@param other
	@return true if this terminal may not be joined to @p other because they
	connect different media and the project's wiring rules refuse it. Off by
	default: two terminals of different media are then joined, and the user
	is only told while drawing.
*/
bool Terminal::refusesMediumOf(const Terminal *other) const
{
	if (!other || sameMediumAs(other)) {
		return false;
	}
	const Diagram *parent_diagram = diagram();
	if (!parent_diagram || !parent_diagram->project()) {
		return false;
	}
	return WiringRules::refusesMixedMedia(parent_diagram->project()->wiringRules(),
										  WiringRules::masterEnabled());
}

/**
	@brief Terminal::magnetTarget
	Drawing a pipe at a low zoom, hitting a nozzle exactly is hard: a pipe
	drawn from this nozzle and released near another one, rather than on
	it, is caught by it. Only nozzles catch pipes, so that the wires keep
	the behaviour they always had.
	@param scene_pos : where the mouse is, on the folio
	@param radius : how near, in units of the folio
	@return the nozzle nearest to @p scene_pos within @p radius that this
	one may be linked to (free, of another symbol), or nullptr; always nullptr
	when this terminal is not a nozzle
*/
Terminal *Terminal::magnetTarget(const QPointF &scene_pos, qreal radius)
{
	Diagram *parent_diagram = diagram();
	if (d->m_medium != TerminalData::Pipe || !parent_diagram || radius <= 0) {
		return nullptr;
	}

	const QRectF area(scene_pos.x() - radius, scene_pos.y() - radius,
					  2 * radius, 2 * radius);
	Terminal *nearest = nullptr;
	qreal nearest_distance = 0;
	for (QGraphicsItem *item : parent_diagram->items(area))
	{
		Terminal *terminal = qgraphicsitem_cast<Terminal *>(item);
			//Not the other nozzles of its own symbol: released just off the
			//nozzle it starts from, a pipe would loop back to its symbol
		if (!terminal || terminal == this
				|| terminal->parentElement() == parentElement()
				|| !sameMediumAs(terminal) || !canBeLinkedTo(terminal)) {
			continue;
		}
		const qreal distance = QLineF(scene_pos, terminal->dockConductor()).length();
		if (distance > radius) {
			continue;
		}
		if (!nearest || distance < nearest_distance) {
			nearest = terminal;
			nearest_distance = distance;
		}
	}
	return nearest;
}

/**
	@brief Terminal::wireLimit
	@return the most wires this terminal may take under its project's
	wiring rules (discussion #1158), 0 for no limit. A nozzle (a terminal
	of the pipe medium) takes one, whatever the rules: it is a single
	flange, and a branch is drawn on the pipe, not on the nozzle.
*/
int Terminal::wireLimit() const
{
	if (d->m_medium == TerminalData::Pipe) {
		return 1;
	}
	const Diagram *parent_diagram = diagram();
	if (!parent_diagram || !parent_diagram->project()) {
		return 0;
	}
	const Element *element = parentElement();
	const bool is_report = element
			&& (element->linkType() & (Element::NextReport | Element::PreviousReport));
	return WiringRules::limit(parent_diagram->project()->wiringRules(),
							  WiringRules::masterEnabled(),
							  is_report);
}

/**
	@brief Terminal::hasRoomForWire
	@return true if one more wire may be connected to this terminal
*/
bool Terminal::hasRoomForWire() const
{
	return WiringRules::hasRoom(wireLimit(), conductorsCount());
}

/**
	@brief Terminal::conductors
	@return La liste des conducteurs lies a cette borne
*/
QList<Conductor *> Terminal::conductors() const
{
	return(m_conductors_list);
}

/**
	@brief Terminal::toXml
	Methode d'export en XML
	@param doc Le Document XML a utiliser pour creer l'element XML
	@return un QDomElement representant cette borne
*/
QDomElement Terminal::toXml(QDomDocument &doc) const
{
	QDomElement qdo = doc.createElement("terminal");

	// for backward compatibility
	qdo.setAttribute("x", QString("%1").arg(dock_elmt_.x()));
	qdo.setAttribute("y", QString("%1").arg(dock_elmt_.y()));
	// end for backward compatibility

	qdo.setAttribute("orientation", d->m_orientation);
	return(qdo);
}

/**
	@brief Terminal::valideXml
	Permet de savoir si un element XML represente une borne
	@param terminal Le QDomElement a analyser
	@return true si le QDomElement passe en parametre est une borne, false sinon
*/
bool Terminal::valideXml(QDomElement &terminal)
{
	// verifie le nom du tag
	if (terminal.tagName() != "terminal") return(false);

	// verifie la presence des attributs minimaux
	if (!terminal.hasAttribute("x")) return(false);
	if (!terminal.hasAttribute("y")) return(false);
	if (!terminal.hasAttribute("orientation")) return(false);

	bool conv_ok;
		//QString::toDouble() accepts "nan"/"inf"/"-inf" and reports a
		//successful conversion for them, so conv_ok alone doesn't reject a
		//non-finite coordinate -- see the matching comment in
		//Element::valideXml().
	// parse l'abscisse
	double x = terminal.attribute("x").toDouble(&conv_ok);
	if (!conv_ok || !qIsFinite(x)) return(false);

	// parse l'ordonnee
	double y = terminal.attribute("y").toDouble(&conv_ok);
	if (!conv_ok || !qIsFinite(y)) return(false);

	// parse l'id
	terminal.attribute("id").toInt(&conv_ok);
	if (!conv_ok) return(false);

	// parse l'orientation
	int terminal_or = terminal.attribute("orientation").toInt(&conv_ok);
	if (!conv_ok) return(false);
	if (terminal_or != Qet::North
			&& terminal_or != Qet::South
			&& terminal_or != Qet::East
			&& terminal_or != Qet::West) return(false);

	// a ce stade, la borne est syntaxiquement correcte
	return(true);
}

/**
	@brief Terminal::fromXml
	Enables you to find out whether an XML element represents this terminal.
	Warning, the XML element is not checked
	Permet de savoir si un element XML represente cette borne. Attention,
	l'element XML n'est pas verifie
	@param terminal Le QDomElement a analyser / QDomElement to check
	@return true si la borne "se reconnait"
	(memes coordonnes, meme orientation), false sinon
	true, if the terminal ‘recognises’ itself (same coordinates,
	same orientation), false otherwise
*/
bool Terminal::fromXml(QDomElement &terminal)
{
	return (
		qFuzzyCompare(terminal.attribute("x").toDouble(), dock_elmt_.x()) &&
		qFuzzyCompare(terminal.attribute("y").toDouble(), dock_elmt_.y()) &&
		(terminal.attribute("orientation").toInt() == d->m_orientation)
	);
}

/**
	@brief Terminal::dockConductor
	@return the position, relative to the scene, of the docking point for
	conductors.
*/
QPointF Terminal::dockConductor() const
{
	return(mapToScene(d->m_pos));
}

/**
	@brief Terminal::diagram
	@return le Diagram auquel cette borne appartient,
	ou 0 si cette borne est independant
*/
Diagram *Terminal::diagram() const
{
	return(qobject_cast<Diagram *>(scene()));
}

/**
	@brief Terminal::parentElement
	@return L'element auquel cette borne est rattachee
*/
Element *Terminal::parentElement() const
{
	return(parent_element_);
}

QUuid Terminal::uuid() const
{
	return d->m_uuid;
}

/**
	@brief Terminal::setUuid
	Give this terminal the uuid of the terminal that replaces it in another
	definition of its symbol (see Element::changeLocation()), so that the
	conductors saved against it find it again when the project is reopened.
	@param uuid
*/
void Terminal::setUuid(const QUuid &uuid)
{
	d->m_uuid = uuid;
}

/**
	@brief Terminal::stableUuid
	An identity for this terminal that exists on every element, not only on
	those saved by a uuid-aware element editor.

	uuid() comes from the catalog .elmt definition and is empty for every
	element authored before that field existed -- which is most of the
	installed base. Anything keyed on uuid() alone therefore cannot see those
	elements at all.

	When there is no uuid, derive one from the terminal's local position and
	orientation inside its element. That is not an arbitrary choice: it is the
	same thing the project format itself uses to match a conductor back to a
	terminal ("each connection is made by using the local position of the
	terminal and a dynamic id" -- TerminalData::m_uuid). m_pos is the position
	read from the definition and is not touched by moving the element on the
	folio, so the result is stable across loads, saves and folio moves, and it
	is unique within an element except where a definition genuinely declares
	two terminals at the same point -- three cases in the whole example
	corpus. Opening a project gives every terminal of its symbols a uuid
	(TerminalUuids::fillMissing()), the same value for all but the second
	of such a pair, so in a project this is the fallback for terminals of a
	symbol imported since it was opened.

	Derived values are UUID v5 in a fixed namespace, so they are reproducible
	without being written to the file, and cannot collide with the v4 uuids
	the element editor generates.

	@return the terminal's own uuid when it has one, otherwise a derived one
*/
/**
	@brief Terminal::newUuid
	Give this terminal a brand new identity: a copied terminal carries
	the uuid of the one it was copied from, and two terminals of one
	name would have every end docked to one of them stolen by the other.
*/
void Terminal::newUuid()
{
	d->m_uuid = QUuid::createUuid();
}

QUuid Terminal::stableUuid() const
{
	if (!d->m_uuid.isNull()) {
		return d->m_uuid;
	}
	return derivedUuid();
}

/**
	@brief Terminal::derivedUuid
	The identity worked out from this terminal's local position and
	orientation, whether or not it carries a uuid of its own: the value
	stableUuid() gives when it has none, and the one a project gives it
	when it is opened (TerminalUuids::fillMissing()).

	Position and orientation only. The name is deliberately excluded: it
	is not stable across a save cycle -- QET rewrites a terminal named "_"
	as unnamed, which would silently change the identity of 1421 of
	industrial.qet's 1790 terminals on the first resave. It is also not
	needed: keying on geometry alone produces exactly the same number of
	collisions across the example corpus, and it means renaming a terminal
	does not change what it is.

	A second terminal at the same point with the same orientation gets the
	next occurrence (see setPlaceRank()), as TerminalUuids::fillMissing()
	gives it, so the two terminals of such a pair are told apart.
*/
QUuid Terminal::derivedUuid() const
{
	return TerminalUuids::derived(d->m_pos.x(),
								  d->m_pos.y(),
								  static_cast<int>(d->m_orientation),
								  m_place_rank);
}

/**
	@brief Terminal::setPlaceRank
	@param rank : how many terminals of the definition, before this one in
	document order, sit at the same point with the same orientation.
	Set by Element::parseTerminal().
*/
void Terminal::setPlaceRank(int rank)
{
	m_place_rank = rank;
}

QList<Terminal *> Terminal::runSiblings() const
{
	QList<Terminal *> siblings;
	if (d->m_medium != TerminalData::Pipe || !parent_element_) {
		return siblings;
	}
	const QStringList run = parent_element_->pipeRun();
	if (!run.contains(d->m_name)) {
		return siblings;
	}
	for (Terminal *terminal : parent_element_->terminals()) {
		if (terminal != this && terminal->medium() == TerminalData::Pipe
			&& run.contains(terminal->d->m_name)) {
			siblings << terminal;
		}
	}
	return siblings;
}

bool Terminal::followsPipe() const
{
	if (d->m_medium != TerminalData::Pipe || !parent_element_) {
		return false;
	}
	if (!runSiblings().isEmpty()) {
		return true;
	}
		//The only nozzle of a piece of pipe: a free end, a vent
	if (!parent_element_->followsPipes()) {
		return false;
	}
	for (const Terminal *terminal : parent_element_->terminals()) {
		if (terminal != this && terminal->medium() == TerminalData::Pipe) {
			return false;
		}
	}
	return true;
}

PipeDiameter Terminal::ownDiameter() const
{
	if (d->m_medium != TerminalData::Pipe || followsPipe()) {
		return PipeDiameter();
	}
	PipeDiameter given = d->m_diameter;
	if (parent_element_) {
		const NozzleDiameters own = parent_element_->nozzleDiameters();
		const auto it = own.constFind(d->m_name);
		if (it != own.constEnd()) {
			given = it.value();
		}
	}
		//A DN on a thread, or the reverse: by the fixed table
	return PipeConnection::fitted(connection(), given);
}

PipeConnection::Type Terminal::connection() const
{
	if (d->m_medium != TerminalData::Pipe) {
		return PipeConnection::Flange;
	}
	if (parent_element_) {
		const NozzleConnections own = parent_element_->nozzleConnections();
		const auto it = own.constFind(d->m_name);
		if (it != own.constEnd()) {
			return it.value();
		}
	}
	return d->m_connection;
}

void Terminal::setSymbolConnection(PipeConnection::Type connection)
{
	d->m_connection = connection;
	updateDiameterToolTip();
	updateConnectionMark();
}

PipeConnection::MarkChoice Terminal::markChoice() const
{
	if (d->m_medium != TerminalData::Pipe || !parent_element_) {
		return PipeConnection::MarkChoice::Project;
	}
	return parent_element_->nozzleMarks().value(d->m_name, PipeConnection::MarkChoice::Project);
}

bool Terminal::drawsConnectionMark() const
{
	if (d->m_medium != TerminalData::Pipe) {
		return false;
	}
	const Diagram *folio = diagram();
	if (!folio || !folio->project()) {
		return false;
	}
	const PipeConnection::Type type = connection();
	switch (markChoice()) {
		case PipeConnection::MarkChoice::Never:
			return false;
		case PipeConnection::MarkChoice::Always:
			break;
		case PipeConnection::MarkChoice::Project:
			if (!folio->project()->pidCodification().drawsConnection(PipeConnection::drawnWith(type))) {
				return false;
			}
			break;
	}
	return !touchesNozzle();
}

qreal Terminal::markDistance() const
{
	return parent_element_ && parent_element_->isPipeInline()
			? PipeConnection::inlineMarkDistance : 0;
}

bool Terminal::drawsOwnMark() const
{
	if (!drawsConnectionMark() || !PipeConnection::hasOwnMark(connection())) {
		return false;
	}
	return connection() != PipeConnection::Flange
			|| !parent_element_ || !parent_element_->isPipeInline();
}

bool Terminal::touchesNozzle() const
{
	for (const Conductor *conductor : m_conductors_list) {
		if (conductor->endsTouch()) {
			return true;
		}
	}
		//Placed against each other without a pipe
	const QGraphicsScene *folio = scene();
	if (!folio) {
		return false;
	}
	const QPointF dock = dockConductor();
	for (QGraphicsItem *item : folio->items(dock)) {
		const Terminal *other = qgraphicsitem_cast<Terminal *>(item);
		if (other && other != this && other->parent_element_ != parent_element_
			&& other->d->m_medium == TerminalData::Pipe && other->dockConductor() == dock) {
			return true;
		}
	}
	return false;
}

void Terminal::updateConnectionMark()
{
	update();
	if (d->m_medium != TerminalData::Pipe) {
		return;
	}
	for (Conductor *conductor : std::as_const(m_conductors_list)) {
		conductor->update();
	}
}

QTransform Terminal::markTransform() const
{
	qreal angle = 0;
	switch (d->m_orientation) {
		case Qet::East:  angle = 0;   break;
		case Qet::South: angle = 90;  break;
		case Qet::West:  angle = 180; break;
		case Qet::North: angle = 270; break;
	}
	const QPointF c = mapFromParent(d->m_pos);
	return QTransform().rotate(angle) * QTransform::fromTranslate(c.x(), c.y());
}

/**
	@brief Terminal::drawConnectionMark
	Draw the mark of the connection of this nozzle in the ink of its symbol,
	with the lines of a valve (one unit), but never thinner than a pixel on
	the screen
	@param painter
	@param detail : the level of detail of the drawing
*/
void Terminal::drawConnectionMark(QPainter *painter, qreal detail) const
{
	QPainterPath strokes, fills;
	PipeConnection::markShapes(connection(), markDistance(), &strokes, &fills);
	const QColor ink = parent_element_ ? parent_element_->symbolInk() : QColor(Qt::black);
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setTransform(markTransform(), true);
	QPen pen(ink, 1.0, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
	const bool on_screen = painter->device()
			&& painter->device()->devType() == QInternal::Widget;
	if (on_screen && detail < 1.0) {
		pen.setCosmetic(true);
	}
	painter->setPen(pen);
	painter->setBrush(Qt::NoBrush);
	painter->drawPath(strokes);
	painter->setPen(Qt::NoPen);
	painter->setBrush(ink);
	painter->drawPath(fills);
	painter->restore();
}

PipeDiameter Terminal::connectedDiameter(const Conductor **pipe) const
{
	if (d->m_medium != TerminalData::Pipe) {
		return PipeDiameter();
	}
	for (const Conductor *conductor : m_conductors_list) {
		if (conductor->medium() == TerminalData::Pipe) {
			const PipeDiameter size = conductor->properties().m_pipe.lineSize();
			if (pipe && size.isSet()) {
				*pipe = conductor;
			}
			return size;
		}
	}
	return PipeDiameter();
}

QString Terminal::sizeText(const PipeDiameter &size, const Conductor *pipe) const
{
	static const PidCodification none;
	const Diagram *folio = diagram();
	const PidCodification &codification = folio && folio->project()
			? folio->project()->pidCodification() : none;
	return pipe ? codification.text(size, pipe->properties().m_pipe.material)
				: size.text(codification.locale());
}

PipeDiameter Terminal::comparedDiameter() const
{
	if (ownDiameter().isOutside()) {
		for (const Conductor *conductor : m_conductors_list) {
			if (conductor->medium() == TerminalData::Pipe) {
				const PipeDiameter outside = conductor->properties().m_pipe.outside;
				if (outside.isSet()) {
					return outside;
				}
				break;
			}
		}
	}
	return facingDiameter();
}

Terminal *Terminal::contactNozzle() const
{
	if (d->m_medium != TerminalData::Pipe) {
		return nullptr;
	}
		//Joined by a pipe of no length, which is neither drawn nor clicked
		//(Conductor::endsTouch()): the automatic connection makes one
	for (const Conductor *conductor : m_conductors_list) {
		if (conductor->medium() != TerminalData::Pipe || !conductor->endsTouch()) {
			continue;
		}
		Terminal *other = conductor->terminal1 == this ? conductor->terminal2 : conductor->terminal1;
		if (other && other != this && other->d->m_medium == TerminalData::Pipe) {
			return other;
		}
	}
	if (!m_conductors_list.isEmpty()) {
		return nullptr;
	}
	const QGraphicsScene *folio = scene();
	if (!folio) {
		return nullptr;
	}
	const QPointF dock = dockConductor();
	for (QGraphicsItem *item : folio->items(dock)) {
		Terminal *other = qgraphicsitem_cast<Terminal *>(item);
		if (other && other != this && other->parent_element_ != parent_element_
			&& other->d->m_medium == TerminalData::Pipe
			&& other->m_conductors_list.isEmpty() && other->dockConductor() == dock) {
			return other;
		}
	}
	return nullptr;
}

QList<Terminal *> Terminal::contactGroup() const
{
	QList<Terminal *> group{const_cast<Terminal *>(this)};
	for (int i = 0 ; i < group.size() ; ++i)
	{
		QList<Terminal *> next = group.at(i)->runSiblings();
		if (Terminal *against = group.at(i)->contactNozzle()) {
			next << against;
		}
		for (Terminal *nozzle : std::as_const(next)) {
			if (!group.contains(nozzle)) {
				group << nozzle;
			}
		}
	}
	return group;
}

PipeDiameter Terminal::lineDiameter(QList<const Terminal *> todo, QList<const Terminal *> seen,
									bool pipes_only, const Conductor **pipe_found)
{
	seen << todo;
	auto add = [&todo, &seen](const QList<Terminal *> &nozzles) {
		for (const Terminal *nozzle : nozzles) {
			if (!seen.contains(nozzle)) {
				seen << nozzle;
				todo << nozzle;
			}
		}
	};
	for (int i = 0 ; i < todo.size() ; ++i)
	{
		const Terminal *nozzle = todo.at(i);
		const PipeDiameter pipe = nozzle->connectedDiameter(pipe_found);
		if (pipe.isSet()) {
			return pipe;
		}
			//Against the nozzle of another symbol: its size, or its line
		const Terminal *against = nozzle->contactNozzle();
		if (against && !seen.contains(against)) {
			seen << against;
			const PipeDiameter own = against->ownDiameter();
			if (own.isSet() && !pipes_only) {
				return own;
			}
			add(against->runSiblings());
		}
		add(nozzle->runSiblings());
	}
	return PipeDiameter();
}

PipeDiameter Terminal::facingDiameter(const Conductor **pipe) const
{
	const PipeDiameter connected = connectedDiameter(pipe);
	if (connected.isSet()) {
		return connected;
	}
		//Against another nozzle, or joined to it by a pipe of no length
		//which was given no size
	const Terminal *against = contactNozzle();
	if (!against) {
		return PipeDiameter();
	}
	const auto constList = [](const QList<Terminal *> &nozzles) {
		QList<const Terminal *> list;
		for (const Terminal *nozzle : nozzles) {
			list << nozzle;
		}
		return list;
	};
	const PipeDiameter own = against->ownDiameter();
	if (own.isSet()) {
			//The size lives on the line: a nozzle which follows a line
			//with a size keeps it, and the nozzle against it marks the
			//difference; without one, it has the size of that nozzle
		const PipeDiameter mine = lineDiameter(constList(runSiblings()), {this, against}, true, pipe);
		return mine.isSet() ? mine : own;
	}
		//The line beyond the symbol placed against this nozzle
	return lineDiameter(constList(against->runSiblings()), {this, against}, false, pipe);
}

PipeDiameter Terminal::pipeDiameter(const Conductor **pipe) const
{
	const PipeDiameter facing = facingDiameter(pipe);
	if (facing.isSet()) {
		return facing;
	}
		//What is brought to another nozzle of its run
	QList<const Terminal *> siblings;
	for (const Terminal *sibling : runSiblings()) {
		siblings << sibling;
	}
	QList<const Terminal *> seen{this};
	if (const Terminal *against = contactNozzle()) {
		seen << against;
	}
	return lineDiameter(siblings, seen, false, pipe);
}

const Terminal *Terminal::runSiblingOtherThan(const PipeDiameter &pipe) const
{
	for (const Terminal *sibling : runSiblings()) {
		if (compareDiameters(pipe, sibling->facingDiameter()) != PidCodification::DiameterMatch::Same) {
			return sibling;
		}
	}
	return nullptr;
}

void Terminal::refreshSizes()
{
	for (Terminal *nozzle : contactGroup()) {
		nozzle->updateDiameterToolTip();
		nozzle->update();
	}
}

void Terminal::refreshContact()
{
	if (d->m_medium != TerminalData::Pipe) {
		return;
	}
	Terminal *now = contactNozzle();
	Terminal *before = m_contact.data();
	if (now == before) {
		return;
	}
	m_contact = now;
	if (before && before->m_contact == this) {
		before->m_contact = nullptr;
	}
	if (now) {
		now->m_contact = this;
	}
		//Both sides: the one it left has other sizes too
	refreshSizes();
	if (before) {
		before->refreshSizes();
	}
}

bool Terminal::marksContactGap() const
{
	const Terminal *against = contactNozzle();
		//Either of the two: the symbol of one may hide the other
	return against && (hasDiameterGap() || against->hasDiameterGap());
}

PipeDiameter Terminal::diameter() const
{
	const PipeDiameter own = ownDiameter();
	return own.isSet() ? own : pipeDiameter();
}

PidCodification::DiameterMatch Terminal::compareDiameters(const PipeDiameter &a,
														  const PipeDiameter &b) const
{
	const Diagram *folio = diagram();
	if (folio && folio->project()) {
		return folio->project()->pidCodification().compare(a, b);
	}
	return PidCodification().compare(a, b);
}

bool Terminal::hasDiameterGap() const
{
	const PipeDiameter pipe = facingDiameter();
	if (!pipe.isSet()) {
		return false;
	}
		//Another pipe of its run, which differs
	if (runSiblingOtherThan(pipe)) {
		return true;
	}
	return compareDiameters(ownDiameter(), comparedDiameter()) != PidCodification::DiameterMatch::Same;
}

/**
	@brief Terminal::updateDiameterToolTip
	Say how a pipe is joined to a nozzle, and what differs on a nozzle
	marked by its element (hasDiameterGap())
*/
void Terminal::updateDiameterToolTip()
{
	if (d->m_medium != TerminalData::Pipe) {
		return;
	}
		//The pipes these sizes are of, when they are those of pipes: a
		//pipe without material is written with the DN said equal to its own
	const Conductor *line = nullptr;
	const Conductor *other_line = nullptr;
	const PipeDiameter pipe = facingDiameter(&line);
	const Terminal *sibling = runSiblingOtherThan(pipe);
	const PipeDiameter other = sibling ? sibling->facingDiameter(&other_line) : pipe;
	if (!sibling) {
		other_line = line;
	}
	const PipeDiameter own = ownDiameter();
	const QLocale locale = sizeLocale();
	const auto unknown = tr("The materials of the project give %1 no DN, or not the same.");
	QString tip = PipeConnection::name(connection());
	const auto match = compareDiameters(pipe, other);
	if (!hasDiameterGap()) {
			//The connection alone
	} else if (match != PidCodification::DiameterMatch::Same) {
		tip += QLatin1Char('\n') + tr("Inline item between %1 and %2, without a reducer")
								   .arg(sizeText(pipe, line), sizeText(other, other_line));
		if (match == PidCodification::DiameterMatch::Unknown) {
			tip += QLatin1Char('\n') + unknown.arg((pipe.isOutside() ? pipe : other).text(locale));
		}
	} else {
			//An outside diameter against that of the pipe, when it has one
		const PipeDiameter compared = comparedDiameter();
		tip += QLatin1Char('\n') + (contactNozzle()
				? tr("Nozzle %1 against %2, without a pipe")
				: tr("Nozzle %1, pipe %2")).arg(own.text(locale),
												sizeText(compared, compared == pipe ? line : nullptr));
		if (compareDiameters(own, compared) == PidCodification::DiameterMatch::Unknown) {
			tip += QLatin1Char('\n') + unknown.arg((compared.isOutside() ? compared : own).text(locale));
		}
	}
	setToolTip(tip);
		//The pipe draws the mark
	for (Conductor *conductor : std::as_const(m_conductors_list)) {
		conductor->update();
	}
}

void Terminal::setSymbolDiameter(const PipeDiameter &diameter)
{
	d->m_diameter = diameter;
	updateDiameterToolTip();
	update();
}

QString Terminal::labelText() const
{
	const Diagram *folio = diagram();
	if (folio && folio->project() && !folio->project()->pidCodification().show_nozzle_sizes) {
		return name();
	}
		//Its own size, else that of its line, written as the project
		//writes the size of this pipe
	const Conductor *pipe = nullptr;
	PipeDiameter size = ownDiameter();
	if (!size.isSet()) {
		size = pipeDiameter(&pipe);
	}
	if (!size.isSet()) {
		return name();
	}
	const QString shown = sizeText(size, pipe);
	return name().isEmpty() ? shown : name() + QLatin1Char('\n') + shown;
}

QLocale Terminal::sizeLocale() const
{
	const Diagram *folio = diagram();
	return folio && folio->project() ? folio->project()->pidCodification().locale()
									 : PidCodification().locale();
}

QFont Terminal::labelFont() const
{
	QFont font = d->m_label_font;
	const Diagram *folio = diagram();
	if (d->m_medium == TerminalData::Pipe && folio && folio->project())
	{
		const int size = folio->project()->pidCodification().text_sizes.value(
					PidCodification::nozzleTexts());
		if (size > 0) {
			font.setPointSize(size);
		}
	}
	return font;
}

bool Terminal::labelIsDrawn() const
{
	const Diagram *folio = diagram();
	if (!folio) {
		return true;
	}
	return d->m_medium == TerminalData::Pipe ? folio->drawNozzleNames()
											 : folio->drawTerminalNames();
}

QString Terminal::name() const
{
	if (d->m_use_master_label && parent_element_) {
		for (Element *elmt : parent_element_->linkedElements()) {
			if (elmt->linkType() == Element::Master) {
				int group_idx = elmt->groupIndexForElement(parent_element_);
				if (group_idx >= 0) {
					if (elmt->elementData().m_master_type == ElementData::PLC) {
						const auto &plc_data = elmt->elementData().plcMasterData();
						if (group_idx < plc_data.ios.size()) {
							int label_idx = d->m_master_label_index;
							const QStringList labels = plc_data.ios.at(group_idx).effectiveTerminals();
							if (label_idx >= 0 && label_idx < labels.size()) {
								return labels.at(label_idx);
							}
						}
					} else {
						const auto &groups = elmt->elementData().m_slave_contact_groups;
						if (group_idx < groups.size()) {
							int label_idx = d->m_master_label_index;
							const QStringList &labels = groups.at(group_idx).labels;
							if (label_idx >= 0 && label_idx < labels.size()) {
								return labels.at(label_idx);
							}
						}
					}
				}
				break;
			}
		}
	}
	return d->m_name;
}

/**
	@brief Terminal::baseName
	Return the original terminal name (T1, T2...) without master label override.
	Used for sorting when linking.
*/
QString Terminal::baseName() const
{
	return d->m_name;
}

/**
	@brief Terminal::terminalType
	@return the type of this terminal (Generic, Inner, Outer, No, Nc, Common)
*/
TerminalData::Type Terminal::terminalType() const
{
	return d->m_type;
}

/**
	@brief Terminal::medium
	@return the medium this terminal connects (electrical, pipe, signal,
	drive), as its symbol defines it
*/
TerminalData::Medium Terminal::medium() const
{
	return d->m_medium;
}

/**
	@brief Terminal::potential
	@return this terminal's own potential-group identifier (@see
	TerminalData::m_potential).
	An author-chosen grouping used only
	within a single multi-terminal element.
*/
QString Terminal::potential() const
{
	return d->m_potential;
}

/**
	@brief Terminal::setUseMasterLabel
	Set whether this terminal uses a label from the master's contact group
	@param use true to use master label
*/
void Terminal::setUseMasterLabel(bool use)
{
	d->m_use_master_label = use;
	update();
}

/**
	@brief Terminal::setMasterLabelIndex
	Set the index into the master's contact group labels
	@param index the label index (0-based)
*/
void Terminal::setMasterLabelIndex(int index)
{
	d->m_master_label_index = index;
	update();
}

/**
	@brief Conductor::relatedPotentialTerminal
	Return terminal at the same potential from the same
	parent element of terminal.
	For folio report, return the terminal of linked other report.
	For Terminal element, return the other terminal of terminal element.
	@param terminal : to start search
	@param all_diagram :if true return all related terminal,
	false return only terminal in the same diagram of t
	@param ignore_function_separation : if true, a parent element whose
	function separation (element information potential_isolating) is
	enabled does not keep its terminals apart: they all count as one
	potential, the way they do when the option is disabled. Only the
	callers that need the conductors a separation hides -- so that the
	one property still allowed to cross it, the voltage / protocol, can
	be applied there (@see Conductor::functionSeparatedConductors) --
	pass true; every other caller keeps the default.
	@return the list of terminal at the same potential
*/
QList<Terminal *> relatedPotentialTerminal (
		const Terminal *terminal, const bool all_diagram,
		const bool ignore_function_separation)
{
	// If terminal parent element is a folio report.
	if (all_diagram && terminal -> parentElement() -> linkType() & Element::AllReport)
	{
			//The report's partner on a folio not built yet (lazy folios)
			//is built, so that a potential is followed as far as when every
			//folio is; not while folios are being read or built, which
			//would build whole report chains
		Element *report = terminal->parentElement();
		QETProject *project = report->diagram() ? report->diagram()->project() : nullptr;
		const QSet<QUuid> waiting = project && project->unloadedFolioCount()
				? project->dataBase()->waitingPartners(report->uuid(), project->symbolsOnUnbuiltFolios())
				: QSet<QUuid>();
		if (!waiting.isEmpty() && project
				&& project->state() != QETProject::ProjectParsingRunning) {
			project->buildFoliosHoldingSymbols(waiting);
		}
		QList <Element *> elmt_list = terminal -> parentElement() -> linkedElements();
			//A partner built alone (stage D5b) has no wires
			//yet: its folio is built
		if (!elmt_list.isEmpty() && project && elmt_list.first()->diagram()
				&& !elmt_list.first()->diagram()->isLoaded()
				&& project->state() != QETProject::ProjectParsingRunning) {
			project->buildFolios({elmt_list.first()->diagram()});
			elmt_list = terminal->parentElement()->linkedElements();
		}
		if (!elmt_list.isEmpty())
		{
			return (elmt_list.first()->terminals());
		}
	}
	// If terminal parent element is a Terminal element.
	else if (terminal -> parentElement() -> linkType() & Element::Terminale)
	{
		// English: Check if the user activated the function separation
		// checkbox for this terminal (the checkbox that used to be called
		// potential separation -- the stored flag is still
		// "potential_isolating" so existing elements keep their setting).
		if (!ignore_function_separation
			&& QET::infoFlagIsTrue(terminal->parentElement()->elementInformations().value(QStringLiteral("potential_isolating")).toString())) {
			// English: The terminals are function separated -- but terminals
			// sharing this terminal's own, non-empty potential-group
			// identifier (@see TerminalData::m_potential, set per-terminal in
			// the element editor) still belong to the same physical terminal
			// and stay linked to each other. An empty/unset potential on
			// every terminal reproduces the previous, fully-separated
			// behavior exactly, so existing elements are unaffected.
			const QString this_potential = terminal->potential();
			QList<Terminal *> same_potential;
			if (!this_potential.isEmpty()) {
				for (Terminal *t : terminal->parentElement()->terminals()) {
					if (t != terminal && t->potential() == this_potential)
						same_potential << t;
				}
			}
			return same_potential;
		}

		QList <Terminal *> terminals = terminal->parentElement()->terminals();
		terminals.removeAll(const_cast<Terminal *>(terminal));
		return terminals;
	}

	return QList<Terminal *>();
}

