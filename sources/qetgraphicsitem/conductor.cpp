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
#include <qetgraphicsitem/conductor.h>
#include <palettegraphicsview.h>
#include <shownkinds.h>
#include <lastusedstyle.h>
#include <conductorcolorcodes.h>
#include <qetproject.h>
#include <QPropertyUndoCommand/qpropertyundocommand.h>
#include <autoNum/numerotationcontextcommands.h>
#include <conductorautonumerotation.h>
#include <conductorsegment.h>
#include <conductorsegmentprofile.h>
#include <diagram.h>
#include <dataBase/projectdatabase.h>
#include <diagramcommands.h>
#include <qetdiagrameditor.h>
#include <qetgraphicsitem/terminal.h>
#include <ui/conductorpropertiesdialog.h>
#include <ui/pipepropertieswidget.h>
#include <ui/signalpropertieswidget.h>
#include "conductortextitem.h"
#include "conductordefinitionmark.h"
#include "element.h"
#include <QetGraphicsItemModeler/qetgraphicshandleritem.h>
#include <utils/qetutils.h>
#include <wirehops.h>
#include <pidtooltip.h>
#include <pidtraversal.h>
#include <pidflow.h>
#include <pidlinedesignation.h>
#include <pidinsulation.h>
#include <pidoffpage.h>
#include <shownkinds.h>
#include <factory/elementpicturefactory.h>
#include <leaderline.h>
#include <qetapp.h>
#include <QSettings>

#include <QApplication>
#include <QFontMetricsF>
#include <QGraphicsPathItem>
#include <QMultiHash>
#include <QStatusBar>
#include <QtDebug>

#include <algorithm>
#include <limits>

namespace {
		//Bumped whenever a conductor changes shape, moves, or enters or
		//leaves a folio: the hops of every conductor may then change.
		//0 is never current, so a fresh cache is always rebuilt.
	quint64 s_conductor_geometry_generation = 1;
	void conductorGeometryChanged() { ++s_conductor_geometry_generation; }
}

quint64 Conductor::geometryGeneration()
{
	return s_conductor_geometry_generation;
}

namespace {

		//The conductors of the last folio whose hops were computed, as
		//scene points and plain path bounds. Rebuilt only when the folio
		//or the generation changes, so a repaint (pan, zoom, selection)
		//costs no scene query: asking the scene for the conductors in a
		//rect strokes each one's shape, and on a folio of a few hundred
		//conductors that made every repaint 0.6 s slower.
	struct HopCandidate {
		const Conductor *conductor;
		QRectF rect;
		QVector<QPointF> points;
			//The width its line is drawn with (WireHops::Mode::Thinner)
		qreal width = 1.0;
	};
	const Diagram *s_hop_candidates_diagram = nullptr;
	quint64 s_hop_candidates_generation = 0;
	QVector<HopCandidate> s_hop_candidates;

		//The symbols of the last folio whose traversals were worked out,
		//with what they draw on the folio (7c-bis). Rebuilt only when the
		//folio changes or a symbol changed (PidTraversal::symbolsChanged()),
		//so that a repaint costs no reading of the drawings
	struct TraversalCandidate {
		const Element *element;
		PidTraversal::Outline outline;
	};
	const Diagram *s_traversal_candidates_diagram = nullptr;
	quint64 s_traversal_candidates_generation = 0;
	QVector<TraversalCandidate> s_traversal_candidates;

		//Read again the symbols of @p folio which a line may run over, when
		//the folio or a symbol changed since the last time: every symbol but
		//the drawing aids, those hidden (View > Show) and the T-junctions
		//shown as a dot
	void refreshTraversalCandidates(const Diagram *folio)
	{
		const quint64 symbols = PidTraversal::symbolsGeneration();
		if (s_traversal_candidates_diagram == folio
			&& s_traversal_candidates_generation == symbols) {
			return;
		}
		s_traversal_candidates.clear();
		const QList<Element *> elements = folio->elements();
		for (const Element *element : elements)
		{
			if (!element->isVisible() || ShownKinds::isHidden(element)
				|| element->isDrawingAid() || element->isShownAsDot()) {
				continue;
			}
			TraversalCandidate candidate;
			candidate.element = element;
			candidate.outline = PidTraversal::outline(
						ElementPictureFactory::instance()->getPrimitives(
							element->location(), element->resolvedOptions()),
						element->sceneTransform());
			if (!candidate.outline.isEmpty()) {
				s_traversal_candidates.append(candidate);
			}
		}
		s_traversal_candidates_diagram = folio;
		s_traversal_candidates_generation = symbols;
	}
}

#define PR(x) qDebug() << #x " = " << x;

bool Conductor::pen_and_brush_initialized = false;
QPen Conductor::conductor_pen = QPen();
QBrush Conductor::conductor_brush = QBrush();


class ConductorXmlRetroCompatibility
{
		friend class Conductor;

		static void loadSequential(const QDomElement &dom_element, const QString& seq, QStringList* list)
		{
			int i = 0;
			while (!dom_element.attribute(seq + QString::number(i+1)).isEmpty())
			{
					list->append(dom_element.attribute(seq + QString::number(i+1)));
					i++;
			}
		}

		static void loadSequential(const QDomElement &dom_element, Conductor *conductor)
		{
			autonum::sequentialNumbers sn;

			loadSequential(dom_element,"sequ_",&sn.unit);
			loadSequential(dom_element,"sequf_",&sn.unit_folio);
			loadSequential(dom_element,"seqt_",&sn.ten);
			loadSequential(dom_element,"seqtf_",&sn.ten_folio);
			loadSequential(dom_element,"seqh_",&sn.hundred);
			loadSequential(dom_element,"seqhf_",&sn.hundred_folio);

			conductor->replaceSequenceNum(sn);
		}
};

/**
	@brief Conductor::Conductor
	Default constructor.
	@param p1 : first terminal of this conductor.
	@param p2 : second terminal of this conductor.
*/
Conductor::Conductor(Terminal *p1, Terminal* p2) :
	terminal1(p1),
	terminal2(p2),
	m_mouse_over(false),
	m_text_item(nullptr),
	segments(nullptr),
	m_moving_segment(false),
	modified_path(false),
	has_to_save_profile(false),
	must_highlight_(Conductor::None)
{
	m_terminal1_alive = p1;
	m_terminal2_alive = p2;
		//set Zvalue at 11 to be upper than the DiagramImageItem and element
	setZValue(11);
	m_previous_z_value = zValue();
	m_uuid = QUuid::createUuid();
	static quint64 created = 0;
	m_creation_rank = ++created;

		//Add this conductor to the list of conductors of each of the two terminals
	bool ajout_p1 = terminal1 -> addConductor(this);
	bool ajout_p2 = terminal2 -> addConductor(this);
		//m_valid become false if the conductor can't be added to terminal (conductor already exist)
	m_valid = (!ajout_p1 || !ajout_p2) ? false : true;

		//Default attribute to paint a conductor
	if (!pen_and_brush_initialized)
	{
		conductor_pen.setJoinStyle(Qt::MiterJoin);
		conductor_pen.setCapStyle(Qt::SquareCap);
		conductor_pen.setColor(Qt::black);
		conductor_pen.setStyle(Qt::SolidLine);
		conductor_pen.setWidthF(1.0);
		conductor_brush.setColor(Qt::white);
		conductor_brush.setStyle(Qt::NoBrush);
		pen_and_brush_initialized = true;
	}

		//By default, the 4 profiles are nuls -> we must use priv_calculeConductor
	conductor_profiles.insert(Qt::TopLeftCorner,     ConductorProfile());
	conductor_profiles.insert(Qt::TopRightCorner,    ConductorProfile());
	conductor_profiles.insert(Qt::BottomLeftCorner,  ConductorProfile());
	conductor_profiles.insert(Qt::BottomRightCorner, ConductorProfile());

		//Generate the path of this conductor, knowing its medium already: a
		//pipe leaves its nozzles farther (exitRoom())
	m_properties.m_medium = p1->medium();
	generateConductorPath(terminal1 -> dockConductor(), terminal1 -> orientation(), terminal2 -> dockConductor(), terminal2 -> orientation());
	setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsScenePositionChanges);
	setAcceptHoverEvents(true);

		// Add the text field
	m_text_item = new ConductorTextItem(m_properties.text, this);
	connect(m_text_item, &ConductorTextItem::textEdited, this, &Conductor::displayedTextChanged);

		//The medium of its terminals: a conductor between two nozzles is a
		//pipe. Between two media, the medium of the terminal it starts from
		//(the one it was drawn from). Kept by setProperties() below.
	m_properties.m_medium = p1->medium();

		//Set the default conductor properties. The color, specifically, is
		//overridden by the last one applied via the F2 color editor this
		//session (#879), the same way LastUsedStyle already does for shapes.
		//A conductor of another medium than electrical takes none of the
		//wire defaults (ConductorProperties::defaultsFor()).
	Diagram *dia = p1->diagram() ? p1->diagram() : p2->diagram();
	if (dia)
	{
		ConductorProperties properties = ConductorProperties::defaultsFor(
					m_properties.m_medium, dia->defaultConductorProperties);
		if (m_properties.m_medium == TerminalData::Electrical
			&& LastUsedStyle::hasConductorColor())
			properties.color = LastUsedStyle::conductorColor();
			//A colour code from the toolbar also gives the colour text
			//and, for green-yellow, the stripes
		if (const auto *code = ConductorColorCodes::find(LastUsedStyle::conductorColorCode()))
			ConductorColorCodes::apply(*code, properties);
		setProperties(properties);
	}
		//The spec of its line first: the outside diameter taken with the
		//size is the one of its material
	takeLineSpec();
	takeNozzleDiameter();
}

/**
	@brief Conductor::takeLineSpec
	A new pipe takes the material, piping class and nominal pressure of its
	line (step 5l-1l): each one it has not, from the nearest pipe which has
	it, on the side of the nozzle it starts from first, then on the other,
	as far as the spec of a line goes (LineReach::Spec: through valves, tees
	and reducers, up to a spec break or equipment); its fluid the same way,
	through spec breaks too (LineReach::Fluid, step 5l-1z); its line number
	and its flow as far as they go (LineReach::LineNumber, LineReach::Flow,
	step 5l-1k); its kind of line (main, secondary, auxiliary) as its line
	number, a branch of a tee starting as a main line (step 6b-1c); its
	insulation from the nearest pipe of its line (LineReach::Insulation,
	step 7d-bis), and its jacket the same way (LineReach::Jacket, step
	8r-1). Done once, when
	it is connected: a pipe given data of its own afterwards (the halves of a
	pipe cut, a pipe read from a file) keeps them, empty fields included.
*/
void Conductor::takeLineSpec()
{
	if (m_properties.m_medium != TerminalData::Pipe) {
		return;
	}
	ConductorProperties pipe = properties();
	PipeProperties &data = pipe.m_pipe;
		//Each field it has not, from the nearest pipe of the reach which has it
	auto take = [this, &data](const QList<QString PipeProperties::*> &fields, LineReach reach) {
		auto missing = [&data, &fields]() {
			for (QString PipeProperties::*field : fields) {
				if ((data.*field).isEmpty()) {
					return true;
				}
			}
			return false;
		};
		for (Terminal *end : {terminal1, terminal2})
		{
			if (!end || !missing()) {
				continue;
			}
			const QList<Conductor *> side = runFrom({end}, reach);
			for (QString PipeProperties::*field : fields)
			{
				if (!(data.*field).isEmpty()) {
					continue;
				}
				for (const Conductor *other : side) {
					const QString &value = other->m_properties.m_pipe.*field;
					if (other != this && other->m_properties.m_medium == TerminalData::Pipe
						&& !value.isEmpty()) {
						data.*field = value;
						break;
					}
				}
			}
		}
	};
	take({&PipeProperties::material, &PipeProperties::piping_class,
		  &PipeProperties::nominal_pressure}, LineReach::Spec);
	take({&PipeProperties::fluid}, LineReach::Fluid);
	take({&PipeProperties::line_number}, LineReach::LineNumber);
	take({&PipeProperties::flow}, LineReach::Flow);
		//Its kind of line, that of the nearest pipe of the line it goes on:
		//as far as the line number goes (step 6b-1c)
	bool kind_taken = false;
	for (Terminal *end : {terminal1, terminal2})
	{
		if (!end || kind_taken) {
			continue;
		}
		for (const Conductor *other : runFrom({end}, LineReach::LineNumber)) {
			if (other != this && other->m_properties.m_medium == TerminalData::Pipe) {
				data.line_kind = other->m_properties.m_pipe.line_kind;
				kind_taken = true;
				break;
			}
		}
	}
		//Its insulation (kind, text, mark), that of the nearest pipe of its
		//line when insulated, as far as an insulation goes: through tees,
		//the branch of a tee included, up to an insulation limit or
		//equipment (step 7d-bis, choice of the user of 22/11)
	if (data.insulation_kind == PipeProperties::NoInsulation)
	{
		for (Terminal *end : {terminal1, terminal2})
		{
			if (!end || data.insulation_kind != PipeProperties::NoInsulation) {
				continue;
			}
			for (const Conductor *other : runFrom({end}, LineReach::Insulation)) {
				if (other == this || other->m_properties.m_medium != TerminalData::Pipe) {
					continue;
				}
				const PipeProperties &theirs = other->m_properties.m_pipe;
				if (theirs.insulation_kind != PipeProperties::NoInsulation) {
					data.insulation_kind = theirs.insulation_kind;
					data.insulation = theirs.insulation;
					data.insulation_mark = theirs.insulation_mark;
				}
					//The nearest only: a bare one says the line is bare
				break;
			}
		}
	}
		//Its jacket (material and size), the same way, up to a jacket
		//limit or equipment (step 8r-1); not the drawing along its whole
		//length, which is each pipe's own
	if (!data.jacketed())
	{
		for (Terminal *end : {terminal1, terminal2})
		{
			if (!end || data.jacketed()) {
				continue;
			}
			for (const Conductor *other : runFrom({end}, LineReach::Jacket)) {
				if (other == this || other->m_properties.m_medium != TerminalData::Pipe) {
					continue;
				}
				data.takeJacket(other->m_properties.m_pipe);
					//The nearest only: a bare one says the line is bare
				break;
			}
		}
	}
	if (!(pipe == properties())) {
		setProperties(pipe);
	}
}

/**
	@brief Conductor::takeNozzleDiameter
	A new pipe without a size takes the one of a nozzle at its ends, the
	nozzle it starts from first: given to it, or on a valve that of the pipe
	on the other side; on the branch of a tee, when neither end has one,
	that of the line of the tee (an equal tee until told otherwise).
	A DN is the size of the line; with it goes the outside
	diameter of the material of the pipe, when it has one only for this DN,
	or a default one among several. An outside diameter is kept as the
	second value, with the DN the sizes of the materials join to it, when
	they join one only; a material named by its DN keeps that DN alone. A thread, which a pipe never has,
	becomes its DN of the fixed table (1"1/2, DN 40). Done once, when it is
	connected: from then on each side keeps its value, and a difference is
	marked. Read back from a file, a pipe has the size saved there.
*/
void Conductor::takeNozzleDiameter()
{
	if (m_properties.m_medium != TerminalData::Pipe) {
		return;
	}
	ConductorProperties pipe = properties();
	if (pipe.m_pipe.hasSize()) {
		return;
	}
		//The size an end has for sure goes before the one the branch of a
		//tee only has by default, that of its line (Terminal::pipeDiameter()):
		//drawn from the branch of a tee on a DN 50 line to a DN 25 nozzle,
		//a pipe is DN 25; to a nozzle without a size, DN 50 (step 6b-1c)
	PipeDiameter taken;
	for (const bool with_default : {false, true}) {
		for (const Terminal *end : {terminal1, terminal2}) {
			if (end && !taken.isSet()) {
				taken = end->diameter(with_default);
			}
		}
	}
		//A pipe of no length, between two symbols placed against each
		//other, is of the line: the size of the end which follows a line
		//goes before the size of a nozzle of equipment
	if (endsTouch() && !terminal1->followsPipe() && terminal2->followsPipe()
		&& terminal2->diameter(false).isSet()) {
		taken = terminal2->diameter(false);
	}
	if (taken.isThread()) {
		taken = taken.threadNominal();
	}
	if (!taken.isSet()) {
		return;
	}
		//The folio of its nozzles: the pipe may not be on it yet
	const Diagram *folio = terminal1 && terminal1->diagram() ? terminal1->diagram()
															 : (terminal2 ? terminal2->diagram() : nullptr);
	const PidCodification codes = folio && folio->project() ? folio->project()->pidCodification()
															: PidCodification();
	giveNozzleSize(pipe.m_pipe, taken, codes);
	setProperties(pipe);
}

/**
	@brief Conductor::giveNozzleSize
	See the declaration
*/
void Conductor::giveNozzleSize(PipeProperties &data, PipeDiameter taken,
							   const PidCodification &codes)
{
	if (taken.isThread()) {
		taken = taken.threadNominal();
	}
	if (!taken.isSet()) {
		return;
	}
	if (taken.isOutside())
	{
		data.nominal = codes.nominalOf(taken, data.material);
			//A material named by its DN has no outside diameter
		const PidCodification::Entry *material = codes.material(data.material);
		if (!data.nominal.isSet() || !material || material->outside_diameter) {
			data.outside = taken;
		}
	}
	else
	{
			//DN 65 on a material which has DN 60 for it
		data.nominal = codes.nominalIn(taken, data.material);
			//The only outside diameter of this DN, or the default one
		data.outside = codes.outsideOf(data.nominal, data.material);
	}
}

/**
	@brief Conductor::~Conductor
	Destructor. The conductor is removed from its terminal
*/
Conductor::~Conductor()
{
	conductorGeometryChanged();
	removeHandler();
		//An undone step that is dropped deletes its symbols before the
		//wires it added to them: their terminals may be gone already
	if (m_terminal1_alive) terminal1->removeConductor(this);
	if (m_terminal2_alive) terminal2->removeConductor(this);
	deleteSegments();
}

/**
	@brief Conductor::isValid
	@return true if conductor is valid else false;
	A non valid conductor, is a conductor without two terminal
*/
bool Conductor::isValid() const
{
	return m_valid;
}

/**
	Met a jour la representation graphique du conducteur en recalculant son
	trace. Cette fonction est typiquement appelee lorsqu'une seule des bornes du
	conducteur a change de position.
	@param rect Rectangle a mettre a jour
	@see QGraphicsPathItem::update()
*/
void Conductor::updatePath(const QRectF &rect) {
	QPointF p1, p2;
	p1 = terminal1 -> dockConductor();
	p2 = terminal2 -> dockConductor();
	if (segmentsCount() && !conductor_profiles[currentPathType()].isNull())
		updateConductorPath(p1, terminal1 -> orientation(), p2, terminal2 -> orientation());
		//A pipe or a signal line drawn by hand whose ends come level or
		//pass each other: the path it has now becomes the profile of the
		//new position, so that it keeps its detours (step 7c-bis)
	else if (segmentsCount() && modified_path && m_properties.m_medium != TerminalData::Electrical) {
		conductor_profiles[currentPathType()].fromConductor(this);
		updateConductorPath(p1, terminal1 -> orientation(), p2, terminal2 -> orientation());
	}
	else
		generateConductorPath(p1, terminal1 -> orientation(), p2, terminal2 -> orientation());
	calculateTextItemPosition();
	QGraphicsObject::update(rect);
}

/**
	@brief Conductor::moveEnd
	Move the end of this conductor on @p from to @p to, as when an option of
	their symbol hides @p from and shows @p to in its place
	(Terminal::replacement(), ChangeElementOptionsCommand). The conductor goes
	on, with its properties and its texts; its path follows its new end as it
	follows a moved element (updatePath()). Neither the options nor the room
	left on @p to are checked: the caller does that, and an undo moves the
	end back the same way.
	@return false, and nothing done, when this conductor does not end on
	@p from, or when a conductor already joins @p to to its other end
*/
bool Conductor::moveEnd(Terminal *from, Terminal *to)
{
	if (!from || !to || from == to) {
		return false;
	}
	const bool first = terminal1 == from;
	if (!first && terminal2 != from) {
		return false;
	}
	Terminal *other = first ? terminal2 : terminal1;
	if (other == to || to->isLinkedTo(other)) {
		return false;
	}
		//On a folio, as a conductor removed and added again (Diagram::removeItem(),
		//Diagram::addItem()), without leaving it: the rows of the project
		//database name its terminals
	Diagram *folio = diagram();
	projectDataBase *db = folio && folio->project() ? folio->project()->dataBase() : nullptr;
	if (db) {
		db->removeConductor(this);
	}
	from->removeConductor(this);
	updateFittings();
	if (first) {
		terminal1 = to;
		m_terminal1_alive = to;
	} else {
		terminal2 = to;
		m_terminal2_alive = to;
	}
	to->addConductor(this);
	updatePath();
	updateFittings();
	if (folio) {
		folio->refreshTerminalStrips();
	}
	if (db) {
		db->addConductor(this);
	}
	return true;
}

/**
	@brief Conductor::segmentsToPath
	Generate the QPainterPath from the list of points
*/
void Conductor::segmentsToPath()
{
	QPainterPath path;

	if (segments == nullptr)
		setPath(path);

		//Start the path
	path.moveTo(segments -> firstPoint());
		//Each segments
	ConductorSegment *segment = segments;
	while(segment -> hasNextSegment()) {
		path.lineTo(segment -> secondPoint());
		segment = segment -> nextSegment();
	}
		//Finish the path
	path.lineTo(segment -> secondPoint());

	setPath(path);
	storePath();

		//If conductor is selected and he's not being modified
		//we update the position of the handlers
	if (isSelected() && !m_moving_segment)
	{
		if(handlerPoints().size() == m_handler_vector.size())
			adjustHandlerPos();
		else
		{
			removeHandler();
			addHandler();
		}
	}
}

/**
	Gere les updates
	@param p1 Coordonnees du point d'amarrage de la borne 1
	@param o1 Orientation de la borne 1
	@param p2 Coordonnees du point d'amarrage de la borne 2
	@param o2 Orientation de la borne 2
*/
void Conductor::updateConductorPath(const QPointF &p1, Qet::Orientation o1, const QPointF &p2, Qet::Orientation o2) {
	ConductorProfile &conductor_profile = conductor_profiles[currentPathType()];

	Q_ASSERT_X(conductor_profile.segmentsCount(QET::Both) > 1, "Conductor::priv_modifieConductor", "pas de points a modifier");
	Q_ASSERT_X(!conductor_profile.isNull(),                 "Conductor::priv_modifieConductor", "pas de profil utilisable");

	// recupere les coordonnees fournies des bornes
	// retrieve the coordinates provided for the terminals
	QPointF new_p1 = mapFromScene(p1);
	QPointF new_p2 = mapFromScene(p2);
	QRectF new_rect = QRectF(new_p1, new_p2);

	// recupere la largeur et la hauteur du profil
	// retrieve the width and height of the profile
	qreal profile_width  = conductor_profile.width();
	qreal profile_height = conductor_profile.height();

	// calcule les differences verticales et horizontales a appliquer
	// calculates the vertical and horizontal differences to be applied
	qreal h_diff = (qAbs(new_rect.width())  - qAbs(profile_width) ) * getSign(profile_width);
	qreal v_diff = (qAbs(new_rect.height()) - qAbs(profile_height)) * getSign(profile_height);
		//A pipe or a signal line keeps its detours on their side, even when
		//its ends come level or pass each other (step 7c-bis): its segments
		//take the difference as it is, none turns the other way
	const bool keeps_side = m_properties.m_medium != TerminalData::Electrical;
	if (keeps_side) {
		h_diff = new_rect.width() - profile_width;
		v_diff = new_rect.height() - profile_height;
	}

	// A profile with no segment of non-zero length along an axis has nowhere
	// to put a difference along that axis: it would be dropped and the last
	// point would join the terminal diagonally. Every straight conductor
	// loaded from a file is like this, as a zero-length segment is saved as
	// horizontal (bugtracker #105). Generate a new path instead, as when
	// there is no profile for this path type.
	const auto can_absorb = [keeps_side](qreal diff, const QList<ConductorSegmentProfile *> &segments_list) {
		if (qAbs(diff) <= 0.01) return(true);
		if (keeps_side) {
				//A segment going the way of the difference takes any of it;
				//the others only shrink, down to nothing
			qreal against = 0;
			for (ConductorSegmentProfile *csp : segments_list) {
				if (csp -> length * diff > 0) return(true);
				against += qAbs(csp -> length);
			}
			return(against >= qAbs(diff) - 0.01);
		}
		for (ConductorSegmentProfile *csp : segments_list) {
			if (csp -> length) return(true);
		}
		return(false);
	};
		//A pipe keeps the runs leaving its nozzles (exitRoom()): the other
		//segments take the difference
	QList<ConductorSegmentProfile *> horizontal = conductor_profile.horizontalSegments();
	QList<ConductorSegmentProfile *> vertical = conductor_profile.verticalSegments();
	QList<ConductorSegmentProfile *> kept;
	if (exitRoom() > 0 && conductor_profile.segments.count() > 2) {
		kept << conductor_profile.segments.first() << conductor_profile.segments.last();
		for (ConductorSegmentProfile *end : std::as_const(kept)) {
			horizontal.removeAll(end);
			vertical.removeAll(end);
		}
	}
	if (!can_absorb(h_diff, horizontal) ||
		!can_absorb(v_diff, vertical)) {
			//A detour that no longer fits on its side: routed by itself,
			//as a pipe whose exit would be too short
		if (keeps_side) {
			conductor_profile = ConductorProfile();
			modified_path = false;
		}
		generateConductorPath(p1, o1, p2, o2);
		return;
	}

	// applique les differences aux segments
	// apply the differences to the segments
	QMultiHash<ConductorSegmentProfile *, qreal> segments_lengths;
	segments_lengths.unite(shareOffsetBetweenSegments(h_diff, horizontal));
	segments_lengths.unite(shareOffsetBetweenSegments(v_diff, vertical));
	for (ConductorSegmentProfile *end : std::as_const(kept)) {
		if (!segments_lengths.contains(end)) {
			segments_lengths.insert(end, end -> length);
		}
	}

	// en deduit egalement les coefficients d'inversion (-1 pour une inversion, +1 pour conserver le meme sens)
	// also deduce the inversion coefficients (-1 for inversion, +1 to keep the same direction)
	int horiz_coeff = keeps_side ? 1 : getCoeff(new_rect.width(),  profile_width);
	int verti_coeff = keeps_side ? 1 : getCoeff(new_rect.height(), profile_height);

	// genere les nouveaux points
	// generate the new points
	QList<QPointF> points;
	points << new_p1;
	int limit = conductor_profile.segments.count() - 1;
	for (int i = 0 ; i < limit ; ++ i) {
		// dernier point
		// last point
		QPointF previous_point = points.last();

		// profil de segment de conducteur en cours
		// current conductor segment profile
		ConductorSegmentProfile *csp = conductor_profile.segments.at(i);

		// coefficient et offset a utiliser pour ce point
		// coefficient and offset to be used for this point
		qreal coeff = csp -> isHorizontal ? horiz_coeff : verti_coeff;
		qreal offset_applied = segments_lengths.value(csp);

		// applique l'offset et le coeff au point
		// apply coefficient and offset to point
		if (csp -> isHorizontal) {
			points << QPointF (
				previous_point.x() + (coeff * offset_applied),
				previous_point.y()
			);
		} else {
			points << QPointF (
				previous_point.x(),
				previous_point.y() + (coeff * offset_applied)
			);
		}
	}
	points << new_p2;
		//A pipe whose run leaving a nozzle would still be too short, or
		//turned back, or a pipe or a signal line going back over itself:
		//routed by itself
	if (exitsTooShort(points) || foldsBack(points)) {
		conductor_profile = ConductorProfile();
		modified_path = false;
		generateConductorPath(p1, o1, p2, o2);
		return;
	}
	pointsToSegments(points);
	segmentsToPath();
}

