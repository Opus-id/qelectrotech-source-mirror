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
#include "deleteqgraphicsitemcommand.h"
#include <wiringrules.h>

#include <cable/cablemanager.h>
#include <diagram.h>
#include <qetproject.h>
#include "addgraphicsobjectcommand.h"
#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include <qetdiagrameditor.h>
#include <qetgraphicsitem/ViewItem/qetgraphicstableitem.h>
#include <qetgraphicsitem/conductor.h>
#include <qetgraphicsitem/conductortextitem.h>
#include <qetgraphicsitem/dynamicelementtextitem.h>
#include <qetgraphicsitem/element.h>
#include <qetgraphicsitem/elementtextitemgroup.h>
#include <shownkinds.h>
#include <qetgraphicsitem/terminal.h>
#include "addelementtextcommand.h"
#include <pidfreeend.h>
#include <TerminalStrip/realterminal.h>
#include <TerminalStrip/physicalterminal.h>

#include <algorithm>

namespace {
		//Defined below, beside rejoinPipesOfRemovedElements()
	void addFreeEndsLeftBySymbols(const DiagramContent &content, QList<Element *> &lonely);

		/// Whether every pipe of @p element is among @p gone: a free end, or
		/// a blind flange placed by the tool, left alone. Its other lines
		/// (the process connection of the flange) do not keep it.
	bool pipesAllGo(const Element *element, const QList<Conductor *> &gone)
	{
		for (Terminal *t : element->terminals()) {
			for (Conductor *c : t->conductors()) {
				if (c->medium() == TerminalData::Pipe && !gone.contains(c)) {
					return false;
				}
			}
		}
		return true;
	}
}

/**
	@brief DeleteQGraphicsItemCommand::DeleteQGraphicsItemCommand
	@param diagram : diagram where this undo work
	@param content : content to remove
	@param parent : parent undo
*/
DeleteQGraphicsItemCommand::DeleteQGraphicsItemCommand(
		Diagram *diagram,
		const DiagramContent &content,
		QUndoCommand *parent) :
	QUndoCommand(parent),
	m_removed_contents(content),
	m_diagram(diagram)
{
	//If parent element of a dynamic element text item is also in m_removed_content,
	//we remove it, because when the element will be removed from the scene every child's will also be removed.
	const QSet<DynamicElementTextItem *> elmt_set = m_removed_contents.m_element_texts;
	for(DynamicElementTextItem *deti : elmt_set)
	{
		if (m_removed_contents.m_elements.contains(deti->parentElement()))
			m_removed_contents.m_element_texts.remove(deti);

	}

		//When removing a deti we must know its parent item, for re-adding deti as child of the parent
		//when undoing this command
	for(DynamicElementTextItem *deti : m_removed_contents.m_element_texts)
	{
		if(deti->parentGroup())
			m_grp_texts_hash.insert(deti, deti->parentGroup());
		else
			m_elmt_text_hash.insert(deti, deti->parentElement());
	}

	//If parent element of ElementTextItemGroup is also in m_removed_content,
	//we remove it, because when the element will be removed from the scene every child's will also be removed.
	const QSet<ElementTextItemGroup *> group_set = m_removed_contents.m_texts_groups;
	for(ElementTextItemGroup *group : group_set)
	{
		if(m_removed_contents.m_elements.contains(group->parentElement()))
			m_removed_contents.m_texts_groups.remove(group);
	}

		//The deletion of the groups is not managed by this undo, but by a RemoveTextsGroupCommand
	for(ElementTextItemGroup *group : m_removed_contents.m_texts_groups) {
		new RemoveTextsGroupCommand(group->parentElement(), group, this);
	}

	m_removed_contents.m_texts_groups.clear();
	setPotentialsOfRemovedElements();
	rejoinPipesOfRemovedElements();
	reopenPipesOfRemovedClosures();

		//Get all linkeds table of removed table.
	for (auto table : m_removed_contents.m_tables)
	{
			//Table is already managed, jump to next loop
		if (m_table_scene_hash.contains(table))
			continue;

		auto first_table  = table; //The first table if the table is linked to another
		while(first_table->previousTable())
			first_table = first_table->previousTable();
		auto current_table = first_table;

		m_table_scene_hash.insert(first_table, first_table->scene());
		while (current_table->nextTable())
		{
			current_table = current_table->nextTable();
			m_table_scene_hash.insert(current_table, current_table->scene());
		}
	}

	setText(QString(QObject::tr(
				"delete %1",
				"undo caption - %1 is a sentence listing the removed content"))
		.arg(m_removed_contents.sentence(DiagramContent::All)));
	//Table is now managed by m_table_scene_hash,
	//we clear the tables of m_removed_content
	m_removed_contents.m_tables.clear();
	m_diagram->qgiManager().manage(m_removed_contents.items(DiagramContent::All));
}

DeleteQGraphicsItemCommand::~DeleteQGraphicsItemCommand()
{
	m_diagram->qgiManager().release(m_removed_contents.items(DiagramContent::All));
}

