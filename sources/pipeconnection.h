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
#ifndef PIPE_CONNECTION_H
#define PIPE_CONNECTION_H

#include "pipediameter.h"

#include <QCoreApplication>
#include <QList>
#include <QMap>
#include <QString>

/**
	@brief The PipeConnection class
	How a pipe is joined to a nozzle: a flange, a plain end, a socket, a
	thread... The list is fixed: each kind of connection decides the kind of
	size the nozzle is named by (allows()), a flange a DN, a solvent socket an
	outside diameter.

	A flange is the default: a nozzle that says nothing is a flange, and a
	flange is never written.
*/
class PipeConnection
{
	public:
		enum Type {
			Flange,
			PlainEnd,
			SolventSocket,	///< a socket to glue (PVC, ABS)
			FusionSocket,	///< a socket to weld (electrofusion, socket fusion)
			GasketSocket,	///< a socket with a gasket (ductile iron, PVC)
			Thread,			///< a pipe thread
			ButtWeld,
			Grooved			///< a grooved end and its coupling
		};

			/// All of them, in the order the lists offer them
		static QList<Type> all()
		{
			return {Flange, PlainEnd, SolventSocket, FusionSocket, GasketSocket,
					Thread, ButtWeld, Grooved};
		}

			/// The word the files use for @p type
		static QString toString(Type type)
		{
			switch (type) {
				case Flange:        return QStringLiteral("flange");
				case PlainEnd:      return QStringLiteral("plain_end");
				case SolventSocket: return QStringLiteral("solvent_socket");
				case FusionSocket:  return QStringLiteral("fusion_socket");
				case GasketSocket:  return QStringLiteral("gasket_socket");
				case Thread:        return QStringLiteral("thread");
				case ButtWeld:      return QStringLiteral("butt_weld");
				case Grooved:       return QStringLiteral("grooved");
			}
			return QStringLiteral("flange");
		}
			/// The connection @p string names; anything else, the absence of
			/// the attribute included, is a flange. @p ok, if given, tells
			/// whether @p string was a word of the list.
		static Type fromString(const QString &string, bool *ok = nullptr)
		{
			for (Type type : all()) {
				if (string == toString(type)) {
					if (ok) *ok = true;
					return type;
				}
			}
			if (ok) *ok = false;
			return Flange;
		}

			/// The name of @p type, for the lists and the tooltips
		static QString name(Type type)
		{
			switch (type) {
				case Flange:
					return QCoreApplication::translate("PipeConnection", "Flange");
				case PlainEnd:
					return QCoreApplication::translate("PipeConnection", "Plain end");
				case SolventSocket:
					return QCoreApplication::translate("PipeConnection", "Solvent socket");
				case FusionSocket:
					return QCoreApplication::translate("PipeConnection", "Fusion socket");
				case GasketSocket:
					return QCoreApplication::translate("PipeConnection", "Gasket socket");
				case Thread:
					return QCoreApplication::translate("PipeConnection", "Pipe thread");
				case ButtWeld:
					return QCoreApplication::translate("PipeConnection", "Butt weld");
				case Grooved:
					return QCoreApplication::translate("PipeConnection", "Grooved");
			}
			return QString();
		}

			/// The kinds of size a nozzle of @p type is named by, the first
			/// being the one a number typed alone is read as
		static QList<PipeDiameter::Kind> kinds(Type type)
		{
			switch (type) {
				case Flange:
				case Thread:
					return {PipeDiameter::Nominal};
				case SolventSocket:
					return {PipeDiameter::Outside};
				case FusionSocket:
					return {PipeDiameter::Outside, PipeDiameter::Nominal};
				case PlainEnd:
				case GasketSocket:
				case ButtWeld:
				case Grooved:
					return {PipeDiameter::Nominal, PipeDiameter::Outside};
			}
			return {PipeDiameter::Nominal};
		}
			/// A nozzle of @p type may be named by @p diameter: true for none
		static bool allows(Type type, const PipeDiameter &diameter)
		{
			return !diameter.isSet() || kinds(type).contains(diameter.kind());
		}
};

	/// The connections a placed element gives its nozzles, by name of nozzle
using NozzleConnections = QMap<QString, PipeConnection::Type>;

#endif
