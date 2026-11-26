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
#include "properties/elementoptions.h"
#include "signalmark.h"

#include <QByteArray>
#include <QColor>
#include <QDomDocument>
#include <QDomElement>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class PipeProperties;

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
		enum class List { Fluids, Materials, Units, Letters };

		QList<Entry> fluids;
		QList<Entry> materials;
			/// The functional units of the plant, which the tags of its
			/// symbols name (PidTag): each project has its own
		QList<Entry> functional_units;
			/// The letters of identification of the instruments the project
			/// uses ("PT", pressure transmitter), offered while the letters of
			/// a tag are typed (PidTag::lettersKey()): a help only, a tag may
			/// have others, and a code changed or removed here changes no tag
		QList<Entry> instrument_letters;

		QList<Entry> &entries(List list);
		const QList<Entry> &entries(List list) const;

			/// What a datum of a symbol is, see DataField
		enum class DataKind { Text, Number, List, YesNo };
			/// A datum the project asks of its symbols of some classes (step
			/// 9b), such as the power of a pump: QElectroTech knows none. The
			/// placed symbol keeps its value in an information of its own, as
			/// a custom property of QElectroTech (the key key(), "pid_power"),
			/// written only when it is filled; the project says here how it is
			/// named, in what unit, and what it may be.
		struct DataField
		{
				/// Its short name, in small letters ("power"), unique: the
				/// information of the symbol is this name after dataPrefix()
			QString name;
				/// The classes of the symbols which have it (pid_class of
				/// their .elmt: "pump", "blower"), one at least
			QStringList classes;
				/// What the user reads, in the language of the project; none:
				/// the name
			QString label;
				/// Its unit ("kW"), shown after the value, which is kept alone
			QString unit;
			DataKind kind = DataKind::Text;
				/// The values of a list, in order
			QStringList values;
				/// The name DEXPI gives it, kept for an export; none here
			QString dexpi;

			QString key() const { return dataPrefix() + name; }
			QString title() const { return label.isEmpty() ? name : label; }
			bool operator==(const DataField &other) const {
				return name == other.name && classes == other.classes && label == other.label
						&& unit == other.unit && kind == other.kind && values == other.values
						&& dexpi == other.dexpi;
			}
		};
			/// The data of the symbols the project declares, in the order
			/// they are shown
		QList<DataField> data_fields;
			/// What begins the key of every datum of a P&ID symbol
		static QString dataPrefix() { return QStringLiteral("pid_"); }
			/// @p name may be the name of a datum: small letters, digits and
			/// "_", a letter first, and not a field of the tag (PidTag), which
			/// has a place of its own
		static bool isDataName(const QString &name);
			/// Add @p field after the others; false, nothing changed, when its
			/// name cannot be one or is there already, when it has no class,
			/// or when it is a list without values or has values without
			/// being a list. A name given with dataPrefix() loses it.
		bool addDataField(DataField field);
			/// The data the symbols of class @p pid_class have, in order; none
			/// for no class
		QList<DataField> dataFieldsOf(const QString &pid_class) const;
			/// The word of @p kind in a file: "text", "number", "list", "yes_no"
		static QString dataKindToString(DataKind kind);
			/// Reads it (or its French word; nothing: a text); false, nothing
			/// changed, for another word
		static bool dataKindFromString(const QString &word, DataKind &kind);
			/// What separates the classes of a datum in a file (a space) and
			/// the values of a list (a bar): signs no separator of a CSV file
			/// cuts
		static QChar dataValueSeparator() { return QLatin1Char('|'); }
			/// The two data which say a symbol a standby: installed, or kept
			/// in its crate. Never both (ElementDataWidget tells it, and
			/// blocks nothing); the project declares them as any other.
		static QString standbyKey() { return QStringLiteral("pid_standby"); }
		static QString spareKey() { return QStringLiteral("pid_spare"); }

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
			/// The DN of the line of @p diameter, to tell the larger of two
			/// sizes: itself for a DN, that of the fixed table for a thread,
			/// the one DN the materials give an outside diameter
			/// (nominalOf()). None when it is not known.
		PipeDiameter lineNominal(const PipeDiameter &diameter) const;
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
			/// The key of what is asked once: the outside diameter of the DN
			/// @p dn in the material of code @p code
		static QString choiceKey(const QString &code, const PipeDiameter &dn);
			/// The DN of the pipe @p p: its own; without one, the DN the sizes
			/// of its material give its outside diameter (those of the other
			/// materials when it has none), the one of @p chosen (by outside
			/// diameter, in tenths) among several; else none
		PipeDiameter pipeNominal(const PipeProperties &p,
								 const QMap<int, PipeDiameter> &chosen = {}) const;
			/// Give the pipe @p p the material of code @p code. Its DN, the
			/// size of the line, stays (pipeNominal(), as this material has
			/// it: nominalIn()); its outside diameter is the one of the new
			/// material: none for a material named by its DN or which the
			/// project has not; else the one it had when the new material has
			/// it for this DN, the only one it has for this DN, the default
			/// one among several (outsideOf()), or without one the one of
			/// @p outside_of (by choiceKey()), none when it is not there. A pipe
			/// which has no DN and gets none keeps its sizes. Never a question:
			/// the pipe properties panel asks first (step 8c-4: also used by
			/// "Copy from the other side", which asks nothing).
		void giveMaterial(PipeProperties &p, const QString &code,
						  const QMap<int, PipeDiameter> &nominal_of = {},
						  const QMap<QString, PipeDiameter> &outside_of = {}) const;
			/// Give the pipe @p p the size of line @p dn, a DN, which its
			/// material keeps: the DN as the material has it (nominalIn(), DN
			/// 65 becomes DN 60 in a material which has that one only), and
			/// the outside diameter of this material: the one it has when the
			/// material has it for this DN, else the only one or the default
			/// one (outsideOf()), else the one of @p outside_of (by
			/// choiceKey()); none for a material named by its DN. Without
			/// material, or with one the project has not, an outside diameter
			/// stays while the materials still give it this DN.
		void giveLineSize(PipeProperties &p, const PipeDiameter &dn,
						  const QMap<QString, PipeDiameter> &outside_of = {}) const;
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
			/// How the loops of the instruments are numbered (ANSI/ISA-5.1,
			/// annex A.6): in parallel, a sequence for each measured variable,
			/// the first letter (F-101, L-101 and P-101 side by side; PT 101
			/// and PIC 101 in the same loop); without, in series, one
			/// sequence whatever the letters. Written only when it is true.
		bool parallel_numbering = false;
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
			/// Else they are drawn in this colour; invalid: the colour of
			/// the text itself (black)
		QColor label_color;
			/// The designations of the pipes are drawn in the colour of
			/// their pipe
		bool colored_designations = false;
			/// Else they are drawn in this colour; invalid: that of the
			/// text of the conductor (black)
		QColor designation_color;
			/// The colour the project gives the labels of the symbols, or
			/// the designations of the pipes (@p designations), whose
			/// object is drawn in @p object (invalid: black): an invalid
			/// one when it leaves them their own
		QColor textInk(bool designations, const QColor &object) const {
			if (designations ? colored_designations : colored_labels) {
				return object;
			}
			return designations ? designation_color : label_color;
		}
			/// How the vertical texts of the P&ID symbols (their labels,
			/// their bubbles, their nozzles) read when they turn with their
			/// symbol: as the computer says
			/// (DynamicElementTextItem::verticalFromBottom()), from the
			/// bottom, or as turned (from the top on a symbol turned a
			/// quarter clockwise). A project so looks the same on every
			/// computer. Written only when set.
		enum class VerticalTexts {
			Computer,
			FromBottom,
			AsTurned
		};
		VerticalTexts vertical_texts = VerticalTexts::Computer;
			/// The names and diameters of the nozzles are printed and
			/// exported, whatever is asked for the names of the terminals
		bool nozzle_texts_on_paper = true;
			/// The sizes of the nozzles are shown under their names
		bool show_nozzle_sizes = true;
			/// A DN shown under the name of a nozzle is written "DN 100";
			/// without, "100". An outside diameter and a thread keep their
			/// sign. Written only when it is false.
		bool nozzle_dn_prefix = true;
			/// A reducer without a label of its own writes its two sizes
			/// there, "100 × 80" (Element::reducerSizesText(), step 8r-3 e).
			/// Written only when it is false.
		bool reducer_sizes = true;
			/// The length in mm of the stubs drawn where the insulated
			/// part of a line begins and ends, and of the mark in its middle
			/// (Conductor::insulationStubs(), step 7d): 8 by default, written
			/// only otherwise. Their colours are object_colors of the keys
			/// insulationColorKey() and tracingColorKey(); none: that of the
			/// pipe.
		qreal insulation_stub = defaultInsulationStub();
		static constexpr qreal defaultInsulationStub() { return 8.0; }
			/// A length the stubs may have, in mm
		static bool isInsulationStub(qreal mm) { return mm >= 2.0 && mm <= 50.0; }
		static QString insulationColorKey() { return QStringLiteral("insulation"); }
		static QString tracingColorKey() { return QStringLiteral("tracing"); }
			/// The key of the colour of the jackets of the double
			/// containment pipes in object_colors (step 8r-1); none: that of
			/// the pipe. A status of works comes first.
		static QString jacketColorKey() { return QStringLiteral("jacket"); }
			/// The patterns of dashes a kind of line may be drawn with (step
			/// 8r-6): a fixed list, in widths of the line, as the dashes of
			/// QElectroTech
		enum class LinePattern { Solid, Dashes, ShortDashes, DashDot };
			/// A kind of line of the project (PipeProperties::line_kind): how
			/// its pipes are drawn
		struct LineKindEntry
		{
				/// The word the pipes keep (pid_line_kind): "main",
				/// "secondary", "auxiliary" for those of ISO 10628-1, which
				/// every project has, or a code the project adds
			QString code;
				/// The name of a kind added; one of ISO 10628-1 has none here,
				/// its name being translated (lineKindName())
			QString name;
				/// The width of its line in mm
			qreal width = 1.0;
			LinePattern pattern = LinePattern::Solid;

			bool operator==(const LineKindEntry &other) const {
				return code == other.code && name == other.name
						&& qFuzzyCompare(width, other.width) && pattern == other.pattern;
			}
			bool operator!=(const LineKindEntry &other) const { return !(*this == other); }
		};
			/// The kinds of line of the project, those of ISO 10628-1 first,
			/// always there, then those it adds, in order: the widths and
			/// dashes of ISO 10628-1 by default (1 mm; 0.5 mm; 0.5 mm in
			/// dashes). Written only where it is not the default.
		QList<LineKindEntry> line_kinds = isoLineKinds();
		static const QList<LineKindEntry> &isoLineKinds();
		static bool isIsoLineKind(const QString &code);
			/// The kind of line of code @p code: that of line_kinds; a code
			/// the project does not know (a kind added by another project)
			/// is drawn as a main line, its code kept
		LineKindEntry lineKind(const QString &code) const;
			/// The name of the kind of line of code @p code: translated for
			/// those of ISO 10628-1; else its name, else its code
		QString lineKindName(const QString &code) const;
			/// Add the kind of line @p entry, or change the one of its code.
			/// One of ISO 10628-1 keeps its code and no name. False, nothing
			/// changed, for a code which cannot be one (letters, digits, "_"
			/// and "-"), or a width out of isLineWidth().
		bool setLineKind(const LineKindEntry &entry);
			/// Take out the kind of line of code @p code; false for one of
			/// ISO 10628-1, or a code the project has not. The pipes keep the
			/// code, drawn as main lines.
		bool removeLineKind(const QString &code);
			/// A width a kind of line may have, in mm
		static bool isLineWidth(qreal mm) { return mm >= 0.1 && mm <= 5.0; }
			/// The dashes of @p pattern, in widths of the line, as
			/// QPen::setDashPattern() takes them; none for a solid line
		static QList<qreal> dashPattern(LinePattern pattern);
		static QList<LinePattern> linePatterns();
		static QString linePatternToString(LinePattern pattern);
			/// False, nothing changed, for another word
		static bool linePatternFromString(const QString &word, LinePattern &pattern);
			/// What the user reads of @p pattern
		static QString linePatternName(LinePattern pattern);

			/// The valves, and the instruments placed on a pipe, are drawn in
			/// the colour of their pipes when all those joined to them agree
			/// (Element::agreedPipeColor(), step 8r-2), as the fittings are;
			/// else in that of their family. A status of works comes first.
			/// On by default, written only when off.
		bool symbols_follow_pipes = true;
			/// A symbol of the family @p pid_family follows its pipes by
			/// symbols_follow_pipes; the fittings always do, by another rule
			/// (Element::inkingPipe())
		bool followsPipes(const QString &pid_family) const {
			return symbols_follow_pipes
					&& (pid_family == QLatin1String("valve") || pid_family == QLatin1String("instrument"));
		}
			/// The connections whose mark is drawn at the end of the nozzles
			/// (Terminal::drawsConnectionMark()), in the order of
			/// PipeConnection::all(); none by default, so that a project
			/// drawn before keeps its look. A placed element may say
			/// otherwise for one of its nozzles.
		QList<PipeConnection::Type> drawn_connections;
		bool drawsConnection(PipeConnection::Type type) const {
			return drawn_connections.contains(type);
		}
			/// The families of symbols whose nozzles draw the connections
			/// above, among connectionFamilies(), in its order: all by
			/// default, written only otherwise
		QStringList drawn_families = connectionFamilies();
			/// The families a project may choose for the connections drawn:
			/// equipment, valve, instrument, pipe (fittings: tee, reducer),
			/// other (a symbol of no family, or of another one)
		static const QStringList &connectionFamilies() {
			static const QStringList families{QStringLiteral("equipment"), QStringLiteral("valve"),
											  QStringLiteral("instrument"), QStringLiteral("pipe"),
											  QStringLiteral("other")};
			return families;
		}
			/// The family of connectionFamilies() of a symbol of family
			/// @p pid_family (ElementData::m_pid_family): "other" for none
		static QString connectionFamily(const QString &pid_family) {
			return connectionFamilies().contains(pid_family) ? pid_family
															 : QStringLiteral("other");
		}
		bool drawsOnFamily(const QString &pid_family) const {
			return drawn_families.contains(connectionFamily(pid_family));
		}
		bool drawsOnAllFamilies() const { return drawn_families == connectionFamilies(); }
			/// The symbol a tool places by itself, by its role (pid_role of
			/// the .elmt, see toolSymbolRoles()): the place of the symbol in
			/// a collection (ElementsLocation::toString()). Set by the
			/// project, none in the code; written only when it is set.
		QMap<QString, QString> tool_symbols;
			/// The roles of the symbols a tool may place: the free end of a
			/// pipe drawn from or to an empty place (DiagramEventAddPipe),
			/// the tee of a pipe drawn from or to a pipe (PidTee), the blind
			/// flange which closes a free end a process connection is drawn
			/// from or to (PidProcessConnection), the reducer inserted by
			/// "Correct" on a diamond of two sizes (PidSizeCorrections)
		static const QStringList &toolSymbolRoles() {
			static const QStringList roles{freeEndRole(), teeRole(), blindFlangeRole(), reducerRole()};
			return roles;
		}
		static QString freeEndRole() { return QStringLiteral("free_end"); }
		static QString teeRole() { return QStringLiteral("tee"); }
		static QString blindFlangeRole() { return QStringLiteral("blind_flange"); }
		static QString reducerRole() { return QStringLiteral("reducer"); }
		QString toolSymbol(const QString &role) const { return tool_symbols.value(role); }
			/// The value a P&ID symbol placed new takes for one of its
			/// options (ElementOptions), by family of the symbol
			/// (ElementData::m_pid_family) then name of the option: the oval
			/// bubbles of a plant, "instrument" -> "shape" -> "oval". Only a
			/// symbol placed new takes it (newChoices()), and keeps it
			/// written: a symbol placed before keeps its own. None by
			/// default; written only when set.
		QMap<QString, ElementOptions::Choices> option_defaults;
			/// The symbols of the family @p family placed new take @p value
			/// for their option @p option; none any more for an empty @p
			/// value. False, nothing changed, when a name cannot be one
			/// (ElementOptions::isValidName()).
		bool setOptionDefault(const QString &family, const QString &option, const QString &value);
			/// Each option of @p choices becomes a default of the family
			/// @p family (setOptionDefault()); false when nothing changes
		bool setOptionDefaults(const QString &family, const ElementOptions::Choices &choices);
			/// What a new symbol of the family @p family whose options are
			/// @p options takes from the project: the default of each of
			/// its options the project has, when it is one of the values
			/// of the option and not already its own default. None for a
			/// symbol of no family.
		ElementOptions::Choices newChoices(const QString &family, const ElementOptions &options) const;
			/// The mark of the signal lines of a type of signal (SignalMark),
			/// where the project says it: the word of the type
			/// (SignalProperties::typeToString()), then that of the mark
			/// (SignalMark::toString()). A type without one takes
			/// SignalMark::defaultMark(): ISO 14617, else ANSI/ISA-5.1. A
			/// word this version does not know, read from a project, is
			/// kept. Written only where it is not the default one.
		QMap<QString, QString> signal_marks;
			/// The mark of the signal lines of type @p type: that of
			/// signal_marks, else the default one
		SignalMark::Mark signalMark(SignalProperties::SignalType type) const;
			/// Give the signal lines of type @p type the mark @p mark; the
			/// default one is not kept
		void setSignalMark(SignalProperties::SignalType type, SignalMark::Mark mark);
			/// A type of signal at least has another mark than the default
			/// one
		bool hasSignalMarks() const;
			/// Take out of signal_marks those which are the default ones, as
			/// a file read may say them all
		void dropDefaultSignalMarks();
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

		bool hasCodes() const {
			return hasCodeLists() || !equal_nominals.isEmpty() || !data_fields.isEmpty();
		}
		bool hasCodeLists() const {
			return !fluids.isEmpty() || !materials.isEmpty() || !functional_units.isEmpty()
					|| !instrument_letters.isEmpty();
		}
		bool hasColors() const {
			return !fluid_family_colors.isEmpty() || !object_colors.isEmpty()
					|| !status_colors.isEmpty() || !symbols_follow_pipes;
		}
		bool hasTextSettings() const {
			return !text_sizes.isEmpty() || colored_labels || label_color.isValid()
					|| colored_designations || designation_color.isValid()
					|| vertical_texts != VerticalTexts::Computer || !nozzle_texts_on_paper
					|| !show_nozzle_sizes || !nozzle_dn_prefix || !reducer_sizes || decimal != Decimal::Language || !tag_models.isEmpty()
					|| insulation_stub != defaultInsulationStub()
					|| parallel_numbering
					|| !drawn_connections.isEmpty() || !drawsOnAllFamilies() || !tool_symbols.isEmpty()
				|| !option_defaults.isEmpty() || line_kinds != isoLineKinds();
		}
		bool isEmpty() const {
			return !hasCodes() && !hasColors() && !hasTextSettings() && signal_marks.isEmpty();
		}

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
			/// labels of a family of objects ("pipe": the fittings, whose
			/// reducers write their sizes there), then nozzleTexts()
		static QStringList textCategories();
		static QString nozzleTexts() { return QStringLiteral("nozzle"); }

			/// This codification, with the codes of @p other when it has
			/// none (and its equal DN when it has none either), its colours
			/// when it has none, its settings of texts when it has none, its
			/// marks of signal lines when it says none and the data of its
			/// symbols when it declares none: a file of codes
			/// does not take the colours of a project away, nor the reverse
		PidCodification completedWith(const PidCodification &other) const;

			/// The <pid_codification> block, a null element when empty
		QDomElement toXml(QDomDocument &document) const;
			/// Reads the <pid_codification> child of @p parent; an entry
			/// without a code, or with a code already read, is left out
		void fromXml(const QDomElement &parent);

			/// Reads a text file of lines "kind;code;name;family", kind being
			/// "fluid", "material", "unit" (a functional unit) or "letters"
			/// (letters of identification of an instrument; or their
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
			/// ("modele_repere"), of the line
			/// "tag_option;instrument_numbering;parallel" ("option_repere";
			/// "serial", the default, or "parallel", or their French words "série"
			/// and "parallèle": parallel_numbering), and of lines
			/// "kind;key;color", kind being "fluid_color", "object_color" or
			/// "status_color" ("couleur_fluide", "couleur_objet",
			/// "couleur_statut"), key a family or a status, of lines
			/// "text_size;category;points" ("taille_texte") and of lines
			/// "text_option;name;true" ("option_texte"), name being
			/// "nozzle_texts_on_paper", "show_nozzle_sizes",
			/// "nozzle_dn_prefix", "reducer_sizes" or "symbols_follow_pipes" ("colored_labels", of former files,
			/// colouring the labels and the designations), of the lines
			/// "text_option;label_ink;object" and
			/// "text_option;designation_ink;#0000ff" (the colour of their
			/// object, "objet", or a fixed colour), of the line
			/// "text_option;vertical_texts;from_bottom" (or "as_turned",
			/// vertical_texts), or of the line
			/// "text_option;drawn_connections;flange butt_weld" (the words
			/// of PipeConnection, a word not known being left out), and of lines
			/// "tool_symbol;free_end;custom://folder/symbol.elmt" ("symbole_outil",
			/// a role of toolSymbolRoles() and the place of its symbol), of lines
			/// "option_default;instrument;shape;oval" ("option_défaut": a family
			/// of symbols, an option and its value, option_defaults), and of lines
			/// "line_kind;L4;Buried;0.7;dash_dot" ("genre_ligne": a kind of line,
			/// its name, empty for one of ISO 10628-1, its width in mm and its
			/// pattern, step 8r-6), of lines
			/// "signal_mark;pneumatic;bar2" ("marque_signal": a type of signal
			/// and the mark of its lines, by their words of the files; a file
			/// which has some says them all, a type it leaves out taking the
			/// default one), and of lines
			/// "data;pump blower;power;Power;kW;number" ("donnée": a datum of
			/// the symbols, DataField: the classes which have it, separated by
			/// spaces, its name, what the user reads, its unit, its kind
			/// (dataKindFromString()), then for a list its values separated by
			/// bars, "direct|starter|drive", and last the name DEXPI gives it;
			/// a file which has some says them all); the separator
			/// may also be a tab or a comma, a first line naming the columns
			/// is skipped, and so is a line that cannot be read, counted in
			/// @p skipped. Returns the codification read, empty when none.
			/// The sizes of a material the file does not list are lines left
			/// out, unless @p loose_sizes is given: they are written there,
			/// by code of material, for who knows these materials (a file of
			/// sizes alone, read for the materials of a page).
		static PidCodification fromCsv(const QString &text, int *skipped = nullptr,
									   QMap<QString, QList<Size>> *loose_sizes = nullptr);
			/// The presentation: colours, settings of texts, connections
			/// drawn, models of tags, symbols of the tools, defaults of the
			/// options and marks of the signal lines, as lines fromCsv()
			/// reads; what is
			/// kept in the settings, for new projects. The codes stay out,
			/// kept apart (codesToCsv())
		QString presentationToCsv() const;
			/// The codes, the sizes of the materials, the equal DN and the
			/// data of the symbols, as lines fromCsv() reads: the codification
			/// kept in the settings, for new projects, apart from the
			/// presentation, as a company may keep its colours with codes of
			/// another client
		QString codesToCsv() const;
			/// The whole codification, codes then presentation, as lines fromCsv()
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
