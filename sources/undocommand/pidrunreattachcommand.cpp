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
#include "pidrunreattachcommand.h"

#include "../qetgraphicsitem/conductor.h"
#include "../qetgraphicsitem/element.h"
#include "../qetgraphicsitem/terminal.h"

#include <QLineF>
#include <QPointF>
#include <QStringList>

#include <algorithm>
#include <utility>

/**
	@brief PidRunReattachCommand::PidRunReattachCommand
	To be built before the turn or the mirror is done: notes the pairs
	the user drew the wrong way round, their pipes on the nozzles facing
	away from them (betterSwapped()), left as they are.
	@param elements : the elements turned or mirrored
	@param parent : the command which turns or mirrors them
*/
PidRunReattachCommand::PidRunReattachCommand(const QList<Element *> &elements,
											 QUndoCommand *parent) :
	QUndoCommand(parent)
{
	for (Element *element : elements)
	{
		const auto pairs = straightPairs(element);
		if (pairs.isEmpty()) {
			continue;
		}
		m_elements << element;
		for (const auto &pair : pairs) {
			if (betterSwapped(pair.first, pair.second)) {
				m_drawn_swapped.insert(QPair<const Terminal *, const Terminal *>(pair.first, pair.second));
			}
			for (const Terminal *nozzle : {pair.first, pair.second}) {
				Conductor *pipe = pipeOn(nozzle);
				const Terminal *other = otherEnd(pipe, nozzle);
				if (pipe && other) {
					m_routes_before.append({pipe, nozzle, nozzle->dockConductor(),
											other->dockConductor(), pipe->profiles()});
				}
			}
		}
	}
}

/**
	@brief PidRunReattachCommand::undo
	The pipe ends back on their nozzles, the last moved first
*/
void PidRunReattachCommand::undo()
{
	for (auto it = m_moves.crbegin() ; it != m_moves.crend() ; ++it) {
		if (it->conductor && it->from && it->to) {
			it->conductor->moveEnd(it->to, it->from);
		}
	}
}

/**
	@brief PidRunReattachCommand::redo
	The first time, after the turn: find the pipes to move and move them;
	the next times, move the same ones again
*/
void PidRunReattachCommand::redo()
{
	if (!m_moves_known) {
		moveEnds();
		m_moves_known = true;
		return;
	}
	for (const EndMove &move : std::as_const(m_moves)) {
		if (move.conductor && move.from && move.to
			&& move.conductor->moveEnd(move.from, move.to)) {
			takeRouteBack(move.conductor, move.from, move.to);
		}
	}
}

/**
	@brief PidRunReattachCommand::takeRouteBack
	@p pipe, its end moved from @p from to @p to: when @p to stands where
	@p from stood before the turn, and its other end where it was, it takes
	back the route it had (see movePipe())
*/
void PidRunReattachCommand::takeRouteBack(Conductor *pipe, const Terminal *from, const Terminal *to)
{
	const Terminal *other = otherEnd(pipe, to);
	if (!pipe || !to || !other) {
		return;
	}
	for (const RouteBefore &before : std::as_const(m_routes_before)) {
		if (before.conductor == pipe && before.nozzle == from
			&& QLineF(before.dock, to->dockConductor()).length() < 0.5
			&& QLineF(before.other_dock, other->dockConductor()).length() < 0.5) {
			pipe->setProfiles(before.profiles);
			return;
		}
	}
}

/**
	@brief PidRunReattachCommand::restoreRoutes
	See the declaration. A pipe whose ends are not both where they were
	(a turn undone with its animation, still going on) is left as it is.
*/
void PidRunReattachCommand::restoreRoutes()
{
	for (const RouteBefore &before : std::as_const(m_routes_before))
	{
		Conductor *pipe = before.conductor;
		if (!pipe || !pipe->terminal1 || !pipe->terminal2) {
			continue;
		}
		const Terminal *nozzle = pipe->terminal1 == before.nozzle ? pipe->terminal1
							   : pipe->terminal2 == before.nozzle ? pipe->terminal2 : nullptr;
		const Terminal *other = otherEnd(pipe, nozzle);
		if (nozzle && other
			&& QLineF(before.dock, nozzle->dockConductor()).length() < 0.5
			&& QLineF(before.other_dock, other->dockConductor()).length() < 0.5) {
			pipe->setProfiles(before.profiles);
		}
	}
}