/**
	@param offset Longueur a repartir entre les segments
	@param segments_list Segments sur lesquels il faut repartir la longueur
	@param precision seuil en-deca duquel on considere qu'il ne reste rien a repartir
*/
QHash<ConductorSegmentProfile *, qreal> Conductor::shareOffsetBetweenSegments(
	const qreal &offset,
	const QList<ConductorSegmentProfile *> &segments_list,
	const qreal &precision
) const
{
	// construit le QHash qui sera retourne
	QHash<ConductorSegmentProfile *, qreal> segments_hash;
	foreach(ConductorSegmentProfile *csp, segments_list) {
		segments_hash.insert(csp, csp -> length);
	}

	// memorise le signe de la longueur de chaque segement
	QHash<ConductorSegmentProfile *, int> segments_signs;
	foreach(ConductorSegmentProfile *csp, segments_hash.keys()) {
		segments_signs.insert(csp, getSign(csp -> length));
	}

	//qDebug() << "repartition d'un offset de" << offset << "px sur" << segments_list.count() << "segments";

	// repartit l'offset sur les segments
	qreal remaining_offset = offset;
	while (remaining_offset > precision || remaining_offset < -precision) {
		// recupere le nombre de segments differents ayant une longueur non nulle
		uint segments_count = 0;
		foreach(ConductorSegmentProfile *csp, segments_hash.keys()) if (segments_hash[csp]) ++ segments_count;
		//qDebug() << "  remaining_offset =" << remaining_offset;
		qreal local_offset = remaining_offset / segments_count;
		//qDebug() << "  repartition d'un offset local de" << local_offset << "px sur" << segments_count << "segments";
		remaining_offset = 0.0;
		foreach(ConductorSegmentProfile *csp, segments_hash.keys()) {
			// ignore les segments de longueur nulle
			if (!segments_hash[csp]) continue;
			// applique l'offset au segment
			//qreal segment_old_length = segments_hash[csp];
			segments_hash[csp] += local_offset;

			// (la longueur du segment change de signe) <=> (le segment n'a pu absorbe tout l'offset)
			if (segments_signs[csp] != getSign(segments_hash[csp])) {

				// on remet le trop-plein dans la reserve d'offset
				remaining_offset += qAbs(segments_hash[csp]) * getSign(local_offset);
				//qDebug() << "    trop-plein de" << qAbs(segments_hash[csp]) * getSign(local_offset) << "remaining_offset =" << remaining_offset;
				segments_hash[csp] = 0.0;
			} else {
				//qDebug() << "    offset local de" << local_offset << "accepte";
			}
		}
	}

	return(segments_hash);
}

/**
	Calcule un trajet "par defaut" pour le conducteur
	@param p1 Coordonnees du point d'amarrage de la borne 1
	@param o1 Orientation de la borne 1
	@param p2 Coordonnees du point d'amarrage de la borne 2
	@param o2 Orientation de la borne 2
*/
void Conductor::generateConductorPath(const QPointF &p1, Qet::Orientation o1, const QPointF &p2, Qet::Orientation o2) {
	// mappe les points par rapport a la scene
	pointsToSegments(defaultRoute(mapFromScene(p1), o1, mapFromScene(p2), o2, exitRoom()));
	segmentsToPath();
}

/**
	@brief Conductor::defaultRoute
	See the declaration. @p sp1 and @p sp2 are in the frame the points are
	given back in (the frame of the conductor for generateConductorPath()).
*/
QList<QPointF> Conductor::defaultRoute(const QPointF &sp1, Qet::Orientation o1,
									   const QPointF &sp2, Qet::Orientation o2,
									   qreal exit_room)
{
	QPointF depart, newp1, newp2, arrivee, depart0, arrivee0;
	Qet::Orientation ori_depart, ori_arrivee;

	// s'assure qu'il n'y a ni points
	QList<QPointF> points;

		//A pipe between two nozzles facing each other on one line goes
		//straight, however short; between two nozzles on one point (symbols
		//placed against each other) it has no length: no loop out and back
	if (exit_room > 0 && (QLineF(sp1, sp2).length() < 0.5 || facesOnOneLine(sp1, o1, sp2, o2))) {
		return {sp1, sp2};
	}

	// prolonge les bornes
	newp1 = extendTerminal(sp1, o1);
	newp2 = extendTerminal(sp2, o2);
		//A pipe leaves a nozzle straight far enough for the mark of any
		//connection and the part it brings, whatever the connection: the
		//same route for the same symbols (straight between two nozzles
		//facing each other, above). A segment moved by hand never shortens
		//this first one.
	newp1 = extendTerminalBeyond(sp1, o1, exit_room, newp1);
	newp2 = extendTerminalBeyond(sp2, o2, exit_room, newp2);

	// distingue le depart de l'arrivee : le trajet se fait toujours de gauche a droite (apres prolongation)
	if (newp1.x() <= newp2.x()) {
		depart      = newp1;
		arrivee     = newp2;
		depart0     = sp1;
		arrivee0    = sp2;
		ori_depart  = o1;
		ori_arrivee = o2;
	} else {
		depart      = newp2;
		arrivee     = newp1;
		depart0     = sp2;
		arrivee0    = sp1;
		ori_depart  = o2;
		ori_arrivee = o1;
	}

	// debut du trajet
	points << depart0;

	// prolongement de la borne de depart
	// extend start terminal
	points << depart;

	// commence le vrai trajet
	// starts the real path
	//
	// Each cas "3"/"4" below inserts a two-point bridge along one axis, at a
	// coordinate on the OTHER axis computed as a midpoint and then snapped
	// to the grid by a loop that walks it strictly downward until it
	// divides evenly. Whenever depart and arrivee already agree on the axis
	// the bridge would run along, that bridge is unneeded -- the midpoint
	// starts on the correct value already, but the snap can still walk it
	// off, or, if it happens to already be on-grid, the two bridge points
	// just duplicate depart and arrivee outright. Either way the result is
	// a short out-and-back excursion, or a longer looping detour, at a join
	// that needed no bridge in the first place (bugtracker #734). Skip the
	// bridge in exactly that case; depart and arrivee already form the
	// direct run. Which axis each cas guards on is noted at each site below
	// -- descendant and montant are not mirror images of each other here.
	if (depart.y() < arrivee.y()) {
		// trajet descendant
		if ((ori_depart == Qet::North && (ori_arrivee == Qet::South || ori_arrivee == Qet::West)) || (ori_depart == Qet::East && ori_arrivee == Qet::West)) {
			// cas "3": bridge is vertical, at a shared x between depart.y()
			// and arrivee.y() -- unnecessary exactly when depart and
			// arrivee already share an x. Compared qRound()ed, the same
			// rounding the bridge coordinate itself is computed with below:
			// an exact != would miss a pair that's already grid-equal after
			// rounding but off by a sub-pixel remainder, and still route a
			// degenerate bridge for it.
			if (qRound(depart.x()) != qRound(arrivee.x())) {
				int ligne_inter_x = qRound(depart.x() + arrivee.x()) / 2;
				while (ligne_inter_x % Diagram::xGrid) -- ligne_inter_x;
				points << QPointF(ligne_inter_x, depart.y());
				points << QPointF(ligne_inter_x, arrivee.y());
			}
		} else if ((ori_depart == Qet::South && (ori_arrivee == Qet::North || ori_arrivee == Qet::East)) || (ori_depart == Qet::West && ori_arrivee == Qet::East)) {
			// cas "4": bridge is horizontal, at a shared y between
			// depart.x() and arrivee.x() -- unnecessary exactly when depart
			// and arrivee already share a y. (Always true in this branch,
			// since "descendant" requires depart.y() < arrivee.y() strictly
			// -- kept for symmetry with the "montant" branch below.)
			if (qRound(depart.y()) != qRound(arrivee.y())) {
				int ligne_inter_y = qRound(depart.y() + arrivee.y()) / 2;
				while (ligne_inter_y % Diagram::yGrid) -- ligne_inter_y;
				points << QPointF(depart.x(), ligne_inter_y);
				points << QPointF(arrivee.x(), ligne_inter_y);
			}
		} else if ((ori_depart == Qet::North || ori_depart == Qet::East) && (ori_arrivee == Qet::North || ori_arrivee == Qet::East)) {
			points << QPointF(arrivee.x(), depart.y()); // cas "2"
		} else {
			points << QPointF(depart.x(), arrivee.y()); // cas "1"
		}
	} else {
		// trajet montant
		if ((ori_depart == Qet::West && (ori_arrivee == Qet::East || ori_arrivee == Qet::South)) || (ori_depart == Qet::North && ori_arrivee == Qet::South)) {
			// cas "3": bridge is horizontal, at a shared y between
			// depart.x() and arrivee.x() -- unnecessary exactly when depart
			// and arrivee already share a y (the West->East case
			// diagnosed for #734: two stubs extended onto the same y run
			// straight into each other, no bridge needed). Compared
			// qRound()ed, same as the other three guards.
			if (qRound(depart.y()) != qRound(arrivee.y())) {
				int ligne_inter_y = qRound(depart.y() + arrivee.y()) / 2;
				while (ligne_inter_y % Diagram::yGrid) -- ligne_inter_y;
				points << QPointF(depart.x(), ligne_inter_y);
				points << QPointF(arrivee.x(), ligne_inter_y);
			}
		} else if ((ori_depart == Qet::East && (ori_arrivee == Qet::West || ori_arrivee == Qet::North)) || (ori_depart == Qet::South && ori_arrivee == Qet::North)) {
			// cas "4": bridge is vertical, at a shared x between depart.y()
			// and arrivee.y() -- unnecessary exactly when depart and
			// arrivee already share an x.
			if (qRound(depart.x()) != qRound(arrivee.x())) {
				int ligne_inter_x = qRound(depart.x() + arrivee.x()) / 2;
				while (ligne_inter_x % Diagram::xGrid) -- ligne_inter_x;
				points << QPointF(ligne_inter_x, depart.y());
				points << QPointF(ligne_inter_x, arrivee.y());
			}
		} else if ((ori_depart == Qet::West || ori_depart == Qet::North) && (ori_arrivee == Qet::West || ori_arrivee == Qet::North)) {
			points << QPointF(depart.x(), arrivee.y()); // cas "2"
		} else {
			points << QPointF(arrivee.x(), depart.y()); // cas "1"
		}
	}

	// fin du vrai trajet
	points << arrivee;

	// prolongement de la borne d'arrivee
	points << arrivee0;

	// inverse eventuellement l'ordre des points afin que le trajet soit exprime de la borne 1 vers la borne 2
	if (newp1.x() > newp2.x()) {
		QList<QPointF> points2;
		for (int i = points.size() - 1 ; i >= 0 ; -- i) points2 << points.at(i);
		points = points2;
	}

	return points;
}

/**
 * @brief Conductor::extendTerminal
 * @param terminal : point to extend (must be the docking point of a terminal)
 * @param terminal_orientation : the orientation of the terminal
 * @param ext_size : how many to extrend (10 by default)
 * @return the point with an extension of @ext_size
 * and rounded to nearest multiple of ten
 * in order to be snapped to the grid of the diagram.
 */
QPointF Conductor::extendTerminal(const QPointF &terminal, Qet::Orientation terminal_orientation, qreal ext_size)
{
	QPointF extended_terminal;
	switch(terminal_orientation) {
		case Qet::North:
			extended_terminal = QPointF(terminal.x(),
										std::round((terminal.y() - ext_size)/10)*10);
			break;
		case Qet::East:
			extended_terminal = QPointF(std::round((terminal.x() + ext_size)/10)*10,
										terminal.y());
			break;
		case Qet::South:
			extended_terminal = QPointF(terminal.x(),
										std::round((terminal.y() + ext_size)/10)*10);
			break;
		case Qet::West:
			extended_terminal = QPointF(std::round((terminal.x() - ext_size)/10)*10,
										terminal.y());
			break;
		default: extended_terminal = terminal;
	}

	return(extended_terminal);
}

namespace {
	/// See Conductor::outlinesLightWires(); -1 until read from the settings
	int s_outlines_light_wires = -1;
	/// A wire lighter than this (0 black, 1 white) gets the outline
	constexpr qreal light_wire_lightness = 0.85;
}

/**
	@brief Conductor::outlinesLightWires
	@return true if a white or very pale wire is drawn with a thin black
	outline, so it shows on a white folio. Off by default.
*/
bool Conductor::outlinesLightWires()
{
	if (s_outlines_light_wires < 0) {
		QSettings settings;
		s_outlines_light_wires = settings.value(
			QStringLiteral("diagrameditor/outline_light_wires"), false).toBool();
	}
	return s_outlines_light_wires;
}

/**
	@brief Conductor::setOutlinesLightWires
	@param outline : see outlinesLightWires(); kept in the settings
*/
void Conductor::setOutlinesLightWires(bool outline)
{
	s_outlines_light_wires = outline;
	QSettings settings;
	settings.setValue(QStringLiteral("diagrameditor/outline_light_wires"), outline);
}

/**
	@brief Conductor::extendTerminalBeyond
	@param terminal : the docking point of a terminal
	@param orientation : its orientation
	@param room : how far the extension must go at least; 0 for nothing more
	@param extended : the extension extendTerminal() gives
	@return @p extended, or the first point of the grid at least @p room
	beyond @p terminal when that is farther
*/
QPointF Conductor::extendTerminalBeyond(const QPointF &terminal, Qet::Orientation orientation,
										qreal room, const QPointF &extended)
{
	if (room <= 0) {
		return extended;
	}
	switch (orientation) {
		case Qet::North:
			return QPointF(terminal.x(), qMin(extended.y(), std::floor((terminal.y() - room) / 10) * 10));
		case Qet::East:
			return QPointF(qMax(extended.x(), std::ceil((terminal.x() + room) / 10) * 10), terminal.y());
		case Qet::South:
			return QPointF(terminal.x(), qMax(extended.y(), std::ceil((terminal.y() + room) / 10) * 10));
		case Qet::West:
			return QPointF(qMin(extended.x(), std::floor((terminal.x() - room) / 10) * 10), terminal.y());
	}
	return extended;
}

/**
	@brief Conductor::facesOnOneLine
	@return true when the terminals at @p p1 and @p p2 face each other on
	one line, the second ahead of the first: a straight path joins them
*/
bool Conductor::facesOnOneLine(const QPointF &p1, Qet::Orientation o1,
							   const QPointF &p2, Qet::Orientation o2)
{
	const bool same_y = qAbs(p1.y() - p2.y()) < 0.5;
	const bool same_x = qAbs(p1.x() - p2.x()) < 0.5;
	if (o1 == Qet::East && o2 == Qet::West)   return same_y && p2.x() > p1.x();
	if (o1 == Qet::West && o2 == Qet::East)   return same_y && p2.x() < p1.x();
	if (o1 == Qet::South && o2 == Qet::North) return same_x && p2.y() > p1.y();
	if (o1 == Qet::North && o2 == Qet::South) return same_x && p2.y() < p1.y();
	return false;
}

/**
	@brief Conductor::exitRoom
	See the declaration
*/
qreal Conductor::exitRoom() const
{
	return m_properties.m_medium == TerminalData::Pipe ? PipeConnection::pipeExit : 0;
}

/**
	Dessine le conducteur sans antialiasing.
	@param painter Le QPainter a utiliser pour dessiner le conducteur
	@param options Les options de style pour le conducteur
	@param qw Le QWidget sur lequel on dessine
*/
void Conductor::paint(QPainter *painter, const QStyleOptionGraphicsItem *options, QWidget *qw)
{
		//Nothing to draw between two terminals on the same point: the
		//path would be the two terminals' stubs, drawn over their symbols
	if (endsTouch())
		return;
	painter -> save();
	painter -> setRenderHint(QPainter::Antialiasing, false);

		// Set the color of conductor
		//In the dark mode a red pick and a blue highlight would look like
		//the red and light blue conductors around them
	const QET::Palette::SheetInk sheet_ink = PaletteGraphicsView::paintingInk(qw);
	PotentialType potential_type;
	const bool typed = potentialTypeFormat(&potential_type);
	QColor final_conductor_color;
		//False when a highlight, alert or selection colour replaces it
	bool own_colour = false;
	if (m_preview) {
		final_conductor_color = QColor(160, 160, 160);
	} else if (must_highlight_ == Normal) {
		final_conductor_color = QET::Palette::highlightColor(sheet_ink);
	} else if (must_highlight_ == Alert) {
		final_conductor_color =QColor::fromRgb(255, 69, 0, 255);
	} else if (isSelected()) {
		final_conductor_color = QET::Palette::pickedColor(sheet_ink);
	} else {
		own_colour = true;
		final_conductor_color = plainColor();
	}

		//Draw the conductor bigger when is hovered
		//(lineWidth(): a wire's own width, or for a pipe its kind of line)
	const qreal line_width = drawnWidth(typed ? &potential_type : nullptr);
	const qreal detail = options ? options->levelOfDetailFromTransform(painter->worldTransform()) : 1.0;

		//Set the QPen and QBrush to the QPainter
	painter -> setBrush(conductor_brush);
	QPen final_conductor_pen = linePen(final_conductor_color,
									   m_mouse_over ? line_width + 4 : line_width,
									   detail, typed ? &potential_type : nullptr);
		//A signal line: its type of signal says its mark
	const SignalMark::Mark signal_mark = signalMark();

	painter -> setPen(final_conductor_pen);

		//A pipe begins at its nozzle, at the flange it brings or at the mark
		//of its nozzle (pipeStartAt()): what lies before is not drawn, the
		//square end of its pen behind the nozzle included
	const QPainterPath trimmed = m_properties.m_medium == TerminalData::Pipe
			? trimmedEnds(final_conductor_pen.widthF() / 2.0 + 1.0) : QPainterPath();
	if (!trimmed.isEmpty()) {
		QPainterPath kept;
		kept.addRect(boundingRect().adjusted(-20, -20, 20, 20));
		painter->save();
		painter->setClipPath(kept.subtracted(trimmed), Qt::IntersectClip);
	}
		//Draw the conductor, with a hop at each crossing if the project asks for them
	const QPainterPath painted_path = paintedPath();
		//A white wire is invisible on a white folio: draw a black line,
		//one unit wider on each side, under it. Only for the wire's own
		//colour, not a highlight or selection colour; only for a wire,
		//a pipe or a signal line keeps its own drawing
	const bool outlined = m_properties.m_medium == TerminalData::Electrical
			&& outlinesLightWires() && own_colour
			&& final_conductor_color.lightnessF() > light_wire_lightness;
	if (outlined)
	{
		QPen outline_pen = final_conductor_pen;
		outline_pen.setColor(Qt::black);
		const qreal wire_width = final_conductor_pen.widthF();
		outline_pen.setWidthF(wire_width + 2);
			//A dash pattern is in pen widths: keep the dashes where the
			//wire's own are, so a dashed wire stays dashed
		if (outline_pen.style() != Qt::SolidLine && wire_width > 0)
		{
			QVector<qreal> pattern = final_conductor_pen.dashPattern();
			for (qreal &length : pattern)
				length *= wire_width / (wire_width + 2);
			outline_pen.setDashPattern(pattern);
		}
		painter->save();
		painter->setPen(outline_pen);
		painter->drawPath(painted_path);
		painter->restore();
		painter->setPen(final_conductor_pen);
	}
	if (m_properties.m_medium == TerminalData::Pipe) {
		drawOffSeries(painter, painted_path, line_width, detail);
	}
	if (SignalMark::lineGap(signal_mark) > 0) {
			//A mark which stands in the line: the line stops either side
		painter -> drawPath(brokenSignalLine(signal_mark));
	} else {
		painter -> drawPath(painted_path);
	}
		//A dashed line crosses on a dash, never on a blank (ISO 128-20):
		//a dash each side of where it gives way, a dash over a crossing
		//where it goes on. With the crossings of the P&ID (7c) only, so
		//that the hops of upstream are drawn as they were
	drawDashesAtCrossings(painter, final_conductor_pen);
		//On to the middle of a spec break between two pipes, with the same
		//pen: its own width, colour and kind of line on each side
	if (m_properties.m_medium == TerminalData::Pipe) {
		for (const Terminal *end : {terminal1, terminal2}) {
			const QLineF into = lineIntoSpecBreak(end);
			if (!into.isNull()) {
				painter->drawLine(into);
			}
		}
	}
		//Draw the second color
	if(m_properties.m_bicolor && !typed)
	{
		final_conductor_pen.setColor(m_properties.m_color_2);
		final_conductor_pen.setStyle(Qt::CustomDashLine);
		QVector<qreal> dash_pattern;
		dash_pattern << m_properties.m_dash_size << m_properties.m_dash_size *2;
		final_conductor_pen.setDashPattern(dash_pattern);
		painter->save();
		painter->setPen(final_conductor_pen);
		painter->drawPath(painted_path);
		painter->restore();
	}
	if (!trimmed.isEmpty()) {
		painter->restore();
	}

	if (m_properties.type == ConductorProperties::Single) {
		painter -> setBrush(final_conductor_color);
		m_properties.singleLineProperties.draw(
			painter,
			middleSegment() -> isHorizontal() ? QET::Horizontal : QET::Vertical,
			QRectF(middleSegment() -> middle() - QPointF(12.0, 12.0), QSizeF(24.0, 24.0))
		);
		if (isSelected()) painter -> setBrush(Qt::NoBrush);
	}

		//Draw the junctions
	QList<QPointF> junctions_list = junctions();
	if (!junctions_list.isEmpty()) {
		final_conductor_pen.setStyle(Qt::SolidLine);
		if (outlined) final_conductor_pen.setColor(Qt::black);
		QBrush junction_brush(final_conductor_color, Qt::SolidPattern);
		painter -> setPen(final_conductor_pen);
		painter -> setBrush(junction_brush);
		painter -> setRenderHint(QPainter::Antialiasing, true);
			// The junction dot has to read as a dot on top of the conductor
			// that carries it, so it scales with the conductor width instead
			// of being a fixed 3.0 across: on a wide conductor a 3.0 dot is
			// narrower than the line and simply disappears (bugtracker #108).
			//
			// Floored at the historic 3.0 so nothing changes for the default
			// width of 1.0 or anything thinner -- only the wide conductors
			// the report is about are affected. The line width is used
			// rather than the pen, whose width is inflated by 4 while the
			// mouse is over the conductor.
		const qreal junction_diameter = qMax(3.0, 3.0 * line_width);
		const qreal junction_radius = junction_diameter / 2.0;
		foreach(QPointF point, junctions_list) {
			painter -> drawEllipse(QRectF(point.x() - junction_radius,
						      point.y() - junction_radius,
						      junction_diameter,
						      junction_diameter));
		}
	}

	if (m_properties.m_medium == TerminalData::Pipe) {
		drawPipeParts(painter, final_conductor_color, detail);
			//Its stubs and its mark keep their own colour as long as the
			//pipe keeps its own (step 7d)
		const bool plain = !m_preview && must_highlight_ == None && !isSelected();
			//The jacket inside, the insulation around it (step 8r-1)
		drawJacket(painter, final_conductor_color, plain, detail);
		drawInsulation(painter, final_conductor_color, plain, detail);
		drawDiameterGaps(painter);
			//The symbols whose direction of flow disagrees, drawn again when
			//this pipe changes it (8a-bis-2)
		if (diagram() && diagram()->drawTerminals()) {
			PidFlow::refreshMarks(diagram());
		}
	}
	drawSignalMarks(painter, final_conductor_color, detail);
		//Over a symbol it is not docked to there: in orange, on top (7c-bis)
	drawTraversals(painter, line_width, detail);

	painter -> restore();
}

/**
	@brief Conductor::plainColor
	See the declaration
*/
QColor Conductor::plainColor() const
{
	if (const Diagram *parent_diagram = diagram()) {
		if (!parent_diagram -> drawColoredConductors()) {
			return Qt::black;
		}
	}
	return drawnColor();
}

