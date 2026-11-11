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
#ifndef TERMINAL_H
#define TERMINAL_H
#include <qet.h>
#include <properties/terminaldata.h>
#include "../pidcodification.h"

#include <QtWidgets>
#include <QtXml>
class Conductor;
class Diagram;
class Element;


/**
	@brief The Terminal class
	This class represents a terminal of an electrical element, i.e. a possible
	plug point for conductors.
	This class handles all mouse events for connecting conductors
*/
class Terminal : public QGraphicsObject
{
	Q_OBJECT

	signals:
		void conductorWasAdded(Conductor *conductor);
		void conductorWasRemoved(Conductor *conductor);

	// constructors, destructor
	public:
		Terminal(TerminalData* data, Element *e = nullptr);
		~Terminal() override;

	private:
		Terminal(const Terminal &);

	// methods
	public:
		/**
			@brief type
			Enable the use of qgraphicsitem_cast to safely
			cast a QGraphicsItem into a Terminal
			@return the QGraphicsItem type
		*/
		int type() const override { return Type; }

		void paint(
				QPainter *painter,
				const QStyleOptionGraphicsItem *,
				QWidget *) override;
		void   drawHelpLine (bool draw = true);
		QLineF HelpLine     () const;
		QRectF boundingRect () const override;
			/// The room of a nozzle holds the mark of its connection, its
			/// shape does not: a click on the mark does not start a pipe
		QPainterPath shape  () const override;

