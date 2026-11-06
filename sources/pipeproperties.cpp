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
	@return the word the files use for @p kind
*/
QString PipeProperties::lineKindToString(LineKind kind)
{
	switch (kind) {
		case Main:      return QStringLiteral("main");
		case Secondary: return QStringLiteral("secondary");
		case Auxiliary: return QStringLiteral("auxiliary");
	}
	return QStringLiteral("main");
}

/**
	@brief PipeProperties::lineKindFromString
	@param string
	@return the kind @p string names. A kind this version does not know is
	read as the main line, the way a DN it cannot read is left unset: the
	pipe is still loaded, and drawn like any other.
*/
PipeProperties::LineKind PipeProperties::lineKindFromString(const QString &string)
{
	if (string == QLatin1String("secondary")) return Secondary;
	if (string == QLatin1String("auxiliary")) return Auxiliary;
	return Main;
}

/**
	@brief PipeProperties::attributes
	@return the attributes of the conductor these properties are written
	as, in a fixed order: only those that are set, each prefixed with
	"pipe_" so that no attribute of a conductor is ever reused. The project
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
	if (nominal_diameter > 0)
		add("pipe_dn", QString::number(nominal_diameter));
	add("pipe_fluid", fluid);
	add("pipe_material", material);
	add("pipe_flow", flow);
	add("pipe_line_number", line_number);
	add("pipe_piping_class", piping_class);
	add("pipe_nominal_pressure", nominal_pressure);
	if (line_kind != Main)
		add("pipe_line_kind", lineKindToString(line_kind));
	add("pipe_spool", spool);
	add("pipe_external_finish", external_finish);
	if (color.isValid())
		add("pipe_color", color.name());
	add("pipe_status", status);
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
	bool ok = false;
	const int dn = e.attribute(QStringLiteral("pipe_dn")).toInt(&ok);
	nominal_diameter = (ok && dn > 0)? dn : 0;

	fluid            = e.attribute(QStringLiteral("pipe_fluid"));
	material         = e.attribute(QStringLiteral("pipe_material"));
	flow             = e.attribute(QStringLiteral("pipe_flow"));
	line_number      = e.attribute(QStringLiteral("pipe_line_number"));
	piping_class     = e.attribute(QStringLiteral("pipe_piping_class"));
	nominal_pressure = e.attribute(QStringLiteral("pipe_nominal_pressure"));
	line_kind        = lineKindFromString(e.attribute(QStringLiteral("pipe_line_kind")));
	spool            = e.attribute(QStringLiteral("pipe_spool"));
	external_finish  = e.attribute(QStringLiteral("pipe_external_finish"));
	color            = QColor(e.attribute(QStringLiteral("pipe_color")));
	status           = e.attribute(QStringLiteral("pipe_status"));
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
	return nominal_diameter == other.nominal_diameter
		&& material == other.material
		&& fluid == other.fluid
		&& flow == other.flow
		&& line_number == other.line_number
		&& piping_class == other.piping_class
		&& nominal_pressure == other.nominal_pressure
		&& line_kind == other.line_kind
		&& spool == other.spool
		&& external_finish == other.external_finish
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