/**
	@brief Conductor::linePen
	See the declaration
*/
QPen Conductor::linePen(const QColor &color, qreal width, qreal detail,
						const PotentialType *type) const
{
	QPen final_conductor_pen = conductor_pen;
	final_conductor_pen.setWidthF(width);

		//Set the conductor style. Qt's own dashes (4 on, 2 off) leave
		//gaps too short to tell a dashed or a dash-dotted conductor from
		//a solid one at a normal zoom, so these two get longer gaps
	final_conductor_pen.setColor(color);
		//(lineStyle(): a wire's own style, or for a pipe its kind of line,
		//which also never takes a pattern of its own)
	const Qt::PenStyle line_style = type ? type->style : m_properties.lineStyle();
		//The type says how the line looks, so the conductor's own pattern
		//stays stored but is not applied
	bool custom_pattern = !type && m_properties.usesDashPattern()
			&& m_properties.m_dash_pattern.apply(final_conductor_pen, line_style);
		//A signal line: its type of signal says its mark, which may be dashes
	if (signalMark() == SignalMark::Dashes) {
		SignalMark::applyDashes(final_conductor_pen);
		custom_pattern = true;
	}
		//A pipe: the dashes its project gives its kind of line (step 8r-6)
	if (!custom_pattern && !type && m_properties.m_medium == TerminalData::Pipe)
	{
		const QList<qreal> dashes = pipeDashes();
		if (dashes.isEmpty())
			final_conductor_pen.setStyle(Qt::SolidLine);
		else
			final_conductor_pen.setDashPattern(dashes);
	}
	else if (!custom_pattern)
	{
		if (line_style == Qt::DashLine)
			final_conductor_pen.setDashPattern({5, 4});
		else if (line_style == Qt::DashDotLine)
			final_conductor_pen.setDashPattern({6, 4, 1, 4});
		else
			final_conductor_pen.setStyle(line_style);
	}
	final_conductor_pen.setJoinStyle(Qt::SvgMiterJoin); // better rendering with dot

		//Use a cosmetic line, below a certain zoom. A custom pattern is
		//in drawing units, which a cosmetic pen cannot follow
	if (custom_pattern) {
		return final_conductor_pen;
	}
	return zoomedPen(final_conductor_pen, detail, m_properties.m_medium == TerminalData::Pipe);
}

/**
	@brief Conductor::plainPen
	See the declaration
*/
QPen Conductor::plainPen(qreal detail) const
{
	PotentialType potential_type;
	const PotentialType *type = potentialTypeFormat(&potential_type) ? &potential_type : nullptr;
	return linePen(plainColor(), drawnWidth(type), detail, type);
}

/**
	@brief Conductor::zoomedPen
	See the declaration
*/
QPen Conductor::zoomedPen(QPen pen, qreal detail, bool pipe)
{
	if (detail >= 0.5) {
		return pen;
	}
		//A cosmetic pen keeps its width in pixels: fine for a wire of one
		//unit, but a pipe several units wide would swell as the zoom goes
		//down. It follows the zoom, down to one pixel.
	if (!pipe) {
		pen.setCosmetic(true);
	} else if (pen.widthF() * detail < 1.0) {
		pen.setCosmetic(true);
		pen.setWidthF(1.0);
	}
	return pen;
}

/**
	@brief Conductor::signalMark
	See the declaration
*/
SignalMark::Mark Conductor::signalMark() const
{
	if (m_properties.m_medium != TerminalData::Signal) {
		return SignalMark::None;
	}
		//The project says which mark each type of signal takes; it knows
		//the default ones
	const Diagram *parent_diagram = diagram();
	const QETProject *project = parent_diagram ? parent_diagram->project() : nullptr;
	return project ? project->pidCodification().signalMark(m_properties.m_signal.signal_type)
				   : SignalMark::defaultMark(m_properties.m_signal.signal_type);
}

