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
#ifndef CONDUCTOR_H
#define CONDUCTOR_H
#include <autoNum/assignvariables.h>
#include <conductorproperties.h>
#include <dataBase/conductorlayout.h>
#include <dataBase/conductorpath.h>
#include <pipeconnection.h>
#include <properties/potentialtypes.h>
#include <signalmark.h>
#include <wirehops.h>

#include <QElapsedTimer>
#include <QGraphicsPathItem>
#include <QPointer>
#include <QSet>
#include <QUuid>

class ConductorProfile;
class ConductorSegmentProfile;
class Diagram;
class projectDataBase;
class Terminal;
class ConductorSegment;
class ConductorTextItem;
class ConductorDefinitionMark;
class Element;
class QETDiagramEditor;
class NumerotationContext;
class PidCodification;
class QetGraphicsHandlerItem;
class QGraphicsPathItem;

typedef QPair<QPointF, Qt::Corner> ConductorBend;
typedef QHash<Qt::Corner, ConductorProfile> ConductorProfilesGroup;
/**
	This class represents a conductor, i.e. a wire between two element
	terminals.
*/
class Conductor : public QGraphicsObject
{
	Q_OBJECT

	Q_PROPERTY(QPointF pos READ pos WRITE setPos)
	Q_PROPERTY(int animPath READ fakePath WRITE updatePathAnimate)
	Q_PROPERTY(ConductorProperties properties READ properties WRITE setProperties)
	Q_PROPERTY(autonum::sequentialNumbers sequenceNum READ sequenceNum WRITE setSequenceNum)

	signals:
		void propertiesChange();
			/// The function of this wire changed: a project chaining its
			/// folio reports by that function looks its chains over again.
			/// The wire is given with it, so only the reports it ends on
			/// are looked at (QETProject::reportKeySourceChanged())
		void functionChanged(Conductor *conductor);
	
	public:
		Conductor(Terminal *, Terminal *);
		~Conductor() override;

		bool isValid() const;
	
	private:
		Conductor(const Conductor &);

	public:
		enum { Type = UserType + 1001 };
		enum Highlight { None, Normal, Alert };

		Terminal *terminal1;
		Terminal *terminal2;
	
	public:
		/**
			@brief type
			Enable the use of qgraphicsitem_cast
			to safely cast a QGraphicsItem into a conductor.
			@return the QGraphicsItem type
		*/
		int type() const override { return Type; }
		Diagram *diagram() const;
		ConductorTextItem *textItem() const;
		QUuid uuid() const {return m_uuid;}
		void newUuid();	//create new uuid for this conductor
		void setUuid(const QUuid &uuid);	//saved from now on
		void updatePath(const QRectF & = QRectF());
			/// Move the end of this conductor on @p from to @p to, the same
			/// conductor going on (see the definition)
		bool moveEnd(Terminal *from, Terminal *to);

		//This method do nothing, it's only made to be used with Q_PROPERTY
		//It's used to anim the path when is change
		void updatePathAnimate(const int = 1) {updatePath();}
		int fakePath() {return 1;}

