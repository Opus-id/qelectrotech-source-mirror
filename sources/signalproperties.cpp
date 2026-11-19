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

#include <QCoreApplication>

/**
	@brief SignalProperties::typeToString
	@param type
	@return the word the files use for @p type: part of the file format, it
	never changes, whatever the user interface says
*/
QString SignalProperties::typeToString(SignalType type)
{
	switch (type) {
		case Undefined:             return QStringLiteral("undefined");
		case Pneumatic:             return QStringLiteral("pneumatic");
		case Electrical:            return QStringLiteral("electrical");
		case Hydraulic:             return QStringLiteral("hydraulic");
		case Capillary:             return QStringLiteral("capillary");
		case GuidedElectromagnetic: return QStringLiteral("guided_electromagnetic");
		case DataLink:              return QStringLiteral("data_link");
		case ProcessConnection:     return QStringLiteral("process_connection");
	}
	return QStringLiteral("undefined");
}

/**
	@brief SignalProperties::typeFromString
	@param string
	@return the type @p string names. A type this version does not know is
	read as undefined, the way an unknown kind of pipe line is read as a
	main line: the signal line is still loaded, and drawn as a signal of
	unknown type.
*/
SignalProperties::SignalType SignalProperties::typeFromString(const QString &string)
{
	if (string == QLatin1String("pneumatic")) return Pneumatic;
	if (string == QLatin1String("electrical")) return Electrical;
	if (string == QLatin1String("hydraulic")) return Hydraulic;
	if (string == QLatin1String("capillary")) return Capillary;
	if (string == QLatin1String("guided_electromagnetic")) return GuidedElectromagnetic;
	if (string == QLatin1String("data_link")) return DataLink;
	if (string == QLatin1String("process_connection")) return ProcessConnection;
	return Undefined;
}

/**
	@brief SignalProperties::types
	@return every type of signal, in the order they are offered to the user:
	the connection to the process just after the undefined type, as it is
	the one most drawn around an instrument
*/
QList<SignalProperties::SignalType> SignalProperties::types()
{
	return {Undefined, ProcessConnection, Pneumatic, Electrical, Hydraulic,
			Capillary, GuidedElectromagnetic, DataLink};
}

/**
	@brief SignalProperties::typeName
	@param type
	@return the name of @p type shown to the user, translated. The files
	never hold it: see typeToString().
*/
QString SignalProperties::typeName(SignalType type)
{
	switch (type) {
		case Undefined:
			return QCoreApplication::translate("SignalProperties", "Undefined", "type of signal");
		case Pneumatic:
			return QCoreApplication::translate("SignalProperties", "Pneumatic", "type of signal");
		case Electrical:
			return QCoreApplication::translate("SignalProperties", "Electrical", "type of signal");
		case Hydraulic:
			return QCoreApplication::translate("SignalProperties", "Hydraulic", "type of signal");
		case Capillary:
			return QCoreApplication::translate("SignalProperties", "Capillary", "type of signal");
		case GuidedElectromagnetic:
			return QCoreApplication::translate("SignalProperties", "Guided electromagnetic",
											   "type of signal");
		case DataLink:
			return QCoreApplication::translate("SignalProperties", "Data link", "type of signal");
		case ProcessConnection:
			return QCoreApplication::translate("SignalProperties", "Process connection",
											   "type of signal: links an instrument to the process");
	}
	return QString();
}

/**
	@brief SignalProperties::attributes
	@return the attributes of the conductor these properties are written
	as: only those that are set, each prefixed with "pid_", as every P&ID
	attribute outside a P&ID block. A signal line of undefined type, to be
	built, writes none. The status of works takes the attribute of a pipe,
	pid_status: a conductor is one or the other. The project database stores
	the data of a signal line as these, with the other attributes of the
	conductor.
*/
QList<QPair<QString, QString>> SignalProperties::attributes() const
{
	QList<QPair<QString, QString>> a;
	if (signal_type != Undefined)
		a << qMakePair(QStringLiteral("pid_signal_type"), typeToString(signal_type));
	if (!status.isEmpty())
		a << qMakePair(QStringLiteral("pid_status"), status);
	return a;
}

/**
	@brief SignalProperties::fromXml
	Read the signal properties from the attributes of the conductor @p e.
	A missing or unknown type leaves the signal undefined; a status this
	version does not know is kept as it is written.
	@param e the xml element of the conductor
*/
void SignalProperties::fromXml(const QDomElement &e)
{
	signal_type = typeFromString(e.attribute(QStringLiteral("pid_signal_type")));
	status = e.attribute(QStringLiteral("pid_status"));
}

/**
	@brief SignalProperties::operator ==
	@param other the properties to compare with
	@return true if both sets of properties are identical
*/
bool SignalProperties::operator==(const SignalProperties &other) const
{
	return signal_type == other.signal_type
		&& status == other.status;
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