/**
	@brief Conductor::drawSignalMarks
	See the declaration. Part of the drawing: on paper too. The marks keep
	the width of the line when it is hovered, and their place whichever way
	the line was drawn.
	@param painter
	@param color : the colour the line is drawn in
	@param detail : the level of detail of the drawing
*/
void Conductor::drawSignalMarks(QPainter *painter, const QColor &color, qreal detail) const
{
	const SignalMark::Mark mark = signalMark();
	if (!SignalMark::isRepeated(mark)) {
		return;
	}
		//Kept clear of the crossings when the project draws them (7c)
	paintedPath();
	const QPainterPath marks = SignalMark::marksPath(mark, segmentsToPoints(), m_hops_crossings);
	if (marks.isEmpty()) {
		return;
	}
	const bool on_screen = painter->device()
			&& painter->device()->devType() == QInternal::Widget;
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	QPen pen(color, m_properties.lineWidth(), Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
	if (on_screen && detail < 1.0) {
		pen.setCosmetic(true);
	}
	painter->setPen(pen);
	painter->setBrush(Qt::NoBrush);
	painter->drawPath(marks);
	painter->restore();
}

namespace {
		/// The direction a pipe leaves @p terminal in, on the folio
	QPointF outwardOf(const Terminal *terminal)
	{
		switch (terminal->orientation()) {
			case Qet::North: return QPointF(0, -1);
			case Qet::East:  return QPointF(1, 0);
			case Qet::South: return QPointF(0, 1);
			case Qet::West:  return QPointF(-1, 0);
		}
		return QPointF(1, 0);
	}
}

/**
	@brief Conductor::exitsTooShort
	See the declaration
*/
bool Conductor::exitsTooShort(const QList<QPointF> &points) const
{
	if (exitRoom() <= 0 || points.size() < 3 || !terminal1 || !terminal2
		|| QLineF(points.constFirst(), points.constLast()).length() < 0.5) {
		return false;
	}
	const qreal room = exitRoom() - 0.5;
		//The straight run from the first point along @p outward, and
		//whether the path turns after it
	auto tooShort = [room](const QList<QPointF> &path, const QPointF &outward) {
		qreal run = 0;
		for (int i = 0 ; i + 1 < path.size() ; ++i) {
			const QPointF step = path.at(i + 1) - path.at(i);
			if (QLineF(QPointF(), step).length() < 0.01) {
				continue;
			}
			const qreal along = step.x() * outward.x() + step.y() * outward.y();
			const qreal across = step.x() * outward.y() - step.y() * outward.x();
			if (qAbs(across) > 0.01 || along < 0) {
				return run < room;
			}
			run += along;
		}
			//Straight to the other end: its whole length
		return false;
	};
	QList<QPointF> reversed(points.crbegin(), points.crend());
	return tooShort(points, outwardOf(terminal1)) || tooShort(reversed, outwardOf(terminal2));
}

/**
	@brief Conductor::foldsBack
	See the declaration
*/
bool Conductor::foldsBack(const QList<QPointF> &points) const
{
	if (m_properties.m_medium == TerminalData::Electrical) {
		return false;
	}
	QPointF previous;
	bool has_previous = false;
	for (int i = 0 ; i + 1 < points.size() ; ++i) {
		const QPointF step = points.at(i + 1) - points.at(i);
			//A segment of no length lies between the runs it joins
		if (QLineF(QPointF(), step).length() < 0.01) {
			continue;
		}
		if (has_previous) {
			const qreal across = previous.x() * step.y() - previous.y() * step.x();
			const qreal along = previous.x() * step.x() + previous.y() * step.y();
			if (qAbs(across) < 0.01 && along < 0) {
				return true;
			}
		}
		previous = step;
		has_previous = true;
	}
	return false;
}

/**
	@brief Conductor::stoppedAtExits
	See the declaration
*/
QPointF Conductor::stoppedAtExits(const ConductorSegment *moved, const QPointF &asked,
								  const Terminal **stop) const
{
	if (stop) {
		*stop = nullptr;
	}
	QPointF movement = asked;
	if (exitRoom() <= 0 || !moved || moved->isStatic() || !terminal1 || !terminal2) {
		return movement;
	}
		//The straight run of a nozzle, from its dock point to the end of
		//the moved segment: the movement along it stops where it would be
		//shorter than exitRoom(), or than it is. The run may be made of
		//several segments on one line (a bend added by hand, as before
		//this step, goes back over it)
	auto stopAt = [&](bool first) {
		const Terminal *terminal = first ? terminal1 : terminal2;
		const QPointF outward = outwardOf(terminal);
		const bool along_x = qAbs(outward.x()) > 0.5;
			//The moved segment slides along the run only when it crosses it
		if (along_x ? !moved->isVertical() : !moved->isHorizontal()) {
			return;
		}
		auto onTheRun = [along_x](const ConductorSegment *segment) {
			return along_x ? segment->isHorizontal() : segment->isVertical();
		};
		const ConductorSegment *end = first ? moved->previousSegment() : moved->nextSegment();
		while (end && !end->isStatic() && onTheRun(end)) {
			end = first ? end->previousSegment() : end->nextSegment();
		}
		if (!end || !end->isStatic() || !onTheRun(end)) {
			return;
		}
		const QPointF dock = first ? end->firstPoint() : end->secondPoint();
		const QPointF touch = first ? moved->firstPoint() : moved->secondPoint();
		const qreal run = along_x ? (touch.x() - dock.x()) * outward.x()
								  : (touch.y() - dock.y()) * outward.y();
		const qreal room = qMin(exitRoom(), run);
		qreal &asked_along = along_x ? movement.rx() : movement.ry();
		const qreal sign = along_x ? outward.x() : outward.y();
		if (run + asked_along * sign < room - 0.01) {
			asked_along = (room - run) * sign;
			if (stop) {
				*stop = terminal;
			}
		}
	};
	stopAt(true);
	stopAt(false);
	return movement;
}

/**
	@brief Conductor::showExitStop
	See the declaration
*/
void Conductor::showExitStop(const Terminal *terminal)
{
	if (!terminal && !m_exit_stop_shown) {
		return;
	}
	Diagram *folio = diagram();
	QETDiagramEditor *editor = folio && !folio->views().isEmpty()
			? QETApp::diagramEditorAncestorOf(folio->views().constFirst()) : nullptr;
	m_exit_stop_shown = terminal != nullptr;
	if (!editor) {
		return;
	}
	if (!terminal) {
		editor->statusBar()->clearMessage();
		return;
	}
		//4 units to the millimetre, written in the language of the application
	const QString length = QLocale(QETApp::langFromSetting()).toString(exitRoom() / 4.0);
	editor->statusBar()->showMessage(terminal->name().isEmpty()
			? tr("Stop: straight run of %1 mm out of the nozzle").arg(length)
			: tr("Stop: straight run of %1 mm out of nozzle %2").arg(length, terminal->name()));
}

/**
	@brief Conductor::drawsPipePart
	See the declaration
*/
bool Conductor::drawsPipePart(const Terminal *terminal) const
{
	if (m_properties.m_medium != TerminalData::Pipe || !terminal
		|| (terminal != terminal1 && terminal != terminal2) || endsTouch()) {
		return false;
	}
	PipeConnection::Type type;
	qreal at = 0, shift = 0;
		//Never lost: the route leaves room for it (exitRoom())
	return metConnection(terminal, &type, &at, &shift) && PipeConnection::bringsPart(type);
}

/**
	@brief Conductor::pipeStartAt
	See the declaration
*/
qreal Conductor::pipeStartAt(const Terminal *terminal) const
{
	PipeConnection::Type type;
	qreal at = 0, shift = 0;
	if (m_properties.m_medium != TerminalData::Pipe || !terminal
		|| (terminal != terminal1 && terminal != terminal2) || endsTouch()
		|| !metConnection(terminal, &type, &at, &shift)) {
		return 0;
	}
	return PipeConnection::pipeStart(type, at, drawsPipePart(terminal)) + shift;
}

/**
	@brief Conductor::metConnection
	See the declaration
*/
bool Conductor::metConnection(const Terminal *terminal, PipeConnection::Type *type,
							  qreal *at, qreal *shift) const
{
	if (!terminal) {
		return false;
	}
	if (const Terminal *across = terminal->acrossSpecBreak())
	{
		if (!across->drawsConnectionMark()) {
			return false;
		}
		*type = across->connection();
		*at = 0;
			//The counter-flange on the nozzle of the mark, the same space
			//either side of its line (sketch of 15/11/2026)
		*shift = *type == PipeConnection::Flange ? -PipeConnection::pipeFlangeDistance : 0;
		return true;
	}
	if (!terminal->drawsConnectionMark()) {
		return false;
	}
	*type = terminal->connection();
	*at = terminal->markDistance();
	*shift = 0;
	return true;
}

/**
	@brief Conductor::lineIntoSpecBreak
	See the declaration
*/
QLineF Conductor::lineIntoSpecBreak(const Terminal *terminal) const
{
	if (m_properties.m_medium != TerminalData::Pipe || !terminal
		|| (terminal != terminal1 && terminal != terminal2) || endsTouch()) {
		return QLineF();
	}
	const Element *mark = terminal->parentElement();
	if (!mark || !mark->isLineMark()) {
		return QLineF();
	}
	const QList<Terminal *> siblings = terminal->runSiblings();
	if (siblings.size() != 1) {
		return QLineF();
	}
	const Terminal *other = siblings.first();
	bool piped = false;
	for (const Conductor *pipe : other->conductors()) {
		piped = piped || (pipe->medium() == TerminalData::Pipe && !pipe->endsTouch());
	}
	if (!piped) {
		return QLineF();
	}
	return QLineF(mapFromScene(terminal->dockConductor()),
				  mapFromScene((terminal->dockConductor() + other->dockConductor()) / 2));
}

/**
	@brief Conductor::trimmedEnds
	@param half_width : half the width of the pen the pipe is drawn with
	@return what of this pipe is not drawn: at each end, the square end of
	its pen behind the nozzle, half its width into the symbol, and the line
	from the nozzle to where the pipe begins (pipeStartAt()). A pipe begins
	at its nozzle, no sooner. The square end is kept where the line goes on
	with the same pen: into a spec break between two pipes
	(lineIntoSpecBreak()), in a junction (Element::drawJunction()); and for
	a path which does not leave its nozzle straight, which would be notched.
	An empty path for any other medium.
*/
QPainterPath Conductor::trimmedEnds(qreal half_width) const
{
	QPainterPath trimmed;
	if (m_properties.m_medium != TerminalData::Pipe || endsTouch()) {
		return trimmed;
	}
	const QList<QPointF> points = segmentsToPoints();
		//The first step of the path from @p terminal goes the way the nozzle
		//faces
	auto leaves_straight = [this, &points](const Terminal *terminal, const QPointF &outward) {
		const bool from_first = terminal == terminal1;
		for (int i = 0 ; i + 1 < points.size() ; ++i) {
			const QPointF step = from_first
					? points.at(i + 1) - points.at(i)
					: points.at(points.size() - 2 - i) - points.at(points.size() - 1 - i);
			if (QLineF(QPointF(), step).length() < 0.01) {
				continue;
			}
			const qreal along = step.x() * outward.x() + step.y() * outward.y();
			const qreal aside = step.x() * outward.y() - step.y() * outward.x();
			return along > 0 && qAbs(aside) < 0.01;
		}
		return false;
	};
	for (const Terminal *terminal : {terminal1, terminal2})
	{
		if (!terminal) {
			continue;
		}
		const QPointF outward = outwardOf(terminal);
		qreal start = pipeStartAt(terminal);
		if (start <= 0) {
			const Element *symbol = terminal->parentElement();
			if (!lineIntoSpecBreak(terminal).isNull() || (symbol && symbol->isJunction())
				|| !leaves_straight(terminal, outward)) {
				continue;
			}
			start = 0;
		}
		const QPointF across(-outward.y(), outward.x());
		const QPointF dock = mapFromScene(terminal->dockConductor());
			//From behind the nozzle, where the square end of a thick pipe
			//would show, to its start
		const QPointF from = dock - half_width * outward;
		const QPointF to = dock + start * outward;
		trimmed.addPolygon(QPolygonF({from + half_width * across, to + half_width * across,
									  to - half_width * across, from - half_width * across}));
		trimmed.closeSubpath();
	}
	return trimmed;
}

/**
	@brief Conductor::drawPipeParts
	Draw the part this pipe brings to each nozzle it arrives on
	(drawsPipePart()): its flange, its socket, its grooved end, its half of a
	quick coupling, in the colour of the pipe: a new pipe brings a new part.
	Part of the drawing: on paper too.
	@param painter
	@param color : the colour the pipe is drawn in
	@param detail : the level of detail of the drawing
*/
void Conductor::drawPipeParts(QPainter *painter, const QColor &color, qreal detail) const
{
	const bool on_screen = painter->device()
			&& painter->device()->devType() == QInternal::Widget;
	for (const Terminal *terminal : {terminal1, terminal2})
	{
		PipeConnection::Type type;
		qreal at = 0, shift = 0;
		if (!drawsPipePart(terminal) || !metConnection(terminal, &type, &at, &shift)) {
			continue;
		}
		QPainterPath strokes;
		PipeConnection::pipePartShapes(type, at, &strokes);
			//The frame of the mark of the nozzle, turned and mirrored as its
			//element: the two halves of a quick coupling face each other
		const QTransform frame = QTransform::fromTranslate(shift, 0) * terminal->markTransform()
								 * terminal->sceneTransform() * sceneTransform().inverted();
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, true);
		QPen pen(color, 1.0, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
		if (on_screen && detail < 1.0) {
			pen.setCosmetic(true);
		}
		painter->setPen(pen);
		painter->setBrush(Qt::NoBrush);
		painter->drawPath(frame.map(strokes));
		painter->restore();
	}
}

/**
	@brief Conductor::pipesBeyond
	See the declaration
*/
QList<Conductor *> Conductor::pipesBeyond(const Terminal *terminal, bool *limit, bool jacket) const
{
	QList<Conductor *> found;
	if (limit) {
		*limit = false;
	}
	if (!terminal) {
		return found;
	}
	QList<const Terminal *> todo{terminal};
	QList<const Terminal *> seen{terminal};
	for (int i = 0 ; i < todo.size() ; ++i)
	{
		const Terminal *at = todo.at(i);
		const Element *symbol = at->parentElement();
			//The insulation, or the jacket, stops there, on both sides
		if (symbol && (jacket ? symbol->isJacketLimit() : symbol->isInsulationLimit())) {
			if (limit) {
				*limit = true;
			}
			continue;
		}
		QList<Terminal *> next = at->runSiblings();
		if (Terminal *other = at->reducerOtherSide()) {
			if (!next.contains(other)) {
				next << other;
			}
		}
		for (Terminal *sibling : std::as_const(next))
		{
			if (seen.contains(sibling)) {
				continue;
			}
			seen << sibling;
			for (Conductor *pipe : sibling->conductors())
			{
				if (pipe == this || pipe->medium() != TerminalData::Pipe) {
					continue;
				}
					//A pipe of no length joins two symbols: on through it
				if (pipe->endsTouch()) {
					Terminal *far = pipe->terminal1 == sibling ? pipe->terminal2 : pipe->terminal1;
					if (far && !seen.contains(far)) {
						seen << far;
						todo << far;
					}
					continue;
				}
				if (!found.contains(pipe)) {
					found << pipe;
				}
			}
				//Against another symbol: on through the run of that one
			if (Terminal *against = sibling->contactNozzle()) {
				if (!seen.contains(against)) {
					seen << against;
					todo << against;
				}
			}
		}
	}
	return found;
}

/**
	@brief Conductor::insulationEndsAt
	See the declaration
*/
bool Conductor::insulationEndsAt(const Terminal *terminal) const
{
	const PipeProperties &own = m_properties.m_pipe;
	if (m_properties.m_medium != TerminalData::Pipe
		|| own.insulation_kind == PipeProperties::NoInsulation
		|| !terminal || (terminal != terminal1 && terminal != terminal2)) {
		return false;
	}
	bool limit = false;
	const QList<Conductor *> beyond = pipesBeyond(terminal, &limit);
	if (limit) {
		return true;
	}
	for (const Conductor *pipe : beyond) {
		if (pipe->m_properties.m_pipe.insulation_kind == own.insulation_kind) {
			return false;
		}
	}
	return true;
}

namespace {
		/// The length of the insulation stubs of the project of @p folio,
		/// in drawing units (4 by mm)
	qreal insulationStubOf(const Diagram *folio)
	{
		const qreal mm = folio && folio->project()
				? folio->project()->pidCodification().insulation_stub
				: PidCodification::defaultInsulationStub();
		return mm * 4.0;
	}
}

/**
	@brief Conductor::insulationMark
	See the declaration
*/
QList<QPointF> Conductor::insulationMark(QRectF *text_rect) const
{
	if (text_rect) {
		*text_rect = QRectF();
	}
	const PipeProperties &p = m_properties.m_pipe;
	if (m_properties.m_medium != TerminalData::Pipe || !p.insulation_mark
		|| p.insulation_kind == PipeProperties::NoInsulation || endsTouch()) {
		return {};
	}
	const qreal total = PidInsulation::length(segmentsToPoints());
	return insulationMarkBetween(pipeStartAt(terminal1), total - pipeStartAt(terminal2),
								 text_rect, false);
}

/**
	@brief Conductor::insulationMarkBetween
	See the declaration
*/
QList<QPointF> Conductor::insulationMarkBetween(qreal from, qreal to, QRectF *text_rect,
												bool both_sides) const
{
	if (text_rect) {
		*text_rect = QRectF();
	}
	const PipeProperties &p = m_properties.m_pipe;
	if (m_properties.m_medium != TerminalData::Pipe || !p.insulation_mark
		|| p.insulation_kind == PipeProperties::NoInsulation || endsTouch()) {
		return {};
	}
	const QList<QPointF> points = segmentsToPoints();
	const qreal total = PidInsulation::length(points);
		//The longest straight run of the whole path, the first of two as
		//long, then its part drawn
	const QList<QPointF> whole = PidInsulation::stretch(points, 0, total);
	int longest = -1;
	qreal longest_length = 0, longest_start = 0, at = 0;
	for (int i = 0 ; i + 1 < whole.size() ; ++i) {
		const qreal l = QLineF(whole.at(i), whole.at(i + 1)).length();
		if (l > longest_length + 0.01) {
			longest = i;
			longest_length = l;
			longest_start = at;
		}
		at += l;
	}
	if (longest < 0) {
		return {};
	}
	const QList<QPointF> drawn = PidInsulation::stretch(
				whole, qMax(from, longest_start), qMin(to, longest_start + longest_length));
	if (drawn.size() != 2) {
		return {};
	}
	const qreal drawn_length = QLineF(drawn.first(), drawn.last()).length();
		//Room for a hatch at least
	const qreal length = qMin(insulationStubOf(diagram()),
							  drawn_length - 2 * PidInsulation::half_width);
	if (length < 2 * PidInsulation::half_width + 1) {
		return {};
	}
	const QPointF start = drawn.first();
	const QPointF d = (drawn.last() - start) / drawn_length;
	const QPointF normal(d.y(), -d.x());
	const bool horizontal = qAbs(d.x()) >= qAbs(d.y());
	const qreal half = length / 2;
		//Where the mark goes along the segment: in its middle, or past the
		//designation when that lies along it (hand test of 21/11): after
		//its end, else before its start, else in the middle all the same
	const qreal gap = 4.0;
	const qreal middle_at = drawn_length / 2;
	QList<qreal> places{middle_at};
	bool designation_first_side = false;
	if (m_text_item && m_text_item->isVisible()
		&& !m_text_item->toPlainText().trimmed().isEmpty())
	{
		const QRectF designation = mapRectFromItem(m_text_item, m_text_item->boundingRect());
		qreal along_min = 0, along_max = 0, across_min = 0, across_max = 0;
		const QList<QPointF> corners{designation.topLeft(), designation.topRight(),
									 designation.bottomLeft(), designation.bottomRight()};
		for (int i = 0 ; i < corners.size() ; ++i) {
			const QPointF v = corners.at(i) - start;
			const qreal along = v.x() * d.x() + v.y() * d.y();
			const qreal across = v.x() * normal.x() + v.y() * normal.y();
			along_min = i ? qMin(along_min, along) : along;
			along_max = i ? qMax(along_max, along) : along;
			across_min = i ? qMin(across_min, across) : across;
			across_max = i ? qMax(across_max, across) : across;
		}
			//Beside the segment, as QET writes a designation
		const qreal reach = PidInsulation::half_width + 2 * gap;
		const bool beside = across_min < reach && across_max > -reach
				&& along_max > 0 && along_min < drawn_length;
		if (beside)
		{
				//The first side is above a horizontal pipe, at the left of a
				//vertical one: the side of the normal for a step to the
				//east or to the south, the other one otherwise
			const bool normal_first = horizontal ? normal.y() < 0 : normal.x() < 0;
			designation_first_side = ((across_min + across_max) / 2 > 0) == normal_first;
			const bool over = along_min < middle_at + half + gap && along_max > middle_at - half - gap;
				//After the end of the designation as it reads: at its right on
				//a horizontal pipe, above it on a vertical one (its text turned
				//to read upwards), whichever way the pipe was drawn
			const bool reading_along = horizontal ? d.x() > 0 : d.y() < 0;
			const qreal past_max = along_max + gap + half;
			const qreal past_min = along_min - gap - half;
			const qreal after = reading_along ? past_max : past_min;
			const qreal before = reading_along ? past_min : past_max;
			auto fits = [&](qreal at) { return at - half >= 0 && at + half <= drawn_length; };
			if (!both_sides && over) {
				if (fits(after)) {
					places = {after};
				} else if (fits(before)) {
					places = {before};
				}
			}
		}
	}
	const QPointF centre = start + d * places.first();
	const QList<QPointF> mark{centre - d * half, centre + d * half};

	if (text_rect && !p.insulation.trimmed().isEmpty() && m_text_item)
	{
		const QSizeF size = QFontMetricsF(m_text_item->font()).size(0, p.insulation.trimmed());
		const qreal off = (p.jacketed() ? PidInsulation::jacketed_half_width
										: PidInsulation::half_width) + 2.0;
			//Above a horizontal pipe, at the left of a vertical one; on the
			//other side when the designation is on that side
		auto placed = [&](bool first_side, const QPointF &c) {
			if (horizontal) {
				const qreal top = first_side ? c.y() - off - size.height() : c.y() + off;
				return QRectF(QPointF(c.x() - size.width() / 2, top), size);
			}
			const qreal left = first_side ? c.x() - off - size.width() : c.x() + off;
			return QRectF(QPointF(left, c.y() - size.height() / 2), size);
		};
		QRectF rect;
		if (both_sides) {
				//Anywhere along the segment, on both sides: the bounds do not
				//follow the designation, which moves without telling the
				//folio that the bounds of this pipe change
			for (const QPointF &c : {start, drawn.last()}) {
				rect = rect.united(placed(true, c)).united(placed(false, c));
			}
		} else {
			rect = placed(!designation_first_side, centre);
		}
		*text_rect = rect;
	}
	return mark;
}

/**
	@brief Conductor::insulationPaths
	See the declaration
*/
void Conductor::insulationPaths(QPainterPath *hatches, QPainterPath *dashes) const
{
	*hatches = QPainterPath();
	*dashes = QPainterPath();
	const PipeProperties &p = m_properties.m_pipe;
	if (m_properties.m_medium != TerminalData::Pipe
		|| p.insulation_kind == PipeProperties::NoInsulation || endsTouch()) {
		return;
	}
	const QList<QPointF> points = segmentsToPoints();
	const qreal total = PidInsulation::length(points);
	const qreal from = pipeStartAt(terminal1);
	const qreal to = total - pipeStartAt(terminal2);
	const qreal room = to - from;
	if (room < 1) {
		return;
	}
	const bool at_first = insulationEndsAt(terminal1);
	const bool at_second = insulationEndsAt(terminal2);
		//On a short pipe, each stub on its half
	const qreal stub = qMin(insulationStubOf(diagram()), at_first && at_second ? room / 2 : room);
		//Into a line mark across the line (an insulation limit, a spec
		//break), the pipe is drawn to its middle: the stub starts there,
		//where its stroke is (step 7d-bis), its length the same. Not at a
		//flow arrow, which the stub would hide: from its nozzle (choice of
		//the user, 22/11)
	auto stub_at = [this, &points, stub](const Terminal *terminal, qreal start, qreal end) {
		const Element *mark = terminal->parentElement();
		const QLineF into = mark && mark->isFlowArrow() ? QLineF() : lineIntoSpecBreak(terminal);
		const qreal lead = into.isNull() ? 0 : into.length();
		QList<QPointF> part = terminal == terminal1
				? PidInsulation::stretch(points, start, start + qMax(stub - lead, 0.0))
				: PidInsulation::stretch(points, end - qMax(stub - lead, 0.0), end);
		if (lead > 0 && !part.isEmpty()) {
			if (terminal == terminal1) {
				part.prepend(into.p2());
			} else {
				part.append(into.p2());
			}
		}
		return part;
	};
	QList<QList<QPointF>> parts;
	if (at_first) {
		parts << stub_at(terminal1, from, to);
	}
	if (at_second) {
		parts << stub_at(terminal2, from, to);
	}
	const QList<QPointF> mark = insulationMark();
	if (!mark.isEmpty()) {
		parts << mark;
	}
		//Around the jacket of a double containment pipe (step 8r-1)
	const bool jacketed = p.jacketed();
	for (const QList<QPointF> &part : std::as_const(parts)) {
		if (p.insulated()) {
			hatches->addPath(PidInsulation::band(part, jacketed));
		}
		if (p.traced()) {
			dashes->addPath(PidInsulation::tracing(part, p.insulated(), jacketed));
		}
	}
}

/**
	@brief Conductor::drawInsulation
	Draw the insulation of this pipe (insulationPaths()), and the text of
	its mark in the middle (insulationMark()), as its designation is
	written. Part of the drawing: on paper too.
	@param painter
	@param color : the colour the pipe is drawn in
	@param plain : the pipe is drawn in its own colour, neither selected
	nor highlighted: the stubs take the colour the project gives them, when
	it gives one and draws in colour
	@param detail : the level of detail of the drawing
*/
void Conductor::drawInsulation(QPainter *painter, const QColor &color, bool plain, qreal detail) const
{
	QPainterPath hatches, dashes;
	insulationPaths(&hatches, &dashes);
	if (hatches.isEmpty() && dashes.isEmpty()) {
		return;
	}
	const Diagram *folio = diagram();
	const bool colored = plain && folio && folio->project() && folio->drawColoredConductors();
	auto inkOf = [&](const QString &key) {
		if (colored) {
			const QColor set = folio->project()->pidCodification().object_colors.value(key);
			if (set.isValid()) {
				return set;
			}
		}
		return color;
	};
	const bool on_screen = painter->device()
			&& painter->device()->devType() == QInternal::Widget;
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setBrush(Qt::NoBrush);
	QPen pen(inkOf(PidCodification::insulationColorKey()), PidInsulation::line_width,
			 Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
	if (on_screen && detail < 1.0) {
		pen.setCosmetic(true);
	}
	if (!hatches.isEmpty()) {
		painter->setPen(pen);
		painter->drawPath(hatches);
	}
	if (!dashes.isEmpty()) {
		pen.setColor(inkOf(PidCodification::tracingColorKey()));
		pen.setDashPattern({PidInsulation::dash / PidInsulation::line_width,
							PidInsulation::dash_gap / PidInsulation::line_width});
		painter->setPen(pen);
		painter->drawPath(dashes);
	}
	QRectF text_rect;
	insulationMark(&text_rect);
	if (!text_rect.isNull() && m_text_item) {
		painter->setFont(m_text_item->font());
		painter->setPen(m_text_item->color());
		painter->drawText(text_rect, Qt::AlignCenter, m_properties.m_pipe.insulation.trimmed());
	}
	painter->restore();
}

/**
	@brief Conductor::jacketEndsAt
	See the declaration
*/
bool Conductor::jacketEndsAt(const Terminal *terminal) const
{
	const PipeProperties &own = m_properties.m_pipe;
	if (m_properties.m_medium != TerminalData::Pipe || !own.jacketed()
		|| !terminal || (terminal != terminal1 && terminal != terminal2)) {
		return false;
	}
	bool limit = false;
	const QList<Conductor *> beyond = pipesBeyond(terminal, &limit, true);
	if (limit) {
		return true;
	}
	for (const Conductor *pipe : beyond) {
		if (pipe->m_properties.m_pipe.sameJacket(own)) {
			return false;
		}
	}
	return true;
}

/**
	@brief Conductor::jacketPath
	See the declaration
*/
QPainterPath Conductor::jacketPath() const
{
	const PipeProperties &p = m_properties.m_pipe;
	if (m_properties.m_medium != TerminalData::Pipe || !p.jacketed() || endsTouch()) {
		return {};
	}
	const QList<QPointF> points = segmentsToPoints();
	const qreal total = PidInsulation::length(points);
	const qreal from = pipeStartAt(terminal1);
	const qreal to = total - pipeStartAt(terminal2);
	const qreal room = to - from;
	if (room < 1) {
		return {};
	}
		//Into a line mark across the line (a jacket limit, a spec break),
		//the pipe is drawn to its middle: the jacket goes there too, as the
		//stubs of an insulation (step 7d-bis); not into a flow arrow
	auto lead_into = [this](const Terminal *terminal) {
		const Element *mark = terminal->parentElement();
		return mark && mark->isFlowArrow() ? QLineF() : lineIntoSpecBreak(terminal);
	};
	auto with_leads = [&](QList<QPointF> part, bool at_first, bool at_second) {
		if (part.isEmpty()) {
			return part;
		}
		const QLineF first = at_first ? lead_into(terminal1) : QLineF();
		const QLineF second = at_second ? lead_into(terminal2) : QLineF();
		if (!first.isNull()) {
			part.prepend(first.p2());
		}
		if (!second.isNull()) {
			part.append(second.p2());
		}
		return part;
	};
	QPainterPath path;
	if (p.jacket_full) {
		path.addPath(PidInsulation::jacket(
						 with_leads(PidInsulation::stretch(points, from, to), true, true)));
		return path;
	}
	const bool at_first = jacketEndsAt(terminal1);
	const bool at_second = jacketEndsAt(terminal2);
		//On a short pipe, each stub on its half
	const qreal stub = qMin(insulationStubOf(diagram()), at_first && at_second ? room / 2 : room);
	if (at_first) {
		const qreal lead = lead_into(terminal1).length();
		path.addPath(PidInsulation::jacket(with_leads(
			PidInsulation::stretch(points, from, from + qMax(stub - lead, 0.0)), true, false)));
	}
	if (at_second) {
		const qreal lead = lead_into(terminal2).length();
		path.addPath(PidInsulation::jacket(with_leads(
			PidInsulation::stretch(points, to - qMax(stub - lead, 0.0), to), false, true)));
	}
	return path;
}

/**
	@brief Conductor::drawJacket
	See the declaration. Part of the drawing: on paper too.
*/
void Conductor::drawJacket(QPainter *painter, const QColor &color, bool plain, qreal detail) const
{
	const QPainterPath path = jacketPath();
	if (path.isEmpty()) {
		return;
	}
		//The colour of the jackets, unless the status of works of the pipe
		//gives one, which comes first
	QColor ink = color;
	const Diagram *folio = diagram();
	if (plain && folio && folio->project() && folio->drawColoredConductors()) {
		const PidCodification &codes = folio->project()->pidCodification();
		const QColor set = codes.object_colors.value(PidCodification::jacketColorKey());
		if (set.isValid() && !codes.status_colors.value(m_properties.m_pipe.status).isValid()) {
			ink = set;
		}
	}
		//The pattern of the kind of line, half its width: its dashes as
		//long as those of the pipe
	const qreal width = lineWidth() / 2;
	const bool on_screen = painter->device()
			&& painter->device()->devType() == QInternal::Widget;
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setBrush(Qt::NoBrush);
	QPen pen(ink, width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
	const QList<qreal> dashes = pipeDashes();
	if (!dashes.isEmpty()) {
		QList<qreal> doubled;
		for (qreal dash : dashes) {
			doubled << dash * 2;
		}
		pen.setDashPattern(doubled);
	}
	if (on_screen && detail < 1.0) {
		pen.setCosmetic(true);
	}
	painter->setPen(pen);
	painter->drawPath(path);
	painter->restore();
}

/**
	@brief Conductor::drawDiameterGaps
	Mark with an orange diamond each end of this pipe whose nozzle has another
	DN (Terminal::hasDiameterGap()). It says so and blocks nothing: a valve may
	be smaller than its line. Drawn here, not by the element: a pipe goes over
	the elements, and would hide it. Like the terminals, on the screen only.
	@param painter
*/
void Conductor::drawDiameterGaps(QPainter *painter) const
{
	const Diagram *folio = diagram();
	if (!folio || !folio->drawTerminals()) {
		return;
	}
	const QColor orange = QET::Palette::signalColor();
	QPen pen(orange);
	pen.setCosmetic(true);
	painter->setPen(pen);
	painter->setBrush(orange);
	painter->setRenderHint(QPainter::Antialiasing, true);
	for (const Terminal *terminal : {terminal1, terminal2})
	{
		if (terminal && terminal->hasDiameterGap()) {
			const QPointF c = mapFromScene(terminal->dockConductor());
			painter->drawPolygon(QPolygonF({c + QPointF(0, -6), c + QPointF(6, 0),
											c + QPointF(0, 6), c + QPointF(-6, 0)}));
		}
	}
}

/**
	@brief Conductor::offSeriesNote
	See the declaration
*/
QString Conductor::offSeriesNote() const
{
	if (m_properties.m_medium != TerminalData::Pipe) {
		return QString();
	}
	const Diagram *folio = diagram();
	if (!folio || !folio->project()) {
		return QString();
	}
	const PidCodification &codes = folio->project()->pidCodification();
	const PipeProperties &pipe = m_properties.m_pipe;
	const PidCodification::Entry *material = codes.material(pipe.material);
	if (!material || codes.pipeSizes(pipe.material) != PidCodification::SizeRule::Series) {
		return QString();
	}
	const QLocale locale = codes.locale();
	if (pipe.nominal.isSet())
	{
			//DN 65 on a material which has DN 60, said equal, is of its sizes
		const QList<PipeDiameter> line = codes.lineSizes(pipe.material);
		if (!line.contains(pipe.nominal)
			&& !line.contains(codes.nominalIn(pipe.nominal, pipe.material))) {
			return tr("%1 is not a size of the material %2.")
					.arg(pipe.nominal.text(locale), pipe.material);
		}
	}
	if (material->outside_diameter && pipe.outside.isSet()
		&& !codes.acceptsForPipe(pipe.outside, pipe.material)) {
		return tr("%1 is not a size of the material %2.")
				.arg(pipe.outside.text(locale), pipe.material);
	}
	return QString();
}

/**
	@brief Conductor::drawOffSeries
	Mark along its @p path, under the line, a pipe whose size is not one of
	its material (offSeriesNote()): a band of orange, wide enough to be seen
	on a whole folio. It says so and blocks nothing. Like the diamonds of
	drawDiameterGaps(), on the screen only, never printed nor exported.
	@param painter
	@param path : the path the pipe is drawn along
	@param line_width : the width of the line of the pipe
	@param detail : the level of detail of the painter
*/
void Conductor::drawOffSeries(QPainter *painter, const QPainterPath &path,
							  qreal line_width, qreal detail) const
{
	const Diagram *folio = diagram();
	if (!folio || !folio->drawTerminals() || offSeriesNote().isEmpty()) {
		return;
	}
	painter->save();
	QColor band = QET::Palette::signalColor();
	band.setAlpha(110);
	QPen pen(band);
	pen.setWidthF(line_width + 8.0);
		//Never thinner than a few pixels, when the folio is zoomed out
	if (pen.widthF() * detail < 6.0) {
		pen.setCosmetic(true);
		pen.setWidthF(6.0);
	}
	pen.setCapStyle(Qt::FlatCap);
	pen.setJoinStyle(Qt::RoundJoin);
	painter->setPen(pen);
	painter->setBrush(Qt::NoBrush);
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->drawPath(path);
	painter->restore();
}

/**
	@brief Conductor::updateTraversals
	Work out again, when a conductor or a symbol changed since the last
	time, the symbols this pipe or signal line runs over and the parts of
	it which do (PidTraversal::traversals()): every symbol of its folio but
	the drawing aids, those hidden (View > Show) and the T-junctions shown
	as a dot. Nothing for a wire.
*/
void Conductor::updateTraversals() const
{
	const Diagram *folio = diagram();
	if (m_properties.m_medium == TerminalData::Electrical || !folio)
	{
		m_traversed.clear();
		m_traversal_path = QPainterPath();
		m_traversals_folio = nullptr;
		return;
	}
	const quint64 symbols = PidTraversal::symbolsGeneration();
	if (m_traversals_folio == folio
		&& m_traversals_conductors == s_conductor_geometry_generation
		&& m_traversals_symbols == symbols) {
		return;
	}
	m_traversals_folio = folio;
	m_traversals_conductors = s_conductor_geometry_generation;
	m_traversals_symbols = symbols;
	m_traversed.clear();
	m_traversal_path = QPainterPath();
	refreshTraversalCandidates(folio);

	const QList<QPointF> points = segmentsToPoints();
	if (points.size() < 2) {
		return;
	}
	QVector<QPointF> line;
	for (const QPointF &point : points) {
		line.append(mapToScene(point));
	}
	QPainterPath path;
	m_traversed = traversalsAlong(line, &path);
	m_traversal_path = mapFromScene(path);
}

/**
	@brief Conductor::traversalsAlong
	The symbols of the folio (refreshTraversalCandidates(), already read)
	which @p line, points of the scene from terminal1 to terminal2, runs
	over, and in @p path when given, the parts of it which do, in the
	scene: the work of updateTraversals(), for the path of this conductor
	or for the one it would take by itself (step 8r-3 c)
*/
QList<Element *> Conductor::traversalsAlong(const QVector<QPointF> &line, QPainterPath *path) const
{
	QList<Element *> traversed;
	if (line.size() < 2) {
		return traversed;
	}
	const QRectF rect = QPolygonF(line).boundingRect().adjusted(-1, -1, 1, 1);
		//A crossing of a mere line is a point: drawn a little longer
	const qreal min_length = qMax(6.0, 3 * drawnWidth(nullptr));
	for (const TraversalCandidate &candidate : std::as_const(s_traversal_candidates))
	{
		if (!rect.intersects(candidate.outline.rect.adjusted(-1, -1, 1, 1))) {
			continue;
		}
			//A mark on the line is drawn over its own pipes: the points of the
			//arrow of both ways go beyond its nozzles (step 8a-bis-1c). Any
			//other pipe running over it is still said
		if (candidate.element->isLineMark()
			&& ((terminal1 && terminal1->parentElement() == candidate.element)
				|| (terminal2 && terminal2->parentElement() == candidate.element))) {
			continue;
		}
		const QList<PidTraversal::Span> spans = PidTraversal::traversals(line, candidate.outline);
		if (spans.isEmpty()) {
			continue;
		}
		traversed.append(const_cast<Element *>(candidate.element));
		if (path) {
			path->addPath(PidTraversal::pathAlong(line, spans, min_length));
		}
	}
	return traversed;
}

/**
	@brief Conductor::defaultRouteTraversedSymbols
	See the declaration
*/
QList<Element *> Conductor::defaultRouteTraversedSymbols() const
{
	const Diagram *folio = diagram();
	if (m_properties.m_medium == TerminalData::Electrical || !folio
		|| !terminal1 || !terminal2) {
		return {};
	}
	refreshTraversalCandidates(folio);
		//As setProfiles() draws it once its profiles are reset
	const QList<QPointF> points = defaultRoute(
				terminal1->dockConductor(), terminal1->orientation(),
				terminal2->dockConductor(), terminal2->orientation(), exitRoom());
	return traversalsAlong(QVector<QPointF>(points.cbegin(), points.cend()), nullptr);
}

/**
	@brief Conductor::traversedSymbols
	See the declaration
*/
QList<Element *> Conductor::traversedSymbols() const
{
	updateTraversals();
	return m_traversed;
}

/**
	@brief Conductor::traversalNote
	See the declaration
*/
QString Conductor::traversalNote() const
{
	QStringList notes;
	const QList<Element *> symbols = traversedSymbols();
	for (const Element *symbol : symbols)
	{
		const QString label = symbol->actualLabel().trimmed();
		const QString named = label.isEmpty()
				? symbol->name()
				: tr("%1 (%2)", "a symbol a line goes through: its label (its name)")
				  .arg(label, symbol->name());
		notes << (m_properties.m_medium == TerminalData::Pipe
				  ? tr("This pipe goes through the symbol %1.")
				  : tr("This signal line goes through the symbol %1.")).arg(named);
	}
	return notes.join(QLatin1Char('\n'));
}

/**
	@brief Conductor::drawTraversals
	Draw again in orange, over the line, each part of this pipe or signal
	line which runs over a symbol (updateTraversals()): never thinner than
	a few pixels when the folio is zoomed out. It says so and blocks
	nothing. Like the other marks of the P&ID, on the screen only, never
	printed nor exported.
	@param painter
	@param line_width : the width of the line
	@param detail : the level of detail of the painter
*/
void Conductor::drawTraversals(QPainter *painter, qreal line_width, qreal detail) const
{
	const Diagram *folio = diagram();
	if (!folio || !folio->drawTerminals() || m_properties.m_medium == TerminalData::Electrical) {
		return;
	}
	updateTraversals();
	if (m_traversal_path.isEmpty()) {
		return;
	}
	painter->save();
	QPen pen(QET::Palette::signalColor());
	pen.setWidthF(line_width);
	if (pen.widthF() * detail < 3.0) {
		pen.setCosmetic(true);
		pen.setWidthF(3.0);
	}
	pen.setCapStyle(Qt::FlatCap);
	pen.setJoinStyle(Qt::MiterJoin);
	painter->setPen(pen);
	painter->setBrush(Qt::NoBrush);
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->drawPath(m_traversal_path);
	painter->restore();
}

/// @return le Diagram auquel ce conducteur appartient, ou 0 si ce conducteur est independant
Diagram *Conductor::diagram() const
{
	return(qobject_cast<Diagram *>(scene()));
}

/**4
	@return le champ de texte associe a ce conducteur
*/
ConductorTextItem *Conductor::textItem() const
{
	return(m_text_item);
}

/**
	Methode de validation d'element XML
	@param e Un element XML sense represente un Conducteur
	@return true si l'element XML represente bien un Conducteur ; false sinon
*/
bool Conductor::valideXml(QDomElement &e){
	// verifie le nom du tag
	if (e.tagName() != "conductor") return(false);

	// verifie la presence des attributs minimaux
	if (!e.hasAttribute("terminal1")) return(false);
	if (!e.hasAttribute("terminal2")) return(false);

	bool conv_ok;
	// parse l'abscisse
	if (e.hasAttribute("element1")) {
		if (QUuid(e.attribute("element1")).isNull())
			return false;
		if (QUuid(e.attribute("terminal1")).isNull())
			return false;
	} else {
		e.attribute("terminal1").toInt(&conv_ok);
		if (!conv_ok) return(false);
	}

	// parse l'ordonnee
	if (e.hasAttribute("element2")) {
		if (QUuid(e.attribute("element2")).isNull())
			return false;
		if (QUuid(e.attribute("terminal2")).isNull())
			return false;
	} else {
		e.attribute("terminal2").toInt(&conv_ok);
		if (!conv_ok) return(false);
	}
	return(true);
}

/**
	@brief Conductor::mouseDoubleClickEvent
	Manage the mouse double click
	@param event
*/
void Conductor::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
	event->accept();
	editProperty();
}

/**
	@brief Conductor::mousePressEvent
	Manage the mouse press event
	@param event
*/
void Conductor::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	QGraphicsObject::mousePressEvent(event);
	m_last_press.start();
	m_last_press_pos = event->scenePos();

	if (event->modifiers() & Qt::ControlModifier)
		setSelected(!isSelected());
}

/**
	@brief Conductor::mouseReleaseEvent
	@param event
*/
void Conductor::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	if (!(event -> modifiers() & Qt::ControlModifier))
		QGraphicsObject::mouseReleaseEvent(event);
}

/**
	@brief Conductor::hoverEnterEvent
	Manage the hover enter event
	@param event
*/
void Conductor::hoverEnterEvent(QGraphicsSceneHoverEvent *event) {
	Q_UNUSED(event);
		//The click area of a pipe widens (shape()): the folio is told
	if (m_properties.m_medium != TerminalData::Electrical) {
		prepareGeometryChange();
	}
	m_mouse_over = true;
		//What its orange band says (drawOffSeries()), and the symbols it
		//runs over (drawTraversals()); a wire has no tip. First, the
		//direction of flow of a pipe, when it is known or said twice
		//(step 8a-bis)
	if (m_properties.m_medium != TerminalData::Electrical) {
		QStringList notes;
		if (m_properties.m_medium == TerminalData::Pipe
			&& PidFlow::direction(this) != PidFlow::Direction::Unknown) {
			notes << tr("Flow: %1", "direction of flow of a pipe").arg(PidFlow::text(this));
		}
		for (const QString &note : {offSeriesNote(), traversalNote()}) {
			if (!note.isEmpty()) {
				notes << note;
			}
		}
		setToolTip(PidToolTip::wrapped(notes.join(QLatin1Char('\n'))));
	}
	update();
}

/**
	@brief Conductor::hoverLeaveEvent
	Manage the mouse leave event
	@param event
*/
void Conductor::hoverLeaveEvent(QGraphicsSceneHoverEvent *event) {
	Q_UNUSED(event);
	update();
	if (m_properties.m_medium != TerminalData::Electrical) {
		prepareGeometryChange();
	}
	m_mouse_over = false;
}

/**
	@brief Conductor::itemChange
	@param change
	@param value
	@return
*/
QVariant Conductor::itemChange(GraphicsItemChange change, const QVariant &value)
{
	if (change == QGraphicsItem::ItemSelectedChange)
	{
		if (value.toBool())
		{
			m_previous_z_value = zValue();
			setZValue(qAbs(m_previous_z_value) + 10000);
			addHandler();
		}
		else
		{
			setZValue(m_previous_z_value);
			removeHandler();
		}
	}
	else if (change == QGraphicsItem::ItemSceneHasChanged)
	{
			//A pipe given its properties before it was on a folio (a piece
			//of a pipe cut by a symbol) takes the look of its project now
		if (scene())
			updateDesignationLook();
		conductorGeometryChanged();
		calculateTextItemPosition();

		if(!scene())
			removeHandler();
		else if (scene() && isSelected())
			addHandler();
	}
	else if (change == QGraphicsItem::ItemVisibleHasChanged) {
		calculateTextItemPosition();
	}
	else if (change == QGraphicsItem::ItemPositionHasChanged && isSelected()) {
		adjustHandlerPos();
	}
	else if (change == QGraphicsItem::ItemScenePositionHasChanged) {
		storeLayout();
		conductorGeometryChanged();
	}

	return(QGraphicsObject::itemChange(change, value));
}

