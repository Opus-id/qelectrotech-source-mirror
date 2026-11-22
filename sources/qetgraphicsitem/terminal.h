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
			/// The nozzles of its run its line goes on to, with its size,
			/// its line number and its kind of line: the other one of a run
			/// of two (a valve); in a run of three or more (a tee, a cross),
			/// the one opposite only: a branch is another line, of a size
			/// of its own
		QList<Terminal *> lineSiblings() const;
			/// This nozzle has no size of its own: it has the one of its
			/// line, so that nothing goes out of date when the line is
			/// given another size. A nozzle of a run (a valve, an inline
			/// instrument, a tee) and the only nozzle of a piece of pipe (a
			/// free end, a vent). What its symbol or its element would give
			/// it is ignored. Any other nozzle (of equipment, of a reducer,
			/// the outlet of a safety valve) may have a size of its own.
		bool followsPipe() const;
			/// Another nozzle of its run, on its line (lineSiblings()),
			/// which is not brought the size @p pipe (facingDiameter());
			/// nullptr if none
		const Terminal *runSiblingOtherThan(const PipeDiameter &pipe) const;
			/// The nozzle of another symbol placed against this one:
			/// with no pipe on either, or joined to this one by a pipe of
			/// no length (Conductor::endsTouch(), which the automatic
			/// connection makes and nobody sees); nullptr if none. For
			/// the sizes, both are the same: a pipe of no length, which
			/// has the size of its line when it was given none.
		Terminal *contactNozzle() const;
			/// For a nozzle of a spec break (Element::isSpecBreak()): the
			/// nozzle of another symbol placed against its other nozzle,
			/// whose connection a pipe on this one meets as if the mark were
			/// not there; nullptr otherwise (step 5l-1l)
		const Terminal *acrossSpecBreak() const;
			/// The nozzles which have their size from each other around
			/// this one, itself first: those of its run, the nozzles
			/// placed against them, the runs of those, and so on. Never
			/// beyond a pipe.
		QList<Terminal *> contactGroup() const;
			/// The pipes nearest to this nozzle on its line, from which a
			/// change of size goes on (Conductor::inlineRun()): its own pipe;
			/// without one, across the symbols placed against it which
			/// follow their line (a valve, a tee straight on), the first
			/// pipes beyond them. None beyond a nozzle with a size of its
			/// own (equipment, another reducer), nor across the branch of a
			/// tee, nor for any other medium.
		QList<Conductor *> nearestPipes() const;
			/// The size brought to this nozzle from outside its symbol:
			/// that of its pipe; without one, or with a pipe of no length
			/// and no size, what the nozzle placed against it brings
			/// (contactNozzle()): the size of the line beyond it, or its
			/// own size, which gives way to the size of the line of this
			/// nozzle (the size lives on the line). None for none.
			/// @p pipe, if given, is the pipe this size is of, when it is
			/// the size of a pipe. A size goes straight on through a tee
			/// (lineSiblings()); with @p by_default, into and out of its
			/// branch too, see pipeDiameter().
		PipeDiameter facingDiameter(const Conductor **pipe = nullptr,
									bool by_default = false) const;
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
			/// another nozzle of its line (lineSiblings()). Without any,
			/// and with @p with_default, the one a tee has by default: the
			/// size of the line for its branch, the size of its branch for
			/// a line without one (an equal tee until told otherwise).
			/// None for none. @p pipe as for facingDiameter().
		PipeDiameter pipeDiameter(const Conductor **pipe = nullptr,
								  bool with_default = true) const;
			/// The diameter of this nozzle: its own, else that of its pipe
			/// (pipeDiameter(), @p with_default as there)
		PipeDiameter diameter(bool with_default = true) const;
			/// The nozzle has a diameter of its own, its pipe another one; or
			/// the pipes of its line have two diameters, with no reducer (the
			/// branch of a tee has any size, step 6b-1c); or
			/// it is one of a reducer which reduces nothing or is laid the
			/// wrong way.
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
			/// The nozzle on the large side of the reducer of this nozzle,
			/// itself or its other side (Element::largeNozzle()); nullptr
			/// when its symbol names none of the two
		const Terminal *reducerLargeSide() const;
			/// This nozzle is one of a reducer laid the wrong way: the size
			/// of its large side is smaller than that of the other side,
			/// both compared by the DN of their line
			/// (PidCodification::lineNominal()). False when the symbol names
			/// no large side, or a size is missing or not known (step 5l-1ab).
		bool reducerReversed() const;
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
			/// Where the fluid comes in or goes out through this nozzle, as
			/// its symbol says (TerminalData::FlowRole, step 8a-bis); none
			/// for any other medium
		TerminalData::FlowRole flowRole() const {
			return d->m_medium == TerminalData::Pipe ? d->m_flow_role : TerminalData::NoFlowRole;
		}
		void setSymbolFlowRole(TerminalData::FlowRole role);
			/// The chamber of its equipment this nozzle belongs to, as its
			/// symbol says (step 8a-bis); empty for none and for any other
			/// medium
		QString chamber() const {
			return d->m_medium == TerminalData::Pipe ? d->m_chamber : QString();
		}
		void setSymbolChamber(const QString &chamber) { d->m_chamber = chamber; }
			/// The other nozzle of its chamber, when the chamber has two:
			/// the fluid which comes in at one goes out at the other (a side
			/// of a heat exchanger); nullptr otherwise, a chamber of three
			/// nozzles or more being a vessel, which may fill or empty
		Terminal *chamberPartner() const;
			/// Whether this nozzle draws the mark of its connection: as its
			/// project says, unless its element says always or never
		PipeConnection::MarkChoice markChoice() const;
			/// This nozzle draws the mark of its connection at its end, on
			/// the side of the pipe (PipeConnection::markShapes()): its
			/// project or its element asks it (the sockets of the project
			/// for a plain end, whose pipe brings a socket), and no nozzle of
			/// another symbol touches it. Never for another medium, nor on a
			/// spec break (Element::isSpecBreak()), a mark and no piece.
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
			/// against each other, which draw nothing between them. A spec
			/// break counts for no symbol: what is against it keeps its mark
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
			/// The size labelText() writes under the name of a nozzle, as the
			/// project writes it; nothing when it writes none
		QString sizeLabelText() const;
			/// The font of labelText(): the one of the symbol, at the size
			/// the project sets for the texts of the nozzles
		QFont labelFont() const;
			/// False when the folio is drawn without the names of its
			/// terminals, unless the project keeps those of its nozzles
		bool labelIsDrawn() const;
			/// To call before labelFont() changes its size
		void prepareLabelChange() { prepareGeometryChange(); }
			/// To call when the texts of a nozzle may turn on the sheet: its
			/// element turned or mirrored, the setting of its project changed
			/// (nozzleLabelsFor()); nothing for any other terminal
		void updateLabelPlace();
			/// For a nozzle: where texts of the sizes @p name and @p size
			/// are written (TerminalData::nozzleLabels()), after the way its
			/// pipe leaves on the sheet and the setting of its project
		TerminalData::NozzleLabels nozzleLabelsFor(const QSizeF &name, const QSizeF &size) const;
			/// The frame of those texts in the coordinates of this terminal:
			/// at the end of the nozzle, along the axes of the sheet, turned
			/// by @p rotation degrees, whatever its element does
		QTransform nozzleLabelFrame(qreal rotation) const;
		QString potential() const;
		bool useMasterLabel() const { return d->m_use_master_label; }
		void setUseMasterLabel(bool use);
		int masterLabelIndex() const { return d->m_master_label_index; }
		void setMasterLabelIndex(int index);

		QList<Conductor *> conductors() const;
		Qet::Orientation orientation() const;
			/// The way it faces in its symbol, as the definition writes
			/// it: before its element is turned or mirrored (orientation()
			/// gives the way it faces on the folio)
		Qet::Orientation symbolOrientation() const;
		QPointF dockConductor() const;
		void updateConductor();
		void shiftInSymbol(const QPointF &offset);
		bool isLinkedTo(Terminal *);
		bool canBeLinkedTo(Terminal *);
		int wireLimit() const;
		bool hasRoomForWire() const;
		QString showIf() const { return d->m_show_if; }
		bool isHiddenByOptions() const;
		bool isKeptByWire() const;
		QString replaces() const { return d->m_replaces; }
		Terminal *replacement() const;
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
		/// radius of the orange disc of isKeptByWire(): 3 mm across
		static constexpr qreal keptMarkRadius = 6.0;
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
			/// a pipe only. @p pipe as for connectedDiameter(). Straight on
			/// through a tee (lineSiblings()); with @p by_default, through
			/// its branch too.
		static PipeDiameter lineDiameter(QList<const Terminal *> todo,
										 QList<const Terminal *> seen,
										 bool pipes_only = false,
										 const Conductor **pipe = nullptr,
										 bool by_default = false);
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
			/// For a nozzle showing its name: the room of its name and of
			/// the widest size it may write, each apart, beside its pipe
		QList<QRectF> nozzleLabelRooms() const;
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
			/// The tool tip of the terminal, plain text, before the warning
			/// of isKeptByWire() is added (showToolTip())
		QString m_own_tool_tip;

	private:
		void init();
		void setOwnToolTip(const QString &text);
		void showToolTip();
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
						const bool ignore_function_separation = false,
						const bool stop_at_report_links = false);

