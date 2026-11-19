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
#ifndef SIGNAL_PROPERTIES_H
#define SIGNAL_PROPERTIES_H

#include <QDomElement>
#include <QList>
#include <QPair>
#include <QString>

/**
	@brief The SignalProperties class
	The data of a signal line of a P&ID: a conductor of the signal medium
	(ConductorProperties::m_medium), which carries a signal from an
	instrument to another one, not a fluid. A signal line has no diameter,
	no material and no fluid: it is not a pipe, and it is not a wire either,
	even when the signal is an electrical one.
	It keeps the type of signal and the status of works. They belong to one
	line only: they are never passed on to the lines it is connected to.
	Each is saved as an attribute of the conductor, prefixed with "pid_", and
	only when it is set: see attributes().
*/
class SignalProperties
{
	public:
			/// The types of signal a line can carry: those of DEXPI
			/// (SignalConveyingTypeClassification: pneumatic, electrical,
			/// hydraulic, capillary and, for the guided electromagnetic
			/// signal, "conducted radiation"), the data link of
			/// ANSI/ISA-5.1-2009, table 5.3.2, and the connection of an
			/// instrument to the process of its table 5.3.1.
			/// The files hold words, not these numbers (typeToString()), and
			/// the lists offer them in the order of types(): a new type is
			/// added at the end here, wherever it is offered.
			/// Not yet: the supply lines, the unguided (wireless) signal,
			/// the mechanical link, the four kinds of communication link (a
			/// single DataLink for now), nor the direction of the signal.
		enum SignalType
		{
				/// The type does not matter, or is not known yet
			Undefined,
			Pneumatic,
			Electrical,
			Hydraulic,
				/// A capillary tube, filled thermal element
			Capillary,
				/// Guided electromagnetic or sonic signal, optical fibre
			GuidedElectromagnetic,
				/// Communication link between devices and systems
			DataLink,
				/// What links an instrument to the process it measures:
				/// from a nozzle, a blind flange or a pipe to the bubble.
				/// Not a pipe: it carries no data of a pipe, and ends on
				/// signal terminals as any signal line.
			ProcessConnection
		};

			/// Type of signal, undefined by default
		SignalType signal_type = Undefined;
			/// Status of works, as a pipe has one (PidStatus): empty for a
			/// line to be built, the default; "existing", "demolished"
		QString status;

		QList<QPair<QString, QString>> attributes() const;
		void fromXml(const QDomElement &);

		static QString typeToString(SignalType type);
		static SignalType typeFromString(const QString &string);
			/// Every type, in the order they are offered
		static QList<SignalType> types();
			/// The name of @p type for the user, translated
		static QString typeName(SignalType type);

		bool isDefault() const { return *this == SignalProperties(); }
		bool operator==(const SignalProperties &) const;
		bool operator!=(const SignalProperties &) const;
};

#endif
