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
#include "pidoffpage.h"

#include "QPropertyUndoCommand/qpropertyundocommand.h"
#include "conductorproperties.h"
#include "dataBase/projectdatabase.h"
#include "diagram.h"
#include "pidcodification.h"
#include "pidstatus.h"
#include "pipeproperties.h"
#include "qetpalette.h"
#include "qetproject.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/reportelement.h"
#include "qetgraphicsitem/terminal.h"

#include <QCoreApplication>
#include <QHash>
#include <QPainter>
#include <QUndoCommand>
#include <QVariant>

#include <functional>

namespace {

	/// The project database of @p element, if it holds the link rows (a
	/// project opened from a file, or saved once): else the links are those
	/// built only
projectDataBase *linkStore(const Element *element)
{
	Diagram *folio = element ? element->diagram() : nullptr;
	QETProject *project = folio ? folio->project() : nullptr;
	projectDataBase *store = project ? project->dataBase() : nullptr;
	return store && store->holdsLinks() ? store : nullptr;
}

	/// The pipe among @p wires, built: false if none
bool builtPipe(const QList<Conductor *> &wires, PipeProperties *pipe)
{
	for (const Conductor *wire : wires) {
		if (wire->medium() == TerminalData::Pipe) {
			*pipe = wire->properties().m_pipe;
			return true;
		}
	}
	return false;
}

	/// The pipe among the wires @p wires of the project database: false if
	/// none
bool storedPipe(const projectDataBase *store, const QList<QUuid> &wires, PipeProperties *pipe)
{
	for (const QUuid &wire : wires) {
		if (!store->hasConductorProperties(wire)) {
			continue;
		}
		const ConductorProperties properties = store->conductorProperties(wire);
		if (properties.m_medium == TerminalData::Pipe) {
			*pipe = properties.m_pipe;
			return true;
		}
	}
	return false;
}

QList<Conductor *> wiresOf(const Element *element)
{
	QList<Conductor *> wires;
	for (const Terminal *terminal : element->terminals()) {
		wires << terminal->conductors();
	}
	return wires;
}

	/// What @p value reads as in a tool tip: itself, or "nothing"
QString shown(const QString &value)
{
	return value.isEmpty() ? QCoreApplication::translate("PidOffPage", "nothing") : value;
}

	/// The name of a kind of line of ISO 10628-1; the code of a kind added
	/// by the project (step 8r-6)
QString lineKindName(const PipeProperties::LineKind &kind)
{
	if (kind == PipeProperties::Secondary) {
		return QCoreApplication::translate("PidOffPage", "secondary flow line");
	}
	if (kind == PipeProperties::Auxiliary) {
		return QCoreApplication::translate("PidOffPage", "auxiliary system line");
	}
	if (PipeProperties::lineKindToString(kind) == PipeProperties::Main) {
		return QCoreApplication::translate("PidOffPage", "main flow line");
	}
	return kind;
}

QString insulationKindName(PipeProperties::InsulationKind kind)
{
	for (const auto &named : PipeProperties::insulationKinds()) {
		if (named.first == kind) {
			return named.second;
		}
	}
	return QString();
}

QString statusName(const QString &status)
{
	for (const auto &named : PidStatus::statuses()) {
		if (named.first == status) {
			return named.second;
		}
	}
	return status;
}

	/// The pipe at the nozzle of @p element, built; null if none
Conductor *builtPipeWire(const Element *element)
{
	for (Conductor *wire : wiresOf(element)) {
		if (wire->medium() == TerminalData::Pipe) {
			return wire;
		}
	}
	return nullptr;
}

	/// @p start and the pipes a change of @p start reaches with @p reach, as
	/// in the pipe properties panel (Conductor::inlineRun())
QList<Conductor *> runFrom(Conductor *start, Conductor::LineReach reach)
{
	QList<Conductor *> run{start};
	for (Conductor *next : start->inlineRun(reach)) {
		if (!run.contains(next)) {
			run << next;
		}
	}
	return run;
}

} // namespace

bool PidOffPage::isPipeReport(const Element *element)
{
	if (!element || !(element->linkType() & Element::AllReport)) {
		return false;
	}
	for (const Terminal *terminal : element->terminals()) {
		if (terminal->medium() == TerminalData::Pipe) {
			return true;
		}
	}
	return false;
}