		// methods to manage conductors attached to the terminal
		Terminal* alignedWithTerminal () const;
		bool      addConductor        (Conductor *conductor);
		void      removeConductor     (Conductor *conductor);
		int       conductorsCount     () const;
		Diagram  *diagram             () const;
		Element  *parentElement       () const;
		QUuid     uuid                () const;
		void      newUuid             ();
		void      setUuid             (const QUuid &uuid);
		QUuid     stableUuid          () const;
		QUuid     derivedUuid         () const;
		void      setPlaceRank        (int rank);
		QString   name                () const;
		QString   baseName            () const;
		TerminalData::Type terminalType() const;
		TerminalData::Medium medium() const;
			/// The other nozzles of its run, which a line runs through and
			/// which a change of DN goes through (Element::pipeRun()): the
			/// other nozzle of a valve, the others of a tee; none for a
			/// nozzle out of a run and for any other medium
		QList<Terminal *> runSiblings() const;
			/// This nozzle has no size of its own: it has the one of its
			/// line, so that nothing goes out of date when the line is
			/// given another size. A nozzle of a run (a valve, an inline
			/// instrument, a tee) and the only nozzle of a piece of pipe (a
			/// free end, a vent). What its symbol or its element would give
			/// it is ignored. Any other nozzle (of equipment, of a reducer,
			/// the outlet of a safety valve) may have a size of its own.
		bool followsPipe() const;
			/// Another nozzle of its run which is not brought the size
			/// @p pipe (facingDiameter()); nullptr if none
		const Terminal *runSiblingOtherThan(const PipeDiameter &pipe) const;
			/// The nozzle of another symbol placed against this one:
			/// with no pipe on either, or joined to this one by a pipe of
			/// no length (Conductor::endsTouch(), which the automatic
			/// connection makes and nobody sees); nullptr if none. For
			/// the sizes, both are the same: a pipe of no length, which
			/// has the size of its line when it was given none.
		Terminal *contactNozzle() const;
			/// The nozzles which have their size from each other around
			/// this one, itself first: those of its run, the nozzles
			/// placed against them, the runs of those, and so on. Never
			/// beyond a pipe.
		QList<Terminal *> contactGroup() const;
			/// The size brought to this nozzle from outside its symbol:
			/// that of its pipe; without one, or with a pipe of no length
			/// and no size, what the nozzle placed against it brings
			/// (contactNozzle()): the size of the line beyond it, or its
			/// own size, which gives way to the size of the line of this
			/// nozzle (the size lives on the line). None for none.
			/// @p pipe, if given, is the pipe this size is of, when it is
			/// the size of a pipe.
		PipeDiameter facingDiameter(const Conductor **pipe = nullptr) const;
			/// To call when this nozzle may have come against another one
			/// or left it (its symbol moved, a pipe came or went): the
			/// sizes shown on both sides are drawn again
		void refreshContact();
			/// Draw again the sizes of the nozzles of contactGroup()
		void refreshSizes();
			/// The diameter given to this nozzle alone (a DN, an outside
			/// diameter or a thread): by its element, else by its symbol.
			/// None for none, for any other medium, for a nozzle which
			/// follows its pipe (followsPipe()), and when its connection
			/// does not take its kind, a DN and a thread being read as each
			/// other by the fixed table (PipeConnection::fitted()).
		PipeDiameter ownDiameter() const;
			/// The diameter of the line of this nozzle: the one brought to
			/// it (facingDiameter()); else, in a run, the one brought to
			/// another nozzle of the run. None for none. @p pipe as for
			/// facingDiameter().
		PipeDiameter pipeDiameter(const Conductor **pipe = nullptr) const;
			/// The diameter of this nozzle: its own, else that of its pipe
		PipeDiameter diameter() const;
			/// The nozzle has a diameter of its own, its pipe another one; or
			/// the pipes of its run have two diameters, with no reducer.
			/// A nozzle placed against it counts as a pipe.
			/// A DN and an outside diameter are compared with the
			/// table of the project (PidCodification::compare()): one it does
			/// not know is a gap too.
		bool hasDiameterGap() const;
			/// The other nozzle of the reducer of this nozzle
			/// (Element::isReducer()), which has two; nullptr otherwise
		Terminal *reducerOtherSide() const;
			/// This nozzle is one of a reducer whose two sides have the same
			/// size, their own or that of their line: a reducer which
			/// reduces nothing, said as a gap (hasDiameterGap())
		bool reducesNothing() const;
		void updateDiameterToolTip();
			/// The one its symbol gives this nozzle
		PipeDiameter symbolDiameter() const { return d->m_diameter; }
		void setSymbolDiameter(const PipeDiameter &diameter);
			/// How a pipe is joined to this nozzle: the connection given by
			/// its element, else by its symbol; a flange by default
		PipeConnection::Type connection() const;
			/// The one its symbol gives this nozzle
		PipeConnection::Type symbolConnection() const { return d->m_connection; }
		void setSymbolConnection(PipeConnection::Type connection);
			/// Whether this nozzle draws the mark of its connection: as its
			/// project says, unless its element says always or never
		PipeConnection::MarkChoice markChoice() const;
			/// This nozzle draws the mark of its connection at its end, on
			/// the side of the pipe (PipeConnection::markShapes()): its
			/// project or its element asks it (the sockets of the project
			/// for a plain end, whose pipe brings a socket), and no nozzle of
			/// another symbol touches it. Never for another medium.
		bool drawsConnectionMark() const;
			/// Where the mark of this nozzle is drawn, from its point: beyond
			/// it on a symbol a line runs through, whose nozzle is on the
			/// side of its body, else on it (PipeConnection::markShapes())
		qreal markDistance() const;
			/// This nozzle draws a mark itself: its connection is drawn and
			/// has one (not a plain end), but a flange of a symbol a line
			/// runs through is the side of its body, which it does not draw
			/// again
		bool drawsOwnMark() const;
			/// A nozzle of another symbol is on the same point as this one,
			/// joined by a pipe of no length or not: two symbols placed
			/// against each other, which draw nothing between them
		bool touchesNozzle() const;
			/// What draws again the mark of this nozzle and the counter-flange
			/// of its pipe
		void updateConnectionMark();
			/// From the frame of PipeConnection::markShapes() to that of
			/// this terminal: the pipe draws the part it brings in the same
			/// frame, turned and mirrored as the element
		QTransform markTransform() const;
			/// The diameter of this nozzle (diameter()) as it is written:
			/// its own; else that of its pipe, as the project writes the
			/// size of this pipe ("DN 60/65" without material,
			/// PidCodification::text()), but for a flange, which is written
			/// with the usual DN of those the project says equal
			/// (PidCodification::usualNominal(): DN 65 on a cast iron DN 60).
			/// Nothing for none.
		QString diameterText() const;
			/// The text shown at this terminal: its name, and the DN of a nozzle
		QString labelText() const;
			/// The font of labelText(): the one of the symbol, at the size
			/// the project sets for the texts of the nozzles
		QFont labelFont() const;
			/// False when the folio is drawn without the names of its
			/// terminals, unless the project keeps those of its nozzles
		bool labelIsDrawn() const;
			/// To call before labelFont() changes its size
		void prepareLabelChange() { prepareGeometryChange(); }
		QString potential() const;
		bool useMasterLabel() const { return d->m_use_master_label; }
		void setUseMasterLabel(bool use);
		int masterLabelIndex() const { return d->m_master_label_index; }
		void setMasterLabelIndex(int index);

		QList<Conductor *> conductors() const;
		Qet::Orientation orientation() const;
		QPointF dockConductor() const;
		void updateConductor();
		bool isLinkedTo(Terminal *);
		bool canBeLinkedTo(Terminal *);
		int wireLimit() const;
		bool hasRoomForWire() const;
		bool sameMediumAs(const Terminal *other) const;
		bool refusesMediumOf(const Terminal *other) const;
			/// The nozzle a pipe drawn from this nozzle is caught by when it
			/// is released near it rather than on it
		Terminal *magnetTarget(const QPointF &scene_pos, qreal radius);
			/// magnetRadius in units of the folio, at the zoom of the view
			/// the event comes from
		static qreal magnetSceneRadius(const QGraphicsSceneMouseEvent *e);
			/// Draw a conductor from this terminal to @p other, as a release
			/// of the mouse on @p other does, in one undo step
		Conductor *connectTo(Terminal *other);
			/// The hover effect, given from outside (a tool of the folio)
		void showHover(bool hovered, const QColor &color = Terminal::neutralColor);