		void paint(
				QPainter *,
				const QStyleOptionGraphicsItem *,
				QWidget *) override;
		QRectF boundingRect() const override;
		QPainterPath shape() const override;
		virtual QPainterPath nearShape() const;
			/// Its two ends on the same point: see endsTouch()
		bool endsTouch() const;
			/// This pipe draws the part it brings at its end on @p terminal
			/// (PipeConnection::pipePartShapes()): the connection of the
			/// nozzle is drawn and takes one (a flange, a plain end, a grooved
			/// end, a quick coupling). Never lost: a pipe leaves its nozzles
			/// straight far enough for it (exitRoom()).
		bool drawsPipePart(const Terminal *terminal) const;
			/// The path @p points, from terminal1 to terminal2 in the frame
			/// of this conductor, would turn (or go back) before running
			/// exitRoom() straight from one of its nozzles: a pipe then routes
			/// itself (a path read from a file, or a path moved by hand whose
			/// symbols come closer). False for a straight path, for two ends
			/// on one point and for any other medium.
		bool exitsTooShort(const QList<QPointF> &points) const;
			/// The path @p points, from terminal1 to terminal2 in the frame
			/// of this conductor, goes back over itself: two runs following
			/// each other on one line, the second the other way (a U crushed
			/// when its symbols move). A pipe or a signal line then routes
			/// itself (step 7c-bis); always false for an electrical wire.
		bool foldsBack(const QList<QPointF> &points) const;
			/// Where this pipe begins at its end on @p terminal, from the
			/// nozzle: after the part it brings, at the mark of a connection
			/// drawn beyond the nozzle of a valve, else 0
		qreal pipeStartAt(const Terminal *terminal) const;
			/// The connection this pipe meets at its end on @p terminal, and
			/// where its mark is (@p at, as for PipeConnection::markShapes()):
			/// that of the nozzle; on a spec break, that of the nozzle against
			/// its other side (Terminal::acrossSpecBreak()), at the nozzle of
			/// the mark, a flange right on it (@p shift, along the pipe).
			/// False when none is drawn there (step 5l-1l)
		bool metConnection(const Terminal *terminal, PipeConnection::Type *type,
						   qreal *at, qreal *shift) const;
			/// The piece of line this pipe draws into the spec break it
			/// arrives on at @p terminal, to the middle of its two nozzles, in
			/// its own frame: when a pipe arrives on its other nozzle too, so
			/// that the mark touches no symbol; a null line otherwise
		QLineF lineIntoSpecBreak(const Terminal *terminal) const;
			/// The points of the path a conductor takes by itself from the
			/// terminal at @p p1 facing @p o1 to the terminal at @p p2 facing
			/// @p o2, leaving each straight for @p exit_room at least (see
			/// exitRoom()): what a new conductor draws, and what the pipe
			/// drawing tool shows before the pipe is placed
		static QList<QPointF> defaultRoute(const QPointF &p1, Qet::Orientation o1,
										   const QPointF &p2, Qet::Orientation o2,
										   qreal exit_room);
		qreal length() const;
		ConductorSegment *middleSegment();
		QPointF posForText(Qt::Orientations &flag);
		void refreshText();
		void setPath(const QPainterPath &path);
		QPainterPath path() const;

	public:
		static bool valideXml (QDomElement &);
		bool fromXml (QDomElement &);
		QDomElement toXml (
				QDomDocument &,
				QHash<Terminal *,
				int> &,
				const QSet<QUuid> &shared_uuids = QSet<QUuid>()) const;
	private:
		bool pathFromXml(const QDomElement &);

	public:
		QVector <QPointF> handlerPoints() const;
		const QList<ConductorSegment *> segmentsList() const;
		bool moveSegment(int index, qreal dx, qreal dy);
		bool setPathPoints(const QList<QPointF> &scene_points);

		void setPropertyToPotential(
				const ConductorProperties &property,
				bool only_text = false);
		void setProperties(const ConductorProperties &property);
		ConductorProperties properties() const;
		TerminalData::Medium medium() const;
		void setMedium(TerminalData::Medium medium);
			/// This wire's own copy of its properties, whatever the store holds
		ConductorProperties ownProperties() const {return m_properties;}
			/// Where this wire sits and how its text was placed, from its own
			/// members, never from the store
		ConductorLayout ownLayout() const;
		void storeLayout();
			/// The segments toXml() writes for this wire (none if the user
			/// did not reshape it), from its own members, never from the store
		ConductorPath ownPath() const;
		void storePath();
			/// The order wires were made in this session: a project's are
			/// made in the order its file lists them
		quint64 creationRank() const {return m_creation_rank;}
			/// Records which cable core this conductor shows and writes the
			/// label that assignment generates (15W1:br) into its cable
			/// field. Deliberately not routed through
			/// setPropertyToPotential(): a core of a cable belongs to this
			/// conductor alone and must never spill over a pass-through
			/// terminal onto the neighbouring half conductor.
		void setCableReference(const QString &label,
							   const QUuid &cable_uuid,
							   int slot);