QUuid PidOffPage::twin(const Element *report)
{
	const auto *connector = qobject_cast<const ReportElement *>(report);
	if (!connector || !isPipeReport(report)) {
		return QUuid();
	}
	const QList<Element *> linked = report->linkedElements();
	if (!linked.isEmpty()) {
		return linked.first()->uuid();
	}
		//Not linked: its twin may wait on a folio not built. The rows are
		//read once, until the link of the connector changes
	ReportElement::PidTwin &kept = connector->pidTwin();
	if (!kept.known) {
		kept.known = true;
		kept.uuid = QUuid();
		if (projectDataBase *store = linkStore(report)) {
			const auto rows = store->partnersToLink(report->uuid());
			if (!rows.isEmpty()) {
				kept.uuid = rows.first().first;
			}
		}
	}
	return kept.uuid;
}

bool PidOffPage::pipeAt(const Element *report, PipeProperties *pipe)
{
	if (!report || !pipe) {
		return false;
	}
	const Diagram *folio = report->diagram();
	if (!folio || folio->isLoaded()) {
		return builtPipe(wiresOf(report), pipe);
	}
		//Built alone, as the partner of a folio shown: its folio, not
		//built, keeps its pipe in the database
	const projectDataBase *store = linkStore(report);
	return store && storedPipe(store, store->conductorsAt(report->uuid()), pipe);
}

bool PidOffPage::twinPipe(const Element *report, PipeProperties *pipe)
{
	const QUuid uuid = twin(report);
	if (uuid.isNull() || !pipe) {
		return false;
	}
	const QList<Element *> linked = report->linkedElements();
	if (!linked.isEmpty() && linked.first()->diagram() && linked.first()->diagram()->isLoaded()) {
		return builtPipe(wiresOf(linked.first()), pipe);
	}
		//Its folio not built: the wires at the twin, from the database,
		//read once while it is not built (nothing on it changes then)
	const projectDataBase *store = linkStore(report);
	if (!store) {
		return false;
	}
	ReportElement::PidTwin &kept = qobject_cast<const ReportElement *>(report)->pidTwin();
	if (kept.wires_of != uuid) {
		kept.wires_of = uuid;
		kept.wires = store->conductorsAt(uuid);
	}
	return storedPipe(store, kept.wires, pipe);
}

QStringList PidOffPage::differences(const PipeProperties &here, const PipeProperties &there)
{
	QStringList lines;
	const auto compare = [&lines](const QString &data, const QString &a, const QString &b) {
		if (a != b) {
			lines << QCoreApplication::translate("PidOffPage", "%1: %2 here, %3 on the other side").arg(data, shown(a), shown(b));
		}
	};
	compare(QCoreApplication::translate("PidOffPage", "Size"), here.nominal.text(), there.nominal.text());
	compare(QCoreApplication::translate("PidOffPage", "Size"), here.outside.text(), there.outside.text());
	compare(QCoreApplication::translate("PidOffPage", "Material"), here.material, there.material);
	compare(QCoreApplication::translate("PidOffPage", "Piping class"), here.piping_class, there.piping_class);
	compare(QCoreApplication::translate("PidOffPage", "Nominal pressure"), here.nominal_pressure, there.nominal_pressure);
	compare(QCoreApplication::translate("PidOffPage", "Fluid"), here.fluid, there.fluid);
	compare(QCoreApplication::translate("PidOffPage", "Line number"), here.line_number, there.line_number);
	compare(QCoreApplication::translate("PidOffPage", "Kind of line"), lineKindName(here.line_kind), lineKindName(there.line_kind));
	compare(QCoreApplication::translate("PidOffPage", "Insulation"), insulationKindName(here.insulation_kind),
			insulationKindName(there.insulation_kind));
	compare(QCoreApplication::translate("PidOffPage", "Insulation text"), here.insulation, there.insulation);
	compare(QCoreApplication::translate("PidOffPage", "Flow"), here.flow, there.flow);
	compare(QCoreApplication::translate("PidOffPage", "Status of works"), statusName(here.status), statusName(there.status));
	return lines;
}

