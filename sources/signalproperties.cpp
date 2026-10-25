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
#include "signalproperties.h"

namespace
{
		/// The words written in the files: they are part of the file
		/// format and never change, whatever the user interface says
	QString signalTypeToString(SignalProperties::SignalType type)
	{
		switch (type) {
			case SignalProperties::Undefined:
				return QStringLiteral("undefined");
			case SignalProperties::Pneumatic:
				return QStringLiteral("pneumatic");
			case SignalProperties::Electrical:
				return QStringLiteral("electrical");
			case SignalProperties::Hydraulic:
				return QStringLiteral("hydraulic");
			case SignalProperties::Capillary:
				return QStringLiteral("capillary");
			case SignalProperties::GuidedElectromagnetic:
				return QStringLiteral("guided_electromagnetic");
			case SignalProperties::DataLink:
				return QStringLiteral("data_link");
		}
		return QStringLiteral("undefined");
	}

		/// A type this version does not know is read as undefined, the way
		/// an unknown kind of pipe line is read as a main line: the signal
		/// line is still loaded, and drawn as a signal of unknown type.
	SignalProperties::SignalType signalTypeFromString(const QString &s)
	{
		if (s == QLatin1String("pneumatic")) return SignalProperties::Pneumatic;
		if (s == QLatin1String("electrical")) return SignalProperties::Electrical;
		if (s == QLatin1String("hydraulic")) return SignalProperties::Hydraulic;
		if (s == QLatin1String("capillary")) return SignalProperties::Capillary;
		if (s == QLatin1String("guided_electromagnetic")) {
			return SignalProperties::GuidedElectromagnetic;
		}
		if (s == QLatin1String("data_link")) return SignalProperties::DataLink;
		return SignalProperties::Undefined;
	}
}

/**
	@brief SignalProperties::SignalProperties
	Default constructor: the type of signal is undefined.
*/
SignalProperties::SignalProperties() :
	signal_type(Undefined)
{
}

/**
	@brief SignalProperties::toXml
	Export the signal properties to the attributes of the XML element 'e'
	@param e the xml element
*/
void SignalProperties::toXml(QDomElement &e) const
{
	e.setAttribute("signal_type", signalTypeToString(signal_type));
}

/**
	@brief SignalProperties::fromXml
	Import the signal properties from the attributes of the XML element 'e'.
	A missing or unknown type leaves the signal undefined.
	@param e the xml element
*/
void SignalProperties::fromXml(const QDomElement &e)
{
	signal_type = signalTypeFromString(e.attribute("signal_type"));
}

/**
	@brief SignalProperties::operator ==
	@param other the properties to compare with
	@return true if both sets of properties are identical
*/
bool SignalProperties::operator==(const SignalProperties &other) const
{
	return signal_type == other.signal_type;
}

/**
	@brief SignalProperties::operator !=
	@param other the properties to compare with
	@return true if the properties differ
*/
bool SignalProperties::operator!=(const SignalProperties &other) const
{
	return !(*this == other);
}
