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
#ifndef PIDSIZECORRECTIONS_H
#define PIDSIZECORRECTIONS_H

#include "pipediameter.h"

#include <QList>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>

class Conductor;
class Element;
class QGraphicsItem;
class QUndoCommand;
class QWidget;

/**
	What the "Correct" submenu offers on a size reported (step 8r-3 a):
	proposals, never anything done unasked, one undo step each.

	A diamond of two sizes at a nozzle (Terminal::diameterGap()): a nozzle
	with a size of its own which its line has not offers "Give DN x to the
	line" (its size to the pipes of its line, up to the first break, as the
	pipe properties panel gives a size typed) and "Give DN y to the nozzle
	N1" (the size of its line as its own, as the tab of its nozzles does);
	an inline item between two sizes offers "Change the line to DN x" for
	each of them, the whole line through the item. A size which is not one
	of the material of the line is never offered (a diamond stays then).
	A pipe whose size is not one of its material (Conductor::offSeriesNote())
	offers "Change the line to" each of the two sizes of its material which
	frame it, the one below and the one above (one at an end of the series).

	A right click on a pipe looks at its end nearest to the click among
	those which show a diamond; on a symbol, at its nozzle nearest to the
	click among those. A reducer which reduces nothing or is laid the wrong
	way is left to step 8r-3 b.
*/
namespace PidSizeCorrections {

	/// One proposal of the submenu
struct Proposal {
		/// LineSize: @c size given to the line of @c pipes, as the panel of
		/// these pipes would give it; NozzleSize: @c size given to the nozzle
		/// @c nozzle of @c element as its own
	enum Kind { LineSize, NozzleSize };
	Kind kind = LineSize;
		/// What the submenu shows, the name of the undo step too
	QString text;
	PipeDiameter size;
	QList<QPointer<Conductor>> pipes;
	QPointer<Element> element;
	QString nozzle;
};

	/// The proposals for @p item, a pipe or a symbol, right clicked at
	/// @p scene_pos; none on anything else, or with nothing reported
QList<Proposal> proposals(QGraphicsItem *item, const QPointF &scene_pos);

	/// What asks which size goes with another when a line gets a size
	/// (PipePropertiesWidget::setChooser()); none for a list in a dialog
using Chooser = std::function<int(const QString &, const QStringList &)>;

	/// The undo step of @p proposal, null when there is nothing to do any
	/// more (its pipes or its symbol gone, the question cancelled, the size
	/// already there). The panel asks its usual questions, with @p parent
	/// as their parent; @p said gets what it says of the change.
QUndoCommand *apply(const Proposal &proposal, QWidget *parent,
					QString *said = nullptr, const Chooser &chooser = Chooser());

} // namespace PidSizeCorrections

#endif // PIDSIZECORRECTIONS_H
