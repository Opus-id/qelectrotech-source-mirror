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
#ifndef PIPE_PROPERTIES_H
#define PIPE_PROPERTIES_H

#include <QDomElement>
#include <QMetaType>
#include <QString>
#include <QStringList>

/**
	@brief The PipeProperties class
	This class represents the functional properties of a pipe line in a
	P&ID drawing: nominal diameter, material, fluid and flow.
	These properties belong to one pipe line only. Unlike the electrical
	properties of a conductor, they are never propagated to the pipe lines
	connected to it.
*/
class PipeProperties
{
	public:
		PipeProperties();

			/// Nominal diameter (DN) in millimetres, 0 when not set
		int nominal_diameter;
			/// Pipe material, free text (steel, PVC, cast iron...)
		QString material;
			/// Fluid carried by the pipe, free text; see defaultFluids()
		QString fluid;
			/// Flow, free text so that the user keeps the unit (e.g. "120 m3/h")
		QString flow;

		void toXml(QDomElement &) const;
		void fromXml(const QDomElement &);

		static QStringList defaultFluids();

		bool operator==(const PipeProperties &) const;
		bool operator!=(const PipeProperties &) const;
};

Q_DECLARE_METATYPE(PipeProperties)

#endif