/**
	@brief Conductor::sceneEventFilter
	@param watched
	@param event
	@return
*/
bool Conductor::sceneEventFilter(QGraphicsItem *watched, QEvent *event)
{
		//Watched must be an handler
	if(watched->type() == QetGraphicsHandlerItem::Type)
	{
		QetGraphicsHandlerItem *qghi = qgraphicsitem_cast<QetGraphicsHandlerItem *>(watched);

		if(m_handler_vector.contains(qghi)) //Handler must be in m_vector_index, then we can start resize
		{
			m_vector_index = m_handler_vector.indexOf(qghi);
			if (m_vector_index != -1)
			{
				if(event->type() == QEvent::GraphicsSceneMousePress) //Click
				{
					auto *mouse = static_cast<QGraphicsSceneMouseEvent *>(event);
						//The first click selected the wire and showed this
						//handle under the pointer; Qt then hands the second
						//click of the double-click to the handle as a plain
						//press. It is still a double-click on the wire.
					if (m_last_press.isValid()
							&& m_last_press.elapsed() < QApplication::doubleClickInterval()
							&& QLineF(m_last_press_pos, mouse->scenePos()).length() < 4.0)
					{
						m_last_press.invalidate();
						editProperty();
						return true;
					}
					handlerMousePressEvent(qghi, mouse);
					return true;
				}
				else if(event->type() == QEvent::GraphicsSceneMouseMove) //Move
				{
					handlerMouseMoveEvent(qghi, static_cast<QGraphicsSceneMouseEvent *>(event));
					return true;
				}
				else if (event->type() == QEvent::GraphicsSceneMouseRelease) //Release
				{
					handlerMouseReleaseEvent(qghi, static_cast<QGraphicsSceneMouseEvent *>(event));
					return true;
				}
				else if (event->type() == QEvent::GraphicsSceneMouseDoubleClick) //Double click
				{
					editProperty();
					return true;
				}
			}
		}
	}

	return false;
}

/**
	@brief Conductor::adjustHandlerPos
	Adjust the position of the handler item
*/
void Conductor::adjustHandlerPos()
{
	if (m_handler_vector.isEmpty())
		return;

	if (m_handler_vector.size() == handlerPoints().size())
	{
		QVector <QPointF> points_vector = mapToScene(handlerPoints());
		for (int i = 0 ; i < points_vector.size() ; ++i)
			m_handler_vector.at(i)->setPos(points_vector.at(i));
	}
}

/**
	@brief Conductor::handlerMousePressEvent
	@param qghi
	@param event
*/
void Conductor::handlerMousePressEvent(QetGraphicsHandlerItem *qghi, QGraphicsSceneMouseEvent *event)
{
	Q_UNUSED(event);

		//A pipe or a signal line of one or two segments (a line between two
		//free ends) has a handle on each, which no segment follows: its end
		//segments are split, without changing the path, so that the handle
		//takes the segment between (step 7c-bis; it crashed)
	if (m_vector_index > -1 && segmentsList().size() < 3
		&& m_properties.m_medium != TerminalData::Electrical) {
		splitEndSegments();
	}
		//we get the segment corresponding to the handler
	const QList<ConductorSegment *> segments_list = segmentsList();
	const int moved = segments_list.size() >= 3 ? m_vector_index + 1 : m_vector_index;
	if (m_vector_index > -1 && moved < segments_list.size()
		&& !segments_list.at(moved)->isStatic())
	{
		qghi->setColor(Qt::cyan);
		m_moving_segment = true;
		m_moved_segment = segments_list.at(moved);
		before_mov_text_pos_ = m_text_item -> pos();

		for(QetGraphicsHandlerItem *handler : m_handler_vector)
			if(handler != qghi)
				handler->hide();
	}
}

/**
	@brief Conductor::splitEndSegments
	See the declaration
*/
void Conductor::splitEndSegments()
{
	const QList<QPointF> points = segmentsToPoints();
	if (points.size() < 2 || points.size() > 3) {
		return;
	}
	const qreal room = exitRoom() > 0 ? exitRoom() : 10;
		//A point @p room from @p from towards @p to, if the segment is long
		//enough to keep something between
	auto inner = [room](const QPointF &from, const QPointF &to, QPointF *point) {
		const QLineF segment(from, to);
		if (segment.length() <= 2 * room) {
			return false;
		}
		*point = segment.pointAt(room / segment.length());
		return true;
	};
	QPointF first, last;
	if (!inner(points.first(), points.at(1), &first)
		|| !inner(points.last(), points.at(points.size() - 2), &last)) {
		return;
	}
	QList<QPointF> split;
	split << points.first() << first;
	if (points.size() == 3) {
		split << points.at(1);
	}
	split << last << points.last();
	pointsToSegments(split);
}

/**
	@brief Conductor::mergeFoldedEnds
	See the declaration. Upstream keeps the end segments of a wire as they
	are, short exits from the terminals, and adds a segment to join the one
	moved; the end segment of a pipe is often long (a line cut by a symbol
	keeps its route, step 7c), and the segment added then runs back over it.
	The end segment is only shortened, never turned round: the segment
	dragged stops short of a nozzle (stoppedAtExits()).
*/
bool Conductor::mergeFoldedEnds(const ConductorSegment *dragged)
{
	if (m_properties.m_medium == TerminalData::Electrical) {
		return false;
	}
	QList<QPointF> points = segmentsToPoints();
		//The segment between @p i and @p i + 1 is the one dragged: never
		//made one with an end segment (a straight line split in three on
		//the press, splitEndSegments(), the mouse not yet moved a step)
	auto isDragged = [&](int i) {
		return dragged && points.at(i) == dragged->firstPoint()
				&& points.at(i + 1) == dragged->secondPoint();
	};
		//@p end the end of the line, @p bend where its end segment bends,
		//@p next the point after: on one line, whichever way the segment
		//between goes (back over the end segment, on, or no length), as
		//long as the end segment keeps its way
	auto folded = [](const QPointF &end, const QPointF &bend, const QPointF &next) {
		const bool one_line = (qAbs(end.y() - bend.y()) < 0.01 && qAbs(bend.y() - next.y()) < 0.01)
							  || (qAbs(end.x() - bend.x()) < 0.01 && qAbs(bend.x() - next.x()) < 0.01);
		return one_line && QPointF::dotProduct(next - end, bend - end) > 0.01;
	};
	bool merged = false;
	while (points.size() >= 3 && !isDragged(1)
		   && folded(points.at(0), points.at(1), points.at(2))) {
		points.removeAt(1);
		merged = true;
	}
	while (points.size() >= 3 && !isDragged(points.size() - 3)
		   && folded(points.last(), points.at(points.size() - 2),
					 points.at(points.size() - 3))) {
		points.removeAt(points.size() - 2);
		merged = true;
	}
	if (merged) {
		pointsToSegments(points);
		segmentsToPath();
	}
	return merged;
}

/**
	@brief Conductor::handlerMouseMoveEvent
	@param qghi
	@param event
*/
void Conductor::handlerMouseMoveEvent(QetGraphicsHandlerItem *qghi, QGraphicsSceneMouseEvent *event)
{
	if (m_moving_segment)
	{
			//Snap the mouse pos to grid
		QPointF pos_ = Diagram::snapToGrid(mapFromScene(event->scenePos()));

			//Position of the last point
		QPointF p = m_moved_segment -> middle();

			//Calculate the movement; a pipe stops short of its nozzles,
			//and goes on again when the mouse comes back
		const Terminal *stop = nullptr;
		const QPointF movement = stoppedAtExits(m_moved_segment, pos_ - p, &stop);
		showExitStop(stop);
		m_moved_segment -> moveX(movement.x());
		m_moved_segment -> moveY(movement.y());

			//Apply the movement
		modified_path = true;
		has_to_save_profile = true;
		segmentsToPath();
			//Already while dragging, an end segment of a pipe is shortened
			//rather than run back over; the segments are made anew, the one
			//moved is found again by its points
		const QPointF first = m_moved_segment->firstPoint(), second = m_moved_segment->secondPoint();
		if (mergeFoldedEnds(m_moved_segment)) {
			m_moved_segment = nullptr;
			for (ConductorSegment *segment : segmentsList()) {
				if (segment->firstPoint() == first && segment->secondPoint() == second) {
					m_moved_segment = segment;
					break;
				}
			}
			if (!m_moved_segment) {
				m_moving_segment = false;
				return;
			}
		}
		calculateTextItemPosition();
		qghi->setPos(mapToScene(m_moved_segment->middle()));
	}
}

/**
	@brief Conductor::handlerMouseReleaseEvent
	@param qghi
	@param event
*/
void Conductor::handlerMouseReleaseEvent(QetGraphicsHandlerItem *qghi, QGraphicsSceneMouseEvent *event)
{
	Q_UNUSED(event);
	Q_UNUSED(qghi);

	m_vector_index = -1;

	m_moving_segment = false;
	showExitStop(nullptr);
	if (has_to_save_profile)
	{
		mergeFoldedEnds();
		saveProfile();
		has_to_save_profile = false;
	}
		//When handler is released, the conductor can have more segment than before the handler was moved
		//then we remove all handles and new ones are added
	removeHandler();
	addHandler();
}

/**
	@brief Conductor::addHandler
	Add handlers for this item
*/
void Conductor::addHandler()
{
	if (m_handler_vector.isEmpty() && scene())
	{
		m_handler_vector = QetGraphicsHandlerItem::handlerForPoint(mapToScene(handlerPoints()), QETUtils::graphicsHandlerSize(this));

		for(QetGraphicsHandlerItem *handler : m_handler_vector)
		{
			handler->setColor(Qt::blue);
			scene()->addItem(handler);
			handler->installSceneEventFilter(this);
			handler->setZValue(this->zValue()+1);
		}
	}
}

/**
	@brief Conductor::removeHandler
	Remove the handlers of this item
*/
void Conductor::removeHandler()
{
	if (!m_handler_vector.isEmpty())
	{
		qDeleteAll(m_handler_vector);
		m_handler_vector.clear();
	}
}

/**
	@brief Conductor::boundingRect
	@return
*/
QRectF Conductor::boundingRect() const
{
	QRectF br = shape().boundingRect();
		//A pipe holds its counter-flanges, as long as the side of a valve
		//and the band of an insulation around a jacket (step 8r-1)
	const qreal margin = m_properties.m_medium != TerminalData::Pipe ? 10
			: m_properties.m_pipe.jacketed() ? qMax(PipeConnection::markHalfHeight, PidInsulation::jacketed_half_width) + 2
											 : PipeConnection::markHalfHeight + 2;
	br.adjust(-margin, -margin, margin, margin);
		//And the text of its mark of insulation (step 7d); the bands of
		//the stubs are within the margin. Found on the whole path, on both
		//sides, as far as the middle of what is drawn may move: where the
		//pipe starts is not asked here (insulationMarkBetween())
	if (m_properties.m_medium == TerminalData::Pipe && m_properties.m_pipe.insulation_mark) {
		QRectF text_rect;
		insulationMarkBetween(0, PidInsulation::length(segmentsToPoints()), &text_rect, true);
		if (!text_rect.isNull()) {
			const qreal m = PipeConnection::pipeExit / 2 + 1;
			br = br.united(text_rect.adjusted(-m, -m, m, m));
		}
	}
	return br;
}

/**
	@brief Conductor::shape
	@return the shape of conductor.
	The shape thickness is bigger when conductor is hovered
*/
QPainterPath Conductor::shape() const
{
		//Not drawn (see paint()), so not to be clicked either: a click
		//there goes to the symbols
	if (endsTouch())
		return QPainterPath();
	QPainterPathStroker pps;
	if (m_properties.m_medium == TerminalData::Electrical) {
		pps.setWidth(m_mouse_over? 5 : 1);
	} else {
			//A pipe or a signal line: as wide as it is drawn, a little
			//more, and never too thin to pick; hovered, as wide as it is
			//then drawn (paint(): its width + 4)
		PotentialType type;
		const qreal width = drawnWidth(potentialTypeFormat(&type) ? &type : nullptr);
		const qreal area = qMax(width + 2, min_click_width);
		pps.setWidth(m_mouse_over ? qMax(width + 4, area) : area);
	}
	pps.setJoinStyle(conductor_pen.joinStyle());

	QPainterPath shape_(pps.createStroke(path()));

	return shape_;
}

/**
	@brief Conductor::drawnWidth
	See the declaration
*/
qreal Conductor::drawnWidth(const PotentialType *type) const
{
	return type ? type->width : lineWidth();
}

/**
	@brief Conductor::lineWidth
	See the declaration
*/
qreal Conductor::lineWidth() const
{
	if (m_properties.m_medium != TerminalData::Pipe) {
		return m_properties.lineWidth();
	}
	const Diagram *parent_diagram = diagram();
	const QETProject *project = parent_diagram ? parent_diagram->project() : nullptr;
	const PidCodification::LineKindEntry kind = project
			? project->pidCodification().lineKind(m_properties.m_pipe.line_kind)
			: PidCodification().lineKind(m_properties.m_pipe.line_kind);
	return kind.width * LeaderLine::unitsByMillimetre;
}

/**
	@brief Conductor::pipeDashes
	See the declaration
*/
QList<qreal> Conductor::pipeDashes() const
{
	if (m_properties.m_medium != TerminalData::Pipe) {
		return {};
	}
	const Diagram *parent_diagram = diagram();
	const QETProject *project = parent_diagram ? parent_diagram->project() : nullptr;
	const PidCodification::LineKindEntry kind = project
			? project->pidCodification().lineKind(m_properties.m_pipe.line_kind)
			: PidCodification().lineKind(m_properties.m_pipe.line_kind);
	return PidCodification::dashPattern(kind.pattern);
}

/**
	@brief Conductor::endsTouch
	@return true when the two terminals this conductor joins are on the
	same point, as when two symbols are placed against each other: the
	conductor connects them but is not drawn.
*/
bool Conductor::endsTouch() const
{
	return terminal1 && terminal2
			&& terminal1->dockConductor() == terminal2->dockConductor();
}

/**
	@brief Conductor::nearShape
	@return : An area in which it is considered a point is near this conductor.
*/
QPainterPath Conductor::nearShape() const
{
	QPainterPathStroker pps;
	pps.setWidth(1300);
	pps.setJoinStyle(conductor_pen.joinStyle());
	return pps.createStroke(path());
}

/**
	@param type Type de Segments
	@return Le nombre de segments composant le conducteur.
*/
uint Conductor::segmentsCount(QET::ConductorSegmentType type) const
{
	QList<ConductorSegment *> segments_list = segmentsList();
	if (type == QET::Both) return(segments_list.count());
	uint nb_seg = 0;
	foreach(ConductorSegment *conductor_segment, segments_list) {
		if (conductor_segment -> type() == type) ++ nb_seg;
	}
	return(nb_seg);
}

/**
	Genere une liste de points a partir des segments de ce conducteur
	@return La liste de points representant ce conducteur
*/
QList<QPointF> Conductor::segmentsToPoints() const
{
	// liste qui sera retournee
	QList<QPointF> points_list;

	// on retourne la liste tout de suite s'il n'y a pas de segments
	if (segments == nullptr) return(points_list);

	// recupere le premier point
	points_list << segments -> firstPoint();

	// parcourt les segments pour recuperer les autres points
	ConductorSegment *segment = segments;
	while(segment -> hasNextSegment()) {
		points_list << segment -> secondPoint();
		segment = segment -> nextSegment();
	}

	// recupere le dernier point
	points_list << segment -> secondPoint();

	//retourne la liste
	return(points_list);
}

/**
	Regenere les segments de ce conducteur a partir de la liste de points passee en parametre
	@param points_list Liste de points a utiliser pour generer les segments
*/
void Conductor::pointsToSegments(const QList<QPointF>& points_list) {
	// supprime les segments actuels
	deleteSegments();

	// cree les segments a partir de la liste de points
	ConductorSegment *last_segment = nullptr;
	for (int i = 0 ; i < points_list.size() - 1 ; ++ i) {
		last_segment = new ConductorSegment(points_list.at(i), points_list.at(i + 1), last_segment);
		if (!i) segments = last_segment;
	}
}

/**
	@brief Conductor::fromXml
	Load the conductor and its information from xml element
	@param dom_element
	@return true if loading succeeded else return false
*/
bool Conductor::fromXml(QDomElement &dom_element)
{
	const QUuid old_uuid = m_uuid;
		//Older project files have no conductor uuid attribute at all --
		//generate one on load, same treatment terminal uuids got when
		//that field was introduced (see terminal1/terminal2 handling in
		//toXml() below).
	m_uuid = QUuid(dom_element.attribute(QStringLiteral("uuid")));
	m_persist_uuid = !m_uuid.isNull();
	if (m_uuid.isNull()) {
			//Absent, empty or malformed: mint one. A null uuid is not a usable
			//identity -- every conductor carrying one would collide with every
			//other on the conductor table's primary key. It's runtime-only,
			//though: toXml() must not write it back out, or a legacy file
			//with no conductor uuids gets a freshly different one on every
			//single load-and-resave (see #754).
		m_uuid = QUuid::createUuid();
	}
	uuidChanged(old_uuid);

		//"nan" and "inf" parse as numbers; a non-finite position is kept at 0
	const qreal x = dom_element.attribute("x", nullptr).toDouble();
	const qreal y = dom_element.attribute("y", nullptr).toDouble();
	setPos(qIsFinite(x) ? x : 0, qIsFinite(y) ? y : 0);

	bool retval = pathFromXml(dom_element);

	m_text_item -> fromXml(dom_element);
	ConductorProperties pr;
	pr.fromXml(dom_element);

		//Load Sequential Values
	if (dom_element.hasAttribute("sequ_1") || dom_element.hasAttribute("sequf_1") || dom_element.hasAttribute("seqt_1") || dom_element.hasAttribute("seqtf_1") || dom_element.hasAttribute("seqh_1") || dom_element.hasAttribute("seqhf_1"))
		ConductorXmlRetroCompatibility::loadSequential(dom_element, this);
	else
		m_autoNum_seq.fromXml(dom_element.firstChildElement("sequentialNumbers"));

	m_freeze_label = dom_element.attribute("freezeLabel") == "true" ? true : false;
		//Automatic only in a project that keeps automatic connections up
		//to date: elsewhere the wire could never be selected nor removed
		//(#1837), as in files saved before that was checked, or a copy
		//pasted from such a project
	setAutomatic(dom_element.attribute(QStringLiteral("automatic")) == QLatin1String("true")
				 && diagram() && diagram()->project()
				 && diagram()->project()->liveAutoConnect());

		//Changed through the project database while its folio was not
		//built (lazy folios): the change, not what the file says
	if (diagram() && diagram()->project())
		if (const auto edited = diagram()->project()->dataBase()->takeUnbuiltWireEdit(m_uuid))
			pr = *edited;
		//The medium the file says, not the one of the terminals: opening a
		//file changes nothing, a wire saved between two nozzles stays a wire
	m_properties.m_medium = pr.m_medium;
	setProperties(pr);
	storeLayout();
	storePath();
	storeSequence();

	return retval;
}

/**
	@brief Conductor::toXml
	Exporte les caracteristiques du conducteur sous forme d'une element XML.
	@param dom_document :
	Le document XML a utiliser pour creer l'element XML
	@param table_adr_id :
	Hash stockant les correspondances entre les ids des
	bornes dans le document XML et leur adresse en memoire
	@param shared_uuids : uuids carried by more than one symbol of the
	folio. An end on such a symbol is written by its terminal id, as for a
	terminal without uuid: by uuid it would reopen on the first symbol
	carrying it (#1408).
	@return Un element XML representant le conducteur
*/
QDomElement Conductor::toXml(QDomDocument &dom_document,
				 QHash<Terminal *,
				 int> &table_adr_id,
				 const QSet<QUuid> &shared_uuids) const
{
	QDomElement dom_element = dom_document.createElement("conductor");

	if (m_persist_uuid)
		dom_element.setAttribute("uuid", m_uuid.toString());
	dom_element.setAttribute("x", QString::number(pos().x()));
	dom_element.setAttribute("y", QString::number(pos().y()));
	
	// Terminal is uniquely identified by the uuid of the terminal and the element
	if (terminal1->uuid().isNull()
		|| shared_uuids.contains(terminal1->parentElement()->uuid())) {
		// legacy method to identify the terminal
		dom_element.setAttribute("terminal1", table_adr_id.value(terminal1)); // for backward compatibility
	} else {
		dom_element.setAttribute("element1", terminal1->parentElement()->uuid().toString());
		dom_element.setAttribute("element1_label", terminal1->parentElement()->actualLabel());
		if (terminal1->parentElement()->linkedElements().isEmpty()) {
			//
		} else {
			dom_element.setAttribute("element1_linked", terminal1->parentElement()->linkedElements().last()->actualLabel());
		}
		dom_element.setAttribute("element1_name", terminal1->parentElement()->englishName());
		dom_element.setAttribute("terminal1", terminal1->uuid().toString());
		dom_element.setAttribute("terminalname1", terminal1->name());
	}

	if (terminal2->uuid().isNull()
		|| shared_uuids.contains(terminal2->parentElement()->uuid())) {
		// legacy method to identify the terminal
		dom_element.setAttribute("terminal2", table_adr_id.value(terminal2)); // for backward compatibility
	} else {
		dom_element.setAttribute("element2", terminal2->parentElement()->uuid().toString());
		dom_element.setAttribute("element2_label", terminal2->parentElement()->actualLabel());
		if (terminal2->parentElement()->linkedElements().isEmpty()) {
			//
		} else {
			dom_element.setAttribute("element2_linked", terminal2->parentElement()->linkedElements().last()->actualLabel());
		}
		dom_element.setAttribute("element2_name", terminal2->parentElement()->englishName());
		dom_element.setAttribute("terminal2", terminal2->uuid().toString());
		dom_element.setAttribute("terminalname2", terminal2->name());
	}
	dom_element.setAttribute("freezeLabel", m_freeze_label? "true" : "false");
	if (m_automatic)
		dom_element.setAttribute(QStringLiteral("automatic"), QStringLiteral("true"));

	// on n'exporte les segments du conducteur que si ceux-ci ont
	// ete modifies par l'utilisateur
	if (modified_path)
	{
		// parcours et export des segments
		QDomElement current_segment;
		foreach(ConductorSegment *segment, segmentsList())
		{
			current_segment = dom_document.createElement("segment");
			current_segment.setAttribute("orientation", segment -> isHorizontal() ? "horizontal" : "vertical");
			current_segment.setAttribute("length", QString("%1").arg(segment -> length()));
			dom_element.appendChild(current_segment);
		}
	}

	QDomElement dom_seq = m_autoNum_seq.toXml(dom_document);
	dom_element.appendChild(dom_seq);

		// Export the properties and text
	properties().toXml(dom_element);
	if(m_text_item->wasMovedByUser())
	{
		dom_element.setAttribute("userx", QString::number(m_text_item->pos().x()));
		dom_element.setAttribute("usery", QString::number(m_text_item->pos().y()));
	}
	if(m_text_item->wasRotatedByUser())
		dom_element.setAttribute("rotation", QString::number(m_text_item->rotation()));

	return(dom_element);
}

/**
	@brief Conductor::pathFromXml
	Generate the path (of the line) from xml file by checking the segments in the xml
	file
	@param e
	@return true if generate path success else return false
*/
bool Conductor::pathFromXml(const QDomElement &e) {
	// parcourt les elements XML "segment" et en extrait deux listes de longueurs
	// les segments non valides sont ignores
	QList<qreal> segments_x, segments_y;
	for (QDomNode node = e.firstChild() ; !node.isNull() ; node = node.nextSibling()) {
		// on s'interesse aux elements XML "segment"
		QDomElement current_segment = node.toElement();
		if (current_segment.isNull() || current_segment.tagName() != "segment") continue;

		// le segment doit avoir une longueur
		if (!current_segment.hasAttribute("length")) continue;

		// cette longueur doit etre un reel
		bool ok;
		qreal segment_length = current_segment.attribute("length").toDouble(&ok);
			// toDouble() accepts "nan" and "inf". A NaN length would also pass
			// the coherence check below (every comparison with NaN is false)
			// and turn the whole path into NaN.
		if (!ok || !qIsFinite(segment_length)) continue;

		if (current_segment.attribute("orientation") == "horizontal") {
			segments_x << segment_length;
			segments_y << 0.0;
		} else {
			segments_x << 0.0;
			segments_y << segment_length;
		}
	}

	// If there is no segment, we generate an automatic path and return true
	if (!segments_x.size()) {
		generateConductorPath(terminal1 -> dockConductor(), terminal1 -> orientation(), terminal2 -> dockConductor(), terminal2 -> orientation());
		return(true);
	}

	// les longueurs recueillies doivent etre coherentes avec les positions des bornes
	// The collected lengths must be consistent with the positions of the terminals  
	qreal width = 0.0, height = 0.0;
	foreach (qreal t, segments_x) width  += t;
	foreach (qreal t, segments_y) height += t;
	QPointF t1 = terminal1 -> dockConductor();
	QPointF t2 = terminal2 -> dockConductor();
	qreal expected_width  = t2.x() - t1.x();
	qreal expected_height = t2.y() - t1.y();

	// on considere que le trajet est incoherent a partir d'une unite de difference avec l'espacement entre les bornes
	if (
		qAbs(expected_width  - width)  > 1.0 ||
		qAbs(expected_height - height) > 1.0
	) {
		qDebug() << "Conductor::fromXml : les segments du conducteur ne semblent pas coherents - utilisation d'un trajet automatique";
		return(false);
	}

	/* on recree les segments a partir des donnes XML */
	// cree la liste de points
	QList<QPointF> points_list;
	points_list << mapFromScene(t1);
	for (int i = 0 ; i < segments_x.size() ; ++ i) {
		points_list << QPointF(
			points_list.last().x() + segments_x.at(i),
			points_list.last().y() + segments_y.at(i)
		);
	}

		//A pipe drawn before its exit was long enough, turning too close to
		//a nozzle for the part of its connection: routed by itself
	if (exitsTooShort(points_list)) {
		generateConductorPath(terminal1 -> dockConductor(), terminal1 -> orientation(), terminal2 -> dockConductor(), terminal2 -> orientation());
		return(true);
	}

	pointsToSegments(points_list);

	// initialise divers parametres lies a la modification des conducteurs
	modified_path = true;
	saveProfile(false);

	segmentsToPath();
	return(true);
}

