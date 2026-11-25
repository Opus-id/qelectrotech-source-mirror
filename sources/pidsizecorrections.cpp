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
#include "pidsizecorrections.h"

#include "diagram.h"
#include "pidcodification.h"
#include "pipeconnection.h"
#include "pipeproperties.h"
#include "qetproject.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "undocommand/changenozzlediameterscommand.h"
#include "ui/pipepropertieswidget.h"

#include <QCoreApplication>
#include <QLineF>
#include <QSet>
#include <QUndoCommand>

namespace {

	/// The codification of the project of @p folio, an empty one without
PidCodification codificationOf(const Diagram *folio)
{
	return folio && folio->project() ? folio->project()->pidCodification() : PidCodification();
}

	/// The materials of @p pipes, each once
QStringList materialsOf(const QList<Conductor *> &pipes)
{
	QStringList materials;
	for (const Conductor *pipe : pipes) {
		const QString code = pipe->properties().m_pipe.material;
		if (!materials.contains(code)) {
			materials << code;
		}
	}
	return materials;
}

	/// The size the panel of @p pipes is given for @p size: an outside
	/// diameter as it is to pipes of one material named by it, else the DN
	/// of the line (a thread, an outside diameter); none when it has none
PipeDiameter typedFor(const PipeDiameter &size, const QList<Conductor *> &pipes,
					  const PidCodification &codes)
{
	const QStringList materials = materialsOf(pipes);
	if (size.isOutside() && materials.size() == 1 && codes.outsideDiameterFor(materials.first())) {
		return size;
	}
	return codes.lineNominal(size);
}

	/// @p size, given to @p pipes by their panel, is a size of their
	/// material as it is: the panel would not take the nearest one instead
	/// (choice of the user of 25/11: a size out of the series of the line
	/// is not offered). Pipes of several materials take a DN; a material
	/// without a series of sizes takes any.
bool fitsLine(const PipeDiameter &size, const QList<Conductor *> &pipes,
			  const PidCodification &codes)
{
	const PipeDiameter typed = typedFor(size, pipes, codes);
	if (!typed.isSet() || pipes.isEmpty()) {
		return false;
	}
	const QStringList materials = materialsOf(pipes);
	if (materials.size() > 1 && !codes.materials.isEmpty()) {
		for (const QString &code : materials) {
			if (codes.pipeSizes(code) == PidCodification::SizeRule::NoSeries) {
				return false;
			}
		}
		return codes.nominalSizes().contains(codes.nominalIn(typed, QString()))
				|| codes.nominalSizes().contains(typed);
	}
	const QString material = materials.first();
	QList<PipeDiameter> sizes;
	if (codes.pipeSizes(material, &sizes) != PidCodification::SizeRule::Series) {
		return true;
	}
	if (typed.isOutside()) {
		return sizes.contains(typed);
	}
	return codes.lineSizes(material).contains(codes.nominalIn(typed, material));
}

QList<Conductor *> alive(const QList<QPointer<Conductor>> &pipes)
{
	QList<Conductor *> list;
	for (const QPointer<Conductor> &pipe : pipes) {
		if (pipe && !list.contains(pipe.data())) {
			list << pipe.data();
		}
	}
	return list;
}

QList<QPointer<Conductor>> pointers(const QList<Conductor *> &pipes)
{
	QList<QPointer<Conductor>> list;
	for (Conductor *pipe : pipes) {
		list << QPointer<Conductor>(pipe);
	}
	return list;
}