		void setProfile(const ConductorProfile &, Qt::Corner);
		ConductorProfile profile(Qt::Corner) const;
		void setProfiles(const ConductorProfilesGroup &);
		ConductorProfilesGroup profiles() const;
		void calculateTextItemPosition();
		void updateDefinitionMark();
		QList<Conductor *> cornerChain();
		void updateTextVisibility();
		void updateDesignationLook();
		void refreshDesignation();
			/// The pieces of the pipe @a whole cut by a symbol, which all took
			/// its data: only one keeps showing its designation, so that a cut
			/// never writes it twice. The one nearest to the designation of
			/// @a whole, where it was when moved by hand; the longest one
			/// without @a whole
		static void keepOneDesignation(const QList<Conductor *> &pieces,
									   const Conductor *whole = nullptr);
			/// Draw again the leader line which ties the text of this
			/// conductor, moved away from it by hand, to it, as the project
			/// says (QETProject::textLeaderDistance()); none else
		void updateLeader();
			/// The leader line drawn, empty when there is none
		QPainterPath leaderPath() const;
		virtual Highlight highlight() const;
		virtual void setHighlighted(Highlight);
		QSet<Conductor *> relatedPotentialConductors(
				const bool all_diagram = true,
				QList <Terminal *> *t_list=nullptr,
				const bool ignore_function_separation = false,
				const bool stop_at_report_links = false);
		QSet<Conductor *> functionSeparatedConductors(
				const bool all_diagram = true);
		void applySeparatedNodeProperties(ConductorProperties &properties);
		void applySeparatedNodePropertiesIfEmpty();
		QETDiagramEditor* diagramEditor() const;
		void editProperty ();

		autonum::sequentialNumbers sequenceNum () const
		{return m_autoNum_seq;}
			/// Replace the numbers without redrawing the text; the project
			/// database follows
		void replaceSequenceNum(const autonum::sequentialNumbers& sn);
		void setSequenceNum(const autonum::sequentialNumbers& sn);
		void storeSequence();