/**
	@brief Conductor::handlerPoints
	@return The points used to draw the handler square, used to modify
	the path of the conductor.
	The points stored in the QVector are the middle point of each segments that compose the conductor,
	at exception of the first and last segment because there just here to extend the terminal.
*/
QVector<QPointF> Conductor::handlerPoints() const
{
	QList <ConductorSegment *> sl = segmentsList();
	if (sl.size() >= 3)
	{
		sl.removeFirst();
		sl.removeLast();
	}

	QVector <QPointF> middle_points;

	foreach(ConductorSegment *segment, sl)
		middle_points.append(segment->middle());

	return middle_points;
}

/// @return les segments de ce conducteur
const QList<ConductorSegment *> Conductor::segmentsList() const
{
	if (segments == nullptr) return(QList<ConductorSegment *>());

	QList<ConductorSegment *> segments_vector;
	ConductorSegment *segment = segments;

	while (segment -> hasNextSegment()) {
		segments_vector << segment;
		segment = segment -> nextSegment();
	}
	segments_vector << segment;
	return(segments_vector);
}

/**
	@brief Conductor::moveSegment
	Move one segment of this conductor by (dx, dy), the same primitive
	handlerMouseMoveEvent()/handlerMouseReleaseEvent() apply on a manual
	drag -- moveX()/moveY() each silently no-op on the wrong axis or a
	static (terminal-anchored) segment, so both are always called and
	whichever applies takes effect. Unlike a drag this commits the whole
	move as a single undo step.
	@param index a segmentsList() index
	@param dx @param dy the movement, in the diagram's own coordinates
	@return false if index is out of range
*/
bool Conductor::moveSegment(int index, qreal dx, qreal dy)
{
	const QList<ConductorSegment *> segs = segmentsList();
	if (index < 0 || index >= segs.count()) return false;

	before_mov_text_pos_ = m_text_item->pos();
	ConductorSegment *seg = segs.at(index);
		//A pipe stops short of its nozzles, as when dragged
	const QPointF movement = stoppedAtExits(seg, QPointF(dx, dy), nullptr);
	seg->moveX(movement.x());
	seg->moveY(movement.y());
	modified_path = true;
	segmentsToPath();
	mergeFoldedEnds();
	calculateTextItemPosition();
	saveProfile();
	return true;
}

/**
	@brief Conductor::setPathPoints
	Replace this conductor's path by the one through @p scene_points, and
	push one undo step for it -- the same ChangeConductorCommand a manual
	handle drag or moveSegment() pushes, so the path is saved as a
	modified one and survives a reload.
	@param scene_points in scene coordinates, from terminal1's docking
	point to terminal2's, every segment horizontal or vertical
	@return false, changing nothing, if the points do not make such a path
*/
bool Conductor::setPathPoints(const QList<QPointF> &scene_points)
{
	if (scene_points.size() < 2 || !terminal1 || !terminal2) return false;
	const auto isNear = [](const QPointF &a, const QPointF &b) {
		return qAbs(a.x() - b.x()) < 0.5 && qAbs(a.y() - b.y()) < 0.5;
	};
	if (!isNear(scene_points.first(), terminal1->dockConductor()) ||
		!isNear(scene_points.last(), terminal2->dockConductor()))
		return false;
	QList<QPointF> points;
	for (int i = 0; i < scene_points.size(); ++i) {
		const QPointF p = scene_points.at(i);
		if (i && qAbs(p.x() - scene_points.at(i - 1).x()) > 0.01
			  && qAbs(p.y() - scene_points.at(i - 1).y()) > 0.01)
			return false;
		points << mapFromScene(p);
	}
		// A two-point path has nothing to segment between: let the
		// application draw it, as for a conductor with no profile.
	if (points.size() < 3) return false;

	before_mov_text_pos_ = m_text_item->pos();
	pointsToSegments(points);
	modified_path = true;
	segmentsToPath();
	calculateTextItemPosition();
	saveProfile();
	return true;
}

/**
	@brief Conductor::length
	@return the length of this conductor
*/
qreal Conductor::length() const{
	return path().length();
}

/**
	@return Le segment qui contient le point au milieu du conducteur
*/
ConductorSegment *Conductor::middleSegment()
{
	if (segments == nullptr) return(nullptr);

	qreal half_length = length() / 2.0;

	ConductorSegment *s = segments;
	qreal l = 0;

	while (s -> hasNextSegment()) {
		l += qAbs(s -> length());
		if (l >= half_length) break;
		s = s -> nextSegment();
	}
	// s est le segment qui contient le point au milieu du conducteur
	return(s);
}

/**
	@brief Conductor::posForText
	Calculate and return the better pos for text.
	@param flag : flag is used to know if text pos is near of
	a vertical or horizontal conductor segment.
*/
QPointF Conductor::posForText(Qt::Orientations &flag)
{
	ConductorSegment *segment        = segments;
	bool all_segments_are_vertical   = true;
	bool all_segments_are_horizontal = true;

		//Go to first segment
	while (!segment->isFirstSegment()) {
		segment = segment->previousSegment();
	}

	QPointF p1 = segment -> firstPoint(); //<First point of conductor
	ConductorSegment *longest_segment = segment; //<the longest segment of conductor.

		// check if all segments are horizontal or vertical ... first segment:
	if (segment -> firstPoint().x() != segment -> secondPoint().x())
		all_segments_are_vertical   = false;
	if (segment -> firstPoint().y() != segment -> secondPoint().y())
		all_segments_are_horizontal = false;
		// find longest segment
	while (segment -> hasNextSegment())
	{
		segment = segment -> nextSegment();
		if (segment -> firstPoint().x() != segment -> secondPoint().x())
			all_segments_are_vertical   = false;
		if (segment -> firstPoint().y() != segment -> secondPoint().y())
			all_segments_are_horizontal = false;

		if (qAbs(segment->length()) > qAbs(longest_segment -> length()))
			longest_segment = segment;
	}

	QPointF p2 = segment -> secondPoint(); //<Last point of conductor

	//If the conductor is horizontal or vertical
	//Return the point at the middle of conductor
	if (all_segments_are_vertical) { //<Vertical
		flag = Qt::Vertical;
		if (p1.y() > p2.y()) {
			p1.setY(p1.y() - (length()/2));
		} else {
			p1.setY(p1.y() + (length()/2));
		}
	} else if (all_segments_are_horizontal) { //<Horizontal
		flag = Qt::Horizontal;
		if (p1.x() > p2.x()) {
			p1.setX(p1.x() - (length()/2));
		} else {
			p1.setX(p1.x() + (length()/2));
		}
	} else { //Return the point at the middle of longest segment.
		p1 = longest_segment->middle();
		flag = (longest_segment->isHorizontal())? Qt::Horizontal : Qt::Vertical;
	}
	return p1;
}

/**
	@brief Conductor::updateTextVisibility
	Show the text of this conductor if its properties ask for it and wire
	numbers are not hidden (View > Show). With "one text per potential",
	call calculateTextItemPosition() afterwards: it hides all texts of the
	potential but the longest conductor's.
*/
void Conductor::updateTextVisibility()
{
	ShownKinds::setVisible(m_text_item,
						   m_properties.type == ConductorProperties::Multi
						   && m_properties.m_show_text
						   && !hidesPlaceholder()
						   && !markReplacesText()
							   //a pipe without a designation (no model)
						   && !(m_properties.m_medium == TerminalData::Pipe
								&& m_properties.text.isEmpty()));
	updateLeader();
}

/**
	@brief Conductor::updateDesignationLook
	The text of a pipe, its designation, takes the size the project gives
	the designations of the lines (else that of the text of a conductor) and,
	when the project colours the labels, the colour of the pipe. It is never
	typed: the model of the project makes it.
*/
void Conductor::updateDesignationLook()
{
	if (m_properties.m_medium != TerminalData::Pipe || !m_text_item) {
		return;
	}
	const QETProject *project = diagram() ? diagram()->project() : nullptr;
	const PidCodification codification = project ? project->pidCodification() : PidCodification();
	QFont font = m_text_item->font();
	const int size = codification.text_sizes.value(PidLineDesignation::modelKey());
	font.setPointSize(size > 0 ? size : m_properties.text_size);
	m_text_item->setFont(font);
	const QColor ink = codification.textInk(true, pipeColor());
	m_text_item->setColor(ink.isValid() ? ink : m_properties.text_color);
	m_text_item->setNoEditable(true);
	updateLeader();
}

/**
	@brief Conductor::updateLeader
	See the declaration. The line is a child of this conductor, as thin as
	the finest line (0.25 mm), of the colour of the text; it is drawn,
	printed and exported with the folio, never selected.
*/
void Conductor::updateLeader()
{
	const QETProject *project = diagram() ? diagram()->project() : nullptr;
	QPainterPath leader;
	if (project && project->textLeaderDistance() > 0 && m_text_item && segments
		&& m_text_item->wasMovedByUser() && m_text_item->isVisible()
		&& !m_text_item->toPlainText().isEmpty())
	{
		const QRectF text = m_text_item->mapRectToParent(m_text_item->boundingRect());
			//To where the text stands when nobody moved it: the middle of
			//the longest segment, not the nearest point, which may be an end
		Qt::Orientations orientation;
		const QPointF anchor = posForText(orientation);
			//A plain end stops 0.5 mm short of the edge of the line
		const qreal clearance = lineWidth() / 2 + LeaderLine::unitsByMillimetre / 2;
		leader = LeaderLine::path(text, segmentsToPoints(), anchor,
								  project->textLeaderDistance() * LeaderLine::unitsByMillimetre,
								  project->textLeaderEnd(), LeaderLine::unitsByMillimetre, clearance);
	}
	if (leader.isEmpty()) {
		if (m_leader) {
			m_leader->setPath(QPainterPath());
			m_leader->hide();
		}
		return;
	}
	if (!m_leader) {
		m_leader = new QGraphicsPathItem(this);
		m_leader->setAcceptedMouseButtons(Qt::NoButton);
		m_leader->setAcceptHoverEvents(false);
		m_leader->setFlag(QGraphicsItem::ItemIsSelectable, false);
	}
	const QColor ink = m_text_item->color();
	QPen pen(ink);
	pen.setWidthF(1.0);
	pen.setCapStyle(Qt::FlatCap);
	m_leader->setPen(pen);
	m_leader->setBrush(ink);
	m_leader->setPath(leader);
	m_leader->show();
}

/**
	@brief Conductor::leaderPath
	See the declaration
*/
QPainterPath Conductor::leaderPath() const
{
	return m_leader && m_leader->isVisible() ? m_leader->path() : QPainterPath();
}

/**
	@brief Conductor::keepOneDesignation
	Of the pieces of the pipe @a whole cut by a symbol, only one which shows
	its designation keeps showing it: it is written once. The piece nearest
	to where the designation of @a whole was, so that it stays on its side
	of the symbol (step 8a: a flow arrow dropped on a pipe moved it to the
	other side, onto a longer piece); the longest one without @a whole.
	When the designation was moved by hand, the piece takes it where it
	was (and as it was turned); otherwise it is placed again on that piece.
*/
void Conductor::keepOneDesignation(const QList<Conductor *> &pieces, const Conductor *whole)
{
	const bool known = whole && whole->m_text_item;
	const bool moved = known && whole->m_text_item->wasMovedByUser();
	const QPointF text_centre = known
			? whole->m_text_item->mapToScene(whole->m_text_item->boundingRect().center())
			: QPointF();
		//The distance from the designation where it was to a piece
	auto distance = [&text_centre](const Conductor *piece) {
		const QList<QPointF> points = piece->segmentsToPoints();
		qreal best = std::numeric_limits<qreal>::max();
		for (int i = 0 ; i + 1 < points.size() ; ++i) {
			const QPointF a = piece->mapToScene(points.at(i));
			const QPointF b = piece->mapToScene(points.at(i + 1));
			const QPointF along = b - a;
			const qreal length2 = QPointF::dotProduct(along, along);
			const qreal t = length2 > 0
					? qBound(0.0, QPointF::dotProduct(text_centre - a, along) / length2, 1.0)
					: 0.0;
			best = qMin(best, QLineF(text_centre, a + t * along).length());
		}
		return best;
	};

	Conductor *kept = nullptr;
	for (Conductor *piece : pieces) {
		if (piece && piece->m_properties.m_medium == TerminalData::Pipe
			&& piece->m_properties.m_show_text
			&& (!kept || (known ? distance(piece) < distance(kept)
								: piece->length() > kept->length()))) {
			kept = piece;
		}
	}
	if (!kept) {
		return;
	}
	if (moved && kept->m_text_item) {
		kept->m_text_item->setRotation(whole->m_text_item->rotation());
		kept->m_text_item->forceRotateByUser(whole->m_text_item->wasRotatedByUser());
		kept->m_text_item->setPos(kept->mapFromScene(whole->m_text_item->scenePos()));
		kept->m_text_item->forceMovedByUser(true);
	}
	for (Conductor *piece : pieces) {
		if (piece && piece != kept && piece->m_properties.m_medium == TerminalData::Pipe
			&& piece->m_properties.m_show_text) {
			ConductorProperties properties = piece->m_properties;
			properties.m_show_text = false;
			piece->setProperties(properties);
		}
	}
}

/**
	@brief Conductor::refreshDesignation
	Write the designation of this pipe again and give it its look, after a
	change of the codification of its project (its model, the size of its
	text, its colours). Nothing for a wire or a signal line.
*/
void Conductor::refreshDesignation()
{
	if (m_properties.m_medium != TerminalData::Pipe) {
		return;
	}
	setProperties(m_properties);
	updateDesignationLook();
	updateTextVisibility();
	calculateTextItemPosition();
}

/**
	@brief Conductor::markReplacesText
	@return true when the stroke with colour and section is shown and its
	properties ask it to stand in for the conductor's text
*/
bool Conductor::markReplacesText() const
{
	return m_properties.m_definition_mark && m_properties.m_definition_mark_hides_text;
}

/**
	@brief Conductor::hidesPlaceholder
	@return true for an automatic wire whose text is only QElectroTech's
	placeholder "_" or nothing: it would stand beside every bridge and
	corner the automatic connections make (as a "|" on an upright wire).
	Such a wire shows no text; one given a text of its own shows it.
*/
bool Conductor::hidesPlaceholder() const
{
	if (!m_automatic)
		return false;
	const QString text = m_text_item->toPlainText().trimmed();
	return text.isEmpty() || text == QLatin1String("_");
}

/**
	@brief Conductor::calculateTextItemPosition
	Move the text at middle of conductor (if is vertical or horizontal)
	otherwise, move conductor at the middle of the longest segment of conductor.
	If text was moved by user, this function do nothing, except check if text is near conductor.
*/
void Conductor::calculateTextItemPosition()
{
	updateDefinitionMark();
	if (!m_text_item || !diagram() || m_properties.type != ConductorProperties::Multi)
		return;

	if (diagram() -> defaultConductorProperties.m_one_text_per_folio == true &&
		relatedPotentialConductors(false).size() > 0)
	{

		Conductor *longest_conductor = longestConductorInPotential(this);

			//This conductor isn't the longest conductor?
			//We call calculateTextItemPosition for the longest conductor
		if(longest_conductor != this)
		{
			longest_conductor -> calculateTextItemPosition();
			return;
		}

			//At this point this conductor is the longest conductor we hide all text of conductor_list
		foreach (Conductor *c, relatedPotentialConductors(false)) {
					ShownKinds::setVisible(c -> textItem(), false);
		}
			//Make sure text item is visible, unless wire numbers are hidden
			//(View > Show)
		ShownKinds::setVisible(m_text_item, !hidesPlaceholder() && !markReplacesText());
	}

		//position
	if (m_text_item -> wasMovedByUser())
	{
			//Text field was moved by user:
			//we check if text field is yet  near the conductor
		QPointF text_item_pos = m_text_item -> pos();
		QPainterPath near_shape = nearShape();
		if (!near_shape.contains(text_item_pos)) {
			m_text_item -> setPos(movePointIntoPolygon(text_item_pos, near_shape));
		}
			//The conductor moved under it: its leader line follows
		updateLeader();
	}
	else
	{
			//Position and rotation of text is calculated.
		Qt::Orientations rotation;
		QPointF text_pos = posForText(rotation);
			//The designation of a pipe stands above a horizontal pipe and on
			//the left of a vertical one, read from the bottom, whatever the
			//texts of the conductors of the folio do
		const bool pipe = m_properties.m_medium == TerminalData::Pipe;
		const Qt::Alignment horizontal_alignment = pipe ? Qt::AlignTop
														: m_properties.m_horizontal_alignment;
		const Qt::Alignment vertical_alignment = pipe ? Qt::AlignLeft
													  : m_properties.m_vertical_alignment;

		if (!m_text_item -> wasRotatedByUser())
		{
			if (pipe)
				m_text_item -> setRotation(rotation == Qt::Vertical ? 270 : 0);
			else
				rotation == Qt::Vertical ? m_text_item -> setRotation(m_properties.verti_rotate_text):
										   m_text_item -> setRotation(m_properties.horiz_rotate_text);
		}

			//Adjust the position of text if his rotation
			//is 0° or 270°, to be exactly centered to the conductor
		if (m_text_item -> rotation() == 0)
		{
			text_pos.rx() -= m_text_item -> boundingRect().width()/2;
			if(horizontal_alignment == Qt::AlignTop)
				text_pos.ry() -= m_text_item->boundingRect().height();
		}
		else if (m_text_item -> rotation() == 270)
		{
			text_pos.ry() += m_text_item -> boundingRect().width()/2;
			if(vertical_alignment == Qt::AlignLeft)
				text_pos.rx() -= m_text_item->boundingRect().height();
		}

		m_text_item -> setPos(text_pos);

			//Ensure text item does not collide with this conductor, nor
			//with the lines of the jacket of a pipe drawn there (step 8r-1,
			//hand test of 25/11: along the whole pipe, they went under it)
		QPainterPath jacket_area;
		if (pipe) {
			const QPainterPath jacket = jacketPath();
			if (!jacket.isEmpty()) {
				QPainterPathStroker stroker;
				stroker.setWidth(lineWidth() / 2 + 2);
				jacket_area = stroker.createStroke(jacket);
			}
		}
		auto collides = [&]() {
			return m_text_item->collidesWithItem(this)
					|| (!jacket_area.isEmpty()
						&& m_text_item->collidesWithPath(m_text_item->mapFromItem(this, jacket_area)));
		};
		while (collides())
		{
			if(rotation == Qt::Vertical)
			{
				if(vertical_alignment == Qt::AlignRight)
					m_text_item->setX(m_text_item->x()+1);
				else if (vertical_alignment == Qt::AlignLeft)
					m_text_item->setX(m_text_item->x()-1);
				else
					return; //avoid infinite loop
			}
			else if (rotation == Qt::Horizontal)
			{
				if(horizontal_alignment == Qt::AlignTop)
					m_text_item->setY(m_text_item->y()-1);
				else if (horizontal_alignment == Qt::AlignBottom)
					m_text_item->setY(m_text_item->y()+1);
				else
					return; //avoid infinite loop
			}
		}
	}
}

/**
	@brief Conductor::cornerChain
	@return this conductor and those joined to it through corners only,
	with the same medium, colour and section: one run of wire, ending at a
	terminal of anything else (a T-junction, a device) or where the wire
	changes
*/
QList<Conductor *> Conductor::cornerChain()
{
	QList<Conductor *> chain {this};
	for (int i = 0 ; i < chain.size() ; ++i)
	{
		Conductor *c = chain.at(i);
		for (Terminal *end : {c->terminal1, c->terminal2})
		{
			if (!end || !end->parentElement() || !end->parentElement()->isCorner())
				continue;
			for (Terminal *other : end->parentElement()->terminals())
				for (Conductor *next : other->conductors())
					if (!chain.contains(next)
					    && next->m_properties.m_medium == c->m_properties.m_medium
					    && next->m_properties.m_wire_color == c->m_properties.m_wire_color
					    && next->m_properties.m_wire_section == c->m_properties.m_wire_section)
						chain << next;
		}
	}
	return chain;
}

/**
	@brief Conductor::updateDefinitionMark
	Show, move or remove the stroke with this conductor's colour and
	section, as its properties ask. A run turned by corners shows one
	stroke, on its longest conductor that asks for one.
*/
void Conductor::updateDefinitionMark()
{
		//Taken off the folio: its terminals no longer list it, the run it
		//was part of shows its stroke on another conductor
	if (!scene()) {
		delete m_definition_mark;
		m_definition_mark = nullptr;
		const QList<Conductor *> run = cornerChain();
		for (Conductor *c : run)
			if (c != this && c->scene())
				c->updateDefinitionMark();
		return;
	}
	const auto wants = [](const Conductor *c) {
		return c->m_properties.m_medium == TerminalData::Electrical
			&& c->m_properties.m_definition_mark
			&& c->m_properties.type == ConductorProperties::Multi;
	};
	Conductor *shown = nullptr;
	const QList<Conductor *> chain = cornerChain();
	for (Conductor *c : chain)
		if (wants(c) && (!shown || c->length() > shown->length()))
			shown = c;

	for (Conductor *c : chain)
	{
		if (c != shown) {
			delete c->m_definition_mark;
			c->m_definition_mark = nullptr;
		} else if (!c->m_definition_mark) {
			c->m_definition_mark = new ConductorDefinitionMark(c);
		} else {
			c->m_definition_mark->refresh();
		}
	}
}


/**
	Sauvegarde le profil courant du conducteur pour l'utiliser ulterieurement
	dans priv_modifieConductor.
	Save the current conductors profile for later use in priv_modifieConductor.
*/
void Conductor::saveProfile(bool undo) {
	Qt::Corner current_path_type = currentPathType();
	ConductorProfile old_profile(conductor_profiles[current_path_type]);
	conductor_profiles[current_path_type].fromConductor(this);
	Diagram *dia = diagram();
	if (undo && dia) {
		ChangeConductorCommand *undo_object = new ChangeConductorCommand(
			this,
			old_profile,
			conductor_profiles[current_path_type],
			current_path_type
		);
		undo_object -> setConductorTextItemMove(before_mov_text_pos_, m_text_item -> pos());
		dia -> undoStack().push(undo_object);
	}
}

/**
	@param value1 Premiere valeur
	@param value2 Deuxieme valeur
	@return 1 si les deux valeurs sont de meme signe, -1 sinon
*/
int Conductor::getCoeff(const qreal &value1, const qreal &value2) {
	return(getSign(value1) * getSign(value2));
}

/**
	@param value valeur
	@return 1 si valeur est negatif, 1 s'il est positif ou nul
*/
int Conductor::getSign(const qreal &value) {
	return(value < 0 ? -1 : 1);
}

/**
	Applique un nouveau profil a ce conducteur
	@param cp Profil a appliquer a ce conducteur
	@param path_type Type de trajet pour lequel ce profil convient
*/
void Conductor::setProfile(const ConductorProfile &cp, Qt::Corner path_type) {
	conductor_profiles[path_type] = cp;
	// si le type de trajet correspond a l'actuel
	if (currentPathType() == path_type) {
		if (conductor_profiles[path_type].isNull()) {
			generateConductorPath(terminal1 -> dockConductor(), terminal1 -> orientation(), terminal2 -> dockConductor(), terminal2 -> orientation());
			modified_path = false;
			storePath();
		} else {
			updateConductorPath(terminal1 -> dockConductor(), terminal1 -> orientation(), terminal2 -> dockConductor(), terminal2 -> orientation());
			modified_path = true;
			storePath();
		}
		if (type() == ConductorProperties::Multi) {
			calculateTextItemPosition();
		}
	}
}

/// @return le profil de ce conducteur
ConductorProfile Conductor::profile(Qt::Corner path_type) const
{
	return(conductor_profiles[path_type]);
}

/**
	@brief Conductor::refreshText
	Refresh the text of this conductor.
	recalculate and set the text according to the formula.
*/
void Conductor::refreshText()
{
	if (m_freeze_label)
	{
		m_text_item->setPlainText(m_properties.text);
	}
	else
	{
		if (!m_properties.m_formula.isEmpty())
		{
			if (diagram())
			{
				autonum::sequentialNumbers numbers = m_autoNum_seq;
				QString text = autonum::AssignVariables::formulaToLabel(m_properties.m_formula, numbers, diagram(), nullptr, this);
				m_properties.text = text;
				m_text_item->setPlainText(text);
			}
			else
			{
				m_properties.text = m_properties.m_formula;
				m_text_item->setPlainText(m_properties.text);
			}
			storeProperties();
		}
		else
		{
			m_text_item->setPlainText(m_properties.text);
		}
	}
}

void Conductor::setPath(const QPainterPath &path)
{
	if(path == m_path)
		return;

	prepareGeometryChange();
	m_path = path;
	conductorGeometryChanged();
	update();
}

QPainterPath Conductor::path() const
{
	return m_path;
}