/**
 * @brief DeleteQGraphicsItemCommand::hasNonDeletableTerminal
 * Return true if @content have terminal element which can't be deleted.
 * The reason why a terminal can't be deleted is because they have bridge
 * or belong to a physical terminal with more than one level.
 * @param diagram
 * @param content
 * @param dialog
 * @return
 */
bool DeleteQGraphicsItemCommand::hasNonDeletableTerminal(const DiagramContent &content)
{
    if (!content.m_terminal_elements.isEmpty())
    {
        for (const auto &terminal : content.m_terminal_elements)
        {
            if (!terminal.isNull())
            {
                if (terminal->parentTerminalStrip()
                    && (terminal->realTerminal()->isBridged()
                        || terminal->realTerminal()->physicalTerminal()->levelCount() != 1)) {
                    return true;
                }
            }
        }
    }

    return false;
}

/**
	@brief DeleteQGraphicsItemCommand::addLonelyFreeEnds
	See the declaration. A pipe the user picked is never joined again by
	rejoinPipesOfRemovedElements() to a symbol that stays: a free end it
	leaves stays alone. A pipe that goes with a symbol picked does too,
	unless the line is joined again through that symbol (5l-1q, 13/11).
*/
int DeleteQGraphicsItemCommand::addLonelyFreeEnds(DiagramContent &content)
{
	const QList<Conductor *> gone = content.conductors(DiagramContent::AnyConductor);
	QList<Element *> lonely;
	for (QGraphicsItem *item : std::as_const(content.m_selected_items))
	{
		auto *pipe = qgraphicsitem_cast<Conductor *>(item);
		if (!pipe || pipe->medium() != TerminalData::Pipe) {
			continue;
		}
		for (Terminal *end : {pipe->terminal1, pipe->terminal2})
		{
			Element *element = end ? end->parentElement() : nullptr;
			if (!element || !element->goesWithItsPipe() || content.m_elements.contains(element)
				|| lonely.contains(element)) {
				continue;
			}
			if (pipesAllGo(element, gone)) {
				lonely << element;
			}
		}
	}
	addFreeEndsLeftBySymbols(content, lonely);
	content.m_elements << lonely;
		//Their other lines go with them: the process connection of a blind
		//flange placed by the tool (6b-3c2)
	for (Element *element : std::as_const(lonely)) {
		for (Terminal *t : element->terminals()) {
			for (Conductor *c : t->conductors()) {
				if (!gone.contains(c) && !content.m_other_conductors.contains(c)) {
					content.m_other_conductors << c;
				}
			}
		}
	}
	return int(lonely.size());
}

/**
	@brief DeleteQGraphicsItemCommand::setPotentialsOfRemovedElements
	This function creates new conductors (if needed) for conserve the electrical potentials
	present at the terminals of each removed elements.
*/
void DeleteQGraphicsItemCommand::setPotentialsOfRemovedElements()
{
	for (Element *elmt : m_removed_contents.m_elements)
	{
			//a list of terminals who have at least two conductors docked in.
		QList<Terminal *> terminals_list;
		for (Terminal *t : elmt->terminals()) {
			if (t->conductors().size() >= 2) {
				terminals_list.append(t);
			}
		}
		if (terminals_list.isEmpty()) {
			continue;
		}

		for (Terminal *t : terminals_list)
		{
				//All new created conductors will be docked to hub_terminal
			Terminal *hub_terminal = nullptr;
			QList<Terminal *> terminals_to_connect_list;

			for (Conductor *c : t->conductors())
			{
				Terminal *other_terminal = c->terminal1 == t ? c->terminal2 : c->terminal1;

				if (m_removed_contents.items(DiagramContent::Elements).contains(other_terminal->parentElement()))
				{
					other_terminal = terminalInSamePotential(other_terminal, c);
					if (other_terminal == nullptr) {
						continue;
					}
				}

				terminals_to_connect_list.append(other_terminal);
				if (hub_terminal == nullptr) {
					hub_terminal = other_terminal;
				}
					//hub_terminal must be the terminal the more at top left of the diagram.
				else if (other_terminal->scenePos().x() < hub_terminal->scenePos().x()) {
					hub_terminal = other_terminal;
				}
				else if (other_terminal->scenePos().x() == hub_terminal->scenePos().x()) {
					if (other_terminal->scenePos().y() < hub_terminal->scenePos().y()) {
						hub_terminal = other_terminal;
					}
				}
			}

			terminals_to_connect_list.removeAll(hub_terminal);
			if (hub_terminal == nullptr || terminals_to_connect_list.isEmpty()) {
				continue;
			}

			ConductorProperties properties = hub_terminal->conductors().first()->properties();

				//Every terminal to the hub (a star), or, when the project
				//limits the wires per terminal (discussion #1158), one after
				//another (a chain): a star gives the hub a wire per other
				//terminal, which the limit forbids.
			QList<std::pair<Terminal *, Terminal *>> pairs;
			const QETProject *project = hub_terminal->diagram() ? hub_terminal->diagram()->project() : nullptr;
			if (project && WiringRules::chainsWires(project->wiringRules(), WiringRules::masterEnabled()))
			{
				QList<Terminal *> chain {hub_terminal};
				chain << terminals_to_connect_list;
				QList<QPointF> points;
				for (Terminal *ct : std::as_const(chain)) {
					points << ct->scenePos();
				}
				const QList<int> order = WiringRules::chainOrder(points);
				for (int i = 1 ; i < order.size() ; ++i) {
					pairs.append(std::make_pair(chain.at(order.at(i - 1)), chain.at(order.at(i))));
				}
			}
			else
			{
				for (Terminal *t : std::as_const(terminals_to_connect_list)) {
					pairs.append(std::make_pair(hub_terminal, t));
				}
			}

			for (const auto &new_pair : std::as_const(pairs))
			{
					//If a conductor was already created between these two terminals
					//in this undo command, from another removed element, we do nothing
				bool exist_ = false;
				for (std::pair<Terminal *, Terminal *> pair : m_connected_terminals)
				{
					if  (pair.first == new_pair.first && pair.second == new_pair.second) {
						exist_ = true;
						continue;
					} else if (pair.first == new_pair.second && pair.second == new_pair.first) {
						exist_ = true;
						continue;
					}
				}

				if (exist_ == false)
				{
					m_connected_terminals.append(new_pair);
					qInfo() << "m_connected_terminals" << m_connected_terminals;
					Conductor *new_cond = new Conductor(new_pair.first, new_pair.second);
					new_cond->setProperties(properties);
						//The wire that joins the two left by the removed
						//symbol gets its uuid from its two ends, as a wire
						//saved without one does on load (Diagram::fromXml()):
						//the same deletion then saves the same file every
						//time, not a random uuid each run (F115)
					if (QETProject *wire_project = new_pair.second->diagram()
							? new_pair.second->diagram()->project() : nullptr) {
						auto end = [](const Terminal *t) {
							return t->parentElement()->uuid().toString()
									+ QLatin1Char('/') + t->stableUuid().toString();
						};
						QStringList ends{end(new_pair.first), end(new_pair.second)};
						ends.sort();
						new_cond->setUuid(wire_project->derivedItemUuid(
								QStringLiteral("conductor"), ends.join(QLatin1Char('\n'))));
					}
					new AddGraphicsObjectCommand(new_cond, new_pair.second->diagram(), QPointF(), this);
				}
			}
		}
	}
}

