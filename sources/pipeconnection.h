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
	size the nozzle is named by (allows()), a flange a DN, a socket a DN or an
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
			Socket,			///< a socket: to glue, to weld or with a gasket, the
							///< material of the pipe tells them apart
			Thread,			///< a pipe thread
			ButtWeld,		///< a butt weld; a brazed joint too
			Grooved,		///< a grooved end and its coupling
			QuickCoupling	///< a quick coupling, its two halves alike
		};

			/// All of them, in the order the lists offer them
		static QList<Type> all()
		{
			return {Flange, PlainEnd, Socket, Thread, ButtWeld, Grooved, QuickCoupling};
		}

			/// The word the files use for @p type
		static QString toString(Type type)
		{
			switch (type) {
				case Flange:        return QStringLiteral("flange");
				case PlainEnd:      return QStringLiteral("plain_end");
				case Socket:        return QStringLiteral("socket");
				case Thread:        return QStringLiteral("thread");
				case ButtWeld:      return QStringLiteral("butt_weld");
				case Grooved:       return QStringLiteral("grooved");
				case QuickCoupling: return QStringLiteral("quick_coupling");
			}
			return QStringLiteral("flange");
		}
			/// The connection @p string names; anything else, the absence of
			/// the attribute included, is a flange. @p ok, if given, tells
			/// whether @p string was a word of the list. The three sockets
			/// of the first trials (to glue, to weld, with a gasket) are read
			/// as the socket they became.
		static Type fromString(const QString &string, bool *ok = nullptr)
		{
			for (Type type : all()) {
				if (string == toString(type)) {
					if (ok) *ok = true;
					return type;
				}
			}
			if (string == QLatin1String("solvent_socket")
				|| string == QLatin1String("fusion_socket")
				|| string == QLatin1String("gasket_socket")) {
				if (ok) *ok = true;
				return Socket;
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
				case Socket:
					return QCoreApplication::translate("PipeConnection", "Socket");
				case Thread:
					return QCoreApplication::translate("PipeConnection", "Pipe thread");
				case ButtWeld:
					return QCoreApplication::translate("PipeConnection", "Butt weld");
				case Grooved:
					return QCoreApplication::translate("PipeConnection", "Grooved");
				case QuickCoupling:
					return QCoreApplication::translate("PipeConnection", "Quick coupling");
			}
			return QString();
		}

			/// The kinds of size a nozzle of @p type is named by, the first
			/// being the one a number typed alone is read as. A socket as the
			/// plain end it receives: a DN or an outside diameter.
		static QList<PipeDiameter::Kind> kinds(Type type)
		{
			switch (type) {
				case Flange:
				case QuickCoupling:
					return {PipeDiameter::Nominal};
				case Thread:
					return {PipeDiameter::Thread};
				case PlainEnd:
				case Socket:
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

			/// Where the mark of a connection is drawn on a symbol a line runs
			/// through (a valve), in units from the point where the pipe
			/// joins the nozzle, on the side of the pipe: such a symbol has its
			/// nozzles on the side of its body, which a mark there would
			/// merge with. On any other symbol (equipment), whose nozzles are
			/// at the end of a short line, the mark is on the nozzle itself.
		static constexpr qreal inlineMarkDistance = 4;
			/// Where a pipe draws the flange it brings to a flange: the
			/// counter-flange of an equipment, the only flange drawn on a
			/// valve, whose side stands for its own
		static constexpr qreal pipeFlangeDistance = 4;
			/// The straight length a pipe needs beyond the part it brings
			/// (pipePartStart()) to draw it: a pipe shorter, or turning
			/// before, has none
		static constexpr qreal pipePartMargin = 8;
			/// Half the length of a flange across the pipe: that of the side
			/// of the generic valve
		static constexpr qreal markHalfHeight = 10;

			/// The mark a nozzle of @p type draws at its end, in a frame whose
			/// x goes from the nozzle along the pipe and whose y goes across,
			/// in units, the mark being at @p at (0 on an equipment,
			/// inlineMarkDistance on a valve): in @p strokes the lines, in
			/// @p fills the dots. A flange is a line across; a socket a bracket
			/// open towards the pipe; a thread two short lines along the
			/// pipe, either side of it, after the side of a valve or astride
			/// the end of the nozzle of an equipment; a butt weld a dot; a
			/// grooved end a shorter line in a collar with hooks; a quick
			/// coupling its half, a hook; a plain end nothing.
		static void markShapes(Type type, qreal at, QPainterPath *strokes, QPainterPath *fills)
		{
			const qreal d = at;
			const qreal h = markHalfHeight;
			switch (type) {
				case Flange:
					strokes->moveTo(d, -h);
					strokes->lineTo(d, h);
					break;
				case PlainEnd:
					break;
				case Socket:
					strokes->moveTo(d + 10, -h);
					strokes->lineTo(d, -h);
					strokes->lineTo(d, h);
					strokes->lineTo(d + 10, h);
					break;
				case Thread: {
					const qreal from = d > 0 ? d : d - 4;
					strokes->moveTo(from, -6);
					strokes->lineTo(from + 8, -6);
					strokes->moveTo(from, 6);
					strokes->lineTo(from + 8, 6);
					break;
				}
				case ButtWeld:
					fills->addEllipse(QPointF(d, 0), 4, 4);
					break;
				case Grooved:
					strokes->moveTo(d, -7);
					strokes->lineTo(d, 7);
					strokes->moveTo(d - 2, -8);
					strokes->lineTo(d - 2, -h);
					strokes->lineTo(d + 6, -h);
					strokes->lineTo(d + 6, -8);
					strokes->moveTo(d - 2, 8);
					strokes->lineTo(d - 2, h);
					strokes->lineTo(d + 6, h);
					strokes->lineTo(d + 6, 8);
					break;
				case QuickCoupling:
					strokes->moveTo(d, -h);
					strokes->lineTo(d, h);
					strokes->lineTo(d + 6, h);
					break;
			}
		}
			/// A nozzle of @p type draws a mark of its own: all but a plain
			/// end, whose drawing is the socket the pipe brings
		static bool hasOwnMark(Type type) { return type != PlainEnd; }
			/// The connection whose setting of the project draws @p type: its
			/// own, but the sockets for a plain end, which a socket joins
		static Type drawnWith(Type type) { return type == PlainEnd ? Socket : type; }

			/// A pipe joined to a nozzle of @p type brings a part of its own,
			/// drawn in its color (pipePartShapes()): its flange to a flange,
			/// its socket to a plain end, its grooved end to a grooved end, its
			/// half to a quick coupling
		static bool bringsPart(Type type)
		{
			return type == Flange || type == PlainEnd || type == Grooved || type == QuickCoupling;
		}
			/// Where a pipe begins when it draws the part it brings to a
			/// nozzle of @p type whose mark is at @p at: after that part
		static qreal pipePartStart(Type type, qreal at)
		{
			switch (type) {
				case Flange:        return pipeFlangeDistance;
				case PlainEnd:      return at + 10;
				case Grooved:       return at + 4;
				case QuickCoupling: return at + 10;
				default:            return 0;
			}
		}
			/// The part a pipe brings to a nozzle of @p type whose mark is at
			/// @p at, in the frame of markShapes(): a flange 1 mm from the
			/// nozzle or from the side of a valve; a socket open towards the
			/// nozzle, which enters it; a grooved end 1 mm after that of the
			/// nozzle, in its collar; the other half of a quick coupling,
			/// the same hook turned over, 1 mm from each line of the first
		static void pipePartShapes(Type type, qreal at, QPainterPath *strokes)
		{
			const qreal h = markHalfHeight;
			switch (type) {
				case Flange:
					strokes->moveTo(pipeFlangeDistance, -h);
					strokes->lineTo(pipeFlangeDistance, h);
					break;
				case PlainEnd:
					strokes->moveTo(at, -h);
					strokes->lineTo(at + 10, -h);
					strokes->lineTo(at + 10, h);
					strokes->lineTo(at, h);
					break;
				case Grooved:
					strokes->moveTo(at + 4, -7);
					strokes->lineTo(at + 4, 7);
					break;
				case QuickCoupling:
					strokes->moveTo(at + 10, h);
					strokes->lineTo(at + 10, -h);
					strokes->lineTo(at + 4, -h);
					break;
				default:
					break;
			}
		}
			/// Where a pipe joined to a nozzle of @p type, whose mark is drawn
			/// at @p at, begins: after the part it brings when it draws it
			/// (@p brings_part); else at the mark of the nozzle beyond it (on
			/// a valve), or at the nozzle
		static qreal pipeStart(Type type, qreal at, bool brings_part)
		{
			if (brings_part && bringsPart(type)) {
				return pipePartStart(type, at);
			}
			return hasOwnMark(type) && type != Flange ? at : 0;
		}

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