/**
	@brief The roles of a junction: which of its connections the wire comes
	in at, and which ones it goes on to.

	The roles belong to the symbol rather than to one copy of it: they are
	read from the definition, so every copy answers alike and no database
	has to hold them.
*/
struct JunctionRoles
{
	QString source;
	QStringList targets;
};

/**
	@brief junctionRolesOf
	@return the roles of @a junction, as its own data holds them: the
	symbol is read once and its roles come back with it, rather than the
	definition being read again for every wire (#1500).
	@param junction the symbol to read, which need not be one
*/
JunctionRoles junctionRolesOf(const Element *junction);

/**
	@brief junctionRolesOf
	@return the roles a definition says. The reader every caller shares,
	so the drawing and the exports always answer alike
	(@see junctionRolesOf(const Element *)).
	@param definition the symbol's definition, or a null element
*/
JunctionRoles junctionRolesOf(const QDomElement &definition);

/**
	@brief junctionSourceKey
	@return the element information a junction's source connection is
	stored against
*/
QString junctionSourceKey();

/**
	@brief junctionTargetKeys
	@return the element informations a junction's connections go on to are
	stored against, in the order the chain follows them
*/
QStringList junctionTargetKeys();

/**
	@brief junctionRoleKeys
	@return every element information a junction role is stored against
*/
QStringList junctionRoleKeys();