		// methods related to XML import/export
		static bool valideXml(QDomElement  &);
		bool fromXml (QDomElement &);
		QDomElement toXml (QDomDocument &) const;

	protected:
		// methods related to events management
		void hoverEnterEvent  (QGraphicsSceneHoverEvent *) override;
		void hoverMoveEvent   (QGraphicsSceneHoverEvent *) override;
		void hoverLeaveEvent  (QGraphicsSceneHoverEvent *) override;
		void mousePressEvent  (QGraphicsSceneMouseEvent *) override;
		void mouseMoveEvent   (QGraphicsSceneMouseEvent *) override;
		void mouseReleaseEvent(QGraphicsSceneMouseEvent *) override;

	// attributes
	public:
		enum { Type = UserType + 1002 };

		static const qreal terminalSize;
		static const qreal Z;
			/// How near a nozzle a pipe is caught by it, in pixels of the
			/// screen (see magnetTarget())
		static const qreal magnetRadius;
		// Various static colors used for hover effects
		/// default color
		static QColor neutralColor;
		/// color for legal actions
		static QColor allowedColor;
		/// color for allowed but fuzzy or not recommended  actions
		static QColor warningColor;
		/// color for forbidden actions
		static QColor forbiddenColor;
		/// color of the stem of a terminal, after the medium it connects
		static QColor stemColor(TerminalData::Medium medium);
		/// name of a medium, to be read in a sentence
		static QString mediumName(TerminalData::Medium medium);

		static bool takesConductorColor();
		static void setTakesConductorColor(bool takes);

	private:
			/// The size of the pipe connected to this nozzle alone: its DN
			/// (PipeProperties::lineSize()); @p pipe, if given, is this pipe
			/// when it has a size
		PipeDiameter connectedDiameter(const Conductor **pipe = nullptr) const;
			/// How the size @p size is written: as the project writes it
			/// for the material of @p pipe when it is the size of a pipe
			/// (PidCodification::text(): "DN 60/65" without material)
		QString sizeText(const PipeDiameter &size, const Conductor *pipe) const;
			/// What the own size of this nozzle is compared with: the size
			/// brought to it (facingDiameter()), a DN; but for a nozzle
			/// given as an outside diameter, the outside diameter of its
			/// pipe when it has one, as two plastic pipes of the same DN may
			/// have two outside diameters
		PipeDiameter comparedDiameter() const;
			/// The first size met from the nozzles @p todo, each in turn:
			/// that of its pipe, else the own size of the nozzle placed
			/// against it, else further through the runs and the contacts;
			/// never back through @p seen. With @p pipes_only, the size of
			/// a pipe only. @p pipe as for connectedDiameter().
		static PipeDiameter lineDiameter(QList<const Terminal *> todo,
										 QList<const Terminal *> seen,
										 bool pipes_only = false,
										 const Conductor **pipe = nullptr);
			/// The mark of two sizes is drawn here: at a contact there is
			/// no pipe to draw it (Conductor::drawDiameterGaps())
		bool marksContactGap() const;
			/// How @p a and @p b compare in the project of this nozzle
		PidCodification::DiameterMatch compareDiameters(const PipeDiameter &a,
														const PipeDiameter &b) const;
			/// How the project writes the tenths of a size
		QLocale sizeLocale() const;
			/// The room of the terminal and of its name, without the mark
		QRectF ownRect() const;
		void drawConnectionMark(QPainter *painter, qreal detail) const;

		bool               m_draw_help_line{false};
		QGraphicsLineItem *m_help_line{nullptr};
		QGraphicsLineItem *m_help_line_a{nullptr};


		TerminalData* d;

		/// Parent electrical element
		Element *parent_element_{nullptr};
	public:
		/// docking point for parent element
		QPointF dock_elmt_;
	private:
		/// List of conductors attached to the terminal
		QList<Conductor *> m_conductors_list;
		QRectF m_br;
			/// Last terminal seen through an attached conductor
		Terminal *m_previous_terminal = nullptr;
			/// The nozzle this one was against when refreshContact() last
			/// looked: only to know which sizes to draw again
		QPointer<Terminal> m_contact;
			/// Whether the mouse pointer is hovering the terminal
		bool m_hovered = false;
			/// How many terminals of the definition, before this one, sit at
			/// the same point with the same orientation; see derivedUuid()
		int m_place_rank = 0;
			/// Color used for the hover effect
		QColor m_hovered_color = Terminal::neutralColor;

	private:
		void init();
		void init(QPointF pf, Qet::Orientation o);
};

/**
	@brief Terminal::conductorsCount
	@return the number of conductors attached to the terminal.
*/
inline int Terminal::conductorsCount() const
{
	return(m_conductors_list.size());
}

QList<Terminal *> relatedPotentialTerminal (const Terminal *terminal,
						const bool all_diagram = true,
						const bool ignore_function_separation = false);

#endif
