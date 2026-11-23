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
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pidflow.h"

#include "diagram.h"
#include "pidtraversal.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"

#include <QCoreApplication>
#include <QPointer>

#include <utility>

namespace PidFlow
{

int Solver::addNode()
{
	const int node = int(m_parent.size());
	m_parent << node;
	m_parity << false;
	return node;
}

int Solver::find(int node, bool *parity) const
{
	bool opposite = false;
	while (m_parent.at(node) != node) {
		opposite = opposite != m_parity.at(node);
		node = m_parent.at(node);
	}
	if (parity) {
		*parity = opposite;
	}
	return node;
}

void Solver::link(int a, int b)
{
	bool pa = false, pb = false;
	const int ra = find(a, &pa);
	const int rb = find(b, &pb);
		//in(a) = in(ra) ^ pa, in(b) = in(rb) ^ pb, and in(a) != in(b)
	if (ra == rb) {
		if (pa == pb) {
			m_bad_links << a;
		}
		return;
	}
	m_parent[ra] = rb;
	m_parity[ra] = pa == pb;
}

void Solver::fix(int node, bool in, int source)
{
	m_fixes << Fix{node, in, source};
}

void Solver::fixBoth(int node, int source)
{
	m_fixes << Fix{node, false, source, true};
}

void Solver::addJunction(const QList<int> &nodes, int source)
{
	Junction junction;
	junction.nodes = nodes;
	junction.source = source;
	m_junctions << junction;
}

Solver::State Solver::stateOf(int node) const
{
	bool parity = false;
	const Group group = m_groups.value(find(node, &parity));
		//Both ways, and one way only too: a disagreement
	if ((group.in && group.out) || (group.both && (group.in || group.out))) {
		return State::Conflict;
	}
	if (group.both) {
		return State::Both;
	}
	if (!group.in && !group.out) {
		return State::Unknown;
	}
		//What the stretch says, from the node which stands for it
	return (group.in != parity) ? State::In : State::Out;
}

void Solver::solve()
{
	m_groups.clear();
	auto give = [this](int node, bool in, int source) {
		bool parity = false;
		Group &group = m_groups[find(node, &parity)];
		if (in != parity) {
			group.in = true;
		} else {
			group.out = true;
		}
		if (!group.sources.contains(source)) {
			group.sources << source;
		}
		group.claims << Claim{source, in != parity ? Way::In : Way::Out};
	};
	for (const Fix &fix : std::as_const(m_fixes)) {
		if (fix.both) {
			Group &group = m_groups[find(fix.node, nullptr)];
			group.both = true;
			if (!group.sources.contains(fix.source)) {
				group.sources << fix.source;
			}
			group.claims << Claim{fix.source, Way::Both};
		} else {
			give(fix.node, fix.in, fix.source);
		}
	}
		//A stretch closed on itself the wrong way goes both ways
	for (int node : std::as_const(m_bad_links)) {
		Group &group = m_groups[find(node, nullptr)];
		group.in = group.out = true;
	}

		//A tee, a cross: the last nozzle not known goes the other way when
		//all the others come in, or all go out; a nozzle used both ways
		//counts as not known, and is never the last one. What it gives may give
		//another tee its last nozzle: again, until nothing more is found
	bool found = true;
	while (found)
	{
		found = false;
		for (const Junction &junction : std::as_const(m_junctions))
		{
			int ins = 0, outs = 0, unknowns = 0, last = -1;
			for (int node : junction.nodes) {
				switch (stateOf(node)) {
					case State::In:  ++ins;  break;
					case State::Out: ++outs; break;
					default: ++unknowns; last = node; break;
				}
			}
			if (unknowns == 1 && stateOf(last) == State::Unknown && (ins == 0) != (outs == 0)) {
				give(last, ins == 0, junction.source);
				found = true;
			}
		}
	}

		//Everything comes in, or everything goes out; or so as soon as the
		//only nozzle used both ways comes in, or goes out (trial of the
		//user, 8a-bis-2): two of them may take turns, and say nothing
	for (Junction &junction : m_junctions)
	{
		int ins = 0, outs = 0, boths = 0;
		bool all_known = true;
		for (int node : std::as_const(junction.nodes)) {
			const State state = stateOf(node);
			ins += state == State::In ? 1 : 0;
			outs += state == State::Out ? 1 : 0;
			boths += state == State::Both ? 1 : 0;
			all_known = all_known && (state == State::In || state == State::Out || state == State::Both);
		}
		junction.conflict = all_known
				&& ((boths == 0 && (ins == 0 || outs == 0))
					|| (boths == 1 && (ins == 0) != (outs == 0)));
	}
}

Solver::State Solver::state(int node) const
{
	return stateOf(node);
}

QList<int> Solver::sources(int node) const
{
	return m_groups.value(find(node, nullptr)).sources;
}

bool Solver::junctionInConflict(int index) const
{
	return index >= 0 && index < m_junctions.size() && m_junctions.at(index).conflict;
}

QList<int> Solver::junctionNodes(int index) const
{
	return index >= 0 && index < m_junctions.size() ? m_junctions.at(index).nodes : QList<int>();
}

QList<int> Solver::opposed(int source) const
{
	QList<int> found;
	for (const Group &group : m_groups)
	{
		for (const Claim &mine : group.claims)
		{
			if (mine.source != source) {
				continue;
			}
			for (const Claim &other : group.claims) {
				if (other.source != source && other.way != mine.way && !found.contains(other.source)) {
					found << other.source;
				}
			}
		}
	}
	return found;
}

bool Solver::saysBoth(int source) const
{
	for (const Fix &fix : m_fixes) {
		if (fix.source == source && fix.both) {
			return true;
		}
	}
	return false;
}

namespace {
	/**
		The nozzles around @p seeds, as far as their direction may come
		from: along the pipes, through the runs of the symbols, the reducers
		and the nozzles placed against each other; each one a node of
		@p solver, solved. In @p todo, the nozzles in the order of their
		nodes; in @p junctions, the tees and the crosses in the order they
		were given to @p solver.
	*/
	QHash<const Terminal *, int> build(const QList<const Terminal *> &seeds, Solver &solver,
									   QList<const Terminal *> &todo,
									   QList<const Element *> &junctions)
	{
		QHash<const Terminal *, int> ids;
		auto idOf = [&ids, &todo, &solver](const Terminal *terminal) {
			const auto it = ids.constFind(terminal);
			if (it != ids.constEnd()) {
				return it.value();
			}
			const int id = solver.addNode();
			ids.insert(terminal, id);
			todo << terminal;
			return id;
		};
		for (const Terminal *seed : seeds) {
			idOf(seed);
		}

		for (int i = 0 ; i < todo.size() ; ++i)
		{
			const Terminal *terminal = todo.at(i);
			const int id = ids.value(terminal);
			if (terminal->medium() != TerminalData::Pipe) {
				continue;
			}
				//What its symbol says
			switch (terminal->flowRole()) {
				case TerminalData::FlowIn:  solver.fix(id, true, id);  break;
				case TerminalData::FlowOut: solver.fix(id, false, id); break;
				case TerminalData::FlowBoth: solver.fixBoth(id, id); break;
				case TerminalData::NoFlowRole: break;
			}
				//Its pipes take out what they bring in at the other end
			for (const Conductor *conductor : terminal->conductors()) {
				if (conductor->medium() != TerminalData::Pipe) {
					continue;
				}
				const Terminal *other = conductor->terminal1 == terminal ? conductor->terminal2
																		 : conductor->terminal1;
				if (other && other != terminal) {
					solver.link(id, idOf(other));
				}
			}
				//Through a valve; at a tee, the rule of the tees
			const QList<Terminal *> siblings = terminal->runSiblings();
			if (siblings.size() == 1) {
				solver.link(id, idOf(siblings.first()));
			} else if (siblings.size() >= 2 && !junctions.contains(terminal->parentElement())) {
				junctions << terminal->parentElement();
				QList<int> nodes{id};
				for (const Terminal *sibling : siblings) {
					nodes << idOf(sibling);
				}
				solver.addJunction(nodes, id);
			}
				//Through a reducer, through a chamber of two nozzles (a side
				//of a heat exchanger), and into a symbol placed against it
			if (const Terminal *other = terminal->reducerOtherSide()) {
				if (!siblings.contains(other)) {
					solver.link(id, idOf(other));
				}
			}
			if (const Terminal *other = terminal->chamberPartner()) {
				if (!siblings.contains(other)) {
					solver.link(id, idOf(other));
				}
			}
			if (const Terminal *against = terminal->contactNozzle()) {
				solver.link(id, idOf(against));
			}
		}
		solver.solve();
		return ids;
	}
}

Direction direction(const Conductor *pipe)
{
	if (!pipe || pipe->medium() != TerminalData::Pipe || !pipe->terminal1 || !pipe->terminal2) {
		return Direction::Unknown;
	}
	Solver solver;
	QList<const Terminal *> nozzles;
	QList<const Element *> junctions;
	const QHash<const Terminal *, int> ids = build({pipe->terminal1, pipe->terminal2}, solver,
												   nozzles, junctions);
		//The fluid comes into the symbol of its last terminal: it goes from
		//the first to the last
	switch (solver.state(ids.value(pipe->terminal2))) {
		case Solver::State::In:       return Direction::Forward;
		case Solver::State::Out:      return Direction::Backward;
		case Solver::State::Both:     return Direction::Both;
		case Solver::State::Conflict: return Direction::Conflict;
		case Solver::State::Unknown:  break;
	}
	return Direction::Unknown;
}

QString symbolName(const Element *symbol)
{
	if (!symbol) {
		return QString();
	}
	const QString label = symbol->actualLabel().trimmed();
	return label.isEmpty() ? symbol->name() : label;
}

QString nozzleName(const Terminal *nozzle)
{
	if (!nozzle) {
		return QString();
	}
	const QString symbol = symbolName(nozzle->parentElement());
	const QString name = nozzle->baseName();
	if (name.isEmpty()) {
		return symbol;
	}
	return QCoreApplication::translate("PidFlow", "%1 (%2)", "a nozzle: its symbol (its name)")
			.arg(symbol, name);
}

QString text(const Conductor *pipe)
{
	if (!pipe || pipe->medium() != TerminalData::Pipe) {
		return QString();
	}
	const Terminal *from = pipe->terminal1;
	const Terminal *to = pipe->terminal2;
	switch (direction(pipe)) {
		case Direction::Unknown:
			return QCoreApplication::translate("PidFlow", "unknown", "direction of flow");
		case Direction::Conflict: {
				//The symbols in cause: those which gave its stretch a direction
			Solver solver;
			QList<const Terminal *> nozzles;
			QList<const Element *> junctions;
			const QHash<const Terminal *, int> ids = build({pipe->terminal1, pipe->terminal2}, solver,
														   nozzles, junctions);
			QList<const Element *> causes;
			for (int source : solver.sources(ids.value(pipe->terminal2))) {
				const Terminal *nozzle = nozzles.value(source);
				if (nozzle && nozzle->parentElement() && !causes.contains(nozzle->parentElement())) {
					causes << nozzle->parentElement();
				}
			}
			QStringList names;
			for (const Element *cause : std::as_const(causes)) {
				names << symbolName(cause);
			}
			if (names.isEmpty()) {
				return QCoreApplication::translate("PidFlow", "disagreement", "direction of flow");
			}
			return QCoreApplication::translate("PidFlow", "disagreement (%1)",
											   "direction of flow; %1: the symbols in cause")
					.arg(names.join(QStringLiteral(", ")));
		}
		case Direction::Both:
			return QCoreApplication::translate("PidFlow", "both ways", "direction of flow");
		case Direction::Backward:
			std::swap(from, to);
			break;
		case Direction::Forward:
			break;
	}
	return QCoreApplication::translate("PidFlow", "from %1 to %2", "direction of flow")
			.arg(nozzleName(from), nozzleName(to));
}

namespace {
	/**
		The symbols of @p folio in disagreement (step 8a-bis-2), each with
		the lines of its tool tip: a source of a direction which another
		source of the same stretch contradicts (one way against the other,
		one way only against both ways), and a tee or a cross into which
		everything comes, or out of which everything goes. A stretch closed
		on itself the wrong way has no source to name: it marks nothing.
	*/
	QHash<const Element *, QStringList> compute(const Diagram *folio)
	{
		QHash<const Element *, QStringList> notes;
		QList<const Terminal *> seeds;
		for (const Element *element : folio->elements()) {
			for (const Terminal *terminal : element->terminals()) {
				if (terminal->medium() == TerminalData::Pipe) {
					seeds << terminal;
				}
			}
		}
		if (seeds.isEmpty()) {
			return notes;
		}
		Solver solver;
		QList<const Terminal *> nozzles;
		QList<const Element *> junctions;
		build(seeds, solver, nozzles, junctions);

			//The names of @p sources, symbol by symbol: by its nozzle when
			//only one of its nozzles is there, else by the symbol alone (both
			//nozzles of an arrow, a tee)
		auto namesOf = [&nozzles](const QList<int> &sources) {
			QList<const Element *> order;
			QHash<const Element *, QList<const Terminal *>> by_symbol;
			for (int source : sources) {
				const Terminal *nozzle = nozzles.value(source);
				if (!nozzle || !nozzle->parentElement()) {
					continue;
				}
				if (!by_symbol.contains(nozzle->parentElement())) {
					order << nozzle->parentElement();
				}
				QList<const Terminal *> &list = by_symbol[nozzle->parentElement()];
				if (!list.contains(nozzle)) {
					list << nozzle;
				}
			}
			QStringList names;
			for (const Element *symbol : std::as_const(order)) {
				const QList<const Terminal *> &list = by_symbol[symbol];
				names << (list.size() == 1 && list.first()->flowRole() != TerminalData::NoFlowRole
							  ? nozzleName(list.first())
							  : symbolName(symbol));
			}
			return names;
		};
		auto add = [&notes](const Element *element, const QString &note) {
			QStringList &list = notes[element];
			if (!list.contains(note)) {
				list << note;
			}
		};
		const QString comma = QStringLiteral(", ");

		for (int id = 0 ; id < nozzles.size() ; ++id)
		{
			const Terminal *nozzle = nozzles.at(id);
			if (nozzle->medium() != TerminalData::Pipe || nozzle->flowRole() == TerminalData::NoFlowRole
				|| !nozzle->parentElement()) {
				continue;
			}
			const QList<int> opposed = solver.opposed(id);
			if (opposed.isEmpty()) {
				continue;
			}
			QList<int> one_way, both_ways;
			for (int other : opposed) {
				(solver.saysBoth(other) ? both_ways : one_way) << other;
			}
			if (solver.saysBoth(id)) {
				add(nozzle->parentElement(),
					QCoreApplication::translate("PidFlow", "Both ways here, one way only at %1.",
												"direction of flow in disagreement")
						.arg(namesOf(one_way).join(comma)));
				continue;
			}
			if (!one_way.isEmpty()) {
				add(nozzle->parentElement(),
					QCoreApplication::translate("PidFlow", "Direction of flow opposite to that of %1.",
												"direction of flow in disagreement")
						.arg(namesOf(one_way).join(comma)));
			}
			if (!both_ways.isEmpty()) {
				add(nozzle->parentElement(),
					QCoreApplication::translate("PidFlow", "One way only here, both ways at %1.",
												"direction of flow in disagreement")
						.arg(namesOf(both_ways).join(comma)));
			}
		}

		for (int index = 0 ; index < junctions.size() ; ++index)
		{
			if (!solver.junctionInConflict(index)) {
				continue;
			}
			const Element *junction = junctions.at(index);
			const QList<int> nodes = solver.junctionNodes(index);
			bool in = false, both = false;
			for (int node : nodes) {
				in = in || solver.state(node) == Solver::State::In;
				both = both || solver.state(node) == Solver::State::Both;
			}
			QList<int> sources;
			for (int node : nodes) {
				for (int source : solver.sources(node)) {
					const Terminal *nozzle = nozzles.value(source);
					if (nozzle && nozzle->parentElement() != junction && !sources.contains(source)) {
						sources << source;
					}
				}
			}
			const QStringList names = namesOf(sources);
			QString note;
			if (both) {
					//A line of both ways always has a source: the arrow
				note = (in ? QCoreApplication::translate("PidFlow",
														 "Everything comes in here when the line of both ways "
														 "comes in: directions given by %1.",
														 "direction of flow at a tee")
						   : QCoreApplication::translate("PidFlow",
														 "Everything goes out here when the line of both ways "
														 "goes out: directions given by %1.",
														 "direction of flow at a tee"))
						.arg(names.join(comma));
			} else if (names.isEmpty()) {
				note = in ? QCoreApplication::translate("PidFlow", "Everything comes in here.",
														"direction of flow at a tee")
						  : QCoreApplication::translate("PidFlow", "Everything goes out here.",
														"direction of flow at a tee");
			} else {
				note = (in ? QCoreApplication::translate("PidFlow",
														 "Everything comes in here: directions given by %1.",
														 "direction of flow at a tee")
						   : QCoreApplication::translate("PidFlow",
														 "Everything goes out here: directions given by %1.",
														 "direction of flow at a tee"))
						.arg(names.join(comma));
			}
			add(junction, note);
		}
		return notes;
	}

