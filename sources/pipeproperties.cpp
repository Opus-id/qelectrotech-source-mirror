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
#include "pipeproperties.h"

#include <QCoreApplication>

/**
	@brief PipeProperties::lineKindToString
	@param kind
	@return the word the files use for @p kind: itself, the main line for
	none
*/
QString PipeProperties::lineKindToString(const LineKind &kind)
{
	return kind.isEmpty() ? Main : kind;
}

/**
	@brief PipeProperties::lineKindFromString
	@param string
	@return the kind @p string names, the main line for none. A word the
	project does not know (a kind added by another project, or by a later
	version) is kept: the pipe is drawn as a main line meanwhile
	(PidCodification::lineKind()), and saved with its word.
*/
PipeProperties::LineKind PipeProperties::lineKindFromString(const QString &string)
{
	const QString word = string.trimmed();
	return word.isEmpty() ? Main : word;
}

/**
	@brief PipeProperties::insulationKindToString
	@param kind
	@return the word the files use for @p kind; empty for none, which is
	not written
*/
QString PipeProperties::insulationKindToString(InsulationKind kind)
{
	switch (kind) {
		case NoInsulation:         return QString();
		case Insulation:           return QStringLiteral("insulation");
		case Tracing:              return QStringLiteral("tracing");
		case InsulationAndTracing: return QStringLiteral("insulation_tracing");
	}
	return QString();
}

/**
	@brief PipeProperties::insulationKindFromString
	@param string
	@return the kind @p string names. A word this version does not know is
	read as an insulation: something was said to wrap the pipe, and its
	stubs are drawn.
*/
PipeProperties::InsulationKind PipeProperties::insulationKindFromString(const QString &string)
{
	if (string.isEmpty()) return NoInsulation;
	if (string == QLatin1String("tracing")) return Tracing;
	if (string == QLatin1String("insulation_tracing")) return InsulationAndTracing;
	return Insulation;
}

/**
	@brief PipeProperties::insulationKinds
	See the declaration
*/
QList<QPair<PipeProperties::InsulationKind, QString>> PipeProperties::insulationKinds()
{
	return {
		{NoInsulation, QCoreApplication::translate("PipeProperties", "(None)", "insulation")},
		{Insulation, QCoreApplication::translate("PipeProperties", "Insulation")},
		{Tracing, QCoreApplication::translate("PipeProperties", "Heat tracing")},
		{InsulationAndTracing, QCoreApplication::translate("PipeProperties", "Insulation and heat tracing")}
	};
}

/**
	@brief PipeProperties::attributes
	@return the attributes of the conductor these properties are written
	as, in a fixed order: only those that are set, each prefixed with
	"pid_", as every P&ID attribute outside a P&ID block, so that no
	attribute of a conductor is ever reused. The project
	database stores a pipe's data as these, with the other attributes of
	the conductor.
*/
QList<QPair<QString, QString>> PipeProperties::attributes() const
{
	QList<QPair<QString, QString>> a;
	const auto add = [&a](const char *name, const QString &value) {
		if (!value.isEmpty())
			a << qMakePair(QString::fromLatin1(name), value);
	};
		//The DN, the size of the line, then the outside diameter of a
		//material named by it: each one only when set; a pipe never has a
		//thread, which only names a nozzle
	if (nominal.isNominal())
		a << qMakePair(QStringLiteral("pid_dn"), PipeDiameter::fileNumber(nominal.tenths()));
	if (outside.isOutside())
		a << qMakePair(QStringLiteral("pid_od"), PipeDiameter::fileNumber(outside.tenths()));
	add("pid_fluid", fluid);
	add("pid_material", material);
	add("pid_flow", flow);
	add("pid_line_number", line_number);
	add("pid_piping_class", piping_class);
	add("pid_nominal_pressure", nominal_pressure);
	if (lineKindToString(line_kind) != Main)
		add("pid_line_kind", lineKindToString(line_kind));
	add("pid_spool", spool);
	add("pid_external_finish", external_finish);
	add("pid_insulation_kind", insulationKindToString(insulation_kind));
	add("pid_insulation", insulation);
	if (insulation_mark)
		add("pid_insulation_mark", QStringLiteral("true"));
	add("pid_jacket_material", jacket_material);
	if (jacket_nominal.isNominal())
		a << qMakePair(QStringLiteral("pid_jacket_dn"), PipeDiameter::fileNumber(jacket_nominal.tenths()));
	if (jacket_outside.isOutside())
		a << qMakePair(QStringLiteral("pid_jacket_od"), PipeDiameter::fileNumber(jacket_outside.tenths()));
	if (jacket_full)
		add("pid_jacket_full", QStringLiteral("true"));
	if (color.isValid())
		add("pid_color", color.name());
	add("pid_status", status);
	return a;
}

