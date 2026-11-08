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

#include "pipediameter.h"

#include <QByteArray>
#include <QColor>
#include <QDomDocument>
#include <QDomElement>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

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

	It also holds the colours of the project: QElectroTech knows none, and
	what has none is drawn in black, as before. The same goes for the sizes
	of its texts: what has none keeps the size of its symbol.
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
				/// A material whose pipes are named by their outside
				/// diameter (PVC, PE), not by their DN (steel, cast iron)
			bool outside_diameter = false;
				/// The sizes of a material found in trade, its standard
				/// series, in tenths of a millimetre (a DN times ten), in
				/// order: its pipes take no other
			QList<int> series;

			bool operator==(const Entry &other) const;
		};

			/// How a diameter compares with another, see compare()
		enum class DiameterMatch { Same, Different, Unknown };
			/// What sizes a pipe of a material may have, see pipeSizes()
		enum class SizeRule {
				/// The project has no material: the DN of ISO 6708
			Iso,
				/// The series of the material
			Series,
				/// No material, or one the codification has not: none
			NoMaterial,
				/// A material without series: none
			NoSeries
		};

			/// The lists of codes
		enum class List { Fluids, Materials, Units };

		QList<Entry> fluids;
		QList<Entry> materials;
			/// The functional units of the plant, which the tags of its
			/// symbols name (PidTag): each project has its own
		QList<Entry> functional_units;

		QList<Entry> &entries(List list);
		const QList<Entry> &entries(List list) const;

			/// The table of the project joining its outside diameters to DN
			/// (a collar and flange): by outside diameter, in tenths of a
			/// millimetre, the DN it may be joined to; none known for some
		QMap<int, QList<int>> outside_diameters;

			/// The sizes of the material @p entry, of its kind
		static QList<PipeDiameter> seriesOf(const Entry &entry);
			/// What sizes a pipe of the material of code @p material_code may
			/// have, written in @p sizes (empty when none)
		SizeRule pipeSizes(const QString &material_code, QList<PipeDiameter> *sizes = nullptr) const;
			/// @p diameter may be given to a pipe of the material of code
			/// @p material_code; none always
		bool acceptsForPipe(const PipeDiameter &diameter, const QString &material_code) const;
			/// The sizes a nozzle may have, having no material: the series of
			/// all the materials, DN then outside diameters; those of ISO 6708
			/// in a project without material
		QList<PipeDiameter> nozzleSizes() const;
			/// @p diameter may be given to a nozzle: one of nozzleSizes(), or
			/// in a project without material any outside diameter too; none
			/// always
		bool acceptsForNozzle(const PipeDiameter &diameter) const;
			/// How @p a and @p b compare at a nozzle: a DN and an outside
			/// diameter are the same when the table of the project joins them,
			/// unknown when it has not this outside diameter; a diameter not
			/// set differs from nothing
		DiameterMatch compare(const PipeDiameter &a, const PipeDiameter &b) const;
			/// What @p diameter may become on a pipe of the material of code
			/// @p material_code, when its series has it not: the sizes of the
			/// series the table joins to it, through a DN when both are
			/// outside diameters (Ø 110 of a plastic, DN 100, Ø 114,3 of a
			/// steel). None when the series has it, or the material has no
			/// series.
		QList<PipeDiameter> conversions(const PipeDiameter &diameter,
										const QString &material_code) const;
			/// The size of @p sizes nearest to @p typed, of its kind; the
			/// greater of two as near; none when there is none of its kind
		static PipeDiameter nearest(const PipeDiameter &typed, const QList<PipeDiameter> &sizes);
			/// The material of code @p code names its pipes by their outside
			/// diameter
		bool outsideDiameterFor(const QString &material_code) const;
			/// The codes of the materials without series, which no pipe can
			/// be given a size of
		QStringList materialsWithoutSeries() const;
			/// The DN written in @p text as "100/125" (or separated by
			/// spaces), whole, in order, each once; @p ok false when something
			/// else is written
		static QList<int> diametersFromText(const QString &text, bool *ok = nullptr);
			/// @p values as "100/125"
		static QString diametersToText(const QList<int> &values);
			/// The sizes written in @p text as "25/32/88,9" (or separated by
			/// spaces; a tenth after a comma or a point), in tenths of a
			/// millimetre, in order, each once; @p ok false when something
			/// else is written
		static QList<int> seriesFromText(const QString &text, bool *ok = nullptr);
			/// @p tenths as "25/32/88.9", as a file writes them; with commas
			/// in a language which writes them so when @p locale says so
		static QString seriesToText(const QList<int> &tenths, const QLocale &locale = QLocale::c());

			/// @p a comes before @p b in a list of codes: in their natural
			/// order, W2 before W10, without regard to case
		static bool codeLessThan(const QString &a, const QString &b);
			/// Put each list of codes in the order of its codes, that of the
			/// lists offered to the user
		void sortCodes();
			/// The model of the tags of a family of objects (equipment,
			/// valve, instrument), see PidTag::label(): a convention of the
			/// company, kept with the colours in the palette
		QMap<QString, QString> tag_models;
			/// The colour of a family of fluids (Entry::family)
		QMap<QString, QColor> fluid_family_colors;
			/// The colour of a family of objects, see objectFamilies()
		QMap<QString, QColor> object_colors;
			/// The colour of a status of works, see statuses()
		QMap<QString, QColor> status_colors;
			/// The size in points of a category of texts, see textCategories()
		QMap<QString, int> text_sizes;
			/// The labels of the symbols are drawn in the colour of their
			/// symbol, nothing being written in the text itself
		bool colored_labels = false;
			/// The names and diameters of the nozzles are printed and
			/// exported, whatever is asked for the names of the terminals
		bool nozzle_texts_on_paper = true;
			/// The sizes of the nozzles are shown under their names
		bool show_nozzle_sizes = true;
			/// How the tenths of a size are written on the screen and on
			/// paper; a file always has a point
		enum class Decimal {
				/// Not set yet: as the language of QElectroTech
			Language,
			Comma,
			Point
		};
		Decimal decimal = Decimal::Language;
			/// The separator of the language of QElectroTech, Comma or Point
		static Decimal languageDecimal();
			/// decimal, set: that of the language when it is not
		Decimal effectiveDecimal() const {
			return decimal == Decimal::Language ? languageDecimal() : decimal;
		}
			/// What writes a size with the separator of the project
		QLocale locale() const;

		bool hasCodes() const { return hasCodeLists() || hasDiameters(); }
		bool hasCodeLists() const {
			return !fluids.isEmpty() || !materials.isEmpty() || !functional_units.isEmpty();
		}
		bool hasDiameters() const { return !outside_diameters.isEmpty(); }
		bool hasColors() const {
			return !fluid_family_colors.isEmpty() || !object_colors.isEmpty()
					|| !status_colors.isEmpty();
		}
		bool hasTextSettings() const {
			return !text_sizes.isEmpty() || colored_labels || !nozzle_texts_on_paper
					|| !show_nozzle_sizes || decimal != Decimal::Language || !tag_models.isEmpty();
		}
		bool isEmpty() const { return !hasCodes() && !hasColors() && !hasTextSettings(); }

			/// The fluid / material of code @p code, or nullptr
		const Entry *fluid(const QString &code) const;
		const Entry *material(const QString &code) const;
			/// The colour of the family of the fluid of code @p code, an
			/// invalid one when it has none
		QColor fluidColor(const QString &code) const;

			/// The words of the families of objects and of the statuses of
			/// works which have a colour, in the order they are offered
		static QStringList objectFamilies();
		static QStringList statuses();
			/// The words of the categories of texts which have a size: the
			/// labels of a family of objects, then nozzleTexts()
		static QStringList textCategories();
		static QString nozzleTexts() { return QStringLiteral("nozzle"); }

			/// This codification, with the codes of @p other when it has
			/// none, its diameters when it has none, its colours when it has
			/// none and its settings of texts
			/// when it has none: a file of codes does not take the colours of
			/// a project away, nor the reverse
		PidCodification completedWith(const PidCodification &other) const;

			/// The <pid_codification> block, a null element when empty
		QDomElement toXml(QDomDocument &document) const;
			/// Reads the <pid_codification> child of @p parent; an entry
			/// without a code, or with a code already read, is left out
		void fromXml(const QDomElement &parent);

			/// Reads a text file of lines "kind;code;name;family", kind being
			/// "fluid", "material" or "unit" (a functional unit; or their
			/// French words), the last field of a material being "od" (or
			/// "Ø") when its pipes are named by their outside diameter, of
			/// lines "series;M05;25/32/40" (the series of a material, which
			/// may come before or after it), of lines "od;125;100/125" (an
			/// outside diameter and the DN it may be joined to, maybe none), of
			/// lines "tag_model;family;model"
			/// ("modele_repere"), and of lines
			/// "kind;key;color", kind being "fluid_color", "object_color" or
			/// "status_color" ("couleur_fluide", "couleur_objet",
			/// "couleur_statut"), key a family or a status, of lines
			/// "text_size;category;points" ("taille_texte") and of lines
			/// "text_option;name;true" ("option_texte"), name being
			/// "colored_labels" or "nozzle_texts_on_paper"; the separator
			/// may also be a tab or a comma, a first line naming the columns
			/// is skipped, and so is a line that cannot be read, counted in
			/// @p skipped. Returns the codification read, empty when none.
		static PidCodification fromCsv(const QString &text, int *skipped = nullptr);
			/// The colours and the settings of texts, as lines fromCsv()
			/// reads: the palette kept in the settings, for new projects. The
			/// codes stay out, each project has its own
		QString paletteToCsv() const;
			/// The whole codification, codes then palette, as lines fromCsv()
			/// reads: what another project imports
		QString toCsv() const;
			/// The text of a file of codes: UTF-8, or Latin-1 when it is not,
			/// as a file saved by a spreadsheet may be
		static QString textOfFile(const QByteArray &bytes);
			/// The texts of the files @p paths one after the other, as one
			/// file of codes; those which could not be read in @p unreadable
		static QString textOfFiles(const QStringList &paths, QStringList *unreadable = nullptr);

		bool operator==(const PidCodification &other) const;
		bool operator!=(const PidCodification &other) const { return !(*this == other); }
};

#endif
