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

#include "pipediameter.h"

#include <QColor>
#include <QDomElement>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

/**
	@brief The PipeProperties class
	The data of a pipe: a conductor of the pipe medium
	(ConductorProperties::m_medium). Nominal diameter, and outside diameter
	for a material named by it, material, fluid,
	flow, line number, piping class, nominal pressure, kind of line, spool,
	external finish, insulation and heat tracing.
	They belong to one pipe only: they are never passed on to the pipes it
	is connected to (a pipe has no potential).
	They are saved as attributes of the conductor, prefixed with "pid_",
	and only those that are set: see attributes().
*/
class PipeProperties
{
	public:
			/// The kind of line, which sets how it is drawn: a word, that of
			/// the files. Those of ISO 10628-1 (5.3.1), which every project
			/// has: the main flow lines, the subsidiary flow lines, and the
			/// energy carrier and auxiliary system lines; or the code of a
			/// kind the project adds (PidCodification::line_kinds, step
			/// 8r-6). DEXPI knows a primary and a secondary segment only.
		using LineKind = QString;
		static inline const QString Main = QStringLiteral("main");
		static inline const QString Secondary = QStringLiteral("secondary");
		static inline const QString Auxiliary = QStringLiteral("auxiliary");
			/// What the pipe is wrapped in, which sets the stubs drawn where
			/// it begins and ends (Conductor::insulationStubs()): hatches for
			/// an insulation, dashes for a heat tracing, both for both
		enum InsulationKind { NoInsulation, Insulation, Tracing, InsulationAndTracing };

			/// The size of the line: its DN (attribute pid_dn), whatever
			/// the material. It is what is passed on along the line,
			/// compared with the nozzles and shown by the nozzles which
			/// follow the pipe; none when not set
		PipeDiameter nominal;
			/// A second value (attribute pid_od), for a material whose
			/// pipes are named by their outside diameter: a plastic pipe of
			/// DN 100 is a Ø 110 or a Ø 125; none when not set
		PipeDiameter outside;
			/// The size of the line: the DN; without one, the outside
			/// diameter (a pipe drawn before both were kept)
		PipeDiameter lineSize() const { return nominal.isSet() ? nominal : outside; }
			/// What is written of the size: the material chooses, its
			/// outside diameter when it names its pipes so
			/// (@p named_by_outside), else the DN; the other value when the
			/// one it names is not set
		PipeDiameter writtenSize(bool named_by_outside) const {
			if (named_by_outside) {
				return outside.isSet() ? outside : nominal;
			}
			return lineSize();
		}
			/// The pipe has a DN or an outside diameter
		bool hasSize() const { return nominal.isSet() || outside.isSet(); }
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
			/// Insulated, heat traced, both or none (attribute
			/// pid_insulation_kind). It belongs to this pipe only, never
			/// passed on: one part of a line may be insulated, the other not.
		InsulationKind insulation_kind = NoInsulation;
			/// What is written of the insulation, free text ("Q 80"): the
			/// {insulation} of the designation, and the text beside the mark
			/// in the middle (attribute pid_insulation)
		QString insulation;
			/// A hatched mark is drawn in the middle of the pipe, with
			/// insulation beside it, besides the stubs at its ends (attribute
			/// pid_insulation_mark); none by default
		bool insulation_mark = false;
			/// An insulation is drawn: hatches
		bool insulated() const {
			return insulation_kind == Insulation || insulation_kind == InsulationAndTracing;
		}
			/// A heat tracing is drawn: dashes
		bool traced() const {
			return insulation_kind == Tracing || insulation_kind == InsulationAndTracing;
		}
			/// The jacket of a double containment pipe (step 8r-1): the pipe
			/// runs in a second one, "PTFE 4x6 in PVC Ø 63". Its material, a
			/// code of the project or free text (attribute pid_jacket_material)
		QString jacket_material;
			/// Its size, as that of the pipe: a DN (pid_jacket_dn), and an
			/// outside diameter for a material named by it (pid_jacket_od)
		PipeDiameter jacket_nominal;
		PipeDiameter jacket_outside;
			/// What is written of the size of the jacket: see writtenSize()
		PipeDiameter jacketWrittenSize(bool named_by_outside) const {
			if (named_by_outside) {
				return jacket_outside.isSet() ? jacket_outside : jacket_nominal;
			}
			return jacket_nominal.isSet() ? jacket_nominal : jacket_outside;
		}
			/// The pipe runs in a jacket: its material or its size is set.
			/// Passed on along the line as the insulation, up to a jacket
			/// limit or equipment (Conductor::LineReach::Jacket)
		bool jacketed() const {
			return !jacket_material.isEmpty() || jacket_nominal.isSet() || jacket_outside.isSet();
		}
			/// The same jacket as @p other: material and sizes
		bool sameJacket(const PipeProperties &other) const {
			return jacket_material == other.jacket_material && jacket_nominal == other.jacket_nominal
					&& jacket_outside == other.jacket_outside;
		}
			/// Take the jacket of @p other (material and sizes, not
			/// jacket_full, which is this pipe's own)
		void takeJacket(const PipeProperties &other) {
			jacket_material = other.jacket_material;
			jacket_nominal = other.jacket_nominal;
			jacket_outside = other.jacket_outside;
		}
			/// The jacket is drawn along the whole pipe, not only as stubs
			/// where it begins and ends (attribute pid_jacket_full); this
			/// pipe only, never passed on
		bool jacket_full = false;
			/// A colour set on this pipe, before that of its fluid; none by
			/// default
		QColor color;
			/// The status of works, see PidStatus: empty for "to be built"
		QString status;

		QList<QPair<QString, QString>> attributes() const;
		void fromXml(const QDomElement &);

		static QString lineKindToString(const LineKind &kind);
		static LineKind lineKindFromString(const QString &string);
		static QString insulationKindToString(InsulationKind kind);
		static InsulationKind insulationKindFromString(const QString &string);
			/// The kinds of insulation the user picks from, with their
			/// names, in order, none first
		static QList<QPair<InsulationKind, QString>> insulationKinds();
		static QStringList defaultFluids();
			/// The preferred DN values of ISO 6708:1995, clause 3
		static QList<int> preferredDiameters();

		bool isDefault() const { return *this == PipeProperties(); }
		bool operator==(const PipeProperties &) const;
		bool operator!=(const PipeProperties &) const;
};

#endif
