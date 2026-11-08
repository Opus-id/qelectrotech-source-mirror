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
			/// The other nozzle of a symbol a line runs through, nullptr for
			/// any other (Element::isPipeInline())
		Terminal *inlineSibling() const;
			/// The nominal diameter given to this nozzle: by its element, else
			/// by its symbol; 0 for none and for any other medium. A symbol a
			/// line runs through has one DN: the other nozzle's counts too.
		int ownDiameter() const;
			/// The DN of the pipe connected to this nozzle, 0 for none; on a
			/// symbol a line runs through, else of the pipe on the other side
		int pipeDiameter() const;
			/// The nominal diameter of this nozzle: its own, else it follows
			/// its pipe, so that nothing goes out of date on a valve
		int nominalDiameter() const;
			/// The nozzle has a DN of its own, its pipe another one; or the
			/// line runs through its symbol with two DN and no reducer
		bool hasDiameterGap() const;
		void updateDiameterToolTip();
			/// The one its symbol gives this nozzle
		int symbolDiameter() const { return d->m_dn; }
		void setSymbolDiameter(int dn);
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
			/// The DN given to this nozzle alone, by its element or its symbol
		int givenDiameter() const;
			/// The DN of the pipe connected to this nozzle alone
		int connectedDiameter() const;

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