/**
	@brief Conductor::setPropertyToPotential
	@param property
	@param only_text
	Set property to conductor and every conductors in
	the same potential of conductor.
	If only_text is true only formula, text,
	function and tension/protocol is set
	to other conductor in the same potential,
	the other values of property stay unmodified

	A name written over the potential stays on the folio it is written on
	in a project chaining its reports by the wire function
	(QETProject::chainsByWireFunction): the folio report links of the
	potential go before the name is written
	(QETProject::dissolveReportLinksOfPotential) and the chaining makes
	them again with the new name (QETProject::reportKeySourceChanged).
	A project chaining by the label of its reports, or not at all, is
	left as it was.
*/
void Conductor::setPropertyToPotential(const ConductorProperties &property,
					   bool only_text)
{
	QETProject *project = diagram() ? diagram()->project() : nullptr;
	const bool renames = property.m_function != properties().m_function;
	const bool stop_at_report_links = renames && project
			&& project->chainsByWireFunction();
	if (stop_at_report_links) {
		project->dissolveReportLinksOfPotential(this);
	}
	QSet <Conductor *> potential_list = relatedPotentialConductors(
				true, nullptr, false, stop_at_report_links);
	setProperties(property);

	foreach (Conductor *other_conductor, potential_list)
	{
			//A core of a cable takes section, colour and type from its
			//cable: no property of the potential is copied over it
		if (!other_conductor->properties().m_cable_uuid.isNull()) {
			continue;
		}
		if (only_text)
		{
			ConductorProperties other_properties = other_conductor->properties();
			other_properties.m_formula = m_properties.m_formula;
			other_properties.text = m_properties.text;
			other_properties.m_function = m_properties.m_function;
			other_properties.m_potential_type = m_properties.m_potential_type;
			other_properties.m_tension_protocol = m_properties.m_tension_protocol;
			other_properties.m_wire_color = m_properties.m_wire_color;
			other_properties.m_wire_section = m_properties.m_wire_section;
			other_conductor->setProperties(other_properties);
		}
		else
		{
			other_conductor->setProperties(property);
		}
	}
}

/**
	@brief Conductor::setProperties
	Set property as current property of conductor. The medium of the
	conductor is kept: the defaults of the folio, the properties of a
	potential or of another wire never turn a wire into a pipe or back
	(setMedium() does).
	@param properties : properties
*/
void Conductor::setProperties(const ConductorProperties &properties)
{
	ConductorProperties property = properties;
	property.m_medium = m_properties.m_medium;
		//A conductor of another medium takes none of the wire fields
		//(cable, definition mark, single-line symbols, wire color...)
	if (property.m_medium != TerminalData::Electrical)
		property = property.withoutElectricalFields();
		//and only a pipe has the data of a pipe
	if (property.m_medium != TerminalData::Pipe)
		property.m_pipe = PipeProperties();
		//and only a signal line the data of a signal line
	if (property.m_medium != TerminalData::Signal)
		property.m_signal = SignalProperties();
		//The text of a pipe is its designation, which the model of its
		//project makes of its data: no formula, no text of its own
	if (property.m_medium == TerminalData::Pipe && diagram() && diagram()->project()) {
		const PidCodification codification = diagram()->project()->pidCodification();
		property.m_formula.clear();
		property.text = PidLineDesignation::text(
					codification.tag_models.value(PidLineDesignation::modelKey()),
					property.m_pipe, codification);
	}
	if (m_properties == property) return;
		//Its width may change which line gives way at a crossing (7c)
	conductorGeometryChanged();

		//The run this conductor was part of, which may part now
	const QList<Conductor *> old_run = cornerChain();
	QString formula = m_properties.m_formula;
		//The click area of a pipe follows its width (shape())
	if (property.m_medium != TerminalData::Electrical) {
		prepareGeometryChange();
	}
	const QString function = m_properties.m_function;
	m_properties = property;

	if (!m_properties.m_formula.isEmpty())
	{
		if (diagram())
		{
			autonum::sequentialNumbers numbers = m_autoNum_seq;
			QString text = autonum::AssignVariables::formulaToLabel(m_properties.m_formula, numbers, diagram(), nullptr, this);
			m_properties.text = text;
		}
		else if (m_properties.text.isEmpty())
		{
			m_properties.text = m_properties.m_formula;
		}

		setUpConnectionForFormula(formula, m_properties.m_formula);
	}

	m_text_item->setPlainText(m_properties.text);
	QFont font = m_text_item->font();
	font.setPointSize(m_properties.text_size);
	m_text_item->setFont(font);
	m_text_item->setColor(m_properties.text_color);
	updateDesignationLook();

	updateTextVisibility();
	calculateTextItemPosition();
	for (Conductor *c : old_run)
		if (c != this)
			c->updateDefinitionMark();
	update();
		//What a strip above this wire draws of it changed with the
		//three fields
	if (diagram()) diagram()->refreshTerminalStrips();
	if (Terminal::takesConductorColor()) {
		if (terminal1) terminal1->update();
		if (terminal2) terminal2->update();
	}

	storeProperties();
		//After the store: what is at its ends reads the new properties there
	updateFittings();
	if (m_properties.m_function != function) {
		emit functionChanged(this);
	}
	emit propertiesChange();
}

/**
	@brief Conductor::properties
	@return the properties of this Conductor
*/
ConductorProperties Conductor::properties() const
{
	if (projectDataBase *store = placedStore())
		return store->conductorProperties(m_uuid);
	return(m_properties);
}

/**
	@brief Conductor::medium
	@return the medium this conductor carries: electrical, or a pipe, a
	signal (ConductorProperties::m_medium)
*/
TerminalData::Medium Conductor::medium() const
{
	return properties().m_medium;
}

/**
	@brief Conductor::setMedium
	Set the medium this conductor carries. The only way to change it once
	the conductor exists: setProperties() keeps it.
	@param medium
*/
void Conductor::setMedium(TerminalData::Medium medium)
{
	if (m_properties.m_medium == medium) return;
	const ConductorProperties before = m_properties;
	m_properties.m_medium = medium;
		//applied again under the new medium: a conductor turned into a
		//pipe loses its wire fields
	setProperties(before);
		//and routed again when it routes itself: a pipe leaves its nozzles
		//farther
	if (!modified_path && terminal1 && terminal2) {
		generateConductorPath(terminal1 -> dockConductor(), terminal1 -> orientation(),
							  terminal2 -> dockConductor(), terminal2 -> orientation());
		calculateTextItemPosition();
	}
	update();
	storeProperties();
	emit propertiesChange();
}

/**
	@brief Conductor::placedStore
	@return the project database holding this wire's properties
	or nullptr if it is not placed in a
	project, or shares its uuid with another wire for the moment a copy
	takes to get its own
*/
projectDataBase *Conductor::placedStore() const
{
	Diagram *d = diagram();
	QETProject *project = d ? d->project() : nullptr;
	projectDataBase *store = project ? project->dataBase() : nullptr;
	return store && store->hasConductorProperties(m_uuid)
			&& store->placedConductorCount(m_uuid) == 1 ? store : nullptr;
}

/**
	@brief Conductor::storeProperties
	Write this wire's properties to its project's store, once it is placed:
	every change of a placed wire's properties comes through here.
*/
void Conductor::storeProperties()
{
	Diagram *d = diagram();
	QETProject *project = d ? d->project() : nullptr;
	if (project && project->dataBase() && project->dataBase()->hasConductorProperties(m_uuid))
		project->dataBase()->conductorPropertiesStored(this, m_properties);
}

/**
	@brief Conductor::ownLayout
	@return where this wire sits and how its text was placed, as toXml()
	writes them
*/
ConductorLayout Conductor::ownLayout() const
{
	ConductorLayout layout;
	layout.x = pos().x();
	layout.y = pos().y();
	layout.freeze_label = m_freeze_label;
	if (m_text_item) {
		if (m_text_item->wasMovedByUser())
			layout.text_pos = m_text_item->pos();
		if (m_text_item->wasRotatedByUser())
			layout.text_rotation = m_text_item->rotation();
	}
	return layout;
}

/**
	@brief Conductor::storeLayout
	Write this wire's layout to its project's store, once it is placed.
*/
void Conductor::storeLayout()
{
	Diagram *d = diagram();
	QETProject *project = d ? d->project() : nullptr;
	projectDataBase *store = project ? project->dataBase() : nullptr;
	if (store && store->hasConductorProperties(m_uuid) && store->placedConductorCount(m_uuid) == 1)
		store->storeConductorLayout(m_uuid, ownLayout());
}

/**
	@brief Conductor::ownPath
	@return the segments toXml() writes for this wire, with the lengths as
	written there (six significant digits); none if the user did not reshape it
*/
ConductorPath Conductor::ownPath() const
{
	ConductorPath path;
	if (!modified_path) return path;
	for (const ConductorSegment *segment = segments ; segment ; segment = segment -> hasNextSegment() ? segment -> nextSegment() : nullptr)
		path.append({segment -> isHorizontal(), savedSegmentLength(segment -> length())});
	return path;
}

/**
	@brief Conductor::storePath
	Write this wire's segments to its project's store, once it is placed.
	Called wherever the segments are rebuilt (segmentsToPath()) and wherever
	the wire stops being a reshaped one without that.
*/
void Conductor::storePath()
{
	Diagram *d = diagram();
	QETProject *project = d ? d->project() : nullptr;
	projectDataBase *store = project ? project->dataBase() : nullptr;
	if (store && store->hasConductorProperties(m_uuid) && store->placedConductorCount(m_uuid) == 1)
		store->storeConductorPath(m_uuid, ownPath());
}

/**
	@brief Conductor::newUuid
	Make a new uuid for this conductor; its stored properties and its row
	follow it.
*/
void Conductor::newUuid()
{
	setUuid(QUuid::createUuid());
}

/**
	@brief Conductor::setUuid
	@param uuid : the uuid of this conductor, saved from now on
*/
void Conductor::setUuid(const QUuid &uuid)
{
	const QUuid old_uuid = m_uuid;
	m_uuid = uuid;
	m_persist_uuid = true;
	uuidChanged(old_uuid);
}

/**
	@brief Conductor::uuidChanged
	Tell the project's store this placed wire's uuid changed from
	@p old_uuid (a paste renews it, a file gives it).
*/
void Conductor::uuidChanged(const QUuid &old_uuid)
{
	Diagram *d = diagram();
	QETProject *project = d ? d->project() : nullptr;
	if (project && project->dataBase())
		project->dataBase()->conductorUuidChanged(this, old_uuid);
}

/**
	@brief Conductor::setCableReference
	Record the cable core this conductor shows, together with the label
	that assignment generates. Called only by CableManager, which works
	the values out from the cable itself: the user never types them.
	@param label the generated cable field, e.g. "15W1:br", empty to
	detach the conductor from its cable
	@param cable_uuid the cable this conductor is a core of
	@param slot which core of that cable it shows, -1 when none
*/
void Conductor::setCableReference(const QString &label,
								  const QUuid &cable_uuid,
								  int slot)
{
		//A cable is made of wires: a pipe or a signal is no core
	if (m_properties.m_medium != TerminalData::Electrical) return;
	if (m_properties.m_cable == label
		&& m_properties.m_cable_uuid == cable_uuid
		&& m_properties.m_cable_slot == slot) {
		return;
	}

	m_properties.m_cable = label;
	m_properties.m_cable_uuid = cable_uuid;
	m_properties.m_cable_slot = slot;
		//Becoming a core: what the three fields showed until now was
		//the wire's own value -- the cable says what its cores are from
		//now on, so the fields show nothing rather than a wrong value
	if (!cable_uuid.isNull()) {
		m_properties.m_wire_color.clear();
		m_properties.m_wire_section.clear();
		m_properties.m_conductor_type.clear();
	}
	storeProperties();
		//Becoming a core of a cable, or ceasing to be one, changes what
		//the terminal strip above this wire draws of it
	if (diagram()) diagram()->refreshTerminalStrips();
	emit propertiesChange();
}

/**
	@return true si le conducteur est mis en evidence
*/
Conductor::Highlight Conductor::highlight() const
{
	return(must_highlight_);
}

/**
	@param hl true pour mettre le conducteur en evidence, false sinon
*/
void Conductor::setHighlighted(Conductor::Highlight hl) {
	must_highlight_ = hl;
	update();
}

/**
	@brief Conductor::displayedTextChanged
	Update the properties (text) of this conductor and other conductors
	at the same potential of this conductor.
*/
void Conductor::displayedTextChanged()
{
	QVariant old_value, new_value;
	old_value.setValue(m_properties);
	ConductorProperties new_properties(m_properties);
	new_properties.m_formula = m_text_item->toPlainText();
	new_properties.text = m_text_item->toPlainText();
	new_value.setValue(new_properties);


	QUndoCommand *undo = new QUndoCommand(tr("Edit conductor properties", "undo caption"));
	new QPropertyUndoCommand(this, "properties", old_value, new_value, undo);

	if (!relatedPotentialConductors().isEmpty())
	{
		undo->setText(tr("Edit the properties of several conductors", "undo caption"));

		foreach (Conductor *potential_conductor, relatedPotentialConductors())
		{
				//A core of a cable keeps what its cable says: the text
				//entered on one side of a terminal does not run over it
				//and write the cable's own field away
			if (!potential_conductor->properties().m_cable_uuid.isNull()) {
				continue;
			}
			old_value.setValue(potential_conductor->properties());
			ConductorProperties new_properties = potential_conductor->properties();
			new_properties.m_formula = m_text_item->toPlainText();
			new_properties.text = m_text_item->toPlainText();
			new_value.setValue(new_properties);
			new QPropertyUndoCommand (potential_conductor, "properties", old_value, new_value, undo);
		}
	}

	diagram()->undoStack().push(undo);
}


/**
	@brief Conductor::relatedPotentialConductors
	Return all conductors at the same potential of this conductor,
	this conductor isn't part of the returned QSet.
	@param all_diagram : if true search in all diagram of the project,
	false search only in the parent diagram of this conductor
	@param t_list : a list of terminal already found for this potential.
	@param ignore_function_separation : if true, a terminal element whose
	function separation is enabled does not stop the search, and the
	conductors of both its sides are returned as one potential. Only
	functionSeparatedConductors() passes true.
	@param stop_at_report_links : if true, a folio report does not join
	its two wires: a name written over the potential stays on the folio
	it was written on (QETProject::chainsByWireFunction)
	A potential is electrical: a conductor of another medium (a pipe, a
	signal) has none, and the search never goes through one, even
	when it shares a terminal with an electrical wire. Every use of the
	potential -- numbering, properties shared over a potential, cables,
	bridges, search and replace... -- leaves such a conductor alone.
	@sa functionSeparatedConductors
	@return  a QSet of conductor at the same potential.
*/
QSet<Conductor *> Conductor::relatedPotentialConductors(const bool all_diagram,
							QList <Terminal *> *t_list,
							const bool ignore_function_separation,
							const bool stop_at_report_links)
{
	if (medium() != TerminalData::Electrical) {
		return {};
	}

	bool declar_t_list = false;
	if (t_list == nullptr)
	{
		declar_t_list = true;
		t_list = new QList <Terminal *>;
	}

	QSet <Conductor *> other_conductors;
	QList <Terminal *> this_terminal;
	this_terminal << terminal1 << terminal2;

		// Return all conductors of terminal 1 and 2
	for (Terminal *terminal : this_terminal)
	{
		if (!t_list->contains(terminal))
		{
			t_list->append(terminal);
			QList <Conductor *> other_conductors_list_t = terminal->conductors();

				//Get the other terminals of the parent element of @terminal, who share the same potential
				//This is use for element type "folio report", "terminal element" and "junction"
			for (Terminal *t : relatedPotentialTerminal(terminal, all_diagram,
								ignore_function_separation,
								stop_at_report_links))
			{
				if (!t_list->contains(t))
				{
					t_list -> append(t);
					other_conductors_list_t += t->conductors();
				}
			}

			other_conductors_list_t.removeAll(this);
				//Only electrical conductors have a potential (see above)
			other_conductors_list_t.removeIf([](Conductor *c) {
				return c->medium() != TerminalData::Electrical;
			});
				//Get the conductors at the same potential for each conductors of other_conductors_list_t
			for (Conductor *c : other_conductors_list_t) {
				other_conductors += c->relatedPotentialConductors(all_diagram, t_list,
								  ignore_function_separation,
								  stop_at_report_links);
			}

			other_conductors += QSet<Conductor*>(other_conductors_list_t.begin(),other_conductors_list_t.end());
		}
	}

	other_conductors.remove(this);

	if (declar_t_list) delete t_list;
	return(other_conductors);
}

/**
	@brief Conductor::functionSeparatedConductors
	Return the conductors that belong to this conductor's potential only
	by crossing a function separated terminal element -- a terminal
	element whose function separation checkbox (element information
	potential_isolating) is enabled, the checkbox the user ticks to stop
	sharing a terminal's properties with the other side of the terminal.

	Those conductors are deliberately not part of
	relatedPotentialConductors(): no property may be copied over them
	by default. Only what the node itself describes crosses that
	separation: the voltage / protocol and the drawing of the wire --
	colour, line style and width (@see
	ConductorProperties::applyNodePropertiesChange). Callers that push
	those fields over the whole potential use this method to reach the
	other side.

	It is simply the potential computed with the separations ignored,
	minus the potential that respects them.
	@param all_diagram : if true search in all diagram of the project,
	false search only in the parent diagram of this conductor
	@return a QSet of conductor, this conductor excluded
*/
QSet<Conductor *> Conductor::functionSeparatedConductors(const bool all_diagram)
{
	QSet<Conductor *> separated = relatedPotentialConductors(all_diagram, nullptr, true);
	separated.subtract(relatedPotentialConductors(all_diagram));
	separated.remove(this);

	return separated;
}

/**
	@brief Conductor::applySeparatedNodeProperties
	Copy into @p properties the fields a function separated terminal
	element lets through, taken from the conductors the other side of
	the separation holds (@see functionSeparatedConductors): the voltage
	/ protocol and the drawing of the wire, as if the terminal had no
	function separation. A field those conductors do not agree on is
	left as it is in @p properties.

	A new conductor applies this when its own side of the separation
	holds no wire to take anything from: what its own side says wins
	over the other side, however far that side reaches, and only the
	fields this side leaves empty come from across the separation.
	@sa ConductorProperties::applyAgreedNodeProperties
*/
void Conductor::applySeparatedNodeProperties(ConductorProperties &properties)
{
	QList<ConductorProperties> separated;
	for (Conductor *c : functionSeparatedConductors()) {
		separated << c->properties();
	}
	properties.applyAgreedNodeProperties(separated);
}

/**
	@brief Conductor::applySeparatedNodePropertiesIfEmpty
	The rule for a wire being created, in one call: what a function
	separated terminal element lets through is taken from the other side
	of the separation (@see applySeparatedNodeProperties), but only when
	this wire's own side holds no wire to take anything from: what its
	own side says wins over the other side, however far that side
	reaches.

	Only wires being created are meant: a pasted wire keeping the
	properties it was copied with, or a wire being renumbered or split,
	has something of its own and is left alone. That is why this is not
	part of ConductorAutoNumerotation::numerate(), which runs on such
	wires too.

	A caller creating several wires in one gesture that join one
	potential (ConductorCreator) calls applySeparatedNodeProperties()
	with its own idea of "nothing of its own" instead: the wires of the
	gesture would see each other here.
*/
void Conductor::applySeparatedNodePropertiesIfEmpty()
{
	if (!relatedPotentialConductors().isEmpty()) {
		return;
	}
	ConductorProperties properties = this->properties();
	applySeparatedNodeProperties(properties);
	if (properties != this->properties()) {
		setProperties(properties);
	}
}

/**
	@brief Conductor::diagramEditor
	@return The parent diagram editor or nullptr;
*/
QETDiagramEditor* Conductor::diagramEditor() const
{
	if (!diagram())                     return nullptr;
	if (diagram() -> views().isEmpty()) return nullptr;

	QWidget *w = const_cast<QGraphicsView *>(diagram() -> views().at(0));
	while (w -> parentWidget() && !w -> isWindow()) {
		w = w -> parentWidget();
	}
	return(qobject_cast<QETDiagramEditor *>(w));
}

/**
	@brief Conductor::editProperty
*/
void Conductor::editProperty()
{
		//The dialog of a wire is all about wires
	if (medium() == TerminalData::Pipe) {
		PipePropertiesWidget::editInDialog(this, diagramEditor());
		return;
	}
	if (medium() == TerminalData::Signal) {
		SignalPropertiesWidget::editInDialog(this, diagramEditor());
		return;
	}
	ConductorPropertiesDialog::PropertiesDialog(this, diagramEditor());
}

void Conductor::setSequenceNum(const autonum::sequentialNumbers& sn)
{
	replaceSequenceNum(sn);
	refreshText();
}

void Conductor::replaceSequenceNum(const autonum::sequentialNumbers& sn)
{
	m_autoNum_seq = sn;
	storeSequence();
}

/**
	@brief Conductor::storeSequence
	Write this wire's sequential numbers to its project's store, once it
	is placed.
*/
void Conductor::storeSequence()
{
	Diagram *d = diagram();
	QETProject *project = d ? d->project() : nullptr;
	projectDataBase *store = project ? project->dataBase() : nullptr;
	if (store && store->hasConductorProperties(m_uuid) && store->placedConductorCount(m_uuid) == 1)
		store->storeSequence(m_uuid, m_autoNum_seq);
}

/**
	@brief Conductor::setUpConnectionForFormula
	setup connection according to the variable of formula
	@param old_formula
	@param new_formula
*/
void Conductor::setUpConnectionForFormula(QString old_formula, QString new_formula)
{
	Diagram *diagram_ {diagram()};
	if (!diagram_) {
		diagram_ = terminal1->diagram();
	}
	if (diagram_)
	{
			//Because the variable %F is a reference to another text which can contain variables,
			//we must replace %F by the real text, to check if the real text contains the variable %id
		if (old_formula.contains("%F"))
			old_formula.replace("%F", diagram_->border_and_titleblock.folio());

		if (old_formula.contains("%id"))
			disconnect(diagram_->project(), &QETProject::projectDiagramsOrderChanged, this, &Conductor::refreshText);

			//Label is frozen, so we don't update it.
		if (m_freeze_label == true)
			return;

		if (new_formula.contains("%F"))
			new_formula.replace("%F", diagram_->border_and_titleblock.folio());

		if (new_formula.contains("%id"))
			connect(diagram_->project(), &QETProject::projectDiagramsOrderChanged, this, &Conductor::refreshText);
	}
}

/**
	@param a point
	@param b point
	@param c point
	@return true si le point a est contenu dans le rectangle delimite par les points b et c
*/
bool isContained(const QPointF &a, const QPointF &b, const QPointF &c) {
	return(
		isBetween(a.x(), b.x(), c.x()) &&
		isBetween(a.y(), b.y(), c.y())
	);
}

/**
	@brief Conductor::drawnColor
	@return the colour the conductor is drawn in: that of a pipe when it has
	one, that of the status of works of a signal line or of the signal lines
	of the project, its potential type's when the project draws from them,
	else its own
*/
QColor Conductor::drawnColor() const
{
	const QColor pipe_color = pipeColor();
	if (pipe_color.isValid()) {
		return pipe_color;
	}
	if (m_properties.m_medium == TerminalData::Signal) {
			//The colour the project gives the status of works of the line,
			//else the one it gives the signal lines, when it has one
		const Diagram *parent_diagram = diagram();
		const QETProject *project = parent_diagram ? parent_diagram->project() : nullptr;
		const PidCodification codification = project ? project->pidCodification() : PidCodification();
		const QColor status = codification.status_colors.value(m_properties.m_signal.status);
		if (status.isValid()) {
			return status;
		}
		const QColor family = codification.object_colors.value(QStringLiteral("signal"));
		if (family.isValid()) {
			return family;
		}
	}
	PotentialType type;
	return potentialTypeFormat(&type) ? type.color : m_properties.color;
}

/**
	@brief Conductor::pipeColor
	@return the colour of a pipe: that of its status of works, else its own,
	else that of its fluid, else that of the family of the pipes, from the
	project; an invalid one when it has none, or for a wire
	@param source : where the colour comes from, written when given
*/
QColor Conductor::pipeColor(PipeColorSource *source) const
{
	auto from = [source](PipeColorSource s, const QColor &color) {
		if (source) {
			*source = color.isValid() ? s : PipeColorSource::None;
		}
		return color;
	};
	if (m_properties.m_medium != TerminalData::Pipe) {
		return from(PipeColorSource::None, QColor());
	}
	const Diagram *parent_diagram = diagram();
	const QETProject *project = parent_diagram ? parent_diagram->project() : nullptr;
	const PidCodification codification = project ? project->pidCodification() : PidCodification();
	const QColor status = codification.status_colors.value(m_properties.m_pipe.status);
	if (status.isValid()) {
		return from(PipeColorSource::Status, status);
	}
	if (m_properties.m_pipe.color.isValid()) {
		return from(PipeColorSource::Manual, m_properties.m_pipe.color);
	}
	const QColor fluid = codification.fluidColor(m_properties.m_pipe.fluid);
	if (fluid.isValid()) {
		return from(PipeColorSource::Fluid, fluid);
	}
	return from(PipeColorSource::Family,
				codification.object_colors.value(QStringLiteral("pipe")));
}

/**
	@brief Conductor::updateFittings
	See the declaration. Called by the folio and by setProperties(), not by
	the terminals: those also lose their conductors while their element is
	being destroyed.
*/
void Conductor::updateFittings() const
{
	if (m_properties.m_medium != TerminalData::Pipe) {
		return;
	}
	for (Terminal *terminal : {terminal1, terminal2})
	{
		Element *element = terminal ? terminal->parentElement() : nullptr;
		if (element && element->takesPipeInk()) {
			element->updateInk();
		}
			//A nozzle follows the DN of its pipe, or marks a difference;
			//so do those of its run and the nozzles placed against them.
			//A pipe which comes or goes ends or starts a contact.
		if (terminal) {
			terminal->refreshContact();
			terminal->refreshSizes();
		}
			//The pipe on the other side of a spec break goes into it, or
			//no more (lineIntoSpecBreak())
		if (element && element->isLineMark() && terminal) {
			for (Terminal *sibling : terminal->runSiblings()) {
				for (Conductor *pipe : sibling->conductors()) {
					pipe->update();
				}
			}
		}
			//The pipes beyond draw their insulation stubs there, or no
			//more, by the insulation of this one (step 7d)
		if (terminal) {
			for (Conductor *pipe : pipesBeyond(terminal)) {
				pipe->update();
			}
				//and their jacket stubs, beyond an insulation limit too
				//(step 8r-1)
			for (Conductor *pipe : pipesBeyond(terminal, nullptr, true)) {
				pipe->update();
			}
		}
			//An off-page connector and its twin compare their pipes (8c-3)
		PidOffPage::refresh(element);
	}
}