namespace {

	/// Where a line goes on, seen from a nozzle of a removed symbol
	struct RunEnd
	{
			/// the nozzle that stays, or nullptr when the line stops
		Terminal *terminal = nullptr;
			/// the pipe with a length nearest the nozzle that stays, or nullptr
		Conductor *pipe = nullptr;
			/// that pipe starts on the side away from the removed symbol
		bool pipe_comes_in = false;
	};

	/**
		@return the points of the line on the side @a end of @a nozzle, a
		nozzle of a removed symbol, from the nozzle that stays to @a nozzle:
		the route of the pipe between them, or the nozzle that stays alone
		when it touched @a nozzle without a pipe; empty when the line goes
		on through other removed symbols
	*/
	QList<QPointF> sideRoute(const RunEnd &end, const Terminal *nozzle)
	{
		if (!end.terminal) {
			return {};
		}
		if (!end.pipe) {
			return {end.terminal->dockConductor()};
		}
		const bool direct = (end.pipe->terminal1 == nozzle && end.pipe->terminal2 == end.terminal)
				|| (end.pipe->terminal2 == nozzle && end.pipe->terminal1 == end.terminal);
		return direct ? PidFreeEnd::pathTo(end.pipe, nozzle) : QList<QPointF>();
	}

	/**
		@return the route of the pipe which joins @a one and @a two, the
		ends of the line on either side of a removed symbol, through its
		nozzles @a first and @a second (7c): the routes of the two pipes it
		replaces, through where the symbol stood, from @a one when
		@a one_first. Empty when a side goes through other removed symbols:
		the pipe then takes the route of a new one.
	*/
	QList<QPointF> rejoinedRoute(const RunEnd &one, const Terminal *first,
								 const RunEnd &two, const Terminal *second, bool one_first)
	{
		QList<QPointF> from_one = sideRoute(one, first);
		QList<QPointF> from_two = sideRoute(two, second);
		if (from_one.isEmpty() || from_two.isEmpty()) {
			return {};
		}
		std::reverse(from_two.begin(), from_two.end());
		QList<QPointF> points = from_one + from_two;
		if (!one_first) {
			std::reverse(points.begin(), points.end());
		}
		return PidFreeEnd::cleaned(points);
	}

		/// The pipe nozzles of @p element its options show
	QList<const Terminal *> shownNozzles(const Element *element)
	{
		QList<const Terminal *> nozzles;
		for (const Terminal *t : element->terminals()) {
			if (t->medium() == TerminalData::Pipe && !t->isHiddenByOptions()) {
				nozzles << t;
			}
		}
		return nozzles;
	}

