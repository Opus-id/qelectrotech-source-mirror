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
#ifndef HIDDENTERMINALCORRECTIONS_H
#define HIDDENTERMINALCORRECTIONS_H

#include "properties/elementoptions.h"

#include <QList>
#include <QPointF>
#include <QPointer>
#include <QString>

class Conductor;
class Element;
class QGraphicsItem;
class QUndoCommand;

/**
	What the "Correct" submenu offers on a terminal the options of its
	symbol hide while a wire is still connected to it
	(Terminal::isKeptByWire(), step 8r-3 b): proposals, never anything done
	unasked, one undo step each.

	"Set <option> to <value>", once for each value of one option which
	shows the terminal again, and hides no other terminal which has a wire
	(choice of the user of 25/11: none when two options would have to
	change); and "Delete the line of the hidden terminal", all its wires.

	A right click on a symbol looks at its terminal of that kind nearest to
	the click; on a wire, at its end on such a terminal nearest to the
	click. Not a P&ID matter only: a terminal hidden by the options of its
	symbol may be one of any symbol.
*/
namespace HiddenTerminalCorrections {

	/// One proposal of the submenu
struct Proposal {
		/// ShowAgain: @c element given the choices @c choices;
		/// DeleteWires: @c wires deleted
	enum Kind { ShowAgain, DeleteWires };
	Kind kind = ShowAgain;
		/// What the submenu shows, the name of the undo step too
	QString text;
	QPointer<Element> element;
	ElementOptions::Choices choices;
	QList<QPointer<Conductor>> wires;
};

	/// The proposals for @p item, a symbol or a wire, right clicked at
	/// @p scene_pos; none on anything else, or with nothing hidden
QList<Proposal> proposals(QGraphicsItem *item, const QPointF &scene_pos);

	/// The undo step of @p proposal, null when there is nothing to do any
	/// more (its symbol or its wires gone, the choices already there)
QUndoCommand *apply(const Proposal &proposal);

} // namespace HiddenTerminalCorrections

#endif // HIDDENTERMINALCORRECTIONS_H
