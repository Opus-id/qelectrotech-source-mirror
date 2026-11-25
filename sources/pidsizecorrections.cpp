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

#include "autobreakconductor.h"
#include "diagram.h"
#include "diagramcontent.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidfreeend.h"
#include "pidtee.h"
#include "pipeconnection.h"
#include "pipeproperties.h"
#include "qetproject.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "undocommand/addgraphicsobjectcommand.h"
#include "undocommand/changenozzlediameterscommand.h"
#include "undocommand/deleteqgraphicsitemcommand.h"
#include "undocommand/mirrorselectioncommand.h"
#include "ui/pipepropertieswidget.h"

#include <QCoreApplication>
#include <QLineF>
#include <QSet>
#include <QUndoCommand>

#include <algorithm>

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

	/// The pipe nozzles of @p element
QList<Terminal *> pipeEnds(const Element *element)
{
	QList<Terminal *> ends;
	for (Terminal *terminal : element->terminals()) {
		if (terminal->medium() == TerminalData::Pipe) {
			ends << terminal;
		}
	}
	return ends;
}

	/// The undo step of "Reverse the reducer", named @p text: @p reducer
	/// mirrored about the axis of its pipes, as a flow arrow is
	/// (PidFlow::mirrorArrows()), its pipes going over to the nozzle facing
	/// them; its nozzles swap their own sizes, so that each keeps the size of
	/// its pipe. Null when it is no longer laid the wrong way.
QUndoCommand *reverseReducer(Element *reducer, const QString &text)
{
	const QList<Terminal *> ends = pipeEnds(reducer);
	if (ends.size() != 2 || !ends.first()->reducerReversed()) {
		return nullptr;
	}
	auto *undo = new QUndoCommand(text);
	const QPointF along = ends.at(1)->dockConductor() - ends.at(0)->dockConductor();
	const Qt::Orientation orientation = qAbs(along.x()) >= qAbs(along.y()) ? Qt::Horizontal
																			: Qt::Vertical;
	new MirrorSelectionCommand(QList<QPair<Element *, Qt::Orientation>>{qMakePair(reducer, orientation)},
							   undo);
	const QString first = ends.at(0)->baseName();
	const QString second = ends.at(1)->baseName();
	NozzleDiameters sizes = reducer->nozzleDiameters();
	if (!first.isEmpty() && !second.isEmpty() && (sizes.contains(first) || sizes.contains(second)))
	{
		const PipeDiameter mine = sizes.take(first);
		const PipeDiameter theirs = sizes.take(second);
		if (theirs.isSet()) {
			sizes.insert(first, theirs);
		}
		if (mine.isSet()) {
			sizes.insert(second, mine);
		}
		new ChangeNozzleDiametersCommand(reducer, sizes, undo);
	}
	return undo;
}

	/// The piece of pipe between a nozzle and the reducer inserted there:
	/// one step of the grid (step 8r-3 d, choice of the user of 25/11); as
	/// much room is left between the reducer and the end of the stretch
constexpr qreal kReducerStub = PidTee::nozzle_room;

Qet::Orientation opposite(Qet::Orientation orientation)
{
	return Qet::Orientation((int(orientation) + 2) % 4);
}

QPointF unitOf(Qet::Orientation orientation)
{
	switch (orientation) {
		case Qet::North: return QPointF(0, -1);
		case Qet::South: return QPointF(0, 1);
		case Qet::East:  return QPointF(1, 0);
		case Qet::West:  return QPointF(-1, 0);
	}
	return QPointF();
}

	/// The symbol of reducer of the project of @p folio, invalid when not
	/// set or not found
ElementsLocation reducerSymbol(const Diagram *folio)
{
	QETProject *project = folio ? folio->project() : nullptr;
	if (!project) {
		return ElementsLocation();
	}
	const QString path = project->pidCodification().toolSymbol(PidCodification::reducerRole());
	if (path.isEmpty()) {
		return ElementsLocation();
	}
		//A project given turns any path into one of its own collection
	const ElementsLocation location = path.startsWith(QLatin1String("embed://"))
			? ElementsLocation(path, project) : ElementsLocation(path);
	return location.isElement() && location.exist() ? location : ElementsLocation();
}

	/// The two nozzles of the pipe of @p reducer, its large side first;
	/// false when it is not a reducer facing both ways (two nozzles facing
	/// away from each other) which names its large side