		QList<QPointF> junctions() const;
		QPainterPath paintedPath() const;
		QColor drawnColor() const;
			/// Where the colour of a pipe comes from, see pipeColor()
		enum class PipeColorSource { None, Status, Manual, Fluid, Family };
			/// The colour of a pipe, and with @p source where it comes from:
			/// the same rule for the folio and for the properties panel
		QColor pipeColor(PipeColorSource *source = nullptr) const;
			/// The colour the line is drawn in at rest, neither picked,
			/// highlighted nor a preview: drawnColor(), black on a folio
			/// drawn in black
		QColor plainColor() const;
			/// The pen the line is drawn with, @p width wide and in @p color:
			/// its style or its dashes (its potential type @p type when it
			/// has one, its kind of line, its type of signal), and a
			/// cosmetic line below a certain zoom (@p detail). The one rule
			/// for paint() and for a junction, which draws its lines as the
			/// pipes they go on (Element::drawJunction())
		QPen linePen(const QColor &color, qreal width, qreal detail,
					 const PotentialType *type) const;
			/// The pen of the line at rest: plainColor(), drawnWidth()
		QPen plainPen(qreal detail) const;
			/// The width of the line of this pipe as its project draws its
			/// kind of line (PidCodification::lineKind(), step 8r-6); that of
			/// ISO 10628-1 without a project. For another conductor, its own
			/// (ConductorProperties::lineWidth()).
		qreal lineWidth() const;
			/// The dashes of this pipe, in widths of its line, as its
			/// project draws its kind of line; none for a solid line, or for
			/// another conductor
		QList<qreal> pipeDashes() const;
			/// @p pen as a line is drawn below a certain zoom (@p detail):
			/// a cosmetic line; that of a pipe (@p pipe), several units wide,
			/// follows the zoom down to one pixel
		static QPen zoomedPen(QPen pen, qreal detail, bool pipe);
			/// Draw again the fittings at the ends of a pipe, which take
			/// its colour (Element::followsPipes())
		void updateFittings() const;
			/// See the definition; to call again on a new pipe after giving
			/// it other properties
		void takeNozzleDiameter();
			/// Give @p data the size @p taken of a nozzle, as a new pipe
			/// takes it (takeNozzleDiameter()): a DN as the material of the
			/// pipe has it (DN 65 → DN 60), with its only outside diameter or
			/// the default one; an outside diameter with its DN; a thread as
			/// its DN of the fixed table. Never a question. Nothing for none.
		static void giveNozzleSize(PipeProperties &data, PipeDiameter taken,
								   const PidCodification &codes);
			/// See the definition; called by the constructor, before
			/// takeNozzleDiameter(): the outside diameter is then the one of
			/// the material taken
		void takeLineSpec();
			/// How far a line goes on through its symbols, see inlineRun():
			/// what its size passes through, what its material, piping
			/// class and nominal pressure pass through, what its fluid
			/// passes through, its line number, its flow, or its insulation
			/// (as its fluid, but stopped by an insulation limit; step
			/// 7d-bis, choice of the user of 22/11), or its jacket (the
			/// same, stopped by a jacket limit; step 8r-1)
		enum class LineReach { Size, Spec, Fluid, LineNumber, Flow, Insulation, Jacket };
		QList<Conductor *> inlineRun(LineReach reach = LineReach::Size);
			/// This pipe, when it has no size, and the pipes without a size
			/// its size would pass to (inlineRun(), LineReach::Size): a pipe
			/// which has a size stops the walk as a reducer or equipment do.
			/// None when this pipe has a size or is not a pipe (step 5l-1aa)
		QList<Conductor *> unsizedRun();
		void drawDiameterGaps(QPainter *painter) const;
			/// What is wrong with the size of this pipe: a DN, or an outside
			/// diameter, which is not one of the sizes of its material (a DN
			/// passed on through valves, a material changed, sizes taken out
			/// of the codification). Nothing for a pipe without material, of a
			/// material without sizes, or of another medium. Said on hovering
			/// it and marked by drawOffSeries()
		QString offSeriesNote() const;
		void drawOffSeries(QPainter *painter, const QPainterPath &path,
						   qreal line_width, qreal detail) const;
			/// The symbols of its folio this pipe or signal line runs over,
			/// into them and out of them (PidTraversal, step 7c-bis): not
			/// the symbols where it begins or ends, nor a basin it ends in.
			/// Worked out again only when a conductor or a symbol changed.
			/// None for a wire.
		QList<Element *> traversedSymbols() const;
			/// What is said on hovering this line about traversedSymbols(),
			/// one line by symbol; nothing when it runs over none
		QString traversalNote() const;
			/// Raised whenever a conductor changes its path, its
			/// properties, or enters or leaves a folio: what was worked out
			/// from the lines of a folio may then change
		static quint64 geometryGeneration();
		void drawTraversals(QPainter *painter, qreal line_width, qreal detail) const;
		void drawPipeParts(QPainter *painter, const QColor &color, qreal detail) const;
			/// The insulated part of the line of this pipe ends at its end
			/// on @p terminal (step 7d): the pipe is insulated or heat
			/// traced, and its end is on equipment, a free end, an
			/// insulation limit (Element::isInsulationLimit()), or beyond
			/// the symbol there is no pipe of the same kind of insulation
			/// (pipesBeyond()). Nothing at a valve or a tee between two
			/// pipes insulated alike. False for any other medium.
		bool insulationEndsAt(const Terminal *terminal) const;
			/// The pipes beyond the symbol of @p terminal, one of the ends
			/// of this pipe: on the other nozzles of its run, the other side
			/// of a reducer, those of a symbol placed against them, through
			/// pipes of no length; none on equipment. @p limit says the walk
			/// met an insulation limit, which it does not cross; with
			/// @p jacket, a jacket limit instead (step 8r-1).
		QList<Conductor *> pipesBeyond(const Terminal *terminal, bool *limit = nullptr,
									   bool jacket = false) const;
			/// The jacket of this pipe ends at its end on @p terminal (step
			/// 8r-1), as insulationEndsAt(): the pipe is jacketed, and its end
			/// is on equipment, a free end, a jacket limit
			/// (Element::isJacketLimit()), or beyond the symbol there is no
			/// pipe of the same jacket. False for any other medium.
		bool jacketEndsAt(const Terminal *terminal) const;
			/// The two lines of the jacket of this pipe, in its frame: stubs
			/// where jacketEndsAt(), as long as those of the insulation, or
			/// along the whole pipe when it asks for it
			/// (PipeProperties::jacket_full); 1.5 mm either side of the axis
			/// (PidInsulation::half_width). None for a pipe without jacket.
		QPainterPath jacketPath() const;
			/// Draw jacketPath() with the pattern of the kind of line of this
			/// pipe, half its width; in the colour its project gives the
			/// jackets (PidCodification::jacketColorKey()) when @p plain and
			/// its status of works gives none, else in @p color
		void drawJacket(QPainter *painter, const QColor &color, bool plain, qreal detail) const;
			/// What draws the insulation of this pipe, in its frame: the
			/// hatched bands in @p hatches, the lines of a heat tracing in
			/// @p dashes, of the stubs at the ends where insulationEndsAt(),
			/// and of the mark in its middle when it asks for one
			/// (PipeProperties::insulation_mark). The length of a stub is
			/// that of the project (PidCodification::insulation_stub), less
			/// on a short pipe; nothing within the parts brought at the
			/// nozzles (pipeStartAt()).
		void insulationPaths(QPainterPath *hatches, QPainterPath *dashes) const;
			/// The axis of the mark in the middle of this pipe, on the middle
			/// of its longest segment, and where its text goes in
			/// @p text_rect (PipeProperties::insulation; null without one),
			/// beside the band, on the side the designation is not; none
			/// when the pipe asks for no mark, or is too short for one
		QList<QPointF> insulationMark(QRectF *text_rect = nullptr) const;
		void drawInsulation(QPainter *painter, const QColor &color, bool plain, qreal detail) const;
			/// How this signal line says its type of signal: the mark its
			/// project gives its type (PidCodification::signalMark(), the
			/// default one unless it says otherwise); none for any other
			/// medium
		SignalMark::Mark signalMark() const;
			/// Draw the marks of this signal line along its straight runs
			/// (SignalMark::places()), in @p color, as thin as its line
		void drawSignalMarks(QPainter *painter, const QColor &color, qreal detail) const;
			/// The line of a signal line whose @p mark stands in it
			/// (SignalMark::lineGap()): stopped either side of each mark,
			/// and where it gives way at a crossing when the project draws
			/// them, its marks kept clear of the crossings (7c)
		QPainterPath brokenSignalLine(SignalMark::Mark mark) const;
			/// What a line drawn with @p pen, dashed, draws solid at its
			/// crossings, so that it crosses on a dash and never on a blank
			/// (ISO 128-20, WireHops::solidAtCrossings()): when the project
			/// draws the crossings of the P&ID (the thinner line giving way,
			/// or a gap); empty for a solid line or with upstream's hops (7c)
		QPainterPath dashesAtCrossings(const QPen &pen) const;
		void drawDashesAtCrossings(QPainter *painter, const QPen &pen) const;
		QPainterPath trimmedEnds(qreal half_width) const;
		bool potentialTypeFormat(PotentialType *type) const;
			/// The width this conductor is drawn with, out of the hover:
			/// that of its potential type @p type when it has one, else its
			/// own (ConductorProperties::lineWidth()). paint() draws with it
			/// and shape() makes the click area of a pipe from it.
		qreal drawnWidth(const PotentialType *type) const;
			/// The narrowest click area of a pipe or a signal line, in
			/// drawing units, so that a thin one is still easy to pick
		static constexpr qreal min_click_width = 4.0;