	/**
		@return the nozzles a line runs through, to be joined when
		@a element goes: its run (Element::pipeRun()), else, for a reducer,
		which has no run since a change of DN stops there, its nozzles; for
		a symbol with two nozzles only, face to face (a pump, a blower), which
		has no run either, those two (5l-1h)
	*/
	QStringList joinedRun(const Element *element)
	{
		QStringList run = element->pipeRun();
		if (run.isEmpty() && element->isReducer()) {
			for (const Terminal *t : element->terminals()) {
				if (t->medium() == TerminalData::Pipe && !t->baseName().isEmpty()) {
					run << t->baseName();
				}
			}
		} else if (run.isEmpty()) {
			const QList<const Terminal *> nozzles = shownNozzles(element);
			if (nozzles.size() == 2
				&& Qet::isOpposed(nozzles[0]->orientation(), nozzles[1]->orientation())) {
				for (const Terminal *t : nozzles) {
					if (!t->baseName().isEmpty()) {
						run << t->baseName();
					}
				}
			}
		}
		return run.size() >= 2 ? run : QStringList();
	}

		/// Whether @p element is an equipment with more than two nozzles (a
		/// tank, a heat exchanger): removed, it leaves its pipes on free ends
		/// (5l-1h, 22/11), as a closure does
	bool isEquipment(const Element *element)
	{
		return !element->goesWithItsPipe() && !element->isClosure()
				&& joinedRun(element).isEmpty() && shownNozzles(element).size() > 2;
	}

		/// Whether @p element is the end of a line, other than a free end or
		/// a closure: a symbol of the pipe family with a single nozzle (an
		/// arrow of inlet or outlet, a strainer, a vent...). Removed, it
		/// leaves its pipe on a free end, as a closure does (8b, 23/11)
	bool isLineEnd(const Element *element)
	{
		return element->pidFamily() == QLatin1String("pipe") && !element->goesWithItsPipe()
				&& !element->isClosure() && shownNozzles(element).size() == 1;
	}

		/// Whether @p element, removed alone, leaves its pipes open on free
		/// ends (reopenPipesOfRemovedClosures()): a closure, an equipment
		/// (isEquipment()) or the end of a line (isLineEnd())
	bool leavesItsPipesOpen(const Element *element)
	{
		return element->isClosure() || isEquipment(element) || isLineEnd(element);
	}

	/**
		@return the nozzle of the run of its symbol (joinedRun()) facing
		@a nozzle, the way a line goes on through it: N2 for N1; none for
		the branch of a tee, a symbol without a run, or when two could be
		chosen
	*/
	Terminal *opposedRunNozzle(const Terminal *nozzle)
	{
		Element *element = nozzle ? nozzle->parentElement() : nullptr;
		if (!element) {
			return nullptr;
		}
		const QStringList run = joinedRun(element);
		if (!run.contains(nozzle->baseName())) {
			return nullptr;
		}
		Terminal *found = nullptr;
		for (Terminal *t : element->terminals())
		{
			if (t == nozzle || t->medium() != TerminalData::Pipe
				|| !run.contains(t->baseName())
				|| !Qet::isOpposed(t->orientation(), nozzle->orientation())) {
				continue;
			}
			if (found) {
				return nullptr;
			}
			found = t;
		}
		return found;
	}

	/**
		@return the conductors the user picked themselves among those
		@a content takes away
	*/
	QSet<const Conductor *> pickedConductors(const DiagramContent &content)
	{
		QSet<const Conductor *> picked;
		for (QGraphicsItem *item : std::as_const(content.m_selected_items)) {
			if (auto *c = qgraphicsitem_cast<Conductor *>(item)) {
				picked << c;
			}
		}
		for (Conductor *c : std::as_const(content.m_other_conductors)) {
			picked << c;
		}
		return picked;
	}

	/**
		@return where the line goes on from @a nozzle of a removed symbol:
		out along it, through the other @a removed symbols, to the first
		nozzle that stays (see rejoinPipesOfRemovedElements())
	*/
	RunEnd goOut(Terminal *nozzle, const QList<Element *> &removed,
				 const QList<Conductor *> &gone, const QSet<const Conductor *> &picked,
				 QSet<Terminal *> &seen)
	{
		RunEnd end;
		Terminal *t = nozzle;
		for (int guard = 0 ; t && guard < 1000 ; ++guard)
		{
			seen << t;
			Conductor *pipe = nullptr;
			for (Conductor *c : t->conductors()) {
				if (c->medium() == TerminalData::Pipe) {
					pipe = c;
					break;
				}
			}
			Terminal *next = nullptr;
			if (pipe)
			{
				next = pipe->terminal1 == t ? pipe->terminal2 : pipe->terminal1;
					//A pipe this command leaves on the folio is not ours
					//to replace
				if (!next || !gone.contains(pipe)) {
					return RunEnd();
				}
				if (!removed.contains(next->parentElement()) && picked.contains(pipe)) {
					return RunEnd();
				}
					//The last one met: the pipe the nozzle that stays was on
				if (!pipe->endsTouch()) {
					end.pipe = pipe;
					end.pipe_comes_in = pipe->terminal1 == next;
				}
			}
			else if (t->conductors().isEmpty()) {
					//Placed against another symbol, without a pipe
				next = t->contactNozzle();
			}
			if (!next || next->medium() != TerminalData::Pipe) {
				return RunEnd();
			}
			if (!removed.contains(next->parentElement())) {
				end.terminal = next;
				return end;
			}
			seen << next;
			t = opposedRunNozzle(next);
			if (t && seen.contains(t)) {
				return RunEnd();
			}
		}
		return RunEnd();
	}

