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
#ifndef PID_CODIFICATION_H
#define PID_CODIFICATION_H

#include <QDomDocument>
#include <QDomElement>
#include <QList>
#include <QString>

/**
	@brief The PidCodification class
	The codes a project of P&ID uses for the fluids and the materials of its
	pipe lines, such as "W01" for raw water or "M01" for high density
	polyethylene: each project has its own, as a drinking water plant and a
	wastewater plant carry other fluids, and each company its codification.
	A pipe line keeps the code (PipeProperties::fluid, material); the list
	gives its name and its family.

	QElectroTech ships no codification: a project takes one by importing a
	file (fromCsv()), kept by the company, and saves it with itself in a
	<pid_codification> block. A project without one offers the default
	fluids (PipeProperties::defaultFluids()) as free text.
*/
class PidCodification
{
	public:
		struct Entry
		{
				/// The code written on the pipe line, unique in its list
			QString code;
				/// What it is, in the language of the project
			QString name;
				/// The family of a fluid, such as "E" (waters) or "Sludges":
				/// free text, it groups the fluids drawn alike
			QString family;

			bool operator==(const Entry &other) const;
		};

		QList<Entry> fluids;
		QList<Entry> materials;

		bool isEmpty() const { return fluids.isEmpty() && materials.isEmpty(); }

			/// The fluid / material of code @p code, or nullptr
		const Entry *fluid(const QString &code) const;
		const Entry *material(const QString &code) const;

			/// The <pid_codification> block, a null element when empty
		QDomElement toXml(QDomDocument &document) const;
			/// Reads the <pid_codification> child of @p parent; an entry
			/// without a code, or with a code already read, is left out
		void fromXml(const QDomElement &parent);

			/// Reads a text file of lines "kind;code;name;family", kind being
			/// "fluid" or "material" (or their French words); the separator
			/// may also be a tab or a comma, a first line naming the columns
			/// is skipped, and so is a line that cannot be read, counted in
			/// @p skipped. Returns the codification read, empty when none.
		static PidCodification fromCsv(const QString &text, int *skipped = nullptr);

		bool operator==(const PidCodification &other) const;
		bool operator!=(const PidCodification &other) const { return !(*this == other); }
};

#endif