	private:
		void setUpConnectionForFormula(
				QString old_formula, QString new_formula);
			/// See insulationMark(): the mark of the part of this pipe drawn
			/// from @p from to @p to along it; with @p both_sides,
			/// @p text_rect holds every place its text may take along its
			/// segment, on both sides, wherever the designation is.
			/// The segment is chosen on the whole path, whatever is drawn
			/// at its ends: boundingRect() asks with the whole path and
			/// must not ask the folio where the pipe starts (pipeStartAt()
			/// looks for nozzles in the folio, which asks the bounding
			/// rectangle of this pipe again while its index is updated)
		QList<QPointF> insulationMarkBetween(qreal from, qreal to, QRectF *text_rect,
											 bool both_sides) const;
			/// See inlineRun(): the pipes reached from @p starts only,
			/// this one first, then the nearest first; with @p unsized_only,
			/// never through a pipe which has a size (unsizedRun())
		QList<Conductor *> runFrom(const QList<Terminal *> &starts, LineReach reach,
								   bool unsized_only = false);
		autonum::sequentialNumbers m_autoNum_seq;

	public:
		void setFreezeLabel(bool freeze);
		bool isAutomatic() const { return m_automatic; }
		void setAutomatic(bool automatic);
			/// Drawn grey while its symbol is dragged: an automatic wire
			/// is worked out anew once the symbol is put down
		bool isPreview() const { return m_preview; }
		void setPreview(bool preview) { if (m_preview != preview) { m_preview = preview; update(); } }
		bool hidesPlaceholder() const;
		bool isFreezeLabel() const { return m_freeze_label; }
		bool markReplacesText() const;
	