	/**
		Add to @a lonely the free ends (Element::isFreeEnd()) the symbols
		removed by @a content leave alone: all their pipes go with them,
		and the line is not joined again through the symbol (5l-1n), which
		would leave them a pipe. A strainer or a vent stays.
	*/
		/// Whether @p element, removed, leaves @p pipe open on a free end of
		/// the project (reopenPipesOfRemovedClosures()): a closure, an
		/// equipment or the end of a line (leavesItsPipesOpen()), the pipe
		/// not picked with it, a free end set
	bool reopens(const Element *element, const Conductor *pipe, const QSet<const Conductor *> &picked)
	{
		return leavesItsPipesOpen(element) && !picked.contains(pipe)
				&& PidFreeEnd::symbol(element->diagram()).isElement();
	}

	void addFreeEndsLeftBySymbols(const DiagramContent &content, QList<Element *> &lonely)
	{
		const QList<Element *> &removed = content.m_elements;
		if (removed.isEmpty()) {
			return;
		}
		const QList<Conductor *> gone = content.conductors(DiagramContent::AnyConductor);
		const QSet<const Conductor *> picked = pickedConductors(content);
		for (Element *element : removed)
		{
			for (Terminal *nozzle : element->terminals())
			{
				if (nozzle->medium() != TerminalData::Pipe) {
					continue;
				}
				for (Conductor *pipe : nozzle->conductors())
				{
					if (pipe->medium() != TerminalData::Pipe || !gone.contains(pipe)) {
						continue;
					}
						//A closure removed alone: its line stays, open again
					if (reopens(element, pipe, picked)) {
						continue;
					}
					Terminal *far = pipe->terminal1 == nozzle ? pipe->terminal2 : pipe->terminal1;
					Element *free_end = far ? far->parentElement() : nullptr;
					if (!free_end || !free_end->goesWithItsPipe() || removed.contains(free_end)
						|| lonely.contains(free_end)) {
						continue;
					}
					if (!pipesAllGo(free_end, gone)) {
						continue;
					}
						//Joined again to the other side of the symbol: it
						//keeps a pipe
					if (Terminal *opposed = opposedRunNozzle(nozzle))
					{
						QSet<Terminal *> seen;
						const RunEnd one = goOut(nozzle, removed, gone, picked, seen);
						const RunEnd two = goOut(opposed, removed, gone, picked, seen);
						if (one.terminal == far && two.terminal && two.terminal != far
							&& (one.pipe || two.pipe)) {
							continue;
						}
					}
					lonely << free_end;
				}
			}
		}
	}

} // namespace

