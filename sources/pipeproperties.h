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

#include <QColor>
#include <QDomElement>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

/**
	@brief The PipeProperties class
	The data of a pipe: a conductor of the pipe medium
	(ConductorProperties::m_medium). Nominal diameter, material, fluid,
	flow, line number, piping class, nominal pressure, kind of line, spool
	and external finish.
	They belong to one pipe only: they are never passed on to the pipes it
	is connected to (a pipe has no potential).
	They are saved as attributes of the conductor, prefixed with "pipe_",
	and only those that are set: see attributes().
*/
class PipeProperties
{
	public:
			/// The kind of line, which sets how it is drawn (ISO 10628-1,
			/// 5.3.1): the main flow lines, the subsidiary flow lines, and the
			/// energy carrier and auxiliary system lines. DEXPI knows a
			/// primary and a secondary segment only.
		enum LineKind { Main, Secondary, Auxiliary };

			/// Nominal diameter (DN) in millimetres, 0 when not set
		int nominal_diameter = 0;
			/// Pipe material: a code of the project's codification, or free
			/// text (steel, PVC, cast iron...)
		QString material;
			/// Fluid carried by the pipe: a code of the project's
			/// codification, or free text; see defaultFluids()
		QString fluid;
			/// Flow, free text so that the user keeps the unit ("120 m3/h")
		QString flow;
			/// Line number (pipeline number) of the line this pipe belongs
			/// to, free text; the natural key of a line list
		QString line_number;
			/// Piping class code, free text ("75HB13")
		QString piping_class;
			/// Nominal pressure, free text so that the standard is not fixed
			/// ("PN16")
		QString nominal_pressure;
			/// Kind of line, main by default
		LineKind line_kind = Main;
			/// Spool number: the prefabricated piece of the line this pipe
			/// belongs to, free text
		QString spool;
			/// External finish of the pipe (paint...), free text
		QString external_finish;
			/// A colour set on this pipe, before that of its fluid; none by
			/// default
		QColor color;
			/// The status of works, see PidStatus: empty for "to be built"
		QString status;

		QList<QPair<QString, QString>> attributes() const;
		void fromXml(const QDomElement &);

		static QString lineKindToString(LineKind kind);
		static LineKind lineKindFromString(const QString &string);
		static QStringList defaultFluids();
			/// The preferred DN values of ISO 6708:1995, clause 3
		static QList<int> preferredDiameters();

		bool isDefault() const { return *this == PipeProperties(); }
		bool operator==(const PipeProperties &) const;
		bool operator!=(const PipeProperties &) const;
};

#endif
