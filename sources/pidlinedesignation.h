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
#ifndef PID_LINE_DESIGNATION_H
#define PID_LINE_DESIGNATION_H

#include <QString>

class PidCodification;
class PipeProperties;

/**
	@brief The PidLineDesignation namespace
	The designation of a pipe line, written along a pipe: its data put
	together by the model the project gives the lines, as the model of a
	family of symbols makes their tags (PidTag). The model is one of the
	models of tags of the project (PidCodification::tag_models), under the
	key modelKey(), "line": a CSV line "tag_model;line;{fluid} {size}".
	QElectroTech has none, and without one nothing is written.
	A pipe shows its designation when its text is shown
	(ConductorProperties::m_show_text): one chooses the pipes which carry
	it, none by default.
*/
namespace PidLineDesignation
{
		/// The key of the model of the lines in PidCodification::tag_models,
		/// and the category of their size in PidCodification::text_sizes
	QString modelKey();

		/**
			The designation @p model gives to a pipe of @p pipe data, in a
			project of @p codification:
			- {fluid}, {line_number}, {material}, {piping_class}, {pn} (the
			  nominal pressure), {flow}, {insulation} (what is written of
			  it, "Q 80"): the data, as typed (codes);
			- {fluid_name}, {material_name}: the name the codification
			  gives the code of the fluid, of the material; the code when
			  it has none, or is not one of the codification;
			- {size}: the size as the drawing writes it, "DN 100", "Ø 110",
			  "DN 60/65" (PidCodification::text()); "DN ?" when the pipe has
			  none;
			- {dn}: the DN alone, "100", "60/65"; "?" when the pipe has none.
			"\n" (backslash then n, as written in a file) starts a new line.
			In each line, spaces in a row are made one and those at the ends
			go, so that a missing field leaves no gap. An unknown {word}
			stays as it is. An empty @p model gives nothing.
		*/
	QString text(const QString &model,
				 const PipeProperties &pipe,
				 const PidCodification &codification);
}

#endif
