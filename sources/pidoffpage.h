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
#ifndef PIDOFFPAGE_H
#define PIDOFFPAGE_H

#include <QPainterPath>
#include <QRectF>
#include <QStringList>
#include <QUuid>

class Element;
class PipeProperties;
class QPainter;
class QUndoCommand;

/**
	The off-page connectors of a pipe (step 8c-3): a folio report (QET's
	next_report / previous_report) whose terminal is a nozzle. Each side of
	a pair keeps the data of its own pipe; they are compared, never passed
	on unasked (copyFromTwin(), on demand, step 8c-4). A connector whose pipe differs from that of its twin, or which has
	no twin, is veiled in orange, on the screen only, and its tool tip says
	why. The twin's pipe is read from the project database while its folio
	is not built (lazy folios): nothing is built to compare. A battery limit
	is not a folio report: it is never marked.
*/
namespace PidOffPage {

	/// True for a folio report whose terminal is a nozzle
bool isPipeReport(const Element *element);
	/// The uuid of the twin of the pipe off-page connector @p report: the
	/// connector it is linked to, else the one the rows of the project
	/// database link it to (a folio not built); null for none
QUuid twin(const Element *report);
	/// The pipe at the nozzle of @p report, put in @p pipe: from the folio
	/// when it is built, else from the project database; false if none
bool pipeAt(const Element *report, PipeProperties *pipe);
	/// The pipe at the nozzle of the twin of @p report (twin()), put in
	/// @p pipe; false if no twin or no pipe there
bool twinPipe(const Element *report, PipeProperties *pipe);
	/// The data compared which differ between @p here and @p there, one
	/// line each ("Fluid: EB here, nothing on the other side"): size,
	/// material, piping class, nominal pressure, fluid, line number, kind
	/// of line, insulation (kind and text), flow, status of works. Filled
	/// on one side and empty on the other is a difference.
QStringList differences(const PipeProperties &here, const PipeProperties &there);
	/// What the tool tip of @p element says of its veil: no twin, or the
	/// data which differ from its twin's; nothing otherwise and for any
	/// symbol but a pipe off-page connector
QStringList warnings(const Element *element);
	/// "Copy from the other side" (step 8c-4), the step which gives the pipe
	/// of the off-page connector @p report, on its folio, the data of the
	/// pipe of its twin, read as warnings() reads it: nothing on the folio
	/// of the twin is built or changed. A value is copied when it is filled
	/// on the other side and differs here: what is filled here is never
	/// emptied (the kind of line and the status of works are never empty).
	/// Each goes as far as in the pipe properties panel: the material
	/// first, then the piping class and the nominal pressure, up to a spec
	/// break or equipment; the size up to a reducer, refused when it is not
	/// a size of the material of this pipe; the fluid, the line number and
	/// the kind of line, the insulation, the flow, by their own rules
	/// (Conductor::inlineRun()); the status of works on this pipe only.
	/// Nothing is placed (a valve whose size no longer follows shows its
	/// diamond). Null, nothing to do, for a folio not built, no twin, no
	/// pipe on either side, or nothing to copy. @p said gets what to tell
	/// the user: what was copied, how many pipes changed, a size refused.
QUndoCommand *copyFromTwin(Element *report, QString *said = nullptr);
	/// copyFromTwin() has something to do: the "Correct" submenu of
	/// @p report then offers it
bool canCopyFromTwin(Element *report);
	/// The veil over a cartouche drawn in @p drawing: its body and its
	/// point, which is on its right as the fork's symbols are drawn
QPainterPath veil(const QRectF &drawing);
	/// Draw the veil of @p element when warnings() has something to say;
	/// on the screen only, as the other orange marks
void drawVeil(const Element *element, QPainter *painter);
	/// The pipe off-page connectors at the ends of the pipe the terminals
	/// of @p element hold, and their twins, are painted again (their pipe
	/// changed, or came, or went)
void refresh(Element *element);

} // namespace PidOffPage

#endif // PIDOFFPAGE_H