/**
	@brief junctionChainNeighbours
	@return the two connections of a junction's chain around @a at: the one
	the wire comes from, and the one it goes on to, either of them null when
	there is none.

	That is the chain the definition gives -- source, then target 1, target
	2, target 3 -- read at the connection @a at sits on. The chain is what
	says which way a wire runs through the junction, so a cable list can
	name its ends and a stub can say where it comes from (#1500).

	@param at a connection of a junction; anything else gives two nulls
	@return {the connection before, the connection after}, in chain order
*/
QPair<Terminal *, Terminal *> junctionChainNeighbours(Terminal *at);

/**
	@brief junctionPassThrough
	@return the connection a wire goes on to when the chain does not say:
	the one other connection of that junction which carries a wire, if
	there is exactly one.

	A junction with a single way out of it has nothing to tell apart, so
	the wire simply goes on that way even when nothing was ever set on the
	symbol -- a corner passes a wire through and needs no source for it
	(#1500). Empty when there is no such connection, or more than one,
	which is where the way on has to be told apart and there is nothing to
	go by.

	@param wired the other connections of the junction which carry a wire
	@return the one of them to go on to, empty for none or several
*/
QString junctionPassThrough(const QStringList &wired);

/**
	@brief junctionChainRoles
	@return the two connections of a chain around @a at: the one the wire
	comes from, and the one it goes on to, either of them empty when there
	is none.

	The chain itself -- source, then target 1, then target 2, then target 3
	-- read as plain uuids rather than as elements, so the drawing and the
	lists, which hold their rows as uuids, follow one rule (#1500).

	@param source the connection the wire comes in at, empty when the
	junction has none set
	@param targets where it goes on to, in the order the lists follow them
	@param at the connection to read the chain at, empty for none
	@return {the connection before, the connection after}, in chain order
*/
QPair<QString, QString> junctionChainRoles(const QString &source, const QStringList &targets,
					   const QString &at);

/**
	@brief endsPastJunctions
	Return the connections a wire ends at, past the junctions it runs
	through.

	A junction is a connection point on a wire rather than a piece of its
	own: it is bought nowhere, built nowhere, and the wire does not end
	there. The chain the junction's definition gives -- what the wire
	comes from, then where it goes on to -- says which way it runs on,
	and a junction reached that way is passed in turn, so a run through
	two of them in a row ends where it really ends (#1500).

	This is the one rule the cable list, the terminal strip and the
	wiring lists name their lands with, so the three can never disagree.

	@param end the connection the wire ends at -- one of a junction's own,
	or one of a piece which names an end
	@return the connections the wire ends at, none of them a junction's own.
	A junction with no source set is passed on when only one way out of it
	carries a wire, because there is nothing to tell apart (#1500), and
	the result is empty when there are more ways than that to choose from,
	or none at all
*/
QList<Terminal *> endsPastJunctions(Terminal *end);

#endif