		/// What compute() found on a folio, and when
	struct FolioMarks {
		quint64 conductors = 0;
		quint64 symbols = 0;
		QHash<const Element *, QStringList> notes;
		QList<QPointer<Element>> marked;
	};
		/// By folio: a folio deleted leaves its entry, never read again
	QHash<const Diagram *, FolioMarks> s_folios;

	const FolioMarks &marksOf(const Diagram *folio)
	{
		FolioMarks &marks = s_folios[folio];
		const quint64 conductors = Conductor::geometryGeneration();
		const quint64 symbols = PidTraversal::symbolsGeneration();
		if (marks.conductors == conductors && marks.symbols == symbols) {
			return marks;
		}
		const QHash<const Element *, QStringList> before = marks.notes;
		const QList<QPointer<Element>> previously = marks.marked;
		marks.conductors = conductors;
		marks.symbols = symbols;
		marks.notes = compute(folio);
		marks.marked.clear();
		for (auto it = marks.notes.cbegin() ; it != marks.notes.cend() ; ++it) {
			marks.marked << QPointer<Element>(const_cast<Element *>(it.key()));
		}
			//Drawn again, the symbols whose marks change; a deleted one is
			//null
		for (const QList<QPointer<Element>> &list : {previously, marks.marked}) {
			for (const QPointer<Element> &element : list) {
				if (element && before.value(element.data()) != marks.notes.value(element.data())) {
					element->update();
				}
			}
		}
		return marks;
	}
}

QStringList disagreements(const Element *symbol)
{
	if (!symbol || !symbol->diagram()) {
		return QStringList();
	}
	return marksOf(symbol->diagram()).notes.value(symbol);
}

void refreshMarks(const Diagram *folio)
{
	if (folio) {
		marksOf(folio);
	}
}

} // namespace PidFlow
