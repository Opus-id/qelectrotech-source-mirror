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
#include <QMetaType>
#include <QString>

/**
	@brief The SignalProperties class
	This class represents the properties of a signal line in a P&ID drawing,
	that is a line that carries a signal from an instrument to another one,
	not a fluid. A signal line has no diameter, no material and no fluid: it
	is not a pipe, and it is not an electrical wire either, even when the
	signal is an electrical one.

	For now only the type of signal is kept.
*/
class SignalProperties
{
	public:
			/// The kinds of signal a line can carry. The first ones are those
			/// of DEXPI (SignalConveyingTypeClassification: pneumatic,
			/// electrical, hydraulic, capillary and, for the guided
			/// electromagnetic signal, "conducted radiation"). The line
			/// symbols are those of ANSI/ISA-5.1-2009, table 5.3.2.
			///
			/// Not yet: the supply lines, the unguided (wireless) signal,
			/// the mechanical link, the four kinds of communication link
			/// (a single DataLink for now), nor the direction of the signal.
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
			DataLink
		};

		SignalProperties();

			/// Type of signal, undefined by default
		SignalType signal_type;

		void toXml(QDomElement &) const;
		void fromXml(const QDomElement &);

		bool operator==(const SignalProperties &) const;
		bool operator!=(const SignalProperties &) const;
};

Q_DECLARE_METATYPE(SignalProperties)

#endif