/**
	@brief Conductor::inlineRun
	@return this pipe and those it continues through the runs of the symbols
	(Element::pipeRun()): a valve, an inline instrument, the three nozzles of
	a tee, which have no size of their own (Terminal::followsPipe()).
	A nozzle of a run placed against the nozzle of another symbol, without a
	pipe, continues through the run of that one (Terminal::contactNozzle()).
	@param reach : LineReach::Size, the pipes the size of this one passes
	to: reducers and equipment stop it, a spec break (Element::isSpecBreak())
	does not; through a tee or a cross, straight on only, as the line number:
	the branch of a tee has a size of its own (step 6b-1c, which revises
	5l-1d). LineReach::Spec, the pipes its material, piping class and
	nominal pressure pass to: on through reducers too, from one side to the
	other (Terminal::reducerOtherSide()); spec breaks and equipment stop it.
	LineReach::Fluid, the pipes its fluid passes to: as the spec, on through
	spec breaks too (step 5l-1z); equipment stops it, and so does a nozzle
	left out of the run of its symbol (the reagent of an injection lance).
	LineReach::LineNumber, the pipes its line number passes to, and its
	kind of line (step 6b-1c): as the
	fluid, but through a tee or a cross (a run of three nozzles or more)
	only straight on, from a nozzle to the one opposite, never into nor out
	of a branch (step 5l-1k). LineReach::Flow, the pipes its flow passes
	to: as the fluid, but a tee or a cross stops it on every side, the flow
	parting there, and so does any junction (Element::isJunction()): beyond
	an injection point the flow is no more the same (step 6b-1c).
	Pipes only: a signal line or a wire never goes on.
*/
QList<Conductor *> Conductor::inlineRun(LineReach reach)
{
	return runFrom({terminal1, terminal2}, reach);
}

/**
	@brief Conductor::unsizedRun
	See the declaration
*/
QList<Conductor *> Conductor::unsizedRun()
{
	if (m_properties.m_medium != TerminalData::Pipe || m_properties.m_pipe.hasSize()) {
		return {};
	}
	return runFrom({terminal1, terminal2}, LineReach::Size, true);
}

/**
	@brief Conductor::runFrom
	See the declaration: the walk of inlineRun(), breadth first, so that the
	pipes come nearest first (takeLineSpec() relies on it)
*/
QList<Conductor *> Conductor::runFrom(const QList<Terminal *> &starts, LineReach reach,
									  bool unsized_only)
{
	QList<Conductor *> run{this};
	QList<Terminal *> seen;
	QList<Terminal *> todo = starts;
	for (int i = 0 ; i < todo.size() ; ++i)
	{
		Terminal *terminal = todo.at(i);
		if (!terminal || seen.contains(terminal)) {
			continue;
		}
		seen << terminal;
		QList<Terminal *> siblings = terminal->runSiblings();
		const Element *element = terminal->parentElement();
			//A tee, a cross (three nozzles or more on the run): the flow
			//parts there, on every side. Any junction stops it: beyond an
			//injection point it is the flow of the line plus the reagent
		if (reach == LineReach::Flow
			&& (siblings.size() >= 2 || (element && element->isJunction()))) {
			continue;
		}
			//The insulation stops at its limit, on both sides; it goes
			//through anything else on the line, as the fluid does. So does
			//the jacket, at its own limit (step 8r-1)
		if (reach == LineReach::Insulation && element && element->isInsulationLimit()) {
			continue;
		}
		if (reach == LineReach::Jacket && element && element->isJacketLimit()) {
			continue;
		}
			//Through a tee or a cross the line goes on straight, with its
			//size and its number: a branch is another line
		if (reach == LineReach::Size || reach == LineReach::LineNumber) {
			siblings = terminal->lineSiblings();
		}
		if (reach != LineReach::Size)
		{
				//The material of the line changes there, not its fluid
			if (reach == LineReach::Spec && element && element->isSpecBreak()) {
				continue;
			}
				//Only its size changes there
			if (Terminal *other = terminal->reducerOtherSide()) {
				if (!siblings.contains(other)) {
					siblings << other;
				}
			}
		}
		for (Terminal *sibling : std::as_const(siblings))
		{
			if (seen.contains(sibling)) {
				continue;
			}
			seen << sibling;
			for (Conductor *next : sibling->conductors()) {
					//A pipe which has a size stops the walk of unsizedRun()
				if (unsized_only && next->properties().m_pipe.hasSize()) {
					continue;
				}
				if (next->medium() == TerminalData::Pipe && !run.contains(next)) {
					run << next;
					todo << next->terminal1 << next->terminal2;
				}
			}
				//Against another symbol: on through the run of that one
			if (Terminal *against = sibling->contactNozzle()) {
				todo << against;
			}
		}
	}
	return run;
}

/**
	@brief Conductor::potentialTypeFormat
	The format the project gives the potential of this conductor.
	@param type : receives the type when there is one
	@return true when the project draws its conductors from potential
	types, and this conductor's type is one of them: its own, or, in the
	mode that takes types from where the potential starts and when it has
	none of its own, the one its potential takes from a source terminal.
	False leaves the conductor drawn from its own stored style.
*/
bool Conductor::potentialTypeFormat(PotentialType *type) const
{
		//A pipe or a signal line has no potential: its own data say how
		//it is drawn
	if (m_properties.m_medium != TerminalData::Electrical)
		return false;
	const Diagram *parent_diagram = diagram();
	QETProject *project = parent_diagram ? parent_diagram->project() : nullptr;
	if (!project || !project->potentialTypes().isEnabled())
		return false;
	if (!m_properties.m_potential_type.isEmpty())
		return project->potentialTypes().find(m_properties.m_potential_type, type);
	if (project->potentialTypes().mode() != PotentialTypes::Mode::AtSource)
		return false;
	return project->potentialTypes().find(project->derivedPotentialType(uuid()), type);
}

/**
	@brief Conductor::paintedPath
	@return the path drawn for this conductor: path(), with a hop at each
	place it crosses another conductor when the project draws hops
	(issue #436, QETProject::wireHops()). Cached until a conductor changes
	shape, moves, or enters or leaves a folio, so a repaint costs nothing.
	A moved conductor still repaints the hops of the others: a crossing
	lies inside the bounding rect of both conductors, which Qt repaints
	when either moves.
*/
QPainterPath Conductor::paintedPath() const
{
	const Diagram *parent_diagram = diagram();
	if (!parent_diagram || !parent_diagram->project()) {
		return path();
	}
	const WireHops::Mode mode = parent_diagram->project()->wireHops();
	if (mode == WireHops::Mode::None) {
		m_hops_cuts.clear();
		m_hops_crossings.clear();
		return path();
	}
	const WireHops::Shape shape = parent_diagram->project()->wireHopsShape();
	const int mode_key = int(mode) * 2 + int(shape);
	if (m_hops_generation == s_conductor_geometry_generation
		&& m_hops_mode == mode_key) {
		return m_hops_path;
	}

	if (s_hop_candidates_diagram != parent_diagram
		|| s_hop_candidates_generation != s_conductor_geometry_generation)
	{
		s_hop_candidates.clear();
		const QList<Conductor *> conductors = parent_diagram->conductors();
		for (const Conductor *conductor : conductors)
		{
			HopCandidate candidate;
			candidate.conductor = conductor;
			candidate.rect = conductor->mapRectToScene(conductor->path().boundingRect());
			for (const QPointF &point : conductor->segmentsToPoints()) {
				candidate.points.append(conductor->mapToScene(point));
			}
			PotentialType type;
			candidate.width = conductor->drawnWidth(conductor->potentialTypeFormat(&type)
													? &type : nullptr);
			s_hop_candidates.append(candidate);
		}
		s_hop_candidates_diagram = parent_diagram;
		s_hop_candidates_generation = s_conductor_geometry_generation;
	}

	m_hops_path = path();
	m_hops_cuts.clear();
	m_hops_crossings.clear();
	m_hops_generation = s_conductor_geometry_generation;
	m_hops_mode = mode_key;

	const QList<QPointF> points = segmentsToPoints();
	if (points.size() < 2) {
		return m_hops_path;
	}
	QVector<QPointF> wire;
	for (const QPointF &point : points) {
		wire.append(mapToScene(point));
	}

		//A conductor crossing this one has path bounds overlapping its own;
		//adjusted by 1 because a straight conductor's bounds have no width
	const QRectF rect = mapRectToScene(path().boundingRect()).adjusted(-1, -1, 1, 1);
	QList<QVector<QPointF>> others;
	QList<WireHops::Line> other_lines;
	for (const HopCandidate &candidate : std::as_const(s_hop_candidates))
	{
		if (candidate.conductor != this
			&& rect.intersects(candidate.rect.adjusted(-1, -1, 1, 1))) {
			others.append(candidate.points);
			other_lines.append({candidate.points, candidate.width});
		}
	}
		//Where the marks of a signal line keep clear of
	for (const QPointF &crossing : WireHops::crossingPoints(wire, others)) {
		m_hops_crossings.append(mapFromScene(crossing));
	}

		//Where this line gives way: a gap, or the thinner line giving way
		//(P&ID, 7c). A dashed signal line has flat ends, any other line
		//square ones
	PotentialType type;
	const qreal width = drawnWidth(potentialTypeFormat(&type) ? &type : nullptr);
	const qreal cap = signalMark() == SignalMark::Dashes ? 0 : width / 2;
	for (const WireHops::Interruption &cut :
		 WireHops::interruptions({wire, width}, other_lines, mode, shape, cap)) {
		m_hops_cuts.append({mapFromScene(cut.from), mapFromScene(cut.to), cut.arc});
	}

		//Upstream's hops (#436) drawn exactly as they were: an arc, chosen
		//by the way the wire runs
	if (shape == WireHops::Shape::Arc
		&& (mode == WireHops::Mode::Horizontal || mode == WireHops::Mode::Vertical))
	{
		const QList<QPointF> scene_hops = WireHops::crossings(wire, others, mode);
		if (!scene_hops.isEmpty())
		{
			QList<QPointF> hops;
			for (const QPointF &hop : scene_hops) {
				hops.append(mapFromScene(hop));
			}
			m_hops_path = WireHops::path(QVector<QPointF>(points.begin(), points.end()), hops, mode);
		}
		return m_hops_path;
	}
	if (!m_hops_cuts.isEmpty()) {
		m_hops_path = WireHops::path(QVector<QPointF>(points.begin(), points.end()), m_hops_cuts);
	}
	return m_hops_path;
}

/**
	@brief Conductor::dashesAtCrossings
	See the declaration
*/
QPainterPath Conductor::dashesAtCrossings(const QPen &pen) const
{
	if (pen.style() == Qt::SolidLine || pen.style() == Qt::NoPen) {
		return QPainterPath();
	}
	const Diagram *parent_diagram = diagram();
	const QETProject *project = parent_diagram ? parent_diagram->project() : nullptr;
	if (!project || (project->wireHops() != WireHops::Mode::Thinner
					 && project->wireHopsShape() != WireHops::Shape::Gap)) {
		return QPainterPath();
	}
	paintedPath();
	if (m_hops_cuts.isEmpty() && m_hops_crossings.isEmpty()) {
		return QPainterPath();
	}
		//A dash of the pattern, which counts in widths of the pen
	const QVector<qreal> pattern = pen.dashPattern();
	const qreal unit = pen.isCosmetic() ? 1.0 : qMax(pen.widthF(), 1.0);
	const qreal dash = pattern.isEmpty() ? SignalMark::dashLength : pattern.first() * unit;
	const QList<QPointF> points = segmentsToPoints();
	return WireHops::solidAtCrossings(QVector<QPointF>(points.begin(), points.end()),
									  m_hops_cuts, m_hops_crossings, dash);
}

/**
	@brief Conductor::drawDashesAtCrossings
	See the declaration
*/
void Conductor::drawDashesAtCrossings(QPainter *painter, const QPen &pen) const
{
	const QPainterPath solid = dashesAtCrossings(pen);
	if (solid.isEmpty()) {
		return;
	}
	QPen plain = pen;
	plain.setStyle(Qt::SolidLine);
	painter->save();
	painter->setPen(plain);
	painter->setBrush(Qt::NoBrush);
	painter->drawPath(solid);
	painter->restore();
}

/**
	@brief Conductor::brokenSignalLine
	See the declaration
*/
QPainterPath Conductor::brokenSignalLine(SignalMark::Mark mark) const
{
	const QList<QPointF> points = segmentsToPoints();
	paintedPath();
	if (m_hops_cuts.isEmpty() && m_hops_crossings.isEmpty()) {
		return SignalMark::brokenLine(mark, points);
	}
		//The holes of the marks standing in the line, kept clear of the
		//crossings, then where the line gives way; on the straight runs,
		//which the holes and the crossings both lie on
	const QList<QPointF> runs = SignalMark::straightRuns(points);
	QList<WireHops::Interruption> cuts = m_hops_cuts;
	const qreal half_gap = SignalMark::lineGap(mark) / 2.0;
	for (const SignalMark::Place &place :
		 SignalMark::places(points, SignalMark::length(mark), SignalMark::pitch, m_hops_crossings)) {
		const QLineF run(runs.at(place.run), runs.at(place.run + 1));
		const QPointF step = (run.p2() - run.p1()) / run.length() * half_gap;
		cuts.append({place.at - step, place.at + step, false});
	}
	return WireHops::path(QVector<QPointF>(runs.begin(), runs.end()), cuts);
}

/**
	@return true if \a scene_point, where this conductor bends with
	\a bend_type, lies on one of the segments of \a c and \a c does not
	bend the same way at the same point.
*/
bool Conductor::bendMakesJunction(const Conductor *c, const QPointF &scene_point,
								  Qt::Corner bend_type) const
{
		// exprime le point dans les coordonnees de l'autre conducteur
	const QPointF conductor_point = c -> mapFromScene(scene_point);
	bool on_conductor = false;
	for (ConductorSegment *segment : c -> segmentsList())
	{
		if (isContained(conductor_point, segment -> firstPoint(), segment -> secondPoint()))
		{
			on_conductor = true;
			break;
		}
	}
	if (!on_conductor)
		return false;

		// ce point commun ne doit pas etre une bifurcation identique a celle-ci
	for (const ConductorBend &cb : c -> bends())
	{
		if (cb.first == conductor_point && cb.second == bend_type)
			return false;
	}
	return true;
}

/**
	@return the positions, in this conductor's coordinates, of the junction
	dots this conductor draws: each of its bends that lies on another
	conductor of the same potential, unless that conductor bends the same
	way at the same point.
*/
QList<QPointF> Conductor::junctions() const
{
	QList<QPointF> junctions_list;

	QList<ConductorBend> bends_list = bends();
	if (bends_list.isEmpty()) {
		return(junctions_list);
	}

		// Every conductor of the potential on this folio, not only those on
		// this conductor's own terminals: a bend can lie on a conductor it
		// shares no terminal with, e.g. when the horizontal parts of a chain
		// of conductors are dragged over each other (issue #1280).
	const QList<Conductor *> other_conductors =
			const_cast<Conductor *>(this)->relatedPotentialConductors(false).values();
	if (other_conductors.isEmpty()) {
		return(junctions_list);
	}

	QList<QPointF> points = segmentsToPoints();
	for (int i = 1 ; i < (points.size() -1) ; ++ i) {
		QPointF point = points.at(i);

		// determine si le point est une bifurcation ou non
		bool is_bend = false;
		Qt::Corner current_bend_type = Qt::TopLeftCorner;
		foreach(ConductorBend cb, bends_list)
		{
			if (cb.first == point)
			{
				is_bend = true;
				current_bend_type = cb.second;
				break;
			}
		}
		// si le point n'est pas une bifurcation, il ne peut etre une jonction (enfin pas au niveau de ce conducteur)
		if (!is_bend) continue;

		QPointF scene_point = mapToScene(point);

		bool is_junction = false;
		for (Conductor *c : other_conductors)
		{
			if (bendMakesJunction(c, scene_point, current_bend_type))
			{
				is_junction = true;
				break;
			}
		}
		if (is_junction) junctions_list << point;
	}
	return(junctions_list);
}

/**
	@return la liste des bifurcations de ce conducteur ; ConductorBend est un
	typedef pour une QPair\<QPointF, Qt::Corner\>. Le point indique la position
	(en coordonnees locales) de la bifurcation tandis que le Corner indique le
	type de bifurcation.
*/
QList<ConductorBend> Conductor::bends() const
{
	QList<ConductorBend> points;
	if (!segments) return(points);

	// recupere la liste des segments de taille non nulle
	QList<ConductorSegment *> visible_segments;
	ConductorSegment *segment = segments;
	while (segment -> hasNextSegment()) {
		if (!segment -> isPoint()) visible_segments << segment;
		segment = segment -> nextSegment();
	}
	if (!segment  -> isPoint()) visible_segments << segment;

	ConductorSegment *next_segment;
	for (int i = 0 ; i < visible_segments.count() -1 ; ++ i) {
		segment = visible_segments[i];
		next_segment = visible_segments[i + 1];
		if (!segment -> isPoint() && !next_segment -> isPoint()) {
			// si les deux segments ne sont pas dans le meme sens, on a une bifurcation
			if (next_segment -> type() != segment -> type()) {
				Qt::Corner bend_type;
				qreal sl = segment -> length();
				qreal nsl = next_segment -> length();

				if (segment -> isHorizontal()) {
					if (sl < 0 && nsl < 0) {
						bend_type = Qt::BottomLeftCorner;
					} else if (sl < 0 && nsl > 0) {
						bend_type = Qt::TopLeftCorner;
					} else if (sl > 0 && nsl < 0) {
						bend_type = Qt::BottomRightCorner;
					} else {
						bend_type = Qt::TopRightCorner;
					}
				} else {
					if (sl < 0 && nsl < 0) {
						bend_type = Qt::TopRightCorner;
					} else if (sl < 0 && nsl > 0) {
						bend_type = Qt::TopLeftCorner;
					} else if (sl > 0 && nsl < 0) {
						bend_type = Qt::BottomRightCorner;
					} else {
						bend_type = Qt::BottomLeftCorner;
					}
				}
				points << qMakePair(segment -> secondPoint(), bend_type);
			}
		}
	}
	return(points);
}

/**
	@param start Point de depart
	@param end Point d'arrivee
	@return le coin vers lequel se dirige le trajet de start vers end
*/
Qt::Corner Conductor::movementType(const QPointF &start, const QPointF &end) {
	Qt::Corner result = Qt::BottomRightCorner;
	if (start.x() <= end.x()) {
		result = start.y() <= end.y() ? Qt::BottomRightCorner : Qt::TopRightCorner;
	} else {
		result = start.y() <= end.y() ? Qt::BottomLeftCorner : Qt::TopLeftCorner;
	}
	return(result);
}

/// @return le type de trajet actuel de ce conducteur
Qt::Corner Conductor::currentPathType() const
{
	return(movementType(terminal1 -> dockConductor(), terminal2 -> dockConductor()));
}

/// @return les profils de ce conducteur
ConductorProfilesGroup Conductor::profiles() const
{
	return(conductor_profiles);
}

/**
	@brief Conductor::setProfiles
	@param cpg : the new profils of conductor
*/
void Conductor::setProfiles(const ConductorProfilesGroup &cpg) {
	conductor_profiles = cpg;
	if (conductor_profiles[currentPathType()].isNull()) {
		generateConductorPath(terminal1 -> dockConductor(), terminal1 -> orientation(), terminal2 -> dockConductor(), terminal2 -> orientation());
		modified_path = false;
		storePath();
	} else {
		updateConductorPath(terminal1 -> dockConductor(), terminal1 -> orientation(), terminal2 -> dockConductor(), terminal2 -> orientation());
		modified_path = true;
		storePath();
	}
	if (properties().type == ConductorProperties::Multi) {
		calculateTextItemPosition();
	}
}

/// Supprime les segments
void Conductor::deleteSegments()
{
	if (segments != nullptr) {
		while (segments -> hasNextSegment()) delete segments -> nextSegment();
		delete segments;
		segments = nullptr;
	}
}

/**
 * @brief Conductor::movePointIntoPolygon
 * @param point : A point located outside the polygon
 * @param polygon : The polygon in which we want to move the point
 * @return the position of the point, once brought back into the polygon,
 * or more exactly on the edge of the polygon
 */
QPointF Conductor::movePointIntoPolygon(const QPointF &point, const QPainterPath &polygon)
{
		// decomposes the polygon into lines and points
	const QList<QPolygonF> polygons = polygon.simplified().toSubpathPolygons();
	QList<QLineF> lines;
	QList<QPointF> points;

	for (QPolygonF polygon : polygons)
	{
		if (polygon.count() <= 1)
			continue;

			// lines and points are counted
		for (int i = 1 ; i < polygon.count() ; ++ i) {
			lines << QLineF(polygon.at(i - 1), polygon.at(i));
			points << polygon.at(i -1);
		}
	}

		// we make orthogonal projections of the point on the different
		// segments of the polygon, sorting them by increasing length
	QMap<qreal, QPointF> intersections;
	for (QLineF line : lines)
	{
		QPointF intersection_point;
		if (QET::orthogonalProjection(point, line, &intersection_point)) {
			intersections.insert(QLineF(intersection_point, point).length(), intersection_point);
		}
	}

	if (intersections.count())
	{
			// the shortest length for an orthogonal project is determined
		QPointF the_point = intersections[intersections.keys().first()];
		return(the_point);
	}
	else
	{
			// determines the corner of the polygon closest to the outer point
		qreal minimum_length = -1;
		int point_index = -1;
		for (int i = 0 ; i < points.count() ; ++ i)
		{
			qreal length = qAbs(QLineF(points.at(i), point).length());
			if (minimum_length < 0 || length < minimum_length) {
				minimum_length = length;
				point_index    = i;
			}
		}
			// we now know the closest corner of the text


		if (point_index == -1 ||
			point_index+1 > points.size()) {
			return QPointF(0,0);
		}

			// no orthogonal projection gave anything, we put the text on one of the corners of the polygon
		return(points.at(point_index));
	}
}

namespace {
	/// The two ends of @p conductor on the folio, smaller first, compared
	/// by x then y.
	QPair<QPointF, QPointF> conductorEnds(const Conductor *conductor)
	{
		auto less = [](const QPointF &a, const QPointF &b) {
			return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
		};
		QPointF a = conductor->terminal1 ? conductor->terminal1->dockConductor() : QPointF();
		QPointF b = conductor->terminal2 ? conductor->terminal2->dockConductor() : QPointF();
		if (less(b, a))
			std::swap(a, b);
		return qMakePair(a, b);
	}

	/// True if @p a comes before @p b in an order that is the same in every
	/// run: by where the conductor's ends are on the folio, then by uuid.
	/// Not by uuid first: a project generated again by a script gets new
	/// uuids each time, and a file with no conductor uuids gets new ones on
	/// every load, while the drawing is the same.
	bool stableConductorLess(const Conductor *a, const Conductor *b)
	{
		const auto ea = conductorEnds(a), eb = conductorEnds(b);
		const qreal ka[4] = {ea.first.x(), ea.first.y(), ea.second.x(), ea.second.y()};
		const qreal kb[4] = {eb.first.x(), eb.first.y(), eb.second.x(), eb.second.y()};
		for (int i = 0; i < 4; ++i)
			if (ka[i] != kb[i])
				return ka[i] < kb[i];
		return a->uuid() < b->uuid();
	}
}

/**
	@brief longestConductorInPotential
	@param conductor : a conductor in the potential to search
	@param all_diagram : true -> search in the whole project, false -> search only in the diagram of conductor
	@return the longest conductor in the same potential of conductor
*/
Conductor * longestConductorInPotential(Conductor *conductor, bool all_diagram) {
	Conductor *longest_conductor = conductor;
	qreal longest_length = conductor->length();
	//Search the longest conductor.
	//Conductors of equal length are common (a symmetrical layout), and the
	//set is iterated in pointer order, which changes from run to run: so a
	//tie is broken by an order that does not, or the potential's text lands
	//on a different conductor each time the same file is opened or exported.
	foreach (Conductor *c, conductor -> relatedPotentialConductors(all_diagram))
	{
		const qreal length = c->length();
		if (qAbs(length - longest_length) < 1e-6) {
			if (stableConductorLess(c, longest_conductor))
				longest_conductor = c;
		}
		else if (length > longest_length) {
			longest_conductor = c;
			longest_length = length;
		}
	}

	return longest_conductor;
}

/**
	@brief relatedConductors
	@param conductor
	@return return all conductors who share the same terminals
	of conductor given as parameter,
	except conductor itself.
*/
QList <Conductor *> relatedConductors(const Conductor *conductor) {
	QList<Conductor *> other_conductors_list = conductor -> terminal1 -> conductors();
	other_conductors_list << conductor -> terminal2->conductors();
	other_conductors_list.removeAll(const_cast<Conductor *> (conductor));
	return(other_conductors_list);
}

/**
	@brief Conductor::setFreezeLabel
	Freeze this conductor label if true
	Unfreeze this conductor label if false
	@param freeze
*/
void Conductor::setFreezeLabel(bool freeze)
{
	m_freeze_label = freeze;
	storeLayout();

	if (m_freeze_label != freeze)
	{
		m_freeze_label = freeze;
		QString f = m_properties.m_formula;
		setUpConnectionForFormula(f,f);
	}
}

/**
	@brief Conductor::setAutomatic
	An automatic wire is laid by AutoConnect, not placed: it cannot be
	selected, so a click or a selection rectangle catches only what the
	user placed.
	@param automatic
*/
void Conductor::setAutomatic(bool automatic)
{
	m_automatic = automatic;
	setFlag(QGraphicsItem::ItemIsSelectable, !automatic);
	if (automatic) {
		setSelected(false);
	}
	updateTextVisibility();
}
