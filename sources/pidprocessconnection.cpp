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
#include "pidprocessconnection.h"

#include "conductorautonumerotation.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidfreeend.h"
#include "pipeconnection.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "signalproperties.h"
#include "undocommand/addgraphicsobjectcommand.h"

#include <QLineF>
#include <QUndoCommand>
#include <QUndoStack>

namespace {

	QString whereToChoose()
	{
		return PidProcessConnection::tr("properties of the project, P&ID, Connections");
	}

		/// The blind flange of the project, built, not on the folio: turned
		/// and placed so that its nozzle stands where @p free_end (the nozzle
		/// of a free end) is, facing the same way, and marked as placed by
		/// the tool. @p nozzle and @p signal get its nozzle and its signal
		/// terminal at the same place. nullptr, and @p why says it, when the
		/// project has none or one without them
	Element *builtBlindFlange(Diagram *diagram, const Terminal *free_end, Terminal **nozzle,
							  Terminal **signal, QString *why)
	{
		const ElementsLocation symbol = PidProcessConnection::blindFlangeSymbol(diagram);
		if (!symbol.isElement()) {
			*why = PidProcessConnection::tr("No blind flange is set to close a free end: choose one (%1).")
					.arg(whereToChoose());
			return nullptr;
		}
		ElementsLocation used = symbol;
		if (!(symbol.isProject() && symbol.project() == diagram->project())) {
			used = diagram->project()->importElement(used);
		}
		int state = 0;
		Element *flange = used.exist() ? ElementFactory::Instance()->createElement(used, nullptr, &state)
									   : nullptr;
		Terminal *n1 = nullptr;
		if (flange && !state) {
			for (Terminal *t : flange->terminals()) {
				if (t->medium() == TerminalData::Pipe) {
					n1 = t;
					break;
				}
			}
		}
		if (n1)
		{
			flange->takeLabelSize(diagram->project()->pidCodification());
			flange->takeOptionDefaults(diagram->project()->pidCodification());
			const int turns = (int(free_end->orientation()) - int(n1->orientation()) + 4) % 4;
			flange->setRotation(90.0 * turns);
			flange->setPos(QPointF());
			flange->setPos(free_end->dockConductor() - n1->dockConductor());
		}
			//Its signal terminal: the one nearest to its nozzle, at the same
			//place on a blind flange of the library
		Terminal *s1 = nullptr;
		qreal nearest = 0;
		if (n1) {
			for (Terminal *t : flange->terminals()) {
				if (t->medium() != TerminalData::Signal || t->isHiddenByOptions()) continue;
				const qreal distance = QLineF(t->dockConductor(), n1->dockConductor()).length();
				if (!s1 || distance < nearest) {
					s1 = t;
					nearest = distance;
				}
			}
		}
		if (!n1 || !s1) {
			delete flange;
			*why = PidProcessConnection::tr("The blind flange of the project needs a nozzle and a signal "
											"terminal: choose another one (%1).").arg(whereToChoose());
			return nullptr;
		}
		flange->setToolPlaced(true);
		*nozzle = n1;
		*signal = s1;
		return flange;
	}
}

/**
	@brief PidProcessConnection::blindFlangeSymbol
	See the declaration
*/
ElementsLocation PidProcessConnection::blindFlangeSymbol(const Diagram *diagram)
{
	QETProject *project = diagram ? diagram->project() : nullptr;
	if (!project) return ElementsLocation();
	const QString path = project->pidCodification().toolSymbol(PidCodification::blindFlangeRole());
	if (path.isEmpty()) return ElementsLocation();
		//A project given turns any path into one of its own collection
	const ElementsLocation location = path.startsWith(QLatin1String("embed://"))
			? ElementsLocation(path, project) : ElementsLocation(path);
	return location.isElement() && location.exist() ? location : ElementsLocation();
}

/**
	@brief PidProcessConnection::isClosureSignal
	See the declaration
*/
bool PidProcessConnection::isClosureSignal(const Terminal *terminal)
{
	return terminal && terminal->medium() == TerminalData::Signal && terminal->parentElement()
			&& terminal->parentElement()->isClosure();
}