bool reducerSides(const Element *reducer, Terminal **large, Terminal **small)
{
	if (!reducer || !reducer->isReducer() || reducer->largeNozzle().isEmpty()) {
		return false;
	}
	const QList<Terminal *> ends = pipeEnds(reducer);
	if (ends.size() != 2 || !Qet::isOpposed(ends.at(0)->orientation(), ends.at(1)->orientation())) {
		return false;
	}
	const bool first = ends.at(0)->baseName() == reducer->largeNozzle();
	const bool second = ends.at(1)->baseName() == reducer->largeNozzle();
	if (first == second) {
		return false;
	}
	if (large) {
		*large = first ? ends.at(0) : ends.at(1);
	}
	if (small) {
		*small = first ? ends.at(1) : ends.at(0);
	}
	return true;
}

	/// A reducer of the project of @p folio, not on the folio yet; nullptr
	/// when there is none or it is not one (reducerSides()). Taken into the
	/// project when @p import
Element *buildReducer(Diagram *folio, bool import)
{
	const ElementsLocation location = reducerSymbol(folio);
	if (!location.isElement()) {
		return nullptr;
	}
	ElementsLocation used = location;
	if (import && !(location.isProject() && location.project() == folio->project())) {
		used = folio->project()->importElement(used);
	}
	if (!used.exist()) {
		return nullptr;
	}
	int state = 0;
	Element *reducer = ElementFactory::Instance()->createElement(used, nullptr, &state);
	if (!reducer || state || !reducerSides(reducer, nullptr, nullptr)) {
		delete reducer;
		return nullptr;
	}
	reducer->takeLabelSize(folio->project()->pidCodification());
	reducer->takeOptionDefaults(folio->project()->pidCodification());
	return reducer;
}

	/// The length of the run of the reducer of the project of @p folio,
	/// between its two nozzles; 0 when it has none
qreal reducerRun(Diagram *folio)
{
	Element *reducer = buildReducer(folio, false);
	Terminal *large = nullptr, *small = nullptr;
	qreal run = 0;
	if (reducerSides(reducer, &large, &small)) {
		run = QLineF(large->dockConductor(), small->dockConductor()).length();
	}
	delete reducer;
	return run;
}

	/// The only pipe on @p nozzle, nullptr for none or several
Conductor *onlyPipe(const Terminal *nozzle)
{
	Conductor *found = nullptr;
	for (Conductor *conductor : nozzle->conductors())
	{
		if (conductor->medium() != TerminalData::Pipe) {
			continue;
		}
		if (found) {
			return nullptr;
		}
		found = conductor;
	}
	return found;
}

	/// The first stretch of @p pipe leaves @p nozzle, one of its ends, the
	/// way the nozzle faces, straight on for at least @p length
bool straightFor(const Conductor *pipe, const Terminal *nozzle, qreal length)
{
	const Terminal *far = pipe->terminal1 == nozzle ? pipe->terminal2 : pipe->terminal1;
		//From the nozzle to the far end
	const QList<QPointF> points = PidFreeEnd::cleaned(PidFreeEnd::pathTo(pipe, far));
	if (points.size() < 2) {
		return false;
	}
	const QPointF dock = nozzle->dockConductor();
	const QPointF out = unitOf(nozzle->orientation());
	const QPointF along = points.at(1) - points.at(0);
	const QPointF from_dock = points.at(0) - dock;
		//Straight out of the nozzle, which it starts at
	const qreal aside = qAbs(along.x() * out.y() - along.y() * out.x());
	const qreal dock_aside = qAbs(from_dock.x() * out.y() - from_dock.y() * out.x());
	if (aside > 0.01 || dock_aside > 0.5 || QPointF::dotProduct(along, out) <= 0) {
		return false;
	}
	return QPointF::dotProduct(points.at(1) - dock, out) >= length - 0.01;
}

	/// How a reducer goes at a nozzle with a diamond (insertReducer())
struct ReducerPlan
{
	Conductor *pipe = nullptr;
		/// What the piece between the nozzle and the reducer has
	ConductorProperties stub;
		/// The large side of the reducer toward the nozzle
	bool stub_large = false;
		/// What the submenu says
	QString text;
	bool isValid() const { return pipe; }
};

	/// The data of @p pipe given @p size, for the piece between a nozzle
	/// and a reducer: as @p beyond has it when it is of the same material
	/// (the pipe on the other side of an inline item, whose size @p size
	/// is), else as the DN of
	/// the line its material has (or an outside diameter of its own for a
	/// material named by it). False when the size cannot be given so
