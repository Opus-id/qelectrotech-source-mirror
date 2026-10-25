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
	@brief PipeProperties::PipeProperties
	Default constructor: nothing is set.
*/
PipeProperties::PipeProperties() :
	nominal_diameter(0),
	line_kind(Main)
{
}

namespace
{
	QString lineKindToString(PipeProperties::LineKind kind)
	{
		switch (kind) {
			case PipeProperties::Main:      return QStringLiteral("main");
			case PipeProperties::Secondary: return QStringLiteral("secondary");
			case PipeProperties::Auxiliary: return QStringLiteral("auxiliary");
		}
		return QStringLiteral("main");
	}

		/// A kind this version does not know is read as the main line,
		/// the way a DN it cannot read is left unset: the pipe is still
		/// loaded, and drawn like any other.
	PipeProperties::LineKind lineKindFromString(const QString &s)
	{
		if (s == QLatin1String("secondary")) return PipeProperties::Secondary;
		if (s == QLatin1String("auxiliary")) return PipeProperties::Auxiliary;
		return PipeProperties::Main;
	}
}

/**
	@brief PipeProperties::toXml
	Export the pipe properties to the attributes of the XML element 'e'
	@param e the xml element
*/
void PipeProperties::toXml(QDomElement &e) const
{
	e.setAttribute("dn", QString::number(nominal_diameter));
	e.setAttribute("material", material);
	e.setAttribute("fluid", fluid);
	e.setAttribute("flow", flow);
	e.setAttribute("line_number", line_number);
	e.setAttribute("piping_class", piping_class);
	e.setAttribute("nominal_pressure", nominal_pressure);
	e.setAttribute("line_kind", lineKindToString(line_kind));
}

/**
	@brief PipeProperties::fromXml
	Import the pipe properties from the attributes of the XML element 'e'.
	A missing or invalid attribute leaves the property unset.
	@param e the xml element
*/
void PipeProperties::fromXml(const QDomElement &e)
{
	bool ok = false;
	const int dn = e.attribute("dn").toInt(&ok);
	nominal_diameter = (ok && dn > 0)? dn : 0;

	material = e.attribute("material");
	fluid    = e.attribute("fluid");
	flow     = e.attribute("flow");
	line_number      = e.attribute("line_number");
	piping_class     = e.attribute("piping_class");
	nominal_pressure = e.attribute("nominal_pressure");
	line_kind        = lineKindFromString(e.attribute("line_kind"));
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
		&& line_kind == other.line_kind;
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
