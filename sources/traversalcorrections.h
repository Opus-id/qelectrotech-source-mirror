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
#ifndef TRAVERSALCORRECTIONS_H
#define TRAVERSALCORRECTIONS_H

#include <QList>
#include <QPointer>
#include <QString>

class Conductor;
class QGraphicsItem;
class QUndoCommand;

/**
	What the "Correct" submenu offers on a pipe or a signal line which runs
	over a symbol (Conductor::traversedSymbols(), step 7c-bis), and on that
	symbol (step 8r-3 c): "Redraw the line", the path it takes by itself
	(what "Reset" draws), offered only when that path runs over no symbol
	any more; never anything done unasked, one undo step.

	A right click on a symbol offers one entry for all the lines running
	over it which that path clears (choice of the user of 25/11); those it
	does not clear stay as they are. Nothing when no line is cleared.
*/
namespace TraversalCorrections {

	/// One proposal of the submenu
struct Proposal {
		/// What the submenu shows, the name of the undo step too
	QString text;
		/// The lines redrawn
	QList<QPointer<Conductor>> lines;
};

	/// Whether redrawing @p line by itself clears it: it runs over a symbol
	/// now, its path was changed by hand, and the path it would take by
	/// itself runs over none
bool clears(const Conductor *line);

	/// The proposals for @p item, a pipe, a signal line or a symbol; none on
	/// anything else, or when no line would be cleared
QList<Proposal> proposals(QGraphicsItem *item);

	/// The undo step of @p proposal, null when there is nothing to do any
	/// more (its lines gone, or already drawn by themselves)
QUndoCommand *apply(const Proposal &proposal);

} // namespace TraversalCorrections

#endif // TRAVERSALCORRECTIONS_H