bool stubData(const Conductor *pipe, const PipeDiameter &size, const Conductor *beyond,
			  const PidCodification &codes, ConductorProperties *data)
{
	ConductorProperties given = pipe->properties();
	PipeProperties &p = given.m_pipe;
	if (beyond && beyond->properties().m_pipe.material == p.material
		&& beyond->properties().m_pipe.hasSize())
	{
		p.nominal = beyond->properties().m_pipe.nominal;
		p.outside = beyond->properties().m_pipe.outside;
	}
	else if (size.isOutside() && codes.outsideDiameterFor(p.material))
	{
		const QList<PipeDiameter> joined = codes.nominalsOf(size, p.material);
		if (joined.size() > 1) {
			return false;
		}
		p.outside = size;
		p.nominal = joined.value(0);
	}
	else
	{
		const PipeDiameter dn = codes.lineNominal(size);
		if (!dn.isSet()) {
			return false;
		}
		codes.giveLineSize(p, dn);
	}
	*data = given;
	return true;
}

	/// How a reducer goes at @p nozzle, one with a diamond between exactly
	/// two sizes; invalid when none goes there (see the declaration of
	/// PidSizeCorrections)
ReducerPlan planReducer(Terminal *nozzle)
{
	ReducerPlan plan;
	Diagram *folio = nozzle ? nozzle->diagram() : nullptr;
	Element *element = nozzle ? nozzle->parentElement() : nullptr;
	if (!folio || !folio->project() || !element || nozzle->medium() != TerminalData::Pipe
		|| !reducerSymbol(folio).isElement()) {
		return plan;
	}
	Conductor *pipe = onlyPipe(nozzle);
	const Terminal::DiameterGap gap = nozzle->diameterGap();
	if (!pipe) {
		return plan;
	}
		//The size of the nozzle, or of the other side of the inline item,
		//on the piece; the size of the pipe beyond the reducer
	PipeDiameter stub_size;
	const Conductor *beyond = nullptr;
	if (gap.kind == Terminal::DiameterGap::Nozzle && gap.own.isSet()) {
		stub_size = gap.own;
	} else if (gap.kind == Terminal::DiameterGap::Inline) {
		stub_size = gap.there;
		beyond = gap.there_pipe;
	} else {
		return plan;
	}
	const PipeDiameter line_size = gap.here;
	const PidCodification codes = codificationOf(folio);
	const PipeDiameter dn_stub = codes.lineNominal(stub_size);
	const PipeDiameter dn_line = codes.lineNominal(line_size);
	if (!dn_stub.isSet() || !dn_line.isSet()
		|| codes.compare(dn_stub, dn_line) == PidCodification::DiameterMatch::Same
		|| !fitsLine(stub_size, {pipe}, codes)
		|| !stubData(pipe, stub_size, beyond, codes, &plan.stub)) {
		return plan;
	}
	const qreal run = reducerRun(folio);
	if (run <= 0 || !straightFor(pipe, nozzle, kReducerStub + run + kReducerStub)) {
		return plan;
	}
	plan.stub_large = dn_stub.tenths() > dn_line.tenths();
	const QLocale locale = codes.locale();
	if (gap.kind == Terminal::DiameterGap::Inline) {
		plan.text = QCoreApplication::translate("PidSizeCorrections", "Insert a reducer here (%1 at %2)",
												"context menu action: a symbol, a size")
				.arg(element->name(), stub_size.text(locale));
	} else {
		const PipeDiameter &large = plan.stub_large ? stub_size : line_size;
		const PipeDiameter &small = plan.stub_large ? line_size : stub_size;
		plan.text = QCoreApplication::translate("PidSizeCorrections", "Insert a reducer here (%1 × %2)",
												"context menu action: the large size, the small one")
				.arg(large.text(locale), small.text(locale));
	}
	plan.pipe = pipe;
	return plan;
}

	/// @p reducer turned, mirrored if need be, so that @p back faces
	/// @p facing and its other side the other way, then moved for @p back
	/// to stand at @p at; turned the least, a mirror rather than a half
	/// turn (an eccentric reducer keeps its flat side). False when it
	/// cannot face so.
bool fitReducer(Element *reducer, Terminal *back, Terminal *ahead, Qet::Orientation facing,
				const QPointF &at)
{
	struct Way { qreal rotation; bool horizontal; bool vertical; };
	static const Way ways[] = {
		{0, false, false}, {0, true, false}, {0, false, true},
		{90, false, false}, {90, true, false}, {90, false, true},
		{270, false, false}, {270, true, false}, {270, false, true},
		{180, false, false}};
	for (const Way &way : ways)
	{
		reducer->setHorizontalMirror(false);
		reducer->setVerticalMirror(false);
		reducer->setRotation(way.rotation);
		reducer->setHorizontalMirror(way.horizontal);
		reducer->setVerticalMirror(way.vertical);
		if (back->orientation() == facing && ahead->orientation() == opposite(facing)) {
			reducer->setPos(QPointF());
			reducer->setPos(at - back->dockConductor());
			return true;
		}
	}
	return false;
}

	/// The undo step of "Insert a reducer here", named @p text: a reducer
	/// of the project on the pipe of @p nozzle, which is cut through it,
	/// the piece on the side of @p nozzle taking the other size. Null when
	/// none goes there any more.
