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

#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"

#include <QCoreApplication>
#include <QSet>

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
	if (group.in && group.out) {
		return State::Conflict;
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
	};
	for (const Fix &fix : std::as_const(m_fixes)) {
		give(fix.node, fix.in, fix.source);
	}
		//A stretch closed on itself the wrong way goes both ways
	for (int node : std::as_const(m_bad_links)) {
		Group &group = m_groups[find(node, nullptr)];
		group.in = group.out = true;
	}

		//A tee, a cross: the last nozzle not known goes the other way when
		//all the others come in, or all go out. What it gives may give
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

	for (Junction &junction : m_junctions)
	{
		int ins = 0, outs = 0;
		bool all_known = true;
		for (int node : std::as_const(junction.nodes)) {
			const State state = stateOf(node);
			ins += state == State::In ? 1 : 0;
			outs += state == State::Out ? 1 : 0;
			all_known = all_known && (state == State::In || state == State::Out);
		}
		junction.conflict = all_known && (ins == 0 || outs == 0);
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

namespace {
	/**
		The nozzles around @p pipe, as far as its direction may come from:
		along the pipes, through the runs of the symbols, the reducers and
		the nozzles placed against each other; each one a node of
		@p solver, solved
	*/
	QHash<const Terminal *, int> build(const Conductor *pipe, Solver &solver)
	{
		QHash<const Terminal *, int> ids;
		QList<const Terminal *> todo;
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
		idOf(pipe->terminal1);
		idOf(pipe->terminal2);

		QSet<const Element *> junctions;
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
				junctions.insert(terminal->parentElement());
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
	const QHash<const Terminal *, int> ids = build(pipe, solver);
		//The fluid comes into the symbol of its last terminal: it goes from
		//the first to the last
	switch (solver.state(ids.value(pipe->terminal2))) {
		case Solver::State::In:       return Direction::Forward;
		case Solver::State::Out:      return Direction::Backward;
		case Solver::State::Conflict: return Direction::Conflict;
		case Solver::State::Unknown:  break;
	}
	return Direction::Unknown;
}

QString nozzleName(const Terminal *nozzle)
{
	if (!nozzle) {
		return QString();
	}
	const Element *element = nozzle->parentElement();
	QString symbol;
	if (element) {
		symbol = element->actualLabel().trimmed();
		if (symbol.isEmpty()) {
			symbol = element->name();
		}
	}
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
		case Direction::Conflict:
			return QCoreApplication::translate("PidFlow", "disagreement", "direction of flow");
		case Direction::Backward:
			std::swap(from, to);
			break;
		case Direction::Forward:
			break;
	}
	return QCoreApplication::translate("PidFlow", "from %1 to %2", "direction of flow")
			.arg(nozzleName(from), nozzleName(to));
}

} // namespace PidFlow