QStringList PidOffPage::warnings(const Element *element)
{
	if (!isPipeReport(element) || !qobject_cast<const ReportElement *>(element)) {
		return QStringList();
	}
	if (twin(element).isNull()) {
		return {QCoreApplication::translate("PidOffPage", "Off-page connector without a twin: no off-page connector "
				   "of another folio is linked to it. A battery limit has a "
				   "symbol of its own.")};
	}
	PipeProperties here;
	PipeProperties there;
	if (!pipeAt(element, &here) || !twinPipe(element, &there)) {
		return QStringList();
	}
	const QStringList lines = differences(here, there);
	if (lines.isEmpty()) {
		return lines;
	}
	return QStringList{QCoreApplication::translate("PidOffPage", "Its pipe differs from that of the off-page connector it is linked to:")} + lines;
}

QUndoCommand *PidOffPage::copyFromTwin(Element *report, QString *said)
{
	if (said) {
		said->clear();
	}
	const Diagram *folio = report ? report->diagram() : nullptr;
	Conductor *start = folio && folio->isLoaded() && isPipeReport(report)
			? builtPipeWire(report) : nullptr;
	PipeProperties there;
	if (!start || !twinPipe(report, &there)) {
		return nullptr;
	}
	const QETProject *project = folio->project();
	const PidCodification codes = project ? project->pidCodification() : PidCodification();

		//The data of the pipes reached, as they become
	QList<Conductor *> order;
	QHash<Conductor *, PipeProperties> after;
	const auto data = [&order, &after](Conductor *pipe) -> PipeProperties & {
		if (!after.contains(pipe)) {
			order << pipe;
			after.insert(pipe, pipe->properties().m_pipe);
		}
		return after[pipe];
	};
	const auto give = [&](Conductor::LineReach reach, const std::function<void(PipeProperties &)> &change) {
		for (Conductor *pipe : runFrom(start, reach)) {
			change(data(pipe));
		}
	};
		//A value is copied when it is filled on the other side and differs
		//here: what is filled here is never emptied
	const auto filled = [](const QString &here, const QString &value) {
		return !value.isEmpty() && here != value;
	};
	QStringList copied;
	QStringList notes;

		//The spec, the material first: it chooses how the size is written
	if (filled(data(start).material, there.material)) {
		give(Conductor::LineReach::Spec, [&](PipeProperties &p) { codes.giveMaterial(p, there.material); });
		copied << QCoreApplication::translate("PidOffPage", "Material");
	}
	if (filled(data(start).piping_class, there.piping_class)) {
		give(Conductor::LineReach::Spec, [&](PipeProperties &p) { p.piping_class = there.piping_class; });
		copied << QCoreApplication::translate("PidOffPage", "Piping class");
	}
	if (filled(data(start).nominal_pressure, there.nominal_pressure)) {
		give(Conductor::LineReach::Spec, [&](PipeProperties &p) { p.nominal_pressure = there.nominal_pressure; });
		copied << QCoreApplication::translate("PidOffPage", "Nominal pressure");
	}
		//The size, as the panel gives it to a line: the pipes of the
		//material of the other side take it as it is, the others the DN as
		//their material has it. Never a size which is not one of the
		//material of this pipe.
	const PipeProperties sized = data(start);
	if ((there.nominal.isSet() || there.outside.isSet())
			&& (sized.nominal != there.nominal || sized.outside != there.outside)) {
		const QString material = sized.material;
		if (there.nominal.isSet() && codes.material(material)
				&& !codes.lineSizes(material).contains(codes.nominalIn(there.nominal, material))) {
			notes << QCoreApplication::translate("PidOffPage", "Size not copied: %1 is not a size of the material %2.")
					 .arg(codes.text(there.nominal, there.material), material);
		} else {
			give(Conductor::LineReach::Size, [&](PipeProperties &p) {
				if (p.material == there.material) {
					p.nominal = there.nominal;
					p.outside = there.outside;
				} else if (there.nominal.isSet()) {
					codes.giveLineSize(p, there.nominal);
				}
			});
			copied << QCoreApplication::translate("PidOffPage", "Size");
		}
	}
	if (filled(data(start).fluid, there.fluid)) {
		give(Conductor::LineReach::Fluid, [&](PipeProperties &p) { p.fluid = there.fluid; });
		copied << QCoreApplication::translate("PidOffPage", "Fluid");
	}
	if (filled(data(start).line_number, there.line_number)) {
		give(Conductor::LineReach::LineNumber, [&](PipeProperties &p) { p.line_number = there.line_number; });
		copied << QCoreApplication::translate("PidOffPage", "Line number");
	}
		//The kind of a line goes as far as its number; it is never empty
	if (data(start).line_kind != there.line_kind) {
		give(Conductor::LineReach::LineNumber, [&](PipeProperties &p) { p.line_kind = there.line_kind; });
		copied << QCoreApplication::translate("PidOffPage", "Kind of line");
	}
	if (there.insulation_kind != PipeProperties::NoInsulation
			&& data(start).insulation_kind != there.insulation_kind) {
		give(Conductor::LineReach::Insulation, [&](PipeProperties &p) { p.insulation_kind = there.insulation_kind; });
		copied << QCoreApplication::translate("PidOffPage", "Insulation");
	}
	if (filled(data(start).insulation, there.insulation)) {
		give(Conductor::LineReach::Insulation, [&](PipeProperties &p) { p.insulation = there.insulation; });
		copied << QCoreApplication::translate("PidOffPage", "Insulation text");
	}
	if (filled(data(start).flow, there.flow)) {
		give(Conductor::LineReach::Flow, [&](PipeProperties &p) { p.flow = there.flow; });
		copied << QCoreApplication::translate("PidOffPage", "Flow");
	}
		//The status of works, of this pipe only, as in the panel; "to be
		//built" is a status, not nothing
	if (data(start).status != there.status) {
		data(start).status = there.status;
		copied << QCoreApplication::translate("PidOffPage", "Status of works");
	}

	auto *undo = new QUndoCommand(QCoreApplication::translate("PidOffPage", "Copy from the other side"));
	int changed = 0;
	for (Conductor *pipe : std::as_const(order)) {
		ConductorProperties properties = pipe->properties();
		properties.m_pipe = after.value(pipe);
		if (properties == pipe->properties()) {
			continue;
		}
		QVariant old_value, new_value;
		old_value.setValue(pipe->properties());
		new_value.setValue(properties);
		new QPropertyUndoCommand(pipe, "properties", old_value, new_value, undo);
		++changed;
	}
	if (!changed) {
		delete undo;
		return nullptr;
	}
	if (said) {
		QStringList lines{QCoreApplication::translate("PidOffPage", "Copied from the other side (%1): %n pipe(s) changed.",
													  "", changed).arg(copied.join(QStringLiteral(", ")))};
		*said = (lines + notes).join(QLatin1Char(' '));
	}
	return undo;
}