/**
	@brief DeleteQGraphicsItemCommand::rejoinPipesOfRemovedElements
	A symbol a line runs through (a valve, the run of a tee) taken away
	leaves one pipe where it stood, as if it had never been dropped there
	(the reverse of autoBreakConductors()): the two nozzles left on either
	side are joined, with the data of the pipe on the N1 side. A pipe has
	no potential, so setPotentialsOfRemovedElements() never does this.
	- through two opposed nozzles of the run only: the branch of a tee
	  goes with the tee, as before;
	- one side touching another symbol with no pipe: the pipe left goes
	  to the nozzle it touched; both sides touching: nothing, there is no
	  line data to take;
	- through symbols removed together, one after the other: one pipe;
	- a pipe the user picked along with the symbol, which runs to a symbol
	  that stays, goes as asked: that side is not joined;
	- a reducer the same way, though it has no run (a change of DN stops
	  there): a size that differs at the far end is marked on its nozzle,
	  as anywhere else; and a symbol with two nozzles only, face to face (a
	  pump, a blower), which has no run either (5l-1h).
	Pipes only: an electrical terminal is never in a run.
*/
void DeleteQGraphicsItemCommand::rejoinPipesOfRemovedElements()
{
	const QList<Element *> &removed = m_removed_contents.m_elements;
	if (removed.isEmpty()) {
		return;
	}
		//The pipes this command takes away, and among them those the user
		//picked themselves
	const QList<Conductor *> gone = m_removed_contents.conductors(DiagramContent::AnyConductor);
	const QSet<const Conductor *> picked = pickedConductors(m_removed_contents);

	QSet<Terminal *> seen;
		//From a nozzle of a removed symbol, out along the line, through the
		//other removed symbols, to the first nozzle that stays
	auto goOut = [&](Terminal *nozzle) {
		return ::goOut(nozzle, removed, gone, picked, seen);
	};

		//Left to right, then top to bottom, not in the order of the
		//selection: symbols removed one after the other give the pipe of
		//the N1 side of the first, and the same file every time
	QList<Element *> in_order = removed;
	std::sort(in_order.begin(), in_order.end(), [](const Element *a, const Element *b) {
		const QPointF pa = a->scenePos(), pb = b->scenePos();
		return pa.x() != pb.x() ? pa.x() < pb.x() : pa.y() < pb.y();
	});
	for (Element *element : std::as_const(in_order))
	{
		const QStringList run = joinedRun(element);
		if (run.isEmpty()) {
			continue;
		}
		for (Terminal *first : element->terminals())
		{
			if (first->medium() != TerminalData::Pipe || !run.contains(first->baseName())
				|| seen.contains(first)) {
				continue;
			}
			Terminal *second = opposedRunNozzle(first);
			if (!second || seen.contains(second)) {
				continue;
			}
			const RunEnd one = goOut(first);
			const RunEnd two = goOut(second);
			if (!one.terminal || !two.terminal || one.terminal == two.terminal
				|| (!one.pipe && !two.pipe)) {
				continue;
			}

				//The data of the pipe on the N1 side, and the way it runs
			const bool from_one = one.pipe;
			const RunEnd &data = from_one ? one : two;
			const bool one_first = data.pipe_comes_in == from_one;
			Terminal *start = one_first ? one.terminal : two.terminal;
			Terminal *end = one_first ? two.terminal : one.terminal;

			auto *pipe = new Conductor(start, end);
			if (!pipe->isValid()) {
				delete pipe;
				continue;
			}
			pipe->setProperties(data.pipe->properties());
				//An insulation limit taken away: the insulation no longer
				//stops there (7d-bis)
			if (element->isInsulationLimit() && one.pipe && two.pipe) {
				const bool first_is_n1 = first->baseName() != QLatin1String("N2");
				spreadInsulationOverRemovedLimit(pipe, first_is_n1 ? one.pipe : two.pipe,
												 first_is_n1 ? two.pipe : one.pipe, gone);
			}
				//A jacket limit taken away: the jacket no longer stops
				//there (8r-1)
			if (element->isJacketLimit() && one.pipe && two.pipe) {
				const bool first_is_n1 = first->baseName() != QLatin1String("N2");
				spreadJacketOverRemovedLimit(pipe, first_is_n1 ? one.pipe : two.pipe,
											 first_is_n1 ? two.pipe : one.pipe, gone);
			}
			pipe->takeNozzleDiameter();
				//The route of the line kept, through where the symbol stood,
				//its bends moved by hand included (7c); else a new one
			pipe->setPathPoints(rejoinedRoute(one, first, two, second, one_first));
				//Its uuid from its two ends, as for a wire above (F115): the
				//same deletion saves the same file every time
			if (QETProject *project = m_diagram ? m_diagram->project() : nullptr) {
				auto id = [](const Terminal *t) {
					return t->parentElement()->uuid().toString()
							+ QLatin1Char('/') + t->stableUuid().toString();
				};
				QStringList ends{id(start), id(end)};
				ends.sort();
				pipe->setUuid(project->derivedItemUuid(
						QStringLiteral("conductor"), ends.join(QLatin1Char('\n'))));
			}
			new AddGraphicsObjectCommand(pipe, m_diagram, QPointF(), this);
		}
	}
}

/**
	@brief DeleteQGraphicsItemCommand::spreadInsulationOverRemovedLimit
	An insulation limit taken away between @p n1_side and @p n2_side, the
	pipes on its N1 and N2 sides, which @p joined replaces: the insulation
	no longer stops there, so the line joined has one (choice of the user
	of 22/11, step 7d-bis). An insulated side gives its insulation (kind,
	text, mark) to @p joined and to the whole line of the other side, up to
	the next limit or equipment (Conductor::LineReach::Insulation), each in
	this step; whichever way the limit was drawn. Two insulations which
	differ: that of the N1 side, as for the other data of the line, and
	note() says so. Neither insulated, or alike: nothing.
*/
void DeleteQGraphicsItemCommand::spreadInsulationOverRemovedLimit(
		Conductor *joined, Conductor *n1_side, Conductor *n2_side, const QList<Conductor *> &gone)
{
	const PipeProperties a = n1_side->properties().m_pipe;
	const PipeProperties b = n2_side->properties().m_pipe;
	const bool a_on = a.insulation_kind != PipeProperties::NoInsulation;
	const bool b_on = b.insulation_kind != PipeProperties::NoInsulation;
	const bool alike = a.insulation_kind == b.insulation_kind && a.insulation == b.insulation
			&& a.insulation_mark == b.insulation_mark;
	if ((!a_on && !b_on) || alike) {
		return;
	}
		//The N1 side, unless it has none
	const bool from_n1 = a_on;
	Conductor *winner = from_n1 ? n1_side : n2_side;
	Conductor *loser = from_n1 ? n2_side : n1_side;
	const PipeProperties given = winner->properties().m_pipe;
	auto take = [&given](ConductorProperties &properties) {
		properties.m_pipe.insulation_kind = given.insulation_kind;
		properties.m_pipe.insulation = given.insulation;
		properties.m_pipe.insulation_mark = given.insulation_mark;
	};

	ConductorProperties joined_data = joined->properties();
	take(joined_data);
	joined->setProperties(joined_data);

		//The line of the other side, the limit still there: it stops at it
	for (Conductor *pipe : loser->inlineRun(Conductor::LineReach::Insulation))
	{
		if (gone.contains(pipe)) {
			continue;
		}
		ConductorProperties data = pipe->properties();
		take(data);
		if (data == pipe->properties()) {
			continue;
		}
		QVariant old_value, new_value;
		old_value.setValue(pipe->properties());
		new_value.setValue(data);
		new QPropertyUndoCommand(pipe, "properties", old_value, new_value, this);
	}

	if (a_on && b_on)
	{
		auto said = [](const PipeProperties &p) {
			QString name;
			for (const auto &entry : PipeProperties::insulationKinds()) {
				if (entry.first == p.insulation_kind) {
					name = entry.second;
				}
			}
			return p.insulation.trimmed().isEmpty()
					? name : QStringLiteral("%1 %2").arg(name, p.insulation.trimmed());
		};
		m_note = QObject::tr("Insulation limit deleted: the insulation of its N1 side (%1) "
							 "replaces that of the other side (%2) on the line joined.")
				 .arg(said(a), said(b));
	}
}