	/// "Change the line to DN x", for each size of @p sizes which fits
void addLineSizes(QList<PidSizeCorrections::Proposal> &found, const QList<PipeDiameter> &sizes,
				  const QList<Conductor *> &pipes, const PidCodification &codes)
{
	for (const PipeDiameter &size : sizes)
	{
		if (!size.isSet() || !fitsLine(size, pipes, codes)) {
			continue;
		}
		const PipeDiameter typed = typedFor(size, pipes, codes);
		PidSizeCorrections::Proposal proposal;
		proposal.kind = PidSizeCorrections::Proposal::LineSize;
		proposal.size = typed;
		proposal.pipes = pointers(pipes);
		proposal.text = QCoreApplication::translate("PidSizeCorrections", "Change the line to %1",
													"context menu action")
				.arg(typed.text(codes.locale()));
		bool twice = false;
		for (const PidSizeCorrections::Proposal &other : std::as_const(found)) {
			twice = twice || other.text == proposal.text;
		}
		if (!twice) {
			found << proposal;
		}
	}
}

	/// The proposals of the diamond of @p nozzle
QList<PidSizeCorrections::Proposal> nozzleProposals(Terminal *nozzle)
{
	QList<PidSizeCorrections::Proposal> found;
	const Terminal::DiameterGap gap = nozzle->diameterGap();
	const PidCodification codes = codificationOf(nozzle->diagram());
	const QLocale locale = codes.locale();
	if (gap.kind == Terminal::DiameterGap::Inline)
	{
			//The whole line, through the item: the pipes of both sides
		QList<Conductor *> pipes = nozzle->nearestPipes();
		if (gap.sibling) {
			for (Conductor *pipe : gap.sibling->nearestPipes()) {
				if (!pipes.contains(pipe)) {
					pipes << pipe;
				}
			}
		}
		addLineSizes(found, {gap.here, gap.there}, pipes, codes);
		return found;
	}
	if (gap.kind != Terminal::DiameterGap::Nozzle || !gap.own.isSet()) {
		return found;
	}
		//Its own size to its line
	const QList<Conductor *> pipes = nozzle->nearestPipes();
	if (!pipes.isEmpty() && fitsLine(gap.own, pipes, codes))
	{
		const PipeDiameter typed = typedFor(gap.own, pipes, codes);
		PidSizeCorrections::Proposal proposal;
		proposal.kind = PidSizeCorrections::Proposal::LineSize;
		proposal.size = typed;
		proposal.pipes = pointers(pipes);
		proposal.text = QCoreApplication::translate("PidSizeCorrections", "Give %1 to the line",
													"context menu action")
				.arg(typed.text(locale));
		found << proposal;
	}
		//The size of its line as its own, of a kind its connection takes
	const PipeDiameter given = PipeConnection::fitted(nozzle->connection(), gap.here);
	if (given.isSet() && given != gap.own && nozzle->parentElement())
	{
		PidSizeCorrections::Proposal proposal;
		proposal.kind = PidSizeCorrections::Proposal::NozzleSize;
		proposal.size = given;
		proposal.element = nozzle->parentElement();
		proposal.nozzle = nozzle->baseName();
		proposal.text = proposal.nozzle.isEmpty()
				? QCoreApplication::translate("PidSizeCorrections", "Give %1 to the nozzle",
											  "context menu action").arg(given.text(locale))
				: QCoreApplication::translate("PidSizeCorrections", "Give %1 to the nozzle %2",
											  "context menu action").arg(given.text(locale), proposal.nozzle);
		found << proposal;
	}
	return found;
}

	/// The proposals of the band of @p pipe, a size out of its series: the
	/// sizes of its material which frame it
QList<PidSizeCorrections::Proposal> bandProposals(Conductor *pipe)
{
	QList<PidSizeCorrections::Proposal> found;
	if (pipe->offSeriesNote().isEmpty()) {
		return found;
	}
	const PidCodification codes = codificationOf(pipe->diagram());
	const PipeProperties &data = pipe->properties().m_pipe;
	const QString &material = data.material;
		//The DN when it is out of the series, else the outside diameter
	PipeDiameter size;
	QList<PipeDiameter> series;
	const QList<PipeDiameter> line = codes.lineSizes(material);
	if (data.nominal.isSet() && !line.contains(data.nominal)
		&& !line.contains(codes.nominalIn(data.nominal, material))) {
		size = data.nominal;
		series = line;
	} else {
		size = data.outside;
		QList<PipeDiameter> sizes;
		codes.pipeSizes(material, &sizes);
		for (const PipeDiameter &outside : std::as_const(sizes)) {
			if (outside.isOutside()) {
				series << outside;
			}
		}
	}
	PipeDiameter below, above;
	for (const PipeDiameter &candidate : std::as_const(series))
	{
		if (candidate.kind() != size.kind()) {
			continue;
		}
		if (candidate.tenths() < size.tenths()
			&& (!below.isSet() || candidate.tenths() > below.tenths())) {
			below = candidate;
		}
		if (candidate.tenths() > size.tenths()
			&& (!above.isSet() || candidate.tenths() < above.tenths())) {
			above = candidate;
		}
	}
	addLineSizes(found, {below, above}, {pipe}, codes);
	return found;
}