bool PidOffPage::canCopyFromTwin(Element *report)
{
	QUndoCommand *undo = copyFromTwin(report);
	const bool can = undo != nullptr;
	delete undo;
	return can;
}

QPainterPath PidOffPage::veil(const QRectF &drawing)
{
	const qreal point = qMin(drawing.height() / 2.0, drawing.width());
	QPainterPath path;
	path.moveTo(drawing.topLeft());
	path.lineTo(drawing.right() - point, drawing.top());
	path.lineTo(drawing.right(), drawing.center().y());
	path.lineTo(drawing.right() - point, drawing.bottom());
	path.lineTo(drawing.bottomLeft());
	path.closeSubpath();
	return path;
}

void PidOffPage::drawVeil(const Element *element, QPainter *painter)
{
	const Diagram *folio = element ? element->diagram() : nullptr;
	if (!folio || !folio->drawTerminals() || !isPipeReport(element)
			|| warnings(element).isEmpty()) {
		return;
	}
	QColor veil_colour = QET::Palette::signalColor();
	veil_colour.setAlpha(80);
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setPen(Qt::NoPen);
	painter->setBrush(veil_colour);
	painter->drawPath(veil(element->drawingRect()));
	painter->restore();
}

void PidOffPage::refresh(Element *element)
{
	if (!isPipeReport(element)) {
		return;
	}
	element->update();
	for (Element *partner : element->linkedElements()) {
		partner->update();
	}
}
