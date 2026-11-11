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

#include "pipeconnection.h"
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
			/// A size of a material found in trade: an outside diameter and
			/// the DN of the line it makes (that of its flange), either of
			/// which may be unknown, not both
		struct Size
		{
				/// In tenths of a millimetre; 0 when it is not known
			int outside = 0;
				/// The DN, a whole number; 0 when it is not known
			int nominal = 0;
				/// The outside diameter a pipe of this DN takes without a
				/// question, where the material has several for it: one at
				/// most by DN (Entry::setDefaultSize())
			bool by_default = false;

			bool operator==(const Size &other) const {
				return outside == other.outside && nominal == other.nominal
						&& by_default == other.by_default;
			}
		};

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
				/// The sizes of a material found in trade, in order (see
				/// addSize()): its pipes take no other. A material named by
				/// its outside diameter needs the outside diameter of each,
				/// and its DN to be joined to a flange; one named by its DN
				/// needs the DN, its outside diameter being for a nozzle
				/// given by its outside diameter only.
			QList<Size> sizes;

				/// Add the size of @p outside (tenths of a millimetre, 0 for
				/// none) and of the DN @p nominal (0 for none), at its place:
				/// by outside diameter, or by DN without one. False, nothing
				/// being added, when it has neither, when this outside
				/// diameter is already there (an outside diameter has one DN
				/// only; a DN may have several outside diameters), or this DN
				/// alone. With @p by_default it becomes the default one of
				/// its DN (setDefaultSize()).
			bool addSize(int outside, int nominal, bool by_default = false);
				/// Remove the size of @p outside and @p nominal; false when
				/// it is not there. The outside diameter left alone for its
				/// DN is no default any more: there is nothing to choose.
			bool removeSize(int outside, int nominal);
				/// Make the size of the outside diameter @p outside the
				/// default one of its DN, the one before being so no more,
				/// or with @p on false no default any more. False when it
				/// is not there, or has no DN.
			bool setDefaultSize(int outside, bool on = true);
				/// How many outside diameters the DN @p nominal has
			int outsideCount(int nominal) const;

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

			/// The DN of the project which are the same size of line, by
			/// pairs, the smaller first, in order: DN 60 exists for cast
			/// iron only, where other materials have DN 65, and their
			/// flanges are joined. Nothing is known without the project
			/// saying so.
		QList<QPair<int, int>> equal_nominals;
			/// Say that the DN @p a and @p b are the same size of line;
			/// false when they are not two DN, or are said so already
		bool addEqualNominals(int a, int b);
			/// The DN which are the same size of line as @p dn, itself among
			/// them, in order: those equal_nominals joins to it, one after
			/// the other
		QList<int> equalNominals(int dn) const;
			/// equal_nominals as it is typed: "60/65, 125/130", the DN which
			/// are the same size of line together
		QString equalNominalsText() const;
			/// Take equal_nominals from @p text, written as
			/// equalNominalsText() does ("DN 60/65" too; a semicolon or a
			/// space between two groups too); none for an empty text. False,
			/// nothing changed, when it cannot be read.
		bool setEqualNominalsText(const QString &text);
			/// The DN @p a and @p b are the same size of line: the same, or
			/// joined by equal_nominals
		bool sameNominal(int a, int b) const;
			/// The usual one of equalNominals(@p dn), which stands for all
			/// of them where none is chosen: the one most materials of the
			/// project have a size of, the greater of two as usual. DN 60
			/// being of cast iron only, DN 65 is the usual one.
		int usualNominal(int dn) const;
			/// The DN a pipe of the material of code @p material_code takes
			/// for the size of line @p nominal: itself when the material has
			/// it, else the first DN of the material equal_nominals says the
			/// same, else itself. Without material, or with one the project
			/// has not, usualNominal(): no DN is chosen yet, and the pipe is
			/// written "DN 60/65" (text()).
		PipeDiameter nominalIn(const PipeDiameter &nominal, const QString &material_code) const;
			/// How the size @p size of a pipe of the material of code
			/// @p material_code is written, with the separator of the
			/// project: "DN 100", "Ø 110"; a DN which has equal ones, on a
			/// pipe without material or with one the project has not, is
			/// written with them, "DN 60/65", its material choosing later
		QString text(const PipeDiameter &size, const QString &material_code) const;
			/// The DN written in @p text as text() writes several, "DN 60/65"
			/// or "60/65": the usual one of them (usualNominal()); none when
			/// @p text is anything else, or when they are not said equal
		PipeDiameter nominalOfEqualText(const QString &text) const;

			/// The sizes the pipes of the material @p entry are named by, of
			/// its kind, in order, each once
		static QList<PipeDiameter> sizesOf(const Entry &entry);
			/// What sizes a pipe of the material of code @p material_code may
			/// have, written in @p sizes (empty when none)
		SizeRule pipeSizes(const QString &material_code, QList<PipeDiameter> *sizes = nullptr) const;
			/// @p diameter may be given to a pipe of the material of code
			/// @p material_code; none always
		bool acceptsForPipe(const PipeDiameter &diameter, const QString &material_code) const;
			/// The sizes a nozzle may have, having no material: the DN of
			/// the sizes of all the materials, then the outside diameters of
			/// those named by their outside diameter; the DN of ISO 6708
			/// in a project without material; then always the pipe threads,
			/// which are fixed (PipeDiameter::threadSeries())
		QList<PipeDiameter> nozzleSizes() const;
			/// @p diameter may be given to a nozzle: one of nozzleSizes(), or
			/// in a project without material any outside diameter too; none
			/// always
		bool acceptsForNozzle(const PipeDiameter &diameter) const;
			/// How @p a and @p b compare at a nozzle. Two DN are the same
			/// when sameNominal() says so. A DN and an outside diameter,
			/// which no material comes with, are compared by the DN the
			/// materials of the project give this outside diameter
			/// (nominalsOf()): the same when they all give this DN,
			/// different when none does, unknown when none has this outside
			/// diameter or when they do not agree. A thread is compared by
			/// the DN of the fixed table (PipeDiameter::threadNominal()); a
			/// diameter not set differs from nothing.
		DiameterMatch compare(const PipeDiameter &a, const PipeDiameter &b) const;
			/// The DN of a pipe of the outside diameter @p outside in the
			/// material of code @p material_code: the one of its sizes.
			/// When the material has not this outside diameter, or without
			/// material: the DN the other materials give it, as this
			/// material takes them (nominalIn()), in order, each once. None
			/// when no material has it, or for another kind of size.
		QList<PipeDiameter> nominalsOf(const PipeDiameter &outside,
									   const QString &material_code = QString()) const;
			/// The one DN of nominalsOf(); none when there is none, or
			/// several
		PipeDiameter nominalOf(const PipeDiameter &outside,
							   const QString &material_code = QString()) const;
			/// The DN a pipe without material may have, the DN being the
			/// size of a line whatever its material: those of the sizes of
			/// all the materials, in order, each once, equal ones as one
			/// (nominalIn()); those of ISO 6708 when the project has none
		QList<PipeDiameter> nominalSizes() const;
			/// The DN a pipe of the material of code @p material_code may
			/// have: those of its sizes, in order, each once;
			/// nominalSizes() for a material the project has not
		QList<PipeDiameter> lineSizes(const QString &material_code) const;
			/// The outside diameters a pipe of the DN @p nominal may have in
			/// the material of code @p material_code: those of its sizes of
			/// this DN, in order; none for a material named by its DN,
			/// whose pipes have no outside diameter, or which the project
			/// has not
		QList<PipeDiameter> outsidesOf(const PipeDiameter &nominal,
									   const QString &material_code) const;
			/// The one of several outsidesOf() the material says the default
			/// one (Size::by_default); none when it says none, or has one
			/// outside diameter only for this DN
		PipeDiameter defaultOutside(const PipeDiameter &nominal,
									const QString &material_code) const;
			/// The outside diameter a pipe of the DN @p nominal takes in the
			/// material of code @p material_code without a question: the
			/// only one of outsidesOf(), or defaultOutside() among several;
			/// none when the user has to choose, or when there is none
		PipeDiameter outsideOf(const PipeDiameter &nominal,
							   const QString &material_code) const;
			/// The size of @p sizes nearest to @p typed, of its kind; the
			/// greater of two as near; none when there is none of its kind
		static PipeDiameter nearest(const PipeDiameter &typed, const QList<PipeDiameter> &sizes);
			/// The material of code @p code names its pipes by their outside
			/// diameter
		bool outsideDiameterFor(const QString &material_code) const;
			/// The codes of the materials without a size their pipes can be
			/// named by, which no pipe can be given a size of
		QStringList materialsWithoutSizes() const;

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
			/// The connections whose mark is drawn at the end of the nozzles
			/// (Terminal::drawsConnectionMark()), in the order of
			/// PipeConnection::all(); none by default, so that a project
			/// drawn before keeps its look. A placed element may say
			/// otherwise for one of its nozzles.
		QList<PipeConnection::Type> drawn_connections;
		bool drawsConnection(PipeConnection::Type type) const {
			return drawn_connections.contains(type);
		}
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

		bool hasCodes() const { return hasCodeLists() || !equal_nominals.isEmpty(); }
		bool hasCodeLists() const {
			return !fluids.isEmpty() || !materials.isEmpty() || !functional_units.isEmpty();
		}
		bool hasColors() const {
			return !fluid_family_colors.isEmpty() || !object_colors.isEmpty()
					|| !status_colors.isEmpty();
		}
		bool hasTextSettings() const {
			return !text_sizes.isEmpty() || colored_labels || !nozzle_texts_on_paper
					|| !show_nozzle_sizes || decimal != Decimal::Language || !tag_models.isEmpty()
					|| !drawn_connections.isEmpty();
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
			/// none (and its equal DN when it has none either), its colours
			/// when it has none and its settings of texts
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
			/// lines "size;M05;125;100" ("taille": a size of a material,
			/// which may come before or after it, its outside diameter then
			/// its DN, either of which may be empty: "size;M01;;100"; a
			/// last field "default", or "défaut", says the outside diameter
			/// a pipe of this DN takes without a question:
			/// "size;M05;125;100;default"), of
			/// lines "dn_equal;60;65" ("dn_equivalent": two DN which are the
			/// same size of line), of
			/// lines "tag_model;family;model"
			/// ("modele_repere"), and of lines
			/// "kind;key;color", kind being "fluid_color", "object_color" or
			/// "status_color" ("couleur_fluide", "couleur_objet",
			/// "couleur_statut"), key a family or a status, of lines
			/// "text_size;category;points" ("taille_texte") and of lines
			/// "text_option;name;true" ("option_texte"), name being
			/// "colored_labels" or "nozzle_texts_on_paper", or of the line
			/// "text_option;drawn_connections;flange butt_weld" (the words
			/// of PipeConnection, a word not known being left out); the separator
			/// may also be a tab or a comma, a first line naming the columns
			/// is skipped, and so is a line that cannot be read, counted in
			/// @p skipped. Returns the codification read, empty when none.
			/// The sizes of a material the file does not list are lines left
			/// out, unless @p loose_sizes is given: they are written there,
			/// by code of material, for who knows these materials (a file of
			/// sizes alone, read for the materials of a page).
		static PidCodification fromCsv(const QString &text, int *skipped = nullptr,
									   QMap<QString, QList<Size>> *loose_sizes = nullptr);
			/// The colours and the settings of texts, as lines fromCsv()
			/// reads: the palette kept in the settings, for new projects. The
			/// codes stay out, kept apart (codesToCsv())
		QString paletteToCsv() const;
			/// The codes, the sizes of the materials and the equal DN, as
			/// lines fromCsv() reads: the codification
			/// kept in the settings, for new projects, apart from the palette,
			/// as a company may keep its colours with codes of another client
		QString codesToCsv() const;
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
