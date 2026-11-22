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

#include <QPointF>
#include <QStringList>

#include <algorithm>
#include <utility>

/**
	@brief PidRunReattachCommand::PidRunReattachCommand
	To be built before the turn or the mirror is done: notes which pipes
	already run back through their symbol.
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
			for (const Terminal *nozzle : {pair.first, pair.second}) {
				const Conductor *pipe = pipeOn(nozzle);
				if (pipe && runsBack(nozzle, pipe)) {
					m_back_before.insert({pipe, nozzle});
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
		if (move.conductor && move.from && move.to) {
			move.conductor->moveEnd(move.from, move.to);
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
	@return the pairs, none for a symbol without a run (an electrical one)
*/
QList<QPair<Terminal *, Terminal *>> PidRunReattachCommand::straightPairs(const Element *element)
{
	QList<QPair<Terminal *, Terminal *>> pairs;
	if (!element) {
		return pairs;
	}
	const QStringList run = element->pipeRun();
	if (run.isEmpty()) {
		return pairs;
	}
	QList<Terminal *> nozzles;
	for (Terminal *terminal : element->terminals()) {
		if (terminal->medium() == TerminalData::Pipe
			&& run.contains(terminal->baseName())
			&& !terminal->isHiddenByOptions()) {
			nozzles << terminal;
		}
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
	return behind(nozzle, pipe->terminal1 == nozzle ? pipe->terminal2 : pipe->terminal1);
}

/**
	@brief PidRunReattachCommand::behind
	@return true when @p other lies behind @p nozzle (see runsBack()), on
	another symbol
*/
bool PidRunReattachCommand::behind(const Terminal *nozzle, const Terminal *other)
{
	if (!nozzle || !other || other == nozzle
		|| other->parentElement() == nozzle->parentElement()) {
		return false;
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
		//Not 0: a turn leaves a little noise on the positions
	return QPointF::dotProduct(facing, towards) < -0.5;
}

/**
	@brief PidRunReattachCommand::turnedBack
	@return true when @p pipe runs back through the symbol of @p nozzle and
	did not before the turn
*/
bool PidRunReattachCommand::turnedBack(Terminal *nozzle, Conductor *pipe) const
{
	return pipe && runsBack(nozzle, pipe) && !m_back_before.contains({pipe, nozzle});
}

/**
	@brief PidRunReattachCommand::moveEnds
	In each straight pair: two pipes turned back swap their nozzles; one
	pipe turned back goes over to the other nozzle when it is free and
	faces it. A pipe which already ran back, or which has nowhere to go,
	stays where it is. Each move is done at once, so that the next pairs
	see the pipes where they now are (two symbols side by side, turned
	each in place).
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
			Conductor *pipe_a = pipeOn(a), *pipe_b = pipeOn(b);
			const bool back_a = turnedBack(a, pipe_a);
			const bool back_b = turnedBack(b, pipe_b);
			if (back_a && back_b
				&& !behind(b, otherEnd(pipe_a, a)) && !behind(a, otherEnd(pipe_b, b))) {
				if (movePipe(pipe_a, a, b) && !movePipe(pipe_b, b, a)) {
						//Never two pipes on one nozzle
					pipe_a->moveEnd(b, a);
					m_moves.removeLast();
				}
			} else if (back_a && !pipe_b && b->hasRoomForWire() && !behind(b, otherEnd(pipe_a, a))) {
				movePipe(pipe_a, a, b);
			} else if (back_b && !pipe_a && a->hasRoomForWire() && !behind(a, otherEnd(pipe_b, b))) {
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
	Move the end of @p pipe on @p from to @p to, and keep the move
	@return true when it was moved
*/
bool PidRunReattachCommand::movePipe(Conductor *pipe, Terminal *from, Terminal *to)
{
	if (!pipe || !pipe->moveEnd(from, to)) {
		return false;
	}
	m_moves.append({pipe, from, to});
	return true;
}