	public slots:
		void displayedTextChanged();
	
	protected:
		void mouseDoubleClickEvent(
				QGraphicsSceneMouseEvent *event) override;
		void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
		void mouseReleaseEvent(
				QGraphicsSceneMouseEvent *event) override;
		void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
		void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
		QVariant itemChange(
				GraphicsItemChange, const QVariant &) override;
		bool sceneEventFilter(
				QGraphicsItem *watched, QEvent *event) override;

	private:
		void adjustHandlerPos();
		
		void handlerMousePressEvent(
				QetGraphicsHandlerItem *qghi,
				QGraphicsSceneMouseEvent *event);
		void handlerMouseMoveEvent(
				QetGraphicsHandlerItem *qghi,
				QGraphicsSceneMouseEvent *event);
		void handlerMouseReleaseEvent(
				QetGraphicsHandlerItem *qghi,
				QGraphicsSceneMouseEvent *event);
		void addHandler();
		void removeHandler();
			/// Splits the first and the last of the one or two segments of
			/// this conductor, exitRoom() (10 for a signal line) from its
			/// ends, the path unchanged: each handle then has a segment that
			/// may move (step 7c-bis). Nothing when a segment is too short
		void splitEndSegments();
			/// After a segment of a pipe or a signal line is moved, while
			/// dragged too: the segment ConductorSegment::moveX() adds on the
			/// line of an end segment (back over it, on, or of no length) is
			/// made one with it, which is shortened or lengthened instead;
			/// each end segment is one straight piece up to its bend. Nothing
			/// for a wire. While dragging, the segment @p dragged is never
			/// made one with an end segment. @return true when the segments
			/// were made anew
		bool mergeFoldedEnds(const ConductorSegment *dragged = nullptr);
			/// The movement @p asked of the segment @p moved, stopped where
			/// the pipe would leave one of its nozzles less than exitRoom()
			/// straight, or behind it (step 7c-bis): only the segment
			/// joining the straight run of a nozzle stops; a run already
			/// shorter may not be shortened further. @p stop gets the
			/// terminal it stopped at, else nullptr. Nothing stops for any
			/// other medium.
		QPointF stoppedAtExits(const ConductorSegment *moved, const QPointF &asked,
							   const Terminal **stop) const;
			/// Says on the status bar that the dragged segment stopped at
			/// @p terminal; nullptr clears what it said
		void showExitStop(const Terminal *terminal);

			/// The status bar says the dragged segment stopped (showExitStop())
		bool m_exit_stop_shown = false;