QUndoCommand *insertReducer(Terminal *nozzle, const QString &text)
{
	const ReducerPlan plan = planReducer(nozzle);
	if (!plan.isValid()) {
		return nullptr;
	}
	Diagram *folio = nozzle->diagram();
	Element *reducer = buildReducer(folio, true);
	Terminal *large = nullptr, *small = nullptr;
	if (!reducerSides(reducer, &large, &small)) {
		delete reducer;
		return nullptr;
	}
	Terminal *back = plan.stub_large ? large : small;
	Terminal *ahead = plan.stub_large ? small : large;
	const Qet::Orientation out = nozzle->orientation();
	const QPointF at = nozzle->dockConductor() + unitOf(out) * kReducerStub;
	if (!fitReducer(reducer, back, ahead, opposite(out), at)) {
		delete reducer;
		return nullptr;
	}

	auto *undo = new QUndoCommand(text);
	folio->addItem(reducer);
	new AddGraphicsObjectCommand(reducer, folio, reducer->pos(), undo);
	Conductor *pipe = plan.pipe;
	DiagramContent content;
	content.m_other_conductors << pipe;
	new DeleteQGraphicsItemCommand(folio, content, undo);

		//The two pieces keep the way of the pipe, its route and its data;
		//a route is given from the first terminal of its piece
	Terminal *far = pipe->terminal1 == nozzle ? pipe->terminal2 : pipe->terminal1;
	const bool from_nozzle = pipe->terminal1 == nozzle;
	QList<QPointF> stub_route = keptPipeRoute(pipe, nozzle, back);
	QList<QPointF> rest_route = keptPipeRoute(pipe, far, ahead);
	if (!from_nozzle) {
		std::reverse(stub_route.begin(), stub_route.end());
	} else {
		std::reverse(rest_route.begin(), rest_route.end());
	}
	auto *stub = from_nozzle ? new Conductor(nozzle, back) : new Conductor(back, nozzle);
	auto *rest = from_nozzle ? new Conductor(ahead, far) : new Conductor(far, ahead);
	stub->setProperties(plan.stub);
	rest->setProperties(pipe->properties());
	rest->takeNozzleDiameter();
	stub->setPathPoints(stub_route);
	rest->setPathPoints(rest_route);
		//Its designation written once
	Conductor::keepOneDesignation({stub, rest}, pipe);
	new AddGraphicsObjectCommand(stub, folio, QPointF(), undo);
	new AddGraphicsObjectCommand(rest, folio, QPointF(), undo);
	return undo;
}

	/// "Insert a reducer here" added to @p found when one goes at @p nozzle
void addInsertReducer(QList<PidSizeCorrections::Proposal> &found, Terminal *nozzle)
{
	const ReducerPlan plan = planReducer(nozzle);
	if (!plan.isValid()) {
		return;
	}
	PidSizeCorrections::Proposal proposal;
	proposal.kind = PidSizeCorrections::Proposal::InsertReducer;
	proposal.text = plan.text;
	proposal.pipes = {QPointer<Conductor>(plan.pipe)};
	proposal.element = nozzle->parentElement();
	proposal.nozzle = nozzle->baseName();
	proposal.at = nozzle;
	found << proposal;
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
			//Or a reducer on the side of the click (step 8r-3 d)
		addInsertReducer(found, nozzle);
		return found;
	}
	if (gap.kind == Terminal::DiameterGap::Reducer)
	{
			//Laid the wrong way: reversed; reducing nothing: no entry
		Element *reducer = nozzle->parentElement();
		if (reducer && nozzle->reducerReversed() && pipeEnds(reducer).size() == 2)
		{
			PidSizeCorrections::Proposal proposal;
			proposal.kind = PidSizeCorrections::Proposal::ReverseReducer;
			proposal.element = reducer;
			proposal.text = QCoreApplication::translate("PidSizeCorrections", "Reverse the reducer",
														"context menu action");
			found << proposal;
		}
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
		//Or a reducer between the two sizes (step 8r-3 d)
	addInsertReducer(found, nozzle);
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
	if (proposal.kind == Proposal::ReverseReducer) {
		Element *reducer = proposal.element.data();
		return reducer ? reverseReducer(reducer, proposal.text) : nullptr;
	}
	if (proposal.kind == Proposal::InsertReducer) {
		Terminal *nozzle = proposal.at.data();
		return nozzle ? insertReducer(nozzle, proposal.text) : nullptr;
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
