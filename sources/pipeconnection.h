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
#include <QPainterPath>
#include <QString>

/**
	@brief The PipeConnection class
	How a pipe is joined to a nozzle: a flange, a plain end, a socket, a
	thread... The list is fixed: each kind of connection decides the kind of
	size the nozzle is named by (allows()), a flange a DN, a solvent socket an
	outside diameter, a thread inches.

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
					return {PipeDiameter::Nominal};
				case Thread:
					return {PipeDiameter::Thread};
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
			/// @p diameter as a nozzle of @p type is named by: itself when
			/// allowed; a DN as the thread the fixed table joins to it
			/// (PipeDiameter::threadTable()), and a thread as its DN; else
			/// none. A threaded nozzle given a DN before the threads existed
			/// is so read in inches.
		static PipeDiameter fitted(Type type, const PipeDiameter &diameter)
		{
			if (allows(type, diameter)) {
				return diameter;
			}
			const QList<PipeDiameter::Kind> taken = kinds(type);
			if (diameter.isNominal() && taken.contains(PipeDiameter::Thread)) {
				return PipeDiameter::threadOfNominal(diameter);
			}
			if (diameter.isThread() && taken.contains(PipeDiameter::Nominal)) {
				return diameter.threadNominal();
			}
			return PipeDiameter();
		}

			/// Where the mark of a connection is drawn, in units from the
			/// point where the pipe joins the nozzle, on the side of the
			/// pipe: never on the nozzle itself, which a valve has on the
			/// side of its body
		static constexpr qreal markDistance = 4;
			/// Where a pipe draws the counter-flange it brings to a flange
		static constexpr qreal counterFlangeDistance = 8;
			/// The straight length a pipe needs from a flange to draw its
			/// counter-flange: a pipe shorter, or turning before, has none
		static constexpr qreal counterFlangeRoom = 12;
			/// Half the length of a flange across the pipe: that of the side
			/// of the generic valve
		static constexpr qreal markHalfHeight = 10;

			/// The mark drawn at the end of a nozzle of @p type, in a frame
			/// whose x goes from the nozzle along the pipe and whose y goes
			/// across, in units: in @p strokes the lines, in @p fills the
			/// dots. A flange is a line across, a thread a shorter one, a
			/// socket a bracket open towards the pipe, a socket to weld the
			/// same with two dots, a socket with a gasket a rounded one, a
			/// butt weld a dot, a grooved end a collar; a plain end nothing.
		static void markShapes(Type type, QPainterPath *strokes, QPainterPath *fills)
		{
			const qreal d = markDistance;
			const qreal h = markHalfHeight;
			const qreal arms = d + 10;
			switch (type) {
				case Flange:
					strokes->moveTo(d, -h);
					strokes->lineTo(d, h);
					break;
				case PlainEnd:
					break;
				case SolventSocket:
				case FusionSocket:
					strokes->moveTo(arms, -h);
					strokes->lineTo(d, -h);
					strokes->lineTo(d, h);
					strokes->lineTo(arms, h);
					if (type == FusionSocket) {
						fills->addEllipse(QPointF(arms, -h), 2, 2);
						fills->addEllipse(QPointF(arms, h), 2, 2);
					}
					break;
				case GasketSocket:
					strokes->moveTo(arms, -h);
					strokes->quadTo(d, -h, d, 0);
					strokes->quadTo(d, h, arms, h);
					break;
				case Thread:
					strokes->moveTo(d, -h / 2);
					strokes->lineTo(d, h / 2);
					break;
				case ButtWeld:
					fills->addEllipse(QPointF(d, 0), 4, 4);
					break;
				case Grooved:
					strokes->addRect(QRectF(d - 2, -h, 10, 2 * h));
					break;
			}
		}
			/// A nozzle of @p type has a mark to draw
		static bool hasMark(Type type) { return type != PlainEnd; }

			/// Whether a nozzle of a placed element draws the mark of its
			/// connection: as its project says, or always, or never
		enum class MarkChoice { Project, Always, Never };
			/// The word the files use for @p choice; none for Project, never
			/// written
		static QString markChoiceToString(MarkChoice choice)
		{
			switch (choice) {
				case MarkChoice::Always: return QStringLiteral("always");
				case MarkChoice::Never:  return QStringLiteral("never");
				case MarkChoice::Project: break;
			}
			return QString();
		}
			/// The choice @p string names; anything else is Project
		static MarkChoice markChoiceFromString(const QString &string)
		{
			if (string == QLatin1String("always")) return MarkChoice::Always;
			if (string == QLatin1String("never")) return MarkChoice::Never;
			return MarkChoice::Project;
		}
};

	/// The connections a placed element gives its nozzles, by name of nozzle
using NozzleConnections = QMap<QString, PipeConnection::Type>;
	/// Whether the nozzles of a placed element draw the mark of their
	/// connection, by name of nozzle; none means as the project says
using NozzleMarks = QMap<QString, PipeConnection::MarkChoice>;

#endif