		QVector<QetGraphicsHandlerItem *> m_handler_vector;
		int m_vector_index = -1;
			/// When and where the wire itself was last pressed: a press on
			/// a handle right after it is the second click of a double-click
		QElapsedTimer m_last_press;
		QPointF m_last_press_pos;
		bool m_mouse_over;
			///Hop path cache for paintedPath(), valid while no conductor geometry changed
		mutable QPainterPath m_hops_path;
		mutable quint64 m_hops_generation = 0;
		mutable int m_hops_mode = -1;
			///Where this conductor gives way at a crossing, and every point
			///where it crosses another one, in its own coordinates; kept
			///with the cache of paintedPath() (7c)
		mutable QList<WireHops::Interruption> m_hops_cuts;
		mutable QList<QPointF> m_hops_crossings;
			///See traversedSymbols(): kept while no conductor nor symbol
			///changed; the parts drawn orange, in its own coordinates
		void updateTraversals() const;
		mutable const Diagram *m_traversals_folio = nullptr;
		mutable quint64 m_traversals_conductors = 0;
		mutable quint64 m_traversals_symbols = 0;
		mutable QList<Element *> m_traversed;
		mutable QPainterPath m_traversal_path;
			/// Functional properties
		ConductorProperties m_properties;
			/// Text input for non simple, non-singleline conductors
		ConductorTextItem *m_text_item;
			/// See updateLeader(): made when first needed
		QGraphicsPathItem *m_leader = nullptr;
		ConductorDefinitionMark *m_definition_mark = nullptr;
			/// Segments composing the conductor
		ConductorSegment *segments;
			/// Attributes related to mouse interaction
		bool m_moving_segment;
		int moved_point;
		qreal m_previous_z_value;
		ConductorSegment *m_moved_segment;
		QPointF before_mov_text_pos_;
			/// Whether the conductor was manually modified by users
		bool modified_path;
			/// Whether the current profile should be saved as soon as possible
		bool has_to_save_profile;
			/// conductor profile: "photography" of what the conductor is supposed to look
			/// like - there is one profile per kind of traject
		ConductorProfilesGroup conductor_profiles;
			/// Define whether and how the conductor should be highlighted
		Highlight must_highlight_;
		bool m_valid;
			/// Null once its terminal is gone: a step that is undone and
			/// then dropped can delete a symbol before the wires it added
		QPointer<Terminal> m_terminal1_alive;
		QPointer<Terminal> m_terminal2_alive;
		bool m_freeze_label = false;
			/// Made by AutoConnect: removed again when its ends leave the line
		bool m_automatic = false;
		bool m_preview = false;
		QUuid m_uuid;
			/// false when m_uuid was synthesized by fromXml() because the
			/// file had none -- toXml() must not persist that value, or
			/// every reload mints and saves a new random one (see #754).
		bool m_persist_uuid = true;
		quint64 m_creation_rank = 0;
		void uuidChanged(const QUuid &old_uuid);
		projectDataBase *placedStore() const;
		void storeProperties();

			/// QPen et QBrush objects used to draw conductors
		static QPen conductor_pen;
		static QBrush conductor_brush;
		static bool pen_and_brush_initialized;
		QPainterPath m_path;
	
	private:
		void segmentsToPath();
		void saveProfile(bool = true);
		void generateConductorPath(const QPointF &, Qet::Orientation, const QPointF &, Qet::Orientation);
		void updateConductorPath(const QPointF &, Qet::Orientation, const QPointF &, Qet::Orientation);
		uint segmentsCount(QET::ConductorSegmentType = QET::Both) const;
		QList<QPointF> segmentsToPoints() const;
		QList<ConductorBend> bends() const;
		bool bendMakesJunction(const Conductor *, const QPointF &, Qt::Corner) const;

		void pointsToSegments(const QList<QPointF>&);
		Qt::Corner currentPathType() const;
		void deleteSegments();
		static int getCoeff(const qreal &, const qreal &);
		static int getSign(const qreal &);
		QHash<ConductorSegmentProfile *, qreal> shareOffsetBetweenSegments(const qreal &offset, const QList<ConductorSegmentProfile *> &, const qreal & = 0.01) const;
		static QPointF extendTerminal(const QPointF &, Qet::Orientation, qreal = 10);
		static QPointF extendTerminalBeyond(const QPointF &terminal, Qet::Orientation orientation,
											qreal room, const QPointF &extended);
		static bool facesOnOneLine(const QPointF &p1, Qet::Orientation o1,
								   const QPointF &p2, Qet::Orientation o2);
			/// How far this conductor leaves its terminals straight at least,
			/// beyond the extension of every conductor: for a pipe,
			/// PipeConnection::pipeExit; 0 for any other medium
		qreal exitRoom() const;
		static Qt::Corner movementType(const QPointF &, const QPointF &);
		static QPointF movePointIntoPolygon(const QPointF &, const QPainterPath &);
};

Conductor * longestConductorInPotential (Conductor *conductor, bool all_diagram = false);
QList <Conductor *> relatedConductors (const Conductor *conductor);


//return true if @a is between or at @b and @c.
template <typename T>
bool isBetween (const T a, const T b, const T c) {
	return (b <= c)? (a >= b && a <= c) : (a <= b && a >= c);
}

#endif
