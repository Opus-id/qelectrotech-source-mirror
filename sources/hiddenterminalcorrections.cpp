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
#include "hiddenterminalcorrections.h"

#include "diagram.h"
#include "diagramcontent.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "undocommand/changeelementoptionscommand.h"
#include "undocommand/deleteqgraphicsitemcommand.h"

#include <QCoreApplication>
#include <QLineF>
#include <QUndoCommand>

namespace {

	/// Whether a terminal whose show_if is @p show_if is shown with @p choices
bool shownWith(const QString &show_if, const ElementOptions::Choices &choices)
{
	return show_if.isEmpty() || ElementOptions::isShown(show_if, choices);
}

	/// The terminal among @p terminals hidden by the options of its symbol
	/// while it keeps a wire, nearest to @p scene_pos; nullptr if none
Terminal *nearestHidden(const QList<Terminal *> &terminals, const QPointF &scene_pos)
{
	Terminal *nearest = nullptr;
	qreal best = -1;
	for (Terminal *terminal : terminals)
	{
		if (!terminal || !terminal->isKeptByWire()) {
			continue;
		}
		const qreal distance = QLineF(terminal->dockConductor(), scene_pos).length();
		if (best < 0 || distance < best) {
			best = distance;
			nearest = terminal;
		}
	}
	return nearest;
}

	/// The proposals of @p hidden, a terminal which keeps a wire
QList<HiddenTerminalCorrections::Proposal> terminalProposals(Terminal *hidden)
{
	using HiddenTerminalCorrections::Proposal;
	QList<Proposal> found;
	Element *element = hidden->parentElement();
	if (!element) {
		return found;
	}
	const ElementOptions::Choices now = element->resolvedOptions();
		//The other terminals with a wire the symbol shows: to stay shown
	QList<const Terminal *> wired;
	for (const Terminal *terminal : element->terminals()) {
		if (terminal != hidden && !terminal->conductors().isEmpty() && !terminal->isHiddenByOptions()) {
			wired << terminal;
		}
	}

		//The values of one option which show it again, in the order of the
		//symbol
	const ElementOptions offered = element->elementData().m_options;
	for (const ElementOptions::Option &option : offered.options())
	{
		for (const ElementOptions::Value &value : option.values)
		{
			if (value.name == now.value(option.name)) {
				continue;
			}
			ElementOptions::Choices tried = now;
			tried.insert(option.name, value.name);
			if (!shownWith(hidden->showIf(), tried)) {
				continue;
			}
			bool hides_another = false;
			for (const Terminal *terminal : std::as_const(wired)) {
				hides_another = hides_another || !shownWith(terminal->showIf(), tried);
			}
			if (hides_another) {
				continue;
			}
				//Written as the options of the panel write it: nothing for
				//a default value
			ElementOptions::Choices chosen = element->optionChoices();
			if (value.name == option.default_value) {
				chosen.remove(option.name);
			} else {
				chosen.insert(option.name, value.name);
			}
			Proposal proposal;
			proposal.kind = Proposal::ShowAgain;
			proposal.element = element;
			proposal.choices = chosen;
			proposal.text = QCoreApplication::translate("HiddenTerminalCorrections", "Set %1 to %2",
														"context menu action: an option of a symbol, a value")
					.arg(option.names.name(option.name), value.names.name(value.name));
			found << proposal;
		}
	}

		//All its wires deleted
	Proposal deletion;
	deletion.kind = Proposal::DeleteWires;
	for (Conductor *wire : hidden->conductors()) {
		deletion.wires << QPointer<Conductor>(wire);
	}
	deletion.text = deletion.wires.size() == 1
			? QCoreApplication::translate("HiddenTerminalCorrections", "Delete the line of the hidden terminal",
										  "context menu action")
			: QCoreApplication::translate("HiddenTerminalCorrections", "Delete the %1 lines of the hidden terminal",
										  "context menu action").arg(deletion.wires.size());
	found << deletion;
	return found;
}

} // namespace

namespace HiddenTerminalCorrections {

QList<Proposal> proposals(QGraphicsItem *item, const QPointF &scene_pos)
{
	Terminal *hidden = nullptr;
	if (auto *wire = qgraphicsitem_cast<Conductor *>(item)) {
		hidden = nearestHidden({wire->terminal1, wire->terminal2}, scene_pos);
	} else if (auto *element = qgraphicsitem_cast<Element *>(item)) {
		hidden = nearestHidden(element->terminals(), scene_pos);
	}
	return hidden ? terminalProposals(hidden) : QList<Proposal>();
}

QUndoCommand *apply(const Proposal &proposal)
{
	if (proposal.kind == Proposal::ShowAgain)
	{
		Element *element = proposal.element.data();
		if (!element || proposal.choices == element->optionChoices()) {
			return nullptr;
		}
		auto *undo = new QUndoCommand(proposal.text);
		new ChangeElementOptionsCommand(element, proposal.choices, undo);
		return undo;
	}

	DiagramContent content;
	Diagram *folio = nullptr;
	for (const QPointer<Conductor> &wire : proposal.wires)
	{
		if (!wire || !wire->diagram() || (folio && wire->diagram() != folio)) {
			continue;
		}
		folio = wire->diagram();
		if (!content.m_other_conductors.contains(wire.data())) {
			content.m_other_conductors << wire.data();
		}
	}
	if (!folio) {
		return nullptr;
	}
	auto *undo = new QUndoCommand(proposal.text);
	new DeleteQGraphicsItemCommand(folio, content, undo);
	return undo;
}

} // namespace HiddenTerminalCorrections