/**
	@brief PidRunReattachCommand::concerns
	See the declaration
*/
bool PidRunReattachCommand::concerns(const QList<Element *> &elements)
{
	for (const Element *element : elements) {
		if (!straightPairs(element).isEmpty()) {
			return true;
		}
	}
	return false;
}

/**
	@brief PidRunReattachCommand::straightPairs
	See the declaration. A nozzle with two opposite ones (a symbol drawn
	oddly) is left out: which one its pipe would go to is not known.
	@param element
	@return the pairs, none for a symbol without a run nor two nozzles
	(an electrical one, a tank)
*/
QList<QPair<Terminal *, Terminal *>> PidRunReattachCommand::straightPairs(const Element *element)
{
	QList<QPair<Terminal *, Terminal *>> pairs;
	if (!element) {
		return pairs;
	}
	const QStringList run = element->pipeRun();
	QList<Terminal *> nozzles;
	for (Terminal *terminal : element->terminals()) {
		if (terminal->medium() == TerminalData::Pipe
			&& (run.isEmpty() || run.contains(terminal->baseName()))
			&& !terminal->isHiddenByOptions()) {
			nozzles << terminal;
		}
	}
		//Without a run, a symbol with two nozzles only (a pump, a blower):
		//no change of size goes through it, but turned or mirrored its
		//pipes follow as those of a valve. Not one with more: the two
		//circuits of a heat exchanger must not be mixed
	if (run.isEmpty() && nozzles.size() != 2) {
		return pairs;
	}
	auto opposite = [](const Terminal *a, const Terminal *b) {
		return (int(a->orientation()) + 2) % 4 == int(b->orientation());
	};
	auto opposites = [&](const Terminal *a) {
		return std::count_if(nozzles.cbegin(), nozzles.cend(), [&](const Terminal *b) {
			return opposite(a, b);
		});
	};
	for (int i = 0 ; i < nozzles.size() ; ++i) {
		for (int j = i + 1 ; j < nozzles.size() ; ++j) {
			if (opposite(nozzles[i], nozzles[j])
				&& opposites(nozzles[i]) == 1 && opposites(nozzles[j]) == 1) {
				pairs << qMakePair(nozzles[i], nozzles[j]);
			}
		}
	}
	return pairs;
}

/**
	@brief PidRunReattachCommand::pipeOn
	See the declaration
*/
Conductor *PidRunReattachCommand::pipeOn(const Terminal *nozzle)
{
	if (!nozzle) {
		return nullptr;
	}
	for (Conductor *conductor : nozzle->conductors()) {
		if (conductor->medium() == TerminalData::Pipe) {
			return conductor;
		}
	}
	return nullptr;
}

/**
	@brief PidRunReattachCommand::runsBack
	See the declaration. Its other end behind the nozzle: on the other side
	of the line across the hotspot of the symbol, square to the way the
	nozzle faces. A pipe whose other end lies on that line (a straight line
	on a symbol turned by a quarter) does not run back. Neither does a pipe
	between two nozzles of the same symbol.
*/
bool PidRunReattachCommand::runsBack(const Terminal *nozzle, const Conductor *pipe)
{
	if (!nozzle || !pipe || (pipe->terminal1 != nozzle && pipe->terminal2 != nozzle)) {
		return false;
	}
	return facing(nozzle, pipe->terminal1 == nozzle ? pipe->terminal2 : pipe->terminal1) < -0.5;
}

/**
	@brief PidRunReattachCommand::facing
	@return how far @p other lies in front of @p nozzle: positive in front,
	negative behind (see runsBack()), 0 on the line across the hotspot of
	its symbol, for no other end, or for one on the same symbol
*/
qreal PidRunReattachCommand::facing(const Terminal *nozzle, const Terminal *other)
{
	if (!nozzle || !other || other == nozzle
		|| other->parentElement() == nozzle->parentElement()) {
		return 0;
	}
	QPointF facing;
	switch (nozzle->orientation()) {
		case Qet::North: facing = QPointF( 0, -1); break;
		case Qet::East:  facing = QPointF( 1,  0); break;
		case Qet::South: facing = QPointF( 0,  1); break;
		case Qet::West:  facing = QPointF(-1,  0); break;
	}
		//From the hotspot, on the axis of the line (step 7g), not from the
		//nozzle: turned by a quarter, a nozzle stands off the line, and the
		//other end of a straight line would seem behind it
	const QPointF towards = other->dockConductor() - nozzle->parentElement()->scenePos();
	return QPointF::dotProduct(facing, towards);
}