	/// The nozzle among @p nozzles which shows a diamond nearest to
	/// @p scene_pos; nullptr if none
Terminal *nearestMarked(const QList<Terminal *> &nozzles, const QPointF &scene_pos)
{
	Terminal *nearest = nullptr;
	qreal best = -1;
	for (Terminal *nozzle : nozzles)
	{
		if (!nozzle || nozzle->medium() != TerminalData::Pipe || !nozzle->hasDiameterGap()) {
			continue;
		}
		const qreal distance = QLineF(nozzle->dockConductor(), scene_pos).length();
		if (best < 0 || distance < best) {
			best = distance;
			nearest = nozzle;
		}
	}
	return nearest;
}

} // namespace

namespace PidSizeCorrections {

QList<Proposal> proposals(QGraphicsItem *item, const QPointF &scene_pos)
{
	QList<Proposal> found;
	if (auto *pipe = qgraphicsitem_cast<Conductor *>(item))
	{
		if (pipe->medium() != TerminalData::Pipe) {
			return found;
		}
		if (Terminal *nozzle = nearestMarked({pipe->terminal1, pipe->terminal2}, scene_pos)) {
			found << nozzleProposals(nozzle);
		}
		for (const Proposal &proposal : bandProposals(pipe)) {
			bool twice = false;
			for (const Proposal &other : std::as_const(found)) {
				twice = twice || other.text == proposal.text;
			}
			if (!twice) {
				found << proposal;
			}
		}
	}
	else if (auto *element = qgraphicsitem_cast<Element *>(item))
	{
		if (Terminal *nozzle = nearestMarked(element->terminals(), scene_pos)) {
			found << nozzleProposals(nozzle);
		}
	}
	return found;
}

QUndoCommand *apply(const Proposal &proposal, QWidget *parent, QString *said, const Chooser &chooser)
{
	if (said) {
		said->clear();
	}
	if (proposal.kind == Proposal::NozzleSize)
	{
		Element *element = proposal.element.data();
		if (!element) {
			return nullptr;
		}
		NozzleDiameters diameters = element->nozzleDiameters();
		diameters.insert(proposal.nozzle, proposal.size);
		auto *undo = new QUndoCommand(proposal.text);
		auto *change = new ChangeNozzleDiametersCommand(element, diameters, undo);
		if (change->isNull()) {
			delete undo;
			return nullptr;
		}
		return undo;
	}

	const QList<Conductor *> pipes = alive(proposal.pipes);
	if (pipes.isEmpty()) {
		return nullptr;
	}
	const PidCodification codes = codificationOf(pipes.first()->diagram());
		//Read as the page of a pipe reads a size typed, with its questions
	PipePropertiesWidget page(pipes, parent);
	page.hide();
	if (chooser) {
		page.setChooser(chooser);
	}
	auto *undo = new QUndoCommand(proposal.text);
	page.sizeLineIn(proposal.size.text(codes.locale()), undo);
	if (said) {
		*said = page.diameterMessage();
	}
	if (!undo->childCount()) {
		delete undo;
		return nullptr;
	}
	return undo;
}

} // namespace PidSizeCorrections
