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
#include "fillunsizedpipescommand.h"

#include "../diagram.h"
#include "../pidcodification.h"
#include "../qetgraphicsitem/conductor.h"
#include "../qetgraphicsitem/element.h"
#include "../qetgraphicsitem/terminal.h"
#include "../qetproject.h"

#include <QCoreApplication>

namespace {
QString translated(const char *text, int n = -1)
{
	return QCoreApplication::translate("FillUnsizedPipesCommand", text, nullptr, n);
}
}

/**
	@brief FillUnsizedPipesCommand::plan
	See the declaration
*/
QList<FillUnsizedPipesCommand::Fill> FillUnsizedPipesCommand::plan(
		const QList<NozzleSize> &sizes, QStringList *notes)
{
	QList<Fill> fills;
	QList<Conductor *> reached;
	auto say = [notes](const QString &note) {
		if (notes && !notes->contains(note)) {
			*notes << note;
		}
	};
	for (const NozzleSize &size : sizes)
	{
		const Terminal *nozzle = size.first;
		if (!nozzle || nozzle->medium() != TerminalData::Pipe || !size.second.isSet()) {
			continue;
		}
		const Diagram *folio = nozzle->diagram();
		const PidCodification codes = folio && folio->project()
				? folio->project()->pidCodification() : PidCodification();
		const QLocale locale = codes.locale();
		PipeDiameter given = size.second;
		if (given.isThread()) {
			given = given.threadNominal();
		}
			//Its own pipe, or the first ones beyond the valves placed against it
		QList<Conductor *> line;
		for (Conductor *pipe : nozzle->nearestPipes()) {
			for (Conductor *next : pipe->unsizedRun()) {
				if (!line.contains(next) && !reached.contains(next)) {
					line << next;
				}
			}
		}
		int filled = 0;
		for (Conductor *pipe : std::as_const(line))
		{
			reached << pipe;
			Fill fill;
			fill.pipe = pipe;
			fill.before = pipe->properties();
			fill.after = fill.before;
			PipeProperties &data = fill.after.m_pipe;
			Conductor::giveNozzleSize(data, size.second, codes);
			if (fill.after == fill.before) {
				continue;
			}
			fills << fill;
			++filled;
				//What its material made of the size
			const QString code = data.material;
			if (!codes.material(code)) {
				continue;
			}
			if (!given.isOutside() && data.nominal.isSet() && data.nominal != given) {
				say(translated("%1 → %2: the same size of line, as the material %3 has it.")
					.arg(given.text(locale), data.nominal.text(locale), code));
			}
			if (!codes.outsideDiameterFor(code) || !data.nominal.isSet()) {
				continue;
			}
			if (!data.outside.isSet()) {
				say(translated("%1: the material %2 has several outside diameters and no default "
							   "one for it; give it on the pipe line.")
					.arg(data.nominal.text(locale), code));
			} else if (!given.isOutside() && data.outside == codes.defaultOutside(data.nominal, code)
					   && codes.outsidesOf(data.nominal, code).size() > 1) {
				say(translated("%1 → %2: the default outside diameter of the material %3.")
					.arg(data.nominal.text(locale), data.outside.text(locale), code));
			}
		}
		if (filled) {
			say(translated("%n pipe line(s) without a size took the size of %1.", filled)
				.arg(nozzle->name()));
		}
	}
	return fills;
}

FillUnsizedPipesCommand::FillUnsizedPipesCommand(const QList<Fill> &fills,
												 QUndoCommand *parent) :
	QUndoCommand(parent),
	m_fills(fills)
{
	setText(translated("Give the size of a nozzle to its line"));
}

FillUnsizedPipesCommand::FillUnsizedPipesCommand(Element *element, QUndoCommand *parent) :
	QUndoCommand(parent),
	m_planned(false)
{
	setText(translated("Give the size of a nozzle to its line"));
	if (!element) {
		return;
	}
	for (Terminal *terminal : element->terminals()) {
		if (terminal->medium() == TerminalData::Pipe) {
			m_sizes_before << qMakePair(QPointer<Terminal>(terminal), terminal->ownDiameter());
		}
	}
}

void FillUnsizedPipesCommand::undo()
{
	for (int i = m_fills.size() - 1 ; i >= 0 ; --i) {
		if (Conductor *pipe = m_fills.at(i).pipe) {
			pipe->setProperties(m_fills.at(i).before);
		}
	}
}

void FillUnsizedPipesCommand::redo()
{
		//The sizes the steps before this one gave: known now only
	if (!m_planned)
	{
		m_planned = true;
		QList<NozzleSize> sizes;
		for (const auto &before : std::as_const(m_sizes_before)) {
			Terminal *nozzle = before.first;
			if (!nozzle || nozzle->followsPipe()) {
				continue;
			}
			const PipeDiameter now = nozzle->ownDiameter();
			if (now.isSet() && now != before.second) {
				sizes << qMakePair(static_cast<const Terminal *>(nozzle), now);
			}
		}
		m_sizes_before.clear();
		m_fills = plan(sizes);
	}
	for (const Fill &fill : std::as_const(m_fills)) {
		if (Conductor *pipe = fill.pipe) {
			pipe->setProperties(fill.after);
		}
	}
}