/**
	@brief PidProcessConnection::isProcessEnd
	See the declaration
*/
bool PidProcessConnection::isProcessEnd(const Terminal *terminal)
{
	if (!terminal || !terminal->parentElement() || terminal->medium() != TerminalData::Pipe) {
		return false;
	}
	return PidFreeEnd::pipeOf(terminal) || terminal->hasRoomForWire();
}

/**
	@brief PidProcessConnection::mayJoin
	See the declaration
*/
bool PidProcessConnection::mayJoin(const Terminal *a, const Terminal *b)
{
	if (!a || !b) return false;
	const Terminal *signal = a->medium() == TerminalData::Signal ? a : b;
	const Terminal *process = signal == a ? b : a;
	if (signal->medium() != TerminalData::Signal || !signal->parentElement()
		|| !signal->hasRoomForWire() || !isProcessEnd(process)) {
		return false;
	}
	return signal->parentElement() != process->parentElement()
			&& signal->diagram() && signal->diagram() == process->diagram();
}

/**
	@brief PidProcessConnection::endNear
	See the declaration
*/
Terminal *PidProcessConnection::endNear(Diagram *diagram, const QPointF &scene_pos, qreal radius,
										const Terminal *from)
{
	if (!diagram || !from || radius <= 0) return nullptr;
	const QRectF area(scene_pos.x() - radius, scene_pos.y() - radius, 2 * radius, 2 * radius);
	Terminal *nearest = nullptr;
	qreal nearest_distance = 0;
	for (QGraphicsItem *item : diagram->items(area))
	{
		auto *terminal = qgraphicsitem_cast<Terminal *>(item);
		if (!terminal || !mayJoin(from, terminal)) continue;
		const qreal distance = QLineF(scene_pos, terminal->dockConductor()).length();
		if (distance > radius) continue;
		if (!nearest || distance < nearest_distance) {
			nearest = terminal;
			nearest_distance = distance;
		}
	}
	return nearest;
}

/**
	@brief PidProcessConnection::wayOut
	See the declaration
*/
Qet::Orientation PidProcessConnection::wayOut(const Terminal *terminal)
{
	return PidFreeEnd::wayOut(terminal);
}

/**
	@brief PidProcessConnection::draw
	See the declaration
*/
Conductor *PidProcessConnection::draw(Terminal *a, Terminal *b, QString *message)
{
	if (message) message->clear();
	if (!mayJoin(a, b)) return nullptr;
	Terminal *signal = a->medium() == TerminalData::Signal ? a : b;
	Terminal *process = signal == a ? b : a;
	Diagram *diagram = signal->diagram();
	if (!diagram || diagram->isReadOnly() || !diagram->project()) return nullptr;

	auto *undo = new QUndoCommand(tr("add a process connection", "undo caption"));
	Terminal *end = process;
	QString note;
	if (PidFreeEnd::pipeOf(process))
	{
			//Rule 1: the line ends on nothing; a blind flange closes it, and
			//the process connection leaves from it
		Terminal *nozzle = nullptr, *flange_signal = nullptr;
		QString why;
		Element *flange = builtBlindFlange(diagram, process, &nozzle, &flange_signal, &why);
		if (!flange) {
			delete undo;
			if (message) *message = why;
			return nullptr;
		}
		const PipeConnection::Type ending = process->connection();
		diagram->addItem(flange);
		new AddGraphicsObjectCommand(flange, diagram, flange->pos(), undo);
		if (!PidFreeEnd::close(diagram, process, nozzle, undo)) {
			diagram->removeItem(flange);
				//Its command lets the flange go
			delete undo;
			return nullptr;
		}
		if (ending != PipeConnection::Flange) {
			note = tr("A blind flange closes a line which ended with \"%1\": change the closure if "
					  "needed (right click, Change type).").arg(PipeConnection::name(ending));
		}
		end = flange_signal;
	}

		//A signal line whichever end it was drawn from: the medium of the
		//terminal it starts from
	auto *line = new Conductor(signal, end);
	ConductorProperties properties = line->properties();
	properties.m_signal.signal_type = SignalProperties::ProcessConnection;
	line->setProperties(properties);
	new AddGraphicsObjectCommand(line, diagram, QPointF(), undo);
	ConductorAutoNumerotation numbering(line, diagram, undo);
	numbering.numerate();
	diagram->undoStack().push(undo);
	if (message) *message = note;
	return line;
}