/**
	@brief DeleteQGraphicsItemCommand::spreadJacketOverRemovedLimit
	A jacket limit taken away between @p n1_side and @p n2_side, as
	spreadInsulationOverRemovedLimit() for an insulation (step 8r-1): a
	jacketed side gives its jacket (material and sizes) to @p joined and
	to the line of the other side, up to the next jacket limit or
	equipment, in this step; two jackets which differ: that of the N1
	side, and note() says so. The drawing along the whole pipe stays each
	pipe's own.
*/
void DeleteQGraphicsItemCommand::spreadJacketOverRemovedLimit(
		Conductor *joined, Conductor *n1_side, Conductor *n2_side, const QList<Conductor *> &gone)
{
	const PipeProperties a = n1_side->properties().m_pipe;
	const PipeProperties b = n2_side->properties().m_pipe;
	if ((!a.jacketed() && !b.jacketed()) || a.sameJacket(b)) {
		return;
	}
	const PipeProperties given = a.jacketed() ? a : b;
	Conductor *loser = a.jacketed() ? n2_side : n1_side;

	ConductorProperties joined_data = joined->properties();
	joined_data.m_pipe.takeJacket(given);
	joined->setProperties(joined_data);

		//The line of the other side, the limit still there: it stops at it
	for (Conductor *pipe : loser->inlineRun(Conductor::LineReach::Jacket))
	{
		if (gone.contains(pipe)) {
			continue;
		}
		ConductorProperties data = pipe->properties();
		data.m_pipe.takeJacket(given);
		if (data == pipe->properties()) {
			continue;
		}
		QVariant old_value, new_value;
		old_value.setValue(pipe->properties());
		new_value.setValue(data);
		new QPropertyUndoCommand(pipe, "properties", old_value, new_value, this);
	}

	if (a.jacketed() && b.jacketed())
	{
		auto said = [](const PipeProperties &p) {
			const PipeDiameter size = p.jacket_outside.isSet() ? p.jacket_outside : p.jacket_nominal;
			return QStringLiteral("%1 %2").arg(p.jacket_material, size.isSet() ? size.text() : QString())
					.trimmed();
		};
		const QString note = QObject::tr("Jacket limit deleted: the jacket of its N1 side (%1) "
										 "replaces that of the other side (%2) on the line joined.")
				.arg(said(a), said(b));
		m_note = m_note.isEmpty() ? note : m_note + QLatin1Char(' ') + note;
	}
}

/**
	@brief DeleteQGraphicsItemCommand::reopenPipesOfRemovedClosures
	A closure of a line (a blind flange, a dished end, a cap) taken away
	alone leaves the line open, as it was before it was closed: its pipe
	goes on to a free end of the project, placed where the closure was, its
	route and its data kept (PidFreeEnd::reopen()). Its process connection
	goes with it. An equipment with more than two nozzles (a tank, a heat
	exchanger) leaves each of its pipes the same way (5l-1h), and so does
	the end of a line, an arrow of inlet or outlet, a strainer, a vent
	(8b). A pipe the user picked along with it goes as asked, and so does
	one whose other end goes too. Without a free end set for the project,
	the pipe goes, as before.
*/
void DeleteQGraphicsItemCommand::reopenPipesOfRemovedClosures()
{
	const QList<Element *> &removed = m_removed_contents.m_elements;
	if (removed.isEmpty()) {
		return;
	}
	const QList<Conductor *> gone = m_removed_contents.conductors(DiagramContent::AnyConductor);
	const QSet<const Conductor *> picked = pickedConductors(m_removed_contents);
	for (Element *element : removed)
	{
		if (!leavesItsPipesOpen(element)) {
			continue;
		}
		for (Terminal *nozzle : element->terminals())
		{
			if (nozzle->medium() != TerminalData::Pipe) {
				continue;
			}
			for (Conductor *pipe : nozzle->conductors())
			{
				if (pipe->medium() != TerminalData::Pipe || !gone.contains(pipe)
					|| !reopens(element, pipe, picked)) {
					continue;
				}
				Terminal *far = pipe->terminal1 == nozzle ? pipe->terminal2 : pipe->terminal1;
				if (!far || !far->parentElement() || removed.contains(far->parentElement())) {
					continue;
				}
				PidFreeEnd::reopen(m_diagram, nozzle, this);
				break;
			}
		}
	}
}