/**
	@brief PidRunReattachCommand::betterSwapped
	@return true when the pipes of @p a and @p b, two opposite nozzles,
	each on the other nozzle, would run back through their symbol fewer
	(their other ends behind their nozzles, facing()). As many either way
	(a straight line on a symbol turned by a quarter: none; a valve at the
	top of a U turned by a quarter: one, whichever the nozzle): not better,
	however little nearer one other end may be. A nozzle without a pipe
	counts 0; two without, nothing to swap.
*/
bool PidRunReattachCommand::betterSwapped(const Terminal *a, const Terminal *b)
{
	const Conductor *pipe_a = pipeOn(a), *pipe_b = pipeOn(b);
	if (!pipe_a && !pipe_b) {
		return false;
	}
	const Terminal *end_a = otherEnd(pipe_a, a), *end_b = otherEnd(pipe_b, b);
		//Not 0: a turn leaves a little noise on the positions
	auto behind = [](const Terminal *nozzle, const Terminal *other) {
		return other && facing(nozzle, other) < -0.5 ? 1 : 0;
	};
	const int now = behind(a, end_a) + behind(b, end_b);
	const int swapped = behind(b, end_a) + behind(a, end_b);
	return swapped < now;
}

/**
	@brief PidRunReattachCommand::moveEnds
	In each straight pair, the pipes go over to the other nozzles when
	fewer then run back through the symbol (betterSwapped()): two pipes
	swap, one goes over to the other nozzle when that one is free. A pair the user drew the
	wrong way round before the turn stays as it is. Each move is done at
	once, so that the next pairs see the pipes where they now are (two
	symbols side by side, turned each in place).
*/
void PidRunReattachCommand::moveEnds()
{
	for (const QPointer<Element> &element : std::as_const(m_elements))
	{
		if (!element) {
			continue;
		}
		for (const auto &pair : straightPairs(element))
		{
			Terminal *a = pair.first, *b = pair.second;
			using Pair = QPair<const Terminal *, const Terminal *>;
			if (m_drawn_swapped.contains(Pair(a, b)) || m_drawn_swapped.contains(Pair(b, a))
				|| !betterSwapped(a, b)) {
				continue;
			}
			Conductor *pipe_a = pipeOn(a), *pipe_b = pipeOn(b);
			if (pipe_a && pipe_b) {
				if (movePipe(pipe_a, a, b) && !movePipe(pipe_b, b, a)) {
						//Never two pipes on one nozzle
					pipe_a->moveEnd(b, a);
					m_moves.removeLast();
				}
			} else if (pipe_a && b->hasRoomForWire()) {
				movePipe(pipe_a, a, b);
			} else if (pipe_b && a->hasRoomForWire()) {
				movePipe(pipe_b, b, a);
			}
		}
	}
}

/**
	@brief PidRunReattachCommand::otherEnd
	@return the terminal @p pipe ends on, other than @p nozzle
*/
Terminal *PidRunReattachCommand::otherEnd(const Conductor *pipe, const Terminal *nozzle)
{
	if (!pipe) {
		return nullptr;
	}
	return pipe->terminal1 == nozzle ? pipe->terminal2 : pipe->terminal1;
}

/**
	@brief PidRunReattachCommand::movePipe
	Move the end of @p pipe on @p from to @p to, and keep the move. When
	@p to now stands where @p from stood before the turn (a symbol
	mirrored, or turned by a half about its hotspot), the pipe takes back
	the route it had: turned, its end had gone over to the other side of
	the symbol, and its route drawn by hand had been given up for a new one.
	@return true when it was moved
*/
bool PidRunReattachCommand::movePipe(Conductor *pipe, Terminal *from, Terminal *to)
{
	if (!pipe || !pipe->moveEnd(from, to)) {
		return false;
	}
	takeRouteBack(pipe, from, to);
	m_moves.append({pipe, from, to});
	return true;
}