/**
	@brief PipeProperties::fromXml
	Read the pipe properties from the attributes of the conductor @p e.
	A missing or invalid attribute leaves the property unset.
	@param e the xml element of the conductor
*/
void PipeProperties::fromXml(const QDomElement &e)
{
		//Each as it is written: a pipe saved with its outside diameter
		//alone, before both were kept, has no DN, and is given none
	nominal = PipeDiameter::fromTenths(
				PipeDiameter::Nominal, PipeDiameter::tenthsOf(e.attribute(QStringLiteral("pid_dn"))));
	outside = PipeDiameter::fromTenths(
				PipeDiameter::Outside, PipeDiameter::tenthsOf(e.attribute(QStringLiteral("pid_od"))));

	fluid            = e.attribute(QStringLiteral("pid_fluid"));
	material         = e.attribute(QStringLiteral("pid_material"));
	flow             = e.attribute(QStringLiteral("pid_flow"));
	line_number      = e.attribute(QStringLiteral("pid_line_number"));
	piping_class     = e.attribute(QStringLiteral("pid_piping_class"));
	nominal_pressure = e.attribute(QStringLiteral("pid_nominal_pressure"));
	line_kind        = lineKindFromString(e.attribute(QStringLiteral("pid_line_kind")));
	spool            = e.attribute(QStringLiteral("pid_spool"));
	external_finish  = e.attribute(QStringLiteral("pid_external_finish"));
	insulation_kind  = insulationKindFromString(e.attribute(QStringLiteral("pid_insulation_kind")));
	insulation       = e.attribute(QStringLiteral("pid_insulation"));
	insulation_mark  = e.attribute(QStringLiteral("pid_insulation_mark")) == QLatin1String("true");
	jacket_material  = e.attribute(QStringLiteral("pid_jacket_material"));
	jacket_nominal   = PipeDiameter::fromTenths(
				PipeDiameter::Nominal, PipeDiameter::tenthsOf(e.attribute(QStringLiteral("pid_jacket_dn"))));
	jacket_outside   = PipeDiameter::fromTenths(
				PipeDiameter::Outside, PipeDiameter::tenthsOf(e.attribute(QStringLiteral("pid_jacket_od"))));
	jacket_full      = e.attribute(QStringLiteral("pid_jacket_full")) == QLatin1String("true");
	color          = QColor(e.attribute(QStringLiteral("pid_color")));
	status           = e.attribute(QStringLiteral("pid_status"));
}

/**
	@brief PipeProperties::preferredDiameters
	@return the preferred DN values of ISO 6708:1995 (clause 3), in order.
	Preferred, not the only ones: a pipe or a nozzle can have another.
*/
QList<int> PipeProperties::preferredDiameters()
{
	return {10, 15, 20, 25, 32, 40, 50, 60, 65, 80, 100, 125, 150, 200,
			250, 300, 350, 400, 450, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1400,
			1500, 1600, 1800, 2000, 2200, 2400, 2600, 2800, 3000, 3200, 3400, 3600, 3800, 4000};
}

/**
	@brief PipeProperties::defaultFluids
	@return the fluids offered by default to the user, translated.
	The fluid of a pipe is stored as the text the user chose, not as an
	index in this list: the list can change without affecting saved files.
*/
QStringList PipeProperties::defaultFluids()
{
	return QStringList{
		QCoreApplication::translate("PipeProperties", "Raw water"),
		QCoreApplication::translate("PipeProperties", "Treated water"),
		QCoreApplication::translate("PipeProperties", "Wastewater"),
		QCoreApplication::translate("PipeProperties", "Sludge"),
		QCoreApplication::translate("PipeProperties", "Air"),
		QCoreApplication::translate("PipeProperties", "Reagent")
	};
}

/**
	@brief PipeProperties::operator ==
	@param other the properties to compare with
	@return true if both sets of properties are identical
*/
bool PipeProperties::operator==(const PipeProperties &other) const
{
	return nominal == other.nominal
		&& outside == other.outside
		&& material == other.material
		&& fluid == other.fluid
		&& flow == other.flow
		&& line_number == other.line_number
		&& piping_class == other.piping_class
		&& nominal_pressure == other.nominal_pressure
		&& line_kind == other.line_kind
		&& spool == other.spool
		&& external_finish == other.external_finish
		&& insulation_kind == other.insulation_kind
		&& insulation == other.insulation
		&& insulation_mark == other.insulation_mark
		&& sameJacket(other)
		&& jacket_full == other.jacket_full
		&& color == other.color
		&& status == other.status;
}

/**
	@brief PipeProperties::operator !=
	@param other the properties to compare with
	@return true if the properties differ
*/
bool PipeProperties::operator!=(const PipeProperties &other) const
{
	return !(*this == other);
}