/**
	@brief DeleteQGraphicsItemCommand::terminalInSamePotential
	Return a terminal at the same potential of terminal,
	by traveling through the conductors connected to terminal
	only if the owner element of the terminal
	is not delete by this undo command.
	Return nullptr if a terminal can't be found.
	@param terminal - terminal from search
	@param conductor_to_exclude - a conductor to exlcude from search.
	@return
*/
Terminal *DeleteQGraphicsItemCommand::terminalInSamePotential(
		Terminal *terminal,
		Conductor *conductor_to_exclude)
{
	QList<Conductor *> conductor_list = terminal->conductors();
	conductor_list.removeAll(conductor_to_exclude);
	for(Conductor *c : conductor_list)
	{
		Terminal *other_terminal = c->terminal1 == terminal ? c->terminal2 : c->terminal1;
		if(!m_removed_contents.items(DiagramContent::Elements).contains(other_terminal->parentElement())) {
			return other_terminal;
		}
	}
		//No one of direct conductor of terminal are docked to an element which is not removed
	for(Conductor *c : conductor_list)
	{
		Terminal *other_terminal = c->terminal1 == terminal ? c->terminal2 : c->terminal1;
		Terminal *terminal_to_return = terminalInSamePotential(other_terminal, c);
		if (terminal_to_return != nullptr) {
			return  terminal_to_return;
		}
	}

	return nullptr;
}

/**
	@brief DeleteQGraphicsItemCommand::undo
	Undo this command
*/
void DeleteQGraphicsItemCommand::undo()
{
	m_diagram->showMe();

	for(QGraphicsItem *item : m_removed_contents.items())
		m_diagram->addItem(item);

		//We relink element after every element was added to diagram
	for(Element *e : m_removed_contents.m_elements)
		for(Element *elmt : m_link_hash[e])
				e->linkToElement(elmt);

	for(DynamicElementTextItem *deti : m_removed_contents.m_element_texts)
	{
		if(m_elmt_text_hash.contains(deti))
			m_elmt_text_hash.value(deti)->addDynamicTextItem(deti);
		else if (m_grp_texts_hash.contains(deti))
		{
			Element *elmt = m_grp_texts_hash.value(deti)->parentElement();
			elmt->addDynamicTextItem(deti);
			elmt->addTextToGroup(deti, m_grp_texts_hash.value(deti));
		}
	}

	for (auto table : m_table_scene_hash.keys())
	{
		if (!m_table_scene_hash.value(table).isNull()) {
			m_table_scene_hash.value(table)->addItem(table);
		}
	}

		//Back on the sheet and linked again: the cables which run over
		//this report claim their wire on the other side once more.
	for (Element *e : m_removed_contents.m_elements) {
		if (e && (e->linkType() & Element::AllReport)) {
			CableManager::refreshLabels(m_diagram->project());
			break;
		}
	}

	QUndoCommand::undo();
}

/**
	@brief DeleteQGraphicsItemCommand::redo
	Redo the delete command
*/
void DeleteQGraphicsItemCommand::redo()
{
	m_diagram -> showMe();

	for(Conductor *c : m_removed_contents.conductors(DiagramContent::AnyConductor))
	{
			//If option one text per folio is enable, and the text item of
			//current conductor is visible (that mean the conductor have the single displayed text)
			//We call adjustTextItemPosition to other conductor at the same potential to keep
			//a visible text on this potential.
			//wantsVisible(): also while wire texts are hidden (View > Show)
		if (m_diagram -> defaultConductorProperties.m_one_text_per_folio && ShownKinds::wantsVisible(c -> textItem()))
		{
			QList <Conductor *> conductor_list;
			conductor_list << c -> relatedPotentialConductors(false).values();
			if (conductor_list.count())
				conductor_list.first() -> calculateTextItemPosition();
		}
	}

	for(Element *e : m_removed_contents.m_elements)
	{
			//Get linked element, for relink it at undo
		if (!e->linkedElements().isEmpty())
			m_link_hash.insert(e, e->linkedElements());
	}

	for(DynamicElementTextItem *deti : m_removed_contents.m_element_texts)
	{
		if(deti->parentGroup() && deti->parentGroup()->parentElement())
			deti->parentGroup()->parentElement()->removeTextFromGroup(deti, deti->parentGroup());

		deti->parentElement()->removeDynamicTextItem(deti);
		deti->setParentItem(nullptr);
	}

	for (auto table : m_table_scene_hash.keys())
	{
		if (!m_table_scene_hash.value(table).isNull()) {
			m_table_scene_hash.value(table)->removeItem(table);
		}
	}

	for(QGraphicsItem *item : m_removed_contents.items())
		m_diagram->removeItem(item);

		//A folio report carries a cable core over to the next sheet:
		//taking one away (putting it back is undo, below) changes which
		//wires belong to which cable, so the cable fields are worked
		//out again straight away.
	for (Element *e : m_removed_contents.m_elements) {
		if (e && (e->linkType() & Element::AllReport)) {
			CableManager::refreshLabels(m_diagram->project());
			break;
		}
	}

	QUndoCommand::redo();
}
