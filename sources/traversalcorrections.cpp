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
#include "traversalcorrections.h"

#include "diagram.h"
#include "diagramcommands.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"

#include <QCoreApplication>
#include <QUndoCommand>

namespace {

	/// Whether the path of @p line was changed by hand: one of its profiles
	/// is kept (what DiagramView::resetConductors() looks at)
bool drawnByHand(const Conductor *line)
{
	const ConductorProfilesGroup profiles = line->profiles();
	for (auto it = profiles.cbegin() ; it != profiles.cend() ; ++it) {
		if (!it.value().isNull()) {
			return true;
		}
	}
	return false;
}

	/// The text of a proposal redrawing @p count lines
QString redrawText(int count)
{
	return count == 1
			? QCoreApplication::translate("TraversalCorrections", "Redraw the line",
										  "context menu action: a pipe or a signal line drawn again by itself")
			: QCoreApplication::translate("TraversalCorrections", "Redraw the %1 lines",
										  "context menu action: pipes or signal lines drawn again by themselves").arg(count);
}

} // namespace

namespace TraversalCorrections {

bool clears(const Conductor *line)
{
	return line && line->diagram()
			&& line->medium() != TerminalData::Electrical
			&& drawnByHand(line)
			&& !line->traversedSymbols().isEmpty()
			&& line->defaultRouteTraversedSymbols().isEmpty();
}

QList<Proposal> proposals(QGraphicsItem *item)
{
	Proposal proposal;
	if (auto *line = qgraphicsitem_cast<Conductor *>(item))
	{
		if (clears(line)) {
			proposal.lines << QPointer<Conductor>(line);
		}
	}
	else if (auto *symbol = qgraphicsitem_cast<Element *>(item))
	{
		if (Diagram *folio = symbol->diagram())
		{
			const QList<Conductor *> lines = folio->conductors();
			for (Conductor *line : lines)
			{
				if (line->medium() != TerminalData::Electrical
					&& line->traversedSymbols().contains(symbol) && clears(line)) {
					proposal.lines << QPointer<Conductor>(line);
				}
			}
		}
	}
	if (proposal.lines.isEmpty()) {
		return {};
	}
	proposal.text = redrawText(proposal.lines.size());
	return {proposal};
}

QUndoCommand *apply(const Proposal &proposal)
{
	QHash<Conductor *, ConductorProfilesGroup> profiles;
	Diagram *folio = nullptr;
	for (const QPointer<Conductor> &line : proposal.lines)
	{
		if (!line || !line->diagram() || (folio && line->diagram() != folio)
			|| !drawnByHand(line.data())) {
			continue;
		}
		folio = line->diagram();
		profiles.insert(line.data(), line->profiles());
	}
	if (profiles.isEmpty()) {
		return nullptr;
	}
	auto *undo = new QUndoCommand(proposal.text);
	new ResetConductorCommand(profiles, undo);
	return undo;
}

} // namespace TraversalCorrections
