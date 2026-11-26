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
#include "pidcodification.h"

#include "pidtag.h"
#include "pipeproperties.h"
#include "qetapp.h"
#include "qetinformation.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace
{
	const QString block_tag = QStringLiteral("pid_codification");
	const QString colored_labels_word = QStringLiteral("colored_labels");
	const QString label_ink_word = QStringLiteral("label_ink");
	const QString designation_ink_word = QStringLiteral("designation_ink");
	const QString object_word = QStringLiteral("object");
	const QString vertical_texts_word = QStringLiteral("vertical_texts");

		/// The word of a way of reading vertical texts, empty for that of
		/// the computer
	QString verticalTextsToString(PidCodification::VerticalTexts v)
	{
		switch (v) {
			case PidCodification::VerticalTexts::FromBottom: return QStringLiteral("from_bottom");
			case PidCodification::VerticalTexts::AsTurned: return QStringLiteral("as_turned");
			default: return QString();
		}
	}

		/// Reads it; false, nothing changed, for another word
	bool verticalTextsFromString(const QString &word, PidCodification::VerticalTexts &v)
	{
		for (auto known : {PidCodification::VerticalTexts::FromBottom,
						   PidCodification::VerticalTexts::AsTurned}) {
			if (word.trimmed().toLower() == verticalTextsToString(known)) {
				v = known;
				return true;
			}
		}
		return false;
	}
	const QString nozzle_texts_word = QStringLiteral("nozzle_texts_on_paper");
	const QString nozzle_sizes_word = QStringLiteral("show_nozzle_sizes");
	const QString dn_prefix_word = QStringLiteral("nozzle_dn_prefix");
	const QString reducer_sizes_word = QStringLiteral("reducer_sizes");
	const QString follow_pipes_word = QStringLiteral("symbols_follow_pipes");
	const QString decimal_word = QStringLiteral("decimal_separator");
	const QString insulation_stub_word = QStringLiteral("insulation_stub");
		/// A length of the insulation stubs read from @p text, in mm with a
		/// point or a comma; false, nothing changed, when it is not one
	bool insulationStubFromText(const QString &text, qreal &mm)
	{
		bool ok = false;
		const qreal read = QString(text).replace(QLatin1Char(','), QLatin1Char('.')).trimmed().toDouble(&ok);
		if (!ok || !PidCodification::isInsulationStub(read)) {
			return false;
		}
		mm = read;
		return true;
	}
	const QString drawn_connections_word = QStringLiteral("drawn_connections");
	const QString drawn_families_word = QStringLiteral("drawn_families");
	const QString size_tag = QStringLiteral("size");
	const QString equal_tag = QStringLiteral("equal_dn");
	const QString tool_symbol_tag = QStringLiteral("tool_symbol");
	const QString option_default_tag = QStringLiteral("option_default");
	const QString signal_mark_tag = QStringLiteral("signal_mark");
	const QString line_kind_tag = QStringLiteral("line_kind");
		/// A kind of line read from a file, in @p entry: its code, its name,
		/// its width in mm with a point or a comma, the word of its pattern
		/// (solid when empty); false when it cannot be read
	bool lineKindFromFields(const QString &code, const QString &name, const QString &width,
							const QString &pattern, PidCodification::LineKindEntry &entry)
	{
		bool ok = false;
		entry.code = code.trimmed();
		entry.name = name.trimmed();
		entry.width = QString(width).replace(QLatin1Char(','), QLatin1Char('.')).trimmed().toDouble(&ok);
		entry.pattern = PidCodification::LinePattern::Solid;
		if (!ok || !PidCodification::isLineWidth(entry.width) || !ElementOptions::isValidName(entry.code)) {
			return false;
		}
		return pattern.trimmed().isEmpty() || PidCodification::linePatternFromString(pattern, entry.pattern);
	}
	const QString letters_tag = QStringLiteral("instrument_letters");
	const QString numbering_word = QStringLiteral("instrument_numbering");
	const QString tag_option_kind = QStringLiteral("tag_option");
	const QString serial_word = QStringLiteral("serial");
	const QString parallel_word = QStringLiteral("parallel");
	const QString letters_word = QStringLiteral("letters");
	const QString data_tag = QStringLiteral("data_field");
	const QString data_word = QStringLiteral("data");

		/// A datum of the symbols read from a file, in @p field: its classes
		/// separated by spaces, its name, what the user reads, its unit, the
		/// word of its kind, its values separated by bars and the name DEXPI
		/// gives it; false when the word of its kind is not one
	bool dataFieldFromTexts(const QString &classes, const QString &name, const QString &label,
							const QString &unit, const QString &kind, const QString &values,
							const QString &dexpi, PidCodification::DataField &field)
	{
		field = PidCodification::DataField();
		field.name = name.trimmed().toLower();
		for (const QString &word : classes.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
			if (!field.classes.contains(word)) {
				field.classes << word;
			}
		}
		field.label = label.trimmed();
		field.unit = unit.trimmed();
		for (const QString &value : values.split(PidCodification::dataValueSeparator(), Qt::SkipEmptyParts)) {
			if (!value.trimmed().isEmpty() && !field.values.contains(value.trimmed())) {
				field.values << value.trimmed();
			}
		}
		field.dexpi = dexpi.trimmed();
		return PidCodification::dataKindFromString(kind, field.kind);
	}

		/// @p word is the word of a type of signal this version knows,
		/// written in @p type if given
	bool isSignalType(const QString &word, SignalProperties::SignalType *type = nullptr)
	{
		for (SignalProperties::SignalType known : SignalProperties::types()) {
			if (word == SignalProperties::typeToString(known)) {
				if (type) *type = known;
				return true;
			}
		}
		return false;
	}

		/// The mark of word @p mark is the one the signal lines of the type
		/// of word @p type take without the project saying it. Not so for a
		/// word this version does not know.
	bool isDefaultSignalMark(const QString &type, const QString &mark)
	{
		SignalProperties::SignalType known_type = SignalProperties::Undefined;
		bool known_mark = false;
		const SignalMark::Mark read = SignalMark::fromString(mark, &known_mark);
		return isSignalType(type, &known_type) && known_mark
				&& read == SignalMark::defaultMark(known_type);
	}

		/// How a kind of texts is inked, as written in a file: "object"
		/// for the colour of their object, the name of a fixed colour,
		/// nothing for their own
	QString inkToText(bool of_object, const QColor &color)
	{
		if (of_object) {
			return object_word;
		}
		return color.isValid() ? color.name() : QString();
	}

		/// Reads @p text, written by inkToText() ("objet" being the
		/// French word); false, nothing changed, for a word which is
		/// neither
	bool inkFromText(const QString &text, bool &of_object, QColor &color)
	{
		const QString word = text.trimmed().toLower();
		if (word == object_word || word == QLatin1String("objet")) {
			of_object = true;
			color = QColor();
			return true;
		}
		const QColor read = QColor::fromString(word);
		if (word.isEmpty() || !read.isValid()) {
			return false;
		}
		of_object = false;
		color = read;
		return true;
	}

		/// @p connections as words separated by spaces: a list the CSV
		/// separators (semicolon, tab, comma) never cut
	QString connectionsToText(const QList<PipeConnection::Type> &connections)
	{
		QStringList words;
		for (PipeConnection::Type type : PipeConnection::all()) {
			if (connections.contains(type)) {
				words << PipeConnection::toString(type);
			}
		}
		return words.join(QLatin1Char(' '));
	}

		/// The connections @p text names, in the order of PipeConnection::all();
		/// @p unknown, if given, counts the words not known
	QList<PipeConnection::Type> connectionsFromText(const QString &text, int *unknown = nullptr)
	{
		QList<PipeConnection::Type> read;
		for (const QString &word : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
			bool ok = false;
			const PipeConnection::Type type = PipeConnection::fromString(word.trimmed(), &ok);
			if (ok && !read.contains(type)) {
				read << type;
			} else if (!ok && unknown) {
				++*unknown;
			}
		}
		QList<PipeConnection::Type> sorted;
		for (PipeConnection::Type type : PipeConnection::all()) {
			if (read.contains(type)) {
				sorted << type;
			}
		}
		return sorted;
	}

		/// The families of PidCodification::connectionFamilies() @p text
		/// names, words separated by spaces, in their order; @p unknown, if
		/// given, counts the words not known
	QStringList familiesFromText(const QString &text, int *unknown = nullptr)
	{
		const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		QStringList sorted;
		for (const QString &family : PidCodification::connectionFamilies()) {
			if (words.contains(family)) {
				sorted << family;
			}
		}
		if (unknown) {
			for (const QString &word : words) {
				if (!PidCodification::connectionFamilies().contains(word)) {
					++*unknown;
				}
			}
		}
		return sorted;
	}

	QString decimalToString(PidCodification::Decimal decimal)
	{
		switch (decimal) {
			case PidCodification::Decimal::Comma: return QStringLiteral("comma");
			case PidCodification::Decimal::Point: return QStringLiteral("point");
			case PidCodification::Decimal::Language: break;
		}
		return QString();
	}

	PidCodification::Decimal decimalFromString(const QString &word)
	{
		if (word == QLatin1String("comma")) return PidCodification::Decimal::Comma;
		if (word == QLatin1String("point")) return PidCodification::Decimal::Point;
		return PidCodification::Decimal::Language;
	}
	constexpr int largest_text_size = 200;

	bool isTextSize(int size)
	{
		return size > 0 && size <= largest_text_size;
	}

	const PidCodification::Entry *find(const QList<PidCodification::Entry> &list,
									   const QString &code)
	{
		for (const PidCodification::Entry &entry : list) {
			if (entry.code == code) return &entry;
		}
		return nullptr;
	}

	void writeList(QDomDocument &document, QDomElement &block, const QString &tag,
				   const QList<PidCodification::Entry> &list)
	{
		for (const PidCodification::Entry &entry : list)
		{
			QDomElement e = document.createElement(tag);
			e.setAttribute(QStringLiteral("code"), entry.code);
			e.setAttribute(QStringLiteral("name"), entry.name);
			if (!entry.family.isEmpty()) {
				e.setAttribute(QStringLiteral("family"), entry.family);
			}
			if (entry.outside_diameter) {
				e.setAttribute(QStringLiteral("diameter"), QStringLiteral("outside"));
			}
				//The sizes of a material: what is known of each only
			for (const PidCodification::Size &size : entry.sizes)
			{
				QDomElement s = document.createElement(size_tag);
				if (size.outside) {
					s.setAttribute(QStringLiteral("od"), PipeDiameter::fileNumber(size.outside));
				}
				if (size.nominal) {
					s.setAttribute(QStringLiteral("dn"), size.nominal);
				}
				if (size.by_default) {
					s.setAttribute(QStringLiteral("default"), QStringLiteral("true"));
				}
				e.appendChild(s);
			}
			block.appendChild(e);
		}
	}

		/// The DN written in @p text, a whole number; 0 for anything else,
		/// and for nothing
	int nominalOfText(const QString &text)
	{
		const int tenths = PipeDiameter::tenthsOf(text);
		return tenths % 10 ? 0 : tenths / 10;
	}

		/// What orders the sizes of a material: its outside diameter, or its
		/// DN without one
	int sizeKey(const PidCodification::Size &size)
	{
		return size.outside ? size.outside : size.nominal * 10;
	}

		/// The DN of @p tenths, sorted, each once
	QList<PipeDiameter> sortedNominals(QList<int> values)
	{
		std::sort(values.begin(), values.end());
		QList<PipeDiameter> sizes;
		for (int value : std::as_const(values)) {
			const PipeDiameter size = PipeDiameter::nominal(value);
			if (size.isSet() && !sizes.contains(size)) {
				sizes << size;
			}
		}
		return sizes;
	}

	void writeColors(QDomDocument &document, QDomElement &block, const QString &group,
					 const QMap<QString, QColor> &colors)
	{
		for (auto it = colors.cbegin() ; it != colors.cend() ; ++it)
		{
			QDomElement e = document.createElement(QStringLiteral("color"));
			e.setAttribute(QStringLiteral("group"), group);
			e.setAttribute(QStringLiteral("key"), it.key());
			e.setAttribute(QStringLiteral("value"), it.value().name());
			block.appendChild(e);
		}
	}

	QMap<QString, QColor> readColors(const QDomElement &block, const QString &group)
	{
		QMap<QString, QColor> colors;
		const QString tag = QStringLiteral("color");
		for (QDomElement e = block.firstChildElement(tag) ; !e.isNull() ;
			 e = e.nextSiblingElement(tag))
		{
			const QString key = e.attribute(QStringLiteral("key")).trimmed();
			const QColor color(e.attribute(QStringLiteral("value")));
			if (e.attribute(QStringLiteral("group")) == group && !key.isEmpty() && color.isValid()) {
				colors.insert(key, color);
			}
		}
		return colors;
	}

		/// The fields of a line of a file of codes: a field between quotes
		/// may hold the separator, and "" stands for a quote in it
	QStringList splitFields(const QString &line, QChar separator)
	{
		QStringList fields;
		QString field;
		bool quoted = false;
		for (int i = 0 ; i < line.size() ; ++i)
		{
			const QChar c = line.at(i);
			if (quoted)
			{
				if (c != QLatin1Char('"')) {
					field += c;
				} else if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
					field += c;
					++i;
				} else {
					quoted = false;
				}
			}
			else if (c == QLatin1Char('"') && field.trimmed().isEmpty()) {
				field.clear();
				quoted = true;
			}
			else if (c == separator) {
				fields << field.trimmed();
				field.clear();
			}
			else {
				field += c;
			}
		}
		fields << field.trimmed();
		return fields;
	}

		/// @p field as a field of a line fromCsv() reads back
	QString csvField(const QString &field)
	{
		if (!field.contains(QLatin1Char(';')) && !field.contains(QLatin1Char('"'))
			&& field.trimmed() == field) {
			return field;
		}
		QString quoted = field;
		quoted.replace(QStringLiteral("\""), QStringLiteral("\"\""));
		return QLatin1Char('"') + quoted + QLatin1Char('"');
	}

	QList<PidCodification::Entry> readList(const QDomElement &block, const QString &tag)
	{
		QList<PidCodification::Entry> list;
		QSet<QString> codes;
		for (QDomElement e = block.firstChildElement(tag) ; !e.isNull() ;
			 e = e.nextSiblingElement(tag))
		{
			PidCodification::Entry entry;
			entry.code = e.attribute(QStringLiteral("code")).trimmed();
			entry.name = e.attribute(QStringLiteral("name")).trimmed();
			entry.family = e.attribute(QStringLiteral("family")).trimmed();
			entry.outside_diameter = e.attribute(QStringLiteral("diameter")) == QLatin1String("outside");
				//A size which cannot be read, or read already, is left out
			for (QDomElement s = e.firstChildElement(size_tag) ; !s.isNull() ;
				 s = s.nextSiblingElement(size_tag))
			{
				const QString od = s.attribute(QStringLiteral("od"));
				const QString dn = s.attribute(QStringLiteral("dn"));
				const int outside = PipeDiameter::tenthsOf(od);
				const int nominal = nominalOfText(dn);
				if ((od.isEmpty() || outside) && (dn.isEmpty() || nominal)) {
					entry.addSize(outside, nominal,
								  s.attribute(QStringLiteral("default")) == QLatin1String("true"));
				}
			}
			if (entry.code.isEmpty() || codes.contains(entry.code)) {
				continue;
			}
			codes.insert(entry.code);
			list << entry;
		}
		return list;
	}
}

bool PidCodification::Entry::operator==(const Entry &other) const
{
	return code == other.code && name == other.name && family == other.family
			&& outside_diameter == other.outside_diameter && sizes == other.sizes;
}

/**
	@brief PidCodification::Entry::addSize
	See the declaration
*/
bool PidCodification::Entry::addSize(int outside, int nominal, bool by_default)
{
	if (outside < 0 || nominal < 0 || (!outside && !nominal)
		|| (outside && !PipeDiameter::fromTenths(PipeDiameter::Outside, outside).isSet())
		|| (nominal && !PipeDiameter::nominal(nominal).isSet())) {
		return false;
	}
	for (const Size &size : std::as_const(sizes)) {
		if (outside ? size.outside == outside : (!size.outside && size.nominal == nominal)) {
			return false;
		}
	}
	Size added;
	added.outside = outside;
	added.nominal = nominal;
	const auto place = std::upper_bound(sizes.begin(), sizes.end(), added,
										[](const Size &a, const Size &b) {
		if (sizeKey(a) != sizeKey(b)) return sizeKey(a) < sizeKey(b);
		return a.nominal != b.nominal ? a.nominal < b.nominal : a.outside < b.outside;
	});
	sizes.insert(place, added);
	if (by_default) {
		setDefaultSize(outside);
	}
	return true;
}

/**
	@brief PidCodification::Entry::removeSize
	See the declaration
*/
bool PidCodification::Entry::removeSize(int outside, int nominal)
{
	Size removed;
	removed.outside = outside;
	removed.nominal = nominal;
	const auto same = [&removed](const Size &size) {
		return size.outside == removed.outside && size.nominal == removed.nominal;
	};
	const auto place = std::find_if(sizes.begin(), sizes.end(), same);
	if (place == sizes.end()) {
		return false;
	}
	sizes.erase(place);
		//Nothing to choose any more for its DN
	if (nominal && outsideCount(nominal) < 2) {
		for (Size &size : sizes) {
			if (size.nominal == nominal) {
				size.by_default = false;
			}
		}
	}
	return true;
}

/**
	@brief PidCodification::Entry::setDefaultSize
	See the declaration
*/
bool PidCodification::Entry::setDefaultSize(int outside, bool on)
{
	const auto place = std::find_if(sizes.begin(), sizes.end(), [outside](const Size &size) {
		return outside && size.outside == outside;
	});
	if (place == sizes.end() || !place->nominal) {
		return false;
	}
		//One at most by DN
	const int nominal = place->nominal;
	for (Size &size : sizes) {
		if (size.nominal == nominal) {
			size.by_default = false;
		}
	}
	place->by_default = on;
	return true;
}

int PidCodification::Entry::outsideCount(int nominal) const
{
	int count = 0;
	for (const Size &size : sizes) {
		if (size.outside && size.nominal == nominal) {
			++count;
		}
	}
	return count;
}

/**
	@brief PidCodification::addEqualNominals
	See the declaration
*/
bool PidCodification::addEqualNominals(int a, int b)
{
	if (a == b || !PipeDiameter::nominal(a).isSet() || !PipeDiameter::nominal(b).isSet()) {
		return false;
	}
	const QPair<int, int> pair(qMin(a, b), qMax(a, b));
	if (equal_nominals.contains(pair)) {
		return false;
	}
	equal_nominals.insert(std::upper_bound(equal_nominals.begin(), equal_nominals.end(), pair), pair);
	return true;
}

/**
	@brief PidCodification::equalNominals
	See the declaration
*/
QList<int> PidCodification::equalNominals(int dn) const
{
	QList<int> equal{dn};
	for (int i = 0 ; i < equal.size() ; ++i) {
		for (const QPair<int, int> &pair : equal_nominals) {
			const int other = pair.first == equal.at(i) ? pair.second
							: pair.second == equal.at(i) ? pair.first : 0;
			if (other && !equal.contains(other)) {
				equal << other;
			}
		}
	}
	std::sort(equal.begin(), equal.end());
	return equal;
}

bool PidCodification::sameNominal(int a, int b) const
{
	return a == b || equalNominals(a).contains(b);
}

/**
	@brief PidCodification::equalNominalsText
	See the declaration
*/
QString PidCodification::equalNominalsText() const
{
	QStringList groups;
	QList<int> written;
	for (const QPair<int, int> &pair : equal_nominals)
	{
		if (written.contains(pair.first)) {
			continue;
		}
		QStringList numbers;
		for (int dn : equalNominals(pair.first)) {
			numbers << QString::number(dn);
			written << dn;
		}
		groups << numbers.join(QLatin1Char('/'));
	}
	return groups.join(QStringLiteral(", "));
}

/**
	@brief PidCodification::setEqualNominalsText
	See the declaration
*/
bool PidCodification::setEqualNominalsText(const QString &text)
{
	QString written = text;
	written.remove(QStringLiteral("DN"), Qt::CaseInsensitive);
		//A space around a bar joins nothing more
	static const QRegularExpression around_bar(QStringLiteral("\\s*/\\s*"));
	written.replace(around_bar, QStringLiteral("/"));
	static const QRegularExpression between_groups(QStringLiteral("[,;\\s]+"));
	PidCodification read;
	for (const QString &group : written.split(between_groups, Qt::SkipEmptyParts))
	{
		const QStringList parts = group.split(QLatin1Char('/'));
		if (parts.size() < 2) {
			return false;
		}
		for (int i = 0 ; i + 1 < parts.size() ; ++i)
		{
			const int a = nominalOfText(parts.at(i)), b = nominalOfText(parts.at(i + 1));
			if (!a || !b || a == b) {
				return false;
			}
				//Said twice is said once
			read.addEqualNominals(a, b);
		}
	}
	equal_nominals = read.equal_nominals;
	return true;
}

/**
	@brief PidCodification::usualNominal
	See the declaration
*/
int PidCodification::usualNominal(int dn) const
{
	const QList<int> equal = equalNominals(dn);
	if (equal.size() < 2) {
		return dn;
	}
	int usual = 0, most = -1;
	for (int candidate : equal)
	{
		int users = 0;
		for (const Entry &entry : materials) {
			for (const Size &size : entry.sizes) {
				if (size.nominal == candidate) {
					++users;
					break;
				}
			}
		}
			//In order: of two as usual, the greater
		if (users >= most) {
			most = users;
			usual = candidate;
		}
	}
	return usual;
}

/**
	@brief PidCodification::text
	See the declaration
*/
QString PidCodification::text(const PipeDiameter &size, const QString &material_code) const
{
	if (size.isNominal() && !material(material_code))
	{
		const QList<int> equal = equalNominals(size.tenths() / 10);
		if (equal.size() > 1) {
			QStringList numbers;
			for (int dn : equal) {
				numbers << QString::number(dn);
			}
			return QStringLiteral("DN ") + numbers.join(QLatin1Char('/'));
		}
	}
	return size.text(locale());
}

/**
	@brief PidCodification::nominalOfEqualText
	See the declaration
*/
PipeDiameter PidCodification::nominalOfEqualText(const QString &text) const
{
	QString written = text.trimmed();
	if (written.startsWith(QLatin1String("DN"), Qt::CaseInsensitive)) {
		written = written.mid(2);
	}
	const QStringList parts = written.split(QLatin1Char('/'));
	if (parts.size() < 2) {
		return PipeDiameter();
	}
	QList<int> numbers;
	for (const QString &part : parts) {
		const int dn = nominalOfText(part);
		if (!dn || (!numbers.isEmpty() && !sameNominal(numbers.first(), dn))) {
			return PipeDiameter();
		}
		numbers << dn;
	}
	return PipeDiameter::nominal(usualNominal(numbers.first()));
}

/**
	@brief PidCodification::nominalIn
	See the declaration
*/
PipeDiameter PidCodification::nominalIn(const PipeDiameter &nominal,
										const QString &material_code) const
{
	const Entry *entry = material(material_code);
	if (!nominal.isNominal()) {
		return nominal;
	}
	const int dn = nominal.tenths() / 10;
	if (!entry) {
			//No material chooses among equal DN: the usual one stands for all
		return PipeDiameter::nominal(usualNominal(dn));
	}
	int equal = 0;
	for (const Size &size : entry->sizes) {
		if (size.nominal == dn) {
			return nominal;
		}
		if (size.nominal && sameNominal(size.nominal, dn) && (!equal || size.nominal < equal)) {
			equal = size.nominal;
		}
	}
	return equal ? PipeDiameter::nominal(equal) : nominal;
}

QList<PipeDiameter> PidCodification::sizesOf(const Entry &entry)
{
	if (!entry.outside_diameter)
	{
		QList<int> nominals;
		for (const Size &size : entry.sizes) {
			if (size.nominal) {
				nominals << size.nominal;
			}
		}
		return sortedNominals(nominals);
	}
		//In the order of the sizes, that of their outside diameters
	QList<PipeDiameter> sizes;
	for (const Size &size : entry.sizes) {
		if (size.outside) {
			sizes << PipeDiameter::fromTenths(PipeDiameter::Outside, size.outside);
		}
	}
	return sizes;
}

/**
	@brief PidCodification::pipeSizes
	See the declaration
*/
PidCodification::SizeRule PidCodification::pipeSizes(const QString &material_code,
													 QList<PipeDiameter> *sizes) const
{
	QList<PipeDiameter> list;
	SizeRule rule = SizeRule::Series;
	if (materials.isEmpty())
	{
			//A project which lists no material is never stuck
		rule = SizeRule::Iso;
		for (int dn : PipeProperties::preferredDiameters()) {
			list << PipeDiameter::nominal(dn);
		}
	}
	else if (const Entry *entry = material(material_code))
	{
		list = sizesOf(*entry);
		if (list.isEmpty()) {
			rule = SizeRule::NoSeries;
		}
	}
	else {
		rule = SizeRule::NoMaterial;
	}
	if (sizes) {
		*sizes = list;
	}
	return rule;
}

bool PidCodification::acceptsForPipe(const PipeDiameter &diameter, const QString &material_code) const
{
	if (!diameter.isSet()) {
		return true;
	}
	QList<PipeDiameter> sizes;
	pipeSizes(material_code, &sizes);
	return sizes.contains(diameter);
}

/**
	@brief PidCodification::nozzleSizes
	See the declaration
*/
QList<PipeDiameter> PidCodification::nozzleSizes() const
{
	if (materials.isEmpty()) {
		QList<PipeDiameter> iso;
		pipeSizes(QString(), &iso);
		return iso + PipeDiameter::threadSeries();
	}
		//A flange has the DN of its line, whatever the material names
	QList<int> nominal, outside;
	for (const Entry &entry : materials) {
		for (const Size &size : entry.sizes) {
			if (size.nominal) {
				nominal << size.nominal;
			}
			if (entry.outside_diameter && size.outside && !outside.contains(size.outside)) {
				outside << size.outside;
			}
		}
	}
	std::sort(outside.begin(), outside.end());
	QList<PipeDiameter> sizes = sortedNominals(nominal);
	for (int tenths : std::as_const(outside)) {
		sizes << PipeDiameter::fromTenths(PipeDiameter::Outside, tenths);
	}
		//The threads are fixed, no material names them
	return sizes + PipeDiameter::threadSeries();
}

bool PidCodification::acceptsForNozzle(const PipeDiameter &diameter) const
{
	if (!diameter.isSet()) {
		return true;
	}
		//A project without material has said nothing of the outside
		//diameters: any is taken
	if (materials.isEmpty() && diameter.isOutside()) {
		return true;
	}
	return nozzleSizes().contains(diameter);
}

/**
	@brief PidCodification::nearest
	See the declaration
*/
PipeDiameter PidCodification::nearest(const PipeDiameter &typed, const QList<PipeDiameter> &sizes)
{
	PipeDiameter best;
	int best_gap = 0;
	for (const PipeDiameter &size : sizes)
	{
		if (size.kind() != typed.kind()) {
			continue;
		}
		const int gap = qAbs(size.tenths() - typed.tenths());
		if (!best.isSet() || gap < best_gap || (gap == best_gap && size.tenths() > best.tenths())) {
			best = size;
			best_gap = gap;
		}
	}
	return best;
}

QStringList PidCodification::materialsWithoutSizes() const
{
	QStringList codes;
	for (const Entry &entry : materials) {
		if (sizesOf(entry).isEmpty()) {
			codes << entry.code;
		}
	}
	return codes;
}

/**
	@brief PidCodification::compare
	See the declaration
*/
PidCodification::DiameterMatch PidCodification::compare(const PipeDiameter &a,
														const PipeDiameter &b) const
{
	if (!a.isSet() || !b.isSet()) {
		return DiameterMatch::Same;
	}
	if (a.kind() == b.kind()) {
		const bool same = a == b || (a.isNominal() && sameNominal(a.tenths() / 10, b.tenths() / 10));
		return same ? DiameterMatch::Same : DiameterMatch::Different;
	}
		//A thread is compared by its DN, of the fixed table
	if (a.isThread() || b.isThread()) {
		const PipeDiameter dn_a = a.isThread() ? a.threadNominal() : a;
		const PipeDiameter dn_b = b.isThread() ? b.threadNominal() : b;
		if (!dn_a.isSet() || !dn_b.isSet()) {
			return DiameterMatch::Unknown;
		}
		return compare(dn_a, dn_b);
	}
		//No material comes with them: what all the materials say of this
		//outside diameter
	const PipeDiameter &outside = a.isOutside() ? a : b;
	const int dn = (a.isOutside() ? b : a).tenths() / 10;
	const QList<PipeDiameter> given = nominalsOf(outside);
	if (given.isEmpty()) {
		return DiameterMatch::Unknown;
	}
	int same = 0;
	for (const PipeDiameter &nominal : given) {
		if (sameNominal(nominal.tenths() / 10, dn)) {
			++same;
		}
	}
	return same == given.size() ? DiameterMatch::Same
								: same ? DiameterMatch::Unknown : DiameterMatch::Different;
}

/**
	@brief PidCodification::nominalOf
	See the declaration
*/
PipeDiameter PidCodification::nominalOf(const PipeDiameter &outside,
										const QString &material_code) const
{
	const QList<PipeDiameter> joined = nominalsOf(outside, material_code);
	return joined.size() == 1 ? joined.first() : PipeDiameter();
}

/**
	@brief PidCodification::lineNominal
	See the declaration
*/
PipeDiameter PidCodification::lineNominal(const PipeDiameter &diameter) const
{
	if (diameter.isNominal()) {
		return diameter;
	}
	if (diameter.isThread()) {
		return diameter.threadNominal();
	}
	return nominalOf(diameter);
}

/**
	@brief PidCodification::nominalsOf
	See the declaration
*/
QList<PipeDiameter> PidCodification::nominalsOf(const PipeDiameter &outside,
												const QString &material_code) const
{
	if (!outside.isOutside()) {
		return {};
	}
		//An outside diameter has one DN in a material
	const auto nominalOfSize = [&outside](const Entry &entry) {
		for (const Size &size : entry.sizes) {
			if (size.outside == outside.tenths()) {
				return size.nominal;
			}
		}
		return 0;
	};
	const Entry *own = material(material_code);
	if (own) {
		if (const int dn = nominalOfSize(*own)) {
			return {PipeDiameter::nominal(dn)};
		}
	}
	QList<int> found;
	for (const Entry &entry : materials) {
		if (const int dn = nominalOfSize(entry)) {
			found << nominalIn(PipeDiameter::nominal(dn), material_code).tenths() / 10;
		}
	}
	return sortedNominals(found);
}

/**
	@brief PidCodification::nominalSizes
	See the declaration
*/
QList<PipeDiameter> PidCodification::nominalSizes() const
{
		//Equal DN as one, the usual one of them
	QList<int> nominals;
	for (const Entry &entry : materials) {
		for (const Size &size : entry.sizes) {
			if (size.nominal) {
				nominals << usualNominal(size.nominal);
			}
		}
	}
	QList<PipeDiameter> sizes = sortedNominals(nominals);
	if (sizes.isEmpty()) {
		for (int dn : PipeProperties::preferredDiameters()) {
			sizes << PipeDiameter::nominal(dn);
		}
	}
	return sizes;
}

/**
	@brief PidCodification::lineSizes
	See the declaration
*/
QList<PipeDiameter> PidCodification::lineSizes(const QString &material_code) const
{
	const Entry *entry = material(material_code);
	if (!entry) {
		return nominalSizes();
	}
	QList<int> nominals;
	for (const Size &size : entry->sizes) {
		if (size.nominal) {
			nominals << size.nominal;
		}
	}
	return sortedNominals(nominals);
}

/**
	@brief PidCodification::outsidesOf
	See the declaration
*/
QList<PipeDiameter> PidCodification::outsidesOf(const PipeDiameter &nominal,
												const QString &material_code) const
{
	QList<PipeDiameter> found;
	const Entry *entry = material(material_code);
	if (!nominal.isNominal() || !entry || !entry->outside_diameter) {
		return found;
	}
	for (const Size &size : entry->sizes) {
		if (size.outside && size.nominal == nominal.tenths() / 10) {
			found << PipeDiameter::fromTenths(PipeDiameter::Outside, size.outside);
		}
	}
	return found;
}

/**
	@brief PidCodification::defaultOutside
	See the declaration
*/
PipeDiameter PidCodification::defaultOutside(const PipeDiameter &nominal,
											 const QString &material_code) const
{
	if (outsidesOf(nominal, material_code).size() < 2) {
		return PipeDiameter();
	}
	for (const Size &size : material(material_code)->sizes) {
		if (size.by_default && size.outside && size.nominal == nominal.tenths() / 10) {
			return PipeDiameter::fromTenths(PipeDiameter::Outside, size.outside);
		}
	}
	return PipeDiameter();
}

/**
	@brief PidCodification::outsideOf
	See the declaration
*/
PipeDiameter PidCodification::outsideOf(const PipeDiameter &nominal,
										const QString &material_code) const
{
	const QList<PipeDiameter> outsides = outsidesOf(nominal, material_code);
	return outsides.size() == 1 ? outsides.first() : defaultOutside(nominal, material_code);
}

/**
	@brief PidCodification::choiceKey
	See the declaration
*/
QString PidCodification::choiceKey(const QString &code, const PipeDiameter &dn)
{
	return code + QLatin1Char('|') + QString::number(dn.tenths());
}

/**
	@brief PidCodification::pipeNominal
	See the declaration
*/
PipeDiameter PidCodification::pipeNominal(const PipeProperties &p, const QMap<int, PipeDiameter> &chosen) const
{
	if (p.nominal.isSet() || !p.outside.isSet()) {
		return p.nominal;
	}
	const QList<PipeDiameter> joined = nominalsOf(p.outside, p.material);
	return joined.size() == 1 ? joined.first() : chosen.value(p.outside.tenths());
}

/**
	@brief PidCodification::giveMaterial
	See the declaration
*/
void PidCodification::giveMaterial(PipeProperties &p, const QString &code,
								   const QMap<int, PipeDiameter> &nominal_of,
								   const QMap<QString, PipeDiameter> &outside_of) const
{
		//Its DN first: without one, it comes from the material it leaves
	const PipeDiameter dn = nominalIn(pipeNominal(p, nominal_of), code);
	p.material = code;
	if (!dn.isSet()) {
		return;
	}
	p.nominal = dn;
	if (!outsideDiameterFor(code)) {
		p.outside = PipeDiameter();
		return;
	}
	if (!outsidesOf(dn, code).contains(p.outside)) {
		const PipeDiameter taken = outsideOf(dn, code);
		p.outside = taken.isSet() ? taken : outside_of.value(choiceKey(code, dn));
	}
}

/**
	@brief PidCodification::giveLineSize
	See the declaration
*/
void PidCodification::giveLineSize(PipeProperties &p, const PipeDiameter &dn,
								   const QMap<QString, PipeDiameter> &outside_of) const
{
	p.nominal = nominalIn(dn, p.material);
	const Entry *entry = material(p.material);
	if (!entry) {
		if (!nominalsOf(p.outside, p.material).contains(p.nominal)) {
			p.outside = PipeDiameter();
		}
		return;
	}
	if (!entry->outside_diameter) {
		p.outside = PipeDiameter();
		return;
	}
	if (!outsidesOf(p.nominal, p.material).contains(p.outside)) {
		const PipeDiameter taken = outsideOf(p.nominal, p.material);
		p.outside = taken.isSet() ? taken : outside_of.value(choiceKey(p.material, p.nominal));
	}
}

/**
	@brief PidCodification::languageDecimal
	See the declaration: the separator the language chosen in QElectroTech
	writes, not that of the computer
*/
PidCodification::Decimal PidCodification::languageDecimal()
{
	return QLocale(QETApp::langFromSetting()).decimalPoint() == QLatin1String(",")
			? Decimal::Comma : Decimal::Point;
}

QLocale PidCodification::locale() const
{
	return effectiveDecimal() == Decimal::Comma ? QLocale(QLocale::French) : QLocale::c();
}

bool PidCodification::outsideDiameterFor(const QString &material_code) const
{
	const Entry *entry = material(material_code);
	return entry && entry->outside_diameter;
}

QList<PidCodification::Entry> &PidCodification::entries(List list)
{
	switch (list) {
		case List::Fluids:    return fluids;
		case List::Materials: return materials;
		case List::Units:     return functional_units;
		case List::Letters:   return instrument_letters;
	}
	return functional_units;
}

const QList<PidCodification::Entry> &PidCodification::entries(List list) const
{
	return const_cast<PidCodification *>(this)->entries(list);
}

/**
	@brief PidCodification::isDataName
	See the declaration
*/
bool PidCodification::isDataName(const QString &name)
{
	static const QRegularExpression rule(QStringLiteral("^[a-z][a-z0-9_]*$"));
	if (!rule.match(name).hasMatch()) {
		return false;
	}
		//The fields of the tag, and the texts made of them
	const QStringList taken{PidTag::unitKey(), PidTag::trainKey(), PidTag::orderKey(),
							PidTag::lettersKey(), QETInformation::PID_TAG_ID,
							QETInformation::PID_TAG_REST};
	return !taken.contains(dataPrefix() + name);
}

/**
	@brief PidCodification::addDataField
	See the declaration
*/
bool PidCodification::addDataField(DataField field)
{
	if (field.name.startsWith(dataPrefix())) {
		field.name.remove(0, dataPrefix().size());
	}
		//The standby: its values are those QElectroTech knows, never typed
	if (field.key() == standbyKey()) {
		field.kind = DataKind::List;
		field.values = standbyValues();
	}
	if (!isDataName(field.name) || field.classes.isEmpty()
		|| (field.kind == DataKind::List) == field.values.isEmpty()) {
		return false;
	}
	for (const DataField &known : std::as_const(data_fields)) {
		if (known.name == field.name) {
			return false;
		}
	}
	data_fields << field;
	return true;
}

/**
	@brief PidCodification::dataValueName
	See the declaration
*/
QString PidCodification::dataValueName(const DataField &field, const QString &value)
{
	if (field.key() == standbyKey())
	{
		if (value == standbyInstalled()) {
			return QCoreApplication::translate("PidCodification", "Installed standby");
		}
		if (value == standbySpare()) {
			return QCoreApplication::translate("PidCodification", "Spare in its crate");
		}
	}
	return value;
}

/**
	@brief PidCodification::dataFieldsOf
	See the declaration
*/
QList<PidCodification::DataField> PidCodification::dataFieldsOf(const QString &pid_class) const
{
	QList<DataField> fields;
	if (pid_class.isEmpty()) {
		return fields;
	}
	for (const DataField &field : data_fields) {
		if (field.classes.contains(pid_class)) {
			fields << field;
		}
	}
	return fields;
}

QString PidCodification::dataKindToString(DataKind kind)
{
	switch (kind) {
		case DataKind::Number: return QStringLiteral("number");
		case DataKind::List:   return QStringLiteral("list");
		case DataKind::YesNo:  return QStringLiteral("yes_no");
		case DataKind::Text:   break;
	}
	return QStringLiteral("text");
}

bool PidCodification::dataKindFromString(const QString &word, DataKind &kind)
{
	const QString read = word.trimmed().toLower();
	if (read.isEmpty() || read == QLatin1String("text") || read == QLatin1String("texte")) {
		kind = DataKind::Text;
	} else if (read == QLatin1String("number") || read == QLatin1String("nombre")) {
		kind = DataKind::Number;
	} else if (read == QLatin1String("list") || read == QLatin1String("liste")) {
		kind = DataKind::List;
	} else if (read == QLatin1String("yes_no") || read == QLatin1String("oui_non")) {
		kind = DataKind::YesNo;
	} else {
		return false;
	}
	return true;
}

/**
	@brief PidCodification::codeLessThan
	See the declaration; two codes which differ only by their case keep an
	order of their own, so that the order never depends on the list read
*/
bool PidCodification::codeLessThan(const QString &a, const QString &b)
{
		//By hand rather than by a collator, whose numeric mode some
		//platforms lack: the order saved must be the same everywhere
	int i = 0, j = 0;
	while (i < a.size() && j < b.size())
	{
		if (a.at(i).isDigit() && b.at(j).isDigit())
		{
			int end_a = i, end_b = j;
			while (end_a < a.size() && a.at(end_a).isDigit()) ++end_a;
			while (end_b < b.size() && b.at(end_b).isDigit()) ++end_b;
				//The numbers without their leading zeros: the longer is the
				//greater, else the first digit which differs
			int start_a = i, start_b = j;
			while (start_a < end_a - 1 && a.at(start_a) == QLatin1Char('0')) ++start_a;
			while (start_b < end_b - 1 && b.at(start_b) == QLatin1Char('0')) ++start_b;
			const QStringView number_a = QStringView(a).mid(start_a, end_a - start_a);
			const QStringView number_b = QStringView(b).mid(start_b, end_b - start_b);
			if (number_a.size() != number_b.size()) {
				return number_a.size() < number_b.size();
			}
			const int order = number_a.compare(number_b);
			if (order != 0) {
				return order < 0;
			}
			i = end_a;
			j = end_b;
			continue;
		}
		const QChar ca = a.at(i).toCaseFolded(), cb = b.at(j).toCaseFolded();
		if (ca != cb) {
			return ca < cb;
		}
		++i;
		++j;
	}
	if ((a.size() - i) != (b.size() - j)) {
		return (a.size() - i) < (b.size() - j);
	}
		//W2 and W02, w1 and W1: an order of their own all the same
	return a < b;
}

/**
	@brief PidCodification::sortCodes
	See the declaration
*/
void PidCodification::sortCodes()
{
	for (List list : {List::Fluids, List::Materials, List::Units, List::Letters}) {
		QList<Entry> &codes = entries(list);
		std::stable_sort(codes.begin(), codes.end(), [](const Entry &a, const Entry &b) {
			return codeLessThan(a.code, b.code);
		});
	}
}

const PidCodification::Entry *PidCodification::fluid(const QString &code) const
{
	return find(fluids, code);
}

const PidCodification::Entry *PidCodification::material(const QString &code) const
{
	return find(materials, code);
}

/**
	@brief PidCodification::fluidColor
	See the declaration
*/
QColor PidCodification::fluidColor(const QString &code) const
{
	const Entry *entry = fluid(code);
	return entry ? fluid_family_colors.value(entry->family) : QColor();
}

QStringList PidCodification::objectFamilies()
{
	return {QStringLiteral("pipe"), QStringLiteral("equipment"), QStringLiteral("valve"),
			QStringLiteral("instrument"), QStringLiteral("signal")};
}

QStringList PidCodification::statuses()
{
	return {QStringLiteral("existing"), QStringLiteral("demolished")};
}

QStringList PidCodification::textCategories()
{
	return {QStringLiteral("equipment"), QStringLiteral("valve"),
			QStringLiteral("instrument"), QStringLiteral("pipe"), nozzleTexts(),
			QStringLiteral("line")};
}

/**
	@brief PidCodification::signalMark
	See the declaration
*/
SignalMark::Mark PidCodification::signalMark(SignalProperties::SignalType type) const
{
	const auto said = signal_marks.constFind(SignalProperties::typeToString(type));
	if (said != signal_marks.cend()) {
		bool known = false;
		const SignalMark::Mark mark = SignalMark::fromString(said.value(), &known);
		if (known) {
			return mark;
		}
	}
	return SignalMark::defaultMark(type);
}

/**
	@brief PidCodification::setSignalMark
	See the declaration
*/
void PidCodification::setSignalMark(SignalProperties::SignalType type, SignalMark::Mark mark)
{
	const QString word = SignalProperties::typeToString(type);
	if (mark == SignalMark::defaultMark(type)) {
		signal_marks.remove(word);
	} else {
		signal_marks.insert(word, SignalMark::toString(mark));
	}
}

/**
	@brief PidCodification::hasSignalMarks
	See the declaration
*/
bool PidCodification::hasSignalMarks() const
{
	for (auto it = signal_marks.cbegin() ; it != signal_marks.cend() ; ++it) {
		if (!isDefaultSignalMark(it.key(), it.value())) {
			return true;
		}
	}
	return false;
}

/**
	@brief PidCodification::dropDefaultSignalMarks
	See the declaration
*/
void PidCodification::dropDefaultSignalMarks()
{
	for (auto it = signal_marks.begin() ; it != signal_marks.end() ; ) {
		if (isDefaultSignalMark(it.key(), it.value())) {
			it = signal_marks.erase(it);
		} else {
			++it;
		}
	}
}

/**
	@brief PidCodification::completedWith
	See the declaration
*/
PidCodification PidCodification::completedWith(const PidCodification &other) const
{
	PidCodification result = *this;
	if (!hasCodeLists()) {
		result.fluids = other.fluids;
		result.materials = other.materials;
		result.functional_units = other.functional_units;
		result.instrument_letters = other.instrument_letters;
			//A file may say the equal DN only
		if (equal_nominals.isEmpty()) {
			result.equal_nominals = other.equal_nominals;
		}
	}
	if (!hasColors()) {
		result.fluid_family_colors = other.fluid_family_colors;
		result.object_colors = other.object_colors;
		result.status_colors = other.status_colors;
		result.symbols_follow_pipes = other.symbols_follow_pipes;
	}
	if (!hasTextSettings()) {
		result.text_sizes = other.text_sizes;
		result.colored_labels = other.colored_labels;
		result.label_color = other.label_color;
		result.colored_designations = other.colored_designations;
		result.designation_color = other.designation_color;
		result.vertical_texts = other.vertical_texts;
		result.nozzle_texts_on_paper = other.nozzle_texts_on_paper;
		result.show_nozzle_sizes = other.show_nozzle_sizes;
		result.nozzle_dn_prefix = other.nozzle_dn_prefix;
		result.reducer_sizes = other.reducer_sizes;
		result.insulation_stub = other.insulation_stub;
		result.decimal = other.decimal;
		result.tag_models = other.tag_models;
		result.parallel_numbering = other.parallel_numbering;
		result.drawn_connections = other.drawn_connections;
		result.drawn_families = other.drawn_families;
		result.tool_symbols = other.tool_symbols;
		result.option_defaults = other.option_defaults;
		result.line_kinds = other.line_kinds;
	}
		//A file which has marks of signal lines says them all
	if (signal_marks.isEmpty()) {
		result.signal_marks = other.signal_marks;
	}
		//So does a file which declares data of the symbols
	if (data_fields.isEmpty()) {
		result.data_fields = other.data_fields;
	}
	return result;
}

/**
	@brief PidCodification::toXml
	@param document
	@return the <pid_codification> block, fluids then materials in their
	order; a null element when there is neither, so that a project without
	one saves as before
*/
QDomElement PidCodification::toXml(QDomDocument &document) const
{
	if (isEmpty()) {
		return QDomElement();
	}
	QDomElement block = document.createElement(block_tag);
	writeList(document, block, QStringLiteral("fluid"), fluids);
	writeList(document, block, QStringLiteral("material"), materials);
	writeList(document, block, QStringLiteral("functional_unit"), functional_units);
	writeList(document, block, letters_tag, instrument_letters);
	for (const QPair<int, int> &pair : equal_nominals)
	{
		QDomElement e = document.createElement(equal_tag);
		e.setAttribute(QStringLiteral("a"), pair.first);
		e.setAttribute(QStringLiteral("b"), pair.second);
		block.appendChild(e);
	}
	for (auto it = tag_models.cbegin() ; it != tag_models.cend() ; ++it)
	{
		QDomElement e = document.createElement(QStringLiteral("tag_model"));
		e.setAttribute(QStringLiteral("family"), it.key());
		e.setAttribute(QStringLiteral("value"), it.value());
		block.appendChild(e);
	}
	if (parallel_numbering) {
		block.setAttribute(numbering_word, parallel_word);
	}
	writeColors(document, block, QStringLiteral("fluid_family"), fluid_family_colors);
	writeColors(document, block, QStringLiteral("object"), object_colors);
	writeColors(document, block, QStringLiteral("status"), status_colors);
	if (!symbols_follow_pipes) {
		block.setAttribute(follow_pipes_word, QStringLiteral("false"));
	}
	for (auto it = text_sizes.cbegin() ; it != text_sizes.cend() ; ++it)
	{
		QDomElement e = document.createElement(QStringLiteral("text_size"));
		e.setAttribute(QStringLiteral("category"), it.key());
		e.setAttribute(QStringLiteral("points"), it.value());
		block.appendChild(e);
	}
	const QString label_ink = inkToText(colored_labels, label_color);
	if (!label_ink.isEmpty()) {
		block.setAttribute(label_ink_word, label_ink);
	}
	const QString designation_ink = inkToText(colored_designations, designation_color);
	if (!designation_ink.isEmpty()) {
		block.setAttribute(designation_ink_word, designation_ink);
	}
	if (vertical_texts != VerticalTexts::Computer) {
		block.setAttribute(vertical_texts_word, verticalTextsToString(vertical_texts));
	}
	if (!nozzle_texts_on_paper) {
		block.setAttribute(nozzle_texts_word, QStringLiteral("false"));
	}
	if (!show_nozzle_sizes) {
		block.setAttribute(nozzle_sizes_word, QStringLiteral("false"));
	}
	if (!nozzle_dn_prefix) {
		block.setAttribute(dn_prefix_word, QStringLiteral("false"));
	}
	if (!reducer_sizes) {
		block.setAttribute(reducer_sizes_word, QStringLiteral("false"));
	}
	if (decimal != Decimal::Language) {
		block.setAttribute(decimal_word, decimalToString(decimal));
	}
	if (insulation_stub != defaultInsulationStub()) {
		block.setAttribute(insulation_stub_word, QString::number(insulation_stub));
	}
	if (!drawn_connections.isEmpty()) {
		block.setAttribute(drawn_connections_word, connectionsToText(drawn_connections));
	}
	if (!drawsOnAllFamilies()) {
		block.setAttribute(drawn_families_word, drawn_families.join(QLatin1Char(' ')));
	}
	for (auto it = tool_symbols.cbegin() ; it != tool_symbols.cend() ; ++it)
	{
		QDomElement e = document.createElement(tool_symbol_tag);
		e.setAttribute(QStringLiteral("role"), it.key());
		e.setAttribute(QStringLiteral("location"), it.value());
		block.appendChild(e);
	}
	for (auto family = option_defaults.cbegin() ; family != option_defaults.cend() ; ++family)
	{
		for (auto it = family.value().cbegin() ; it != family.value().cend() ; ++it)
		{
			QDomElement e = document.createElement(option_default_tag);
			e.setAttribute(QStringLiteral("family"), family.key());
			e.setAttribute(QStringLiteral("option"), it.key());
			e.setAttribute(QStringLiteral("value"), it.value());
			block.appendChild(e);
		}
	}
	for (auto it = signal_marks.cbegin() ; it != signal_marks.cend() ; ++it)
	{
			//The default one is not written: a project which changes
			//nothing saves as before
		if (isDefaultSignalMark(it.key(), it.value())) {
			continue;
		}
		QDomElement e = document.createElement(signal_mark_tag);
		e.setAttribute(QStringLiteral("type"), it.key());
		e.setAttribute(QStringLiteral("mark"), it.value());
		block.appendChild(e);
	}
		//The kinds of line: those of ISO 10628-1 when they are changed,
		//those added always
	for (const LineKindEntry &entry : line_kinds)
	{
		if (isoLineKinds().contains(entry)) {
			continue;
		}
		QDomElement e = document.createElement(line_kind_tag);
		e.setAttribute(QStringLiteral("code"), entry.code);
		if (!entry.name.isEmpty()) {
			e.setAttribute(QStringLiteral("name"), entry.name);
		}
		e.setAttribute(QStringLiteral("width"), QString::number(entry.width));
		e.setAttribute(QStringLiteral("pattern"), linePatternToString(entry.pattern));
		block.appendChild(e);
	}
		//The data of the symbols: what is empty is not written
	for (const DataField &field : data_fields)
	{
		QDomElement e = document.createElement(data_tag);
		e.setAttribute(QStringLiteral("name"), field.name);
		e.setAttribute(QStringLiteral("classes"), field.classes.join(QLatin1Char(' ')));
		if (!field.label.isEmpty()) {
			e.setAttribute(QStringLiteral("label"), field.label);
		}
		if (!field.unit.isEmpty()) {
			e.setAttribute(QStringLiteral("unit"), field.unit);
		}
		if (field.kind != DataKind::Text) {
			e.setAttribute(QStringLiteral("kind"), dataKindToString(field.kind));
		}
		if (!field.values.isEmpty()) {
			e.setAttribute(QStringLiteral("values"), field.values.join(dataValueSeparator()));
		}
		if (!field.dexpi.isEmpty()) {
			e.setAttribute(QStringLiteral("dexpi"), field.dexpi);
		}
		block.appendChild(e);
	}
		//Default marks alone: nothing to write
	if (!block.hasChildNodes() && !block.hasAttributes()) {
		return QDomElement();
	}
	return block;
}

/**
	@brief PidCodification::fromXml
	@param parent : the element holding the <pid_codification> block
*/
void PidCodification::fromXml(const QDomElement &parent)
{
	const QDomElement block = parent.firstChildElement(block_tag);
	fluids = readList(block, QStringLiteral("fluid"));
	materials = readList(block, QStringLiteral("material"));
	functional_units = readList(block, QStringLiteral("functional_unit"));
	instrument_letters = readList(block, letters_tag);
	equal_nominals.clear();
	for (QDomElement e = block.firstChildElement(equal_tag) ; !e.isNull() ;
		 e = e.nextSiblingElement(equal_tag))
	{
		addEqualNominals(nominalOfText(e.attribute(QStringLiteral("a"))),
						 nominalOfText(e.attribute(QStringLiteral("b"))));
	}
	tag_models.clear();
	const QString model_tag = QStringLiteral("tag_model");
	for (QDomElement e = block.firstChildElement(model_tag) ; !e.isNull() ;
		 e = e.nextSiblingElement(model_tag))
	{
		const QString family = e.attribute(QStringLiteral("family")).trimmed();
		const QString model = e.attribute(QStringLiteral("value"));
		if (!family.isEmpty() && !model.trimmed().isEmpty() && !tag_models.contains(family)) {
			tag_models.insert(family, model);
		}
	}
	parallel_numbering = block.attribute(numbering_word) == parallel_word;
	fluid_family_colors = readColors(block, QStringLiteral("fluid_family"));
	object_colors = readColors(block, QStringLiteral("object"));
	status_colors = readColors(block, QStringLiteral("status"));

	text_sizes.clear();
	const QString tag = QStringLiteral("text_size");
	for (QDomElement e = block.firstChildElement(tag) ; !e.isNull() ;
		 e = e.nextSiblingElement(tag))
	{
		const QString category = e.attribute(QStringLiteral("category")).trimmed();
		const int size = e.attribute(QStringLiteral("points")).toInt();
		if (!category.isEmpty() && isTextSize(size)) {
			text_sizes.insert(category, size);
		}
	}
		//A former project says one colour for the labels and the
		//designations: that of their object
	colored_labels = block.attribute(colored_labels_word) == QLatin1String("true");
	colored_designations = colored_labels;
	label_color = QColor();
	designation_color = QColor();
	inkFromText(block.attribute(label_ink_word), colored_labels, label_color);
	inkFromText(block.attribute(designation_ink_word), colored_designations, designation_color);
	vertical_texts = VerticalTexts::Computer;
	verticalTextsFromString(block.attribute(vertical_texts_word), vertical_texts);
	nozzle_texts_on_paper = block.attribute(nozzle_texts_word) != QLatin1String("false");
	show_nozzle_sizes = block.attribute(nozzle_sizes_word) != QLatin1String("false");
	nozzle_dn_prefix = block.attribute(dn_prefix_word) != QLatin1String("false");
	reducer_sizes = block.attribute(reducer_sizes_word) != QLatin1String("false");
	symbols_follow_pipes = block.attribute(follow_pipes_word) != QLatin1String("false");
	decimal =decimalFromString(block.attribute(decimal_word));
	insulation_stub = defaultInsulationStub();
	insulationStubFromText(block.attribute(insulation_stub_word), insulation_stub);
	drawn_connections = connectionsFromText(block.attribute(drawn_connections_word));
		//None written: all of them
	drawn_families = block.hasAttribute(drawn_families_word)
			? familiesFromText(block.attribute(drawn_families_word)) : connectionFamilies();
	tool_symbols.clear();
	for (QDomElement e = block.firstChildElement(tool_symbol_tag) ; !e.isNull() ;
		 e = e.nextSiblingElement(tool_symbol_tag))
	{
		const QString role = e.attribute(QStringLiteral("role")).trimmed();
		const QString location = e.attribute(QStringLiteral("location")).trimmed();
			//A role this version does not know is kept: a later one placed it
		if (!role.isEmpty() && !location.isEmpty() && !tool_symbols.contains(role)) {
			tool_symbols.insert(role, location);
		}
	}
	option_defaults.clear();
	for (QDomElement e = block.firstChildElement(option_default_tag) ; !e.isNull() ;
		 e = e.nextSiblingElement(option_default_tag))
	{
		const QString family = e.attribute(QStringLiteral("family")).trimmed();
		const QString option = e.attribute(QStringLiteral("option")).trimmed();
			//The first one said of an option is kept
		if (!option_defaults.value(family).contains(option)) {
			setOptionDefault(family, option, e.attribute(QStringLiteral("value")).trimmed());
		}
	}
	signal_marks.clear();
	for (QDomElement e = block.firstChildElement(signal_mark_tag) ; !e.isNull() ;
		 e = e.nextSiblingElement(signal_mark_tag))
	{
		const QString type = e.attribute(QStringLiteral("type")).trimmed();
		const QString mark = e.attribute(QStringLiteral("mark")).trimmed();
			//A type or a mark this version does not know is kept: a later
			//one set it, and its lines take the default mark meanwhile
		if (!type.isEmpty() && !mark.isEmpty() && !signal_marks.contains(type)) {
			signal_marks.insert(type, mark);
		}
	}
	line_kinds = isoLineKinds();
	for (QDomElement e = block.firstChildElement(line_kind_tag) ; !e.isNull() ;
		 e = e.nextSiblingElement(line_kind_tag))
	{
		LineKindEntry entry;
		if (lineKindFromFields(e.attribute(QStringLiteral("code")), e.attribute(QStringLiteral("name")),
							   e.attribute(QStringLiteral("width")), e.attribute(QStringLiteral("pattern")),
							   entry)) {
			setLineKind(entry);
		}
	}
	data_fields.clear();
	for (QDomElement e = block.firstChildElement(data_tag) ; !e.isNull() ;
		 e = e.nextSiblingElement(data_tag))
	{
			//One which cannot be read, or named as another, is left out
		DataField field;
		if (dataFieldFromTexts(e.attribute(QStringLiteral("classes")), e.attribute(QStringLiteral("name")),
							   e.attribute(QStringLiteral("label")), e.attribute(QStringLiteral("unit")),
							   e.attribute(QStringLiteral("kind")), e.attribute(QStringLiteral("values")),
							   e.attribute(QStringLiteral("dexpi")), field)) {
			addDataField(field);
		}
	}
}

/**
	@brief PidCodification::presentationToCsv
	See the declaration
*/
QString PidCodification::presentationToCsv() const
{
	QString text;
	auto write = [&text](const QString &kind, const QMap<QString, QColor> &colors) {
		for (auto it = colors.cbegin() ; it != colors.cend() ; ++it) {
			text += kind + QLatin1Char(';') + it.key() + QLatin1Char(';')
					+ it.value().name() + QLatin1Char('\n');
		}
	};
	write(QStringLiteral("fluid_color"), fluid_family_colors);
	write(QStringLiteral("object_color"), object_colors);
	write(QStringLiteral("status_color"), status_colors);
	if (!symbols_follow_pipes) {
		text += QStringLiteral("text_option;") + follow_pipes_word + QStringLiteral(";false\n");
	}
	for (auto it = text_sizes.cbegin() ; it != text_sizes.cend() ; ++it) {
		text += QStringLiteral("text_size;") + it.key() + QLatin1Char(';')
				+ QString::number(it.value()) + QLatin1Char('\n');
	}
	const QString label_ink = inkToText(colored_labels, label_color);
	if (!label_ink.isEmpty()) {
		text += QStringLiteral("text_option;") + label_ink_word + QLatin1Char(';') + label_ink + QLatin1Char('\n');
	}
	const QString designation_ink = inkToText(colored_designations, designation_color);
	if (!designation_ink.isEmpty()) {
		text += QStringLiteral("text_option;") + designation_ink_word + QLatin1Char(';')
				+ designation_ink + QLatin1Char('\n');
	}
	if (vertical_texts != VerticalTexts::Computer) {
		text += QStringLiteral("text_option;") + vertical_texts_word + QLatin1Char(';')
				+ verticalTextsToString(vertical_texts) + QLatin1Char('\n');
	}
	if (!nozzle_texts_on_paper) {
		text += QStringLiteral("text_option;") + nozzle_texts_word + QStringLiteral(";false\n");
	}
	if (!show_nozzle_sizes) {
		text += QStringLiteral("text_option;") + nozzle_sizes_word + QStringLiteral(";false\n");
	}
	if (!nozzle_dn_prefix) {
		text += QStringLiteral("text_option;") + dn_prefix_word + QStringLiteral(";false\n");
	}
	if (!reducer_sizes) {
		text += QStringLiteral("text_option;") + reducer_sizes_word + QStringLiteral(";false\n");
	}
	if (decimal != Decimal::Language) {
		text += QStringLiteral("text_option;") + decimal_word + QLatin1Char(';')
				+ decimalToString(decimal) + QLatin1Char('\n');
	}
	if (insulation_stub != defaultInsulationStub()) {
		text += QStringLiteral("text_option;") + insulation_stub_word + QLatin1Char(';')
				+ QString::number(insulation_stub) + QLatin1Char('\n');
	}
	if (!drawn_connections.isEmpty()) {
		text +=QStringLiteral("text_option;") + drawn_connections_word + QLatin1Char(';')
				+ connectionsToText(drawn_connections) + QLatin1Char('\n');
	}
	if (!drawsOnAllFamilies()) {
		text += QStringLiteral("text_option;") + drawn_families_word + QLatin1Char(';')
				+ drawn_families.join(QLatin1Char(' ')) + QLatin1Char('\n');
	}
	for (auto it = tag_models.cbegin() ; it != tag_models.cend() ; ++it) {
		text += QStringLiteral("tag_model;") + it.key() + QLatin1Char(';') + it.value() + QLatin1Char('\n');
	}
	if (parallel_numbering) {
		text += tag_option_kind + QLatin1Char(';') + numbering_word + QLatin1Char(';')
				+ parallel_word + QLatin1Char('\n');
	}
	for (auto it = tool_symbols.cbegin() ; it != tool_symbols.cend() ; ++it) {
		text += tool_symbol_tag + QLatin1Char(';') + it.key() + QLatin1Char(';')
				+ csvField(it.value()) + QLatin1Char('\n');
	}
	for (auto family = option_defaults.cbegin() ; family != option_defaults.cend() ; ++family) {
		for (auto it = family.value().cbegin() ; it != family.value().cend() ; ++it) {
			text += option_default_tag + QLatin1Char(';') + family.key() + QLatin1Char(';')
					+ it.key() + QLatin1Char(';') + it.value() + QLatin1Char('\n');
		}
	}
	for (auto it = signal_marks.cbegin() ; it != signal_marks.cend() ; ++it) {
		if (!isDefaultSignalMark(it.key(), it.value())) {
			text += signal_mark_tag + QLatin1Char(';') + it.key() + QLatin1Char(';')
					+ it.value() + QLatin1Char('\n');
		}
	}
	for (const LineKindEntry &entry : line_kinds) {
		if (!isoLineKinds().contains(entry)) {
			text += line_kind_tag + QLatin1Char(';') + entry.code + QLatin1Char(';') + csvField(entry.name)
					+ QLatin1Char(';') + QString::number(entry.width) + QLatin1Char(';')
					+ linePatternToString(entry.pattern) + QLatin1Char('\n');
		}
	}
	return text;
}

/**
	@brief PidCodification::toCsv
	See the declaration
*/
QString PidCodification::toCsv() const
{
	return codesToCsv() + presentationToCsv();
}

/**
	@brief PidCodification::codesToCsv
	See the declaration
*/
QString PidCodification::codesToCsv() const
{
	QString text = QStringLiteral("kind;code;name;family\n");
	auto write = [&text](const QString &kind, const QList<Entry> &list) {
		for (const Entry &entry : list) {
			text += kind + QLatin1Char(';') + csvField(entry.code) + QLatin1Char(';')
					+ csvField(entry.name);
				//The last field of a material says how its pipes are named
			if (kind == QLatin1String("material")) {
				if (entry.outside_diameter) {
					text += QStringLiteral(";od");
				}
			} else if (!entry.family.isEmpty()) {
				text += QLatin1Char(';') + csvField(entry.family);
			}
			text += QLatin1Char('\n');
		}
	};
	write(QStringLiteral("fluid"), fluids);
	write(QStringLiteral("material"), materials);
	write(QStringLiteral("unit"), functional_units);
	write(letters_word, instrument_letters);
		//A size of a material by line: its outside diameter, then its DN
	for (const Entry &entry : materials) {
		for (const Size &size : entry.sizes) {
			text += QStringLiteral("size;") + csvField(entry.code) + QLatin1Char(';')
					+ (size.outside ? PipeDiameter::fileNumber(size.outside) : QString())
					+ QLatin1Char(';')
					+ (size.nominal ? QString::number(size.nominal) : QString())
					+ (size.by_default ? QStringLiteral(";default") : QString()) + QLatin1Char('\n');
		}
	}
	for (const QPair<int, int> &pair : equal_nominals) {
		text += QStringLiteral("dn_equal;") + QString::number(pair.first) + QLatin1Char(';')
				+ QString::number(pair.second) + QLatin1Char('\n');
	}
		//A datum of the symbols by line, its last empty fields left out
	for (const DataField &field : data_fields) {
		QStringList fields{data_word, csvField(field.classes.join(QLatin1Char(' '))), field.name,
						   csvField(field.label), csvField(field.unit), dataKindToString(field.kind),
						   csvField(field.values.join(dataValueSeparator())), csvField(field.dexpi)};
		while (fields.last().isEmpty()) {
			fields.removeLast();
		}
		text += fields.join(QLatin1Char(';')) + QLatin1Char('\n');
	}
	return text;
}

/**
	@brief PidCodification::textOfFile
	See the declaration
*/
QString PidCodification::textOfFile(const QByteArray &bytes)
{
	const QString text = QString::fromUtf8(bytes);
	return text.contains(QChar::ReplacementCharacter) ? QString::fromLatin1(bytes) : text;
}

/**
	@brief PidCodification::textOfFiles
	See the declaration
*/
QString PidCodification::textOfFiles(const QStringList &paths, QStringList *unreadable)
{
	QString all;
	for (const QString &path : paths)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			if (unreadable) *unreadable << path;
			continue;
		}
		QString text = textOfFile(file.readAll());
		if (text.startsWith(QChar(0xFEFF))) {
			text.remove(0, 1);
		}
		all += text;
		if (!all.endsWith(QLatin1Char('\n'))) {
			all += QLatin1Char('\n');
		}
	}
	return all;
}

/**
	@brief PidCodification::fromCsv
	See the declaration for the format.
	@param text : the content of the file
	@param skipped : the lines left out, if not nullptr
	@return the codification read
*/
PidCodification PidCodification::fromCsv(const QString &text, int *skipped,
										 QMap<QString, QList<Size>> *loose_sizes)
{
	PidCodification codification;
	int left_out = 0;
	QSet<QString> fluid_codes, material_codes, unit_codes, letter_codes;
		//The sizes read, with the code of their material, which may come
		//after them
	struct SizeLine { QString code; int outside; int nominal; bool by_default; };
	QList<SizeLine> size_lines;
	const QStringList lines = text.split(QLatin1Char('\n'));
	for (int i = 0 ; i < lines.size() ; ++i)
	{
		QString line = lines.at(i);
		line.remove(QLatin1Char('\r'));
		if (i == 0 && line.startsWith(QChar(0xFEFF))) {
			line.remove(0, 1);
		}
		if (line.trimmed().isEmpty()) {
			continue;
		}
		const QChar separator = line.contains(QLatin1Char(';')) ? QLatin1Char(';')
							  : line.contains(QLatin1Char('\t')) ? QLatin1Char('\t')
							  : QLatin1Char(',');
		const QStringList fields = splitFields(line, separator);
		const QString kind = fields.value(0).toLower();
		QMap<QString, QColor> *colors = nullptr;
		if (kind == QLatin1String("fluid_color") || kind == QLatin1String("couleur_fluide")) {
			colors = &codification.fluid_family_colors;
		} else if (kind == QLatin1String("object_color") || kind == QLatin1String("couleur_objet")) {
			colors = &codification.object_colors;
		} else if (kind == QLatin1String("status_color") || kind == QLatin1String("couleur_statut")) {
			colors = &codification.status_colors;
		}
		if (colors)
		{
			const QString key = fields.value(1);
			const QColor color(fields.value(2));
			if (key.isEmpty() || !color.isValid() || colors->contains(key)) {
				++left_out;
			} else {
				colors->insert(key, color);
			}
			continue;
		}
		if (kind == QLatin1String("text_size") || kind == QLatin1String("taille_texte"))
		{
			const QString category = fields.value(1);
			const int size = fields.value(2).toInt();
			if (category.isEmpty() || !isTextSize(size) || codification.text_sizes.contains(category)) {
				++left_out;
			} else {
				codification.text_sizes.insert(category, size);
			}
			continue;
		}
		if (kind == QLatin1String("text_option") || kind == QLatin1String("option_texte"))
		{
			const QString value = fields.value(2).toLower();
			const bool yes = value == QLatin1String("true");
			if (fields.value(1) == drawn_connections_word) {
				int unknown = 0;
				codification.drawn_connections = connectionsFromText(value, &unknown);
				if (unknown) {
					++left_out;
				}
			} else if (fields.value(1) == drawn_families_word) {
				int unknown = 0;
				codification.drawn_families = familiesFromText(value, &unknown);
				if (unknown) {
					++left_out;
				}
			} else if (fields.value(1) == label_ink_word) {
				if (!inkFromText(value, codification.colored_labels, codification.label_color)) {
					++left_out;
				}
			} else if (fields.value(1) == vertical_texts_word) {
				if (!verticalTextsFromString(value, codification.vertical_texts)) {
					++left_out;
				}
			} else if (fields.value(1) == designation_ink_word) {
				if (!inkFromText(value, codification.colored_designations,
								 codification.designation_color)) {
					++left_out;
				}
			} else if (fields.value(1) == insulation_stub_word) {
				if (!insulationStubFromText(value, codification.insulation_stub)) {
					++left_out;
				}
			} else if (fields.value(1) == decimal_word) {
				const Decimal read = decimalFromString(value);
				if (read == Decimal::Language) {
					++left_out;
				} else {
					codification.decimal = read;
				}
			} else if (!yes && value != QLatin1String("false")) {
				++left_out;
			} else if (fields.value(1) == colored_labels_word) {
					//A former file: the labels and the designations
				codification.colored_labels = yes;
				codification.colored_designations = yes;
			} else if (fields.value(1) == nozzle_texts_word) {
				codification.nozzle_texts_on_paper = yes;
			} else if (fields.value(1) == nozzle_sizes_word) {
				codification.show_nozzle_sizes = yes;
			} else if (fields.value(1) == dn_prefix_word) {
				codification.nozzle_dn_prefix = yes;
			} else if (fields.value(1) == reducer_sizes_word) {
				codification.reducer_sizes = yes;
			} else if (fields.value(1) == follow_pipes_word) {
				codification.symbols_follow_pipes = yes;
			} else {
				++left_out;
			}
			continue;
		}
		if (kind == QLatin1String("size") || kind == QLatin1String("taille"))
		{
				//An outside diameter to the tenth, a whole DN, one at least
			const QString od = fields.value(2), dn = fields.value(3);
			const int outside = PipeDiameter::tenthsOf(od);
			const int nominal = nominalOfText(dn);
				//Then nothing, or the word which says it the default one
			const QString last = fields.value(4).toLower();
			const bool by_default = last == QLatin1String("default") || last == QStringLiteral("défaut")
									|| last == QLatin1String("defaut");
			if (fields.value(1).isEmpty() || (od.isEmpty() && dn.isEmpty())
				|| (!od.isEmpty() && !outside) || (!dn.isEmpty() && !nominal)
				|| (!last.isEmpty() && !by_default) || (by_default && (!outside || !nominal))) {
				++left_out;
			} else {
				size_lines << SizeLine{fields.value(1), outside, nominal, by_default};
			}
			continue;
		}
		if (kind == QLatin1String("dn_equal") || kind == QLatin1String("dn_equivalent")
			|| kind == QStringLiteral("dn_équivalent"))
		{
			if (!codification.addEqualNominals(nominalOfText(fields.value(1)),
											   nominalOfText(fields.value(2)))) {
				++left_out;
			}
			continue;
		}
		if (kind == QLatin1String("tag_model") || kind == QStringLiteral("modèle_repère")
			|| kind == QLatin1String("modele_repere"))
		{
				//The model may hold the separator: what follows the family
			const QString family = fields.value(1);
			const int start = line.indexOf(separator, line.indexOf(separator) + 1);
			QString model = start < 0 ? QString() : line.mid(start + 1).trimmed();
			if (model.size() >= 2 && model.startsWith(QLatin1Char('"')) && model.endsWith(QLatin1Char('"'))) {
				model = model.mid(1, model.size() - 2).replace(QStringLiteral("\"\""), QStringLiteral("\""));
			}
			if (family.isEmpty() || model.isEmpty() || codification.tag_models.contains(family)) {
				++left_out;
			} else {
				codification.tag_models.insert(family, model);
			}
			continue;
		}
		if (kind == tag_option_kind || kind == QStringLiteral("option_repère")
			|| kind == QLatin1String("option_repere"))
		{
			const QString value = fields.value(2).trimmed().toLower();
			if (fields.value(1).trimmed() != numbering_word) {
				++left_out;
			} else if (value == parallel_word || value == QStringLiteral("parallèle")
					   || value == QLatin1String("parallele")) {
				codification.parallel_numbering = true;
			} else if (value == serial_word || value == QStringLiteral("série")
					   || value == QLatin1String("serie")) {
				codification.parallel_numbering = false;
			} else {
				++left_out;
			}
			continue;
		}
		if (kind == tool_symbol_tag || kind == QLatin1String("symbole_outil"))
		{
			const QString role = fields.value(1).trimmed().toLower();
			const QString location = fields.value(2).trimmed();
			if (!toolSymbolRoles().contains(role) || location.isEmpty()
				|| codification.tool_symbols.contains(role)) {
				++left_out;
			} else {
				codification.tool_symbols.insert(role, location);
			}
			continue;
		}
		if (kind == option_default_tag || kind == QStringLiteral("option_défaut")
			|| kind == QLatin1String("option_defaut"))
		{
				//A name that cannot be one, no value, an option said twice
			const QString family = fields.value(1).trimmed();
			const QString option = fields.value(2).trimmed();
			const QString value = fields.value(3).trimmed();
			if (value.isEmpty() || codification.option_defaults.value(family).contains(option)
				|| !codification.setOptionDefault(family, option, value)) {
				++left_out;
			}
			continue;
		}
		if (kind == line_kind_tag || kind == QLatin1String("genre_ligne"))
		{
				//A code that cannot be one, a width or a pattern not read
			LineKindEntry entry;
			if (!lineKindFromFields(fields.value(1), fields.value(2), fields.value(3), fields.value(4), entry)
				|| !codification.setLineKind(entry)) {
				++left_out;
			}
			continue;
		}
		if (kind == signal_mark_tag || kind == QLatin1String("marque_signal"))
		{
				//Kept even when it is the default one: the file says it
			const QString type = fields.value(1).trimmed().toLower();
			const QString mark = fields.value(2).trimmed().toLower();
			bool known_mark = false;
			SignalMark::fromString(mark, &known_mark);
			if (!isSignalType(type) || !known_mark || codification.signal_marks.contains(type)) {
				++left_out;
			} else {
				codification.signal_marks.insert(type, mark);
			}
			continue;
		}
		if (kind == data_word || kind == QStringLiteral("donnée") || kind == QLatin1String("donnee"))
		{
				//A kind which is not one, a name which cannot be one or said
				//twice, no class, a list without values
			DataField field;
			if (!dataFieldFromTexts(fields.value(1), fields.value(2), fields.value(3), fields.value(4),
									fields.value(5), fields.value(6), fields.value(7), field)
				|| !codification.addDataField(field)) {
				++left_out;
			}
			continue;
		}
		const bool is_fluid = kind == QLatin1String("fluid") || kind == QLatin1String("fluide");
		const bool is_material = kind == QLatin1String("material") || kind == QStringLiteral("matériau")
							   || kind == QLatin1String("materiau");
		const bool is_unit = kind == QLatin1String("unit") || kind == QLatin1String("functional_unit")
							 || kind == QStringLiteral("unité") || kind == QLatin1String("unite")
							 || kind == QStringLiteral("unité_fonctionnelle")
							 || kind == QLatin1String("unite_fonctionnelle");
		const bool is_letters = kind == letters_word || kind == QLatin1String("lettres")
								|| kind == QLatin1String("instrument_letters");
		Entry entry;
		entry.code = fields.value(1);
		entry.name = fields.value(2);
		entry.family = fields.value(3);
		if (is_material)
		{
				//The last field of a material says how its pipes are named:
				//by their outside diameter, or else by their DN
			const QString named = entry.family.toLower();
			entry.outside_diameter = named == QLatin1String("od") || named == QLatin1String("d")
									 || named.startsWith(PipeDiameter::outsideSign().toLower())
									 || named.startsWith(QLatin1String("outside"))
									 || named.startsWith(QLatin1String("ext"));
			entry.family.clear();
		}
		const bool is_list = is_fluid || is_material || is_unit || is_letters;
		if (!is_list || entry.code.isEmpty())
		{
				//A line naming the columns is not a line left out: the
				//first one, or that of a file read after another
			const bool header = kind == QLatin1String("kind");
			if (!header && (i > 0 || is_list)) {
				++left_out;
			}
			continue;
		}
			//Letters have a meaning, no family
		if (is_letters) {
			entry.family.clear();
		}
		QSet<QString> &codes = is_fluid ? fluid_codes : is_material ? material_codes
										: is_unit ? unit_codes : letter_codes;
		if (codes.contains(entry.code)) {
			++left_out;
			continue;
		}
		codes.insert(entry.code);
		(is_fluid ? codification.fluids
				  : is_material ? codification.materials
								: is_unit ? codification.functional_units
										  : codification.instrument_letters) << entry;
	}
		//Each size to its material; those of a material the file does not
		//list are kept apart for who asks them
	QMap<QString, Entry> loose;
	for (const SizeLine &line : std::as_const(size_lines))
	{
		auto material = std::find_if(codification.materials.begin(), codification.materials.end(),
									 [&line](const Entry &entry) { return entry.code == line.code; });
		const bool listed = material != codification.materials.end();
		if (!listed && !loose_sizes) {
			++left_out;
			continue;
		}
		Entry &entry = listed ? *material : loose[line.code];
		if (!entry.addSize(line.outside, line.nominal, line.by_default)) {
			++left_out;
		}
	}
	if (loose_sizes) {
		loose_sizes->clear();
		for (auto it = loose.cbegin() ; it != loose.cend() ; ++it) {
			if (!it.value().sizes.isEmpty()) {
				loose_sizes->insert(it.key(), it.value().sizes);
			}
		}
	}
	if (skipped) {
		*skipped = left_out;
	}
	return codification;
}

bool PidCodification::operator==(const PidCodification &other) const
{
	return fluids == other.fluids && materials == other.materials
			&& functional_units == other.functional_units
			&& instrument_letters == other.instrument_letters
			&& equal_nominals == other.equal_nominals
			&& tag_models == other.tag_models
			&& parallel_numbering == other.parallel_numbering
			&& fluid_family_colors == other.fluid_family_colors
			&& object_colors == other.object_colors
			&& status_colors == other.status_colors
			&& symbols_follow_pipes == other.symbols_follow_pipes
			&& text_sizes == other.text_sizes
			&& colored_labels == other.colored_labels
			&& label_color == other.label_color
			&& colored_designations == other.colored_designations
			&& designation_color == other.designation_color
			&& vertical_texts == other.vertical_texts
			&& nozzle_texts_on_paper == other.nozzle_texts_on_paper
			&& show_nozzle_sizes == other.show_nozzle_sizes
			&& nozzle_dn_prefix == other.nozzle_dn_prefix
			&& reducer_sizes == other.reducer_sizes
			&& insulation_stub == other.insulation_stub
			&& decimal == other.decimal
			&& drawn_connections == other.drawn_connections
			&& drawn_families == other.drawn_families
			&& tool_symbols == other.tool_symbols
			&& option_defaults == other.option_defaults
			&& line_kinds == other.line_kinds
			&& signal_marks == other.signal_marks
			&& data_fields == other.data_fields;
}

/**
	@brief PidCodification::isoLineKinds
	See the declaration
*/
const QList<PidCodification::LineKindEntry> &PidCodification::isoLineKinds()
{
	static const QList<LineKindEntry> kinds{
		{PipeProperties::Main, QString(), 1.0, LinePattern::Solid},
		{PipeProperties::Secondary, QString(), 0.5, LinePattern::Solid},
		{PipeProperties::Auxiliary, QString(), 0.5, LinePattern::Dashes}};
	return kinds;
}

bool PidCodification::isIsoLineKind(const QString &code)
{
	return code == PipeProperties::Main || code == PipeProperties::Secondary
			|| code == PipeProperties::Auxiliary;
}

/**
	@brief PidCodification::lineKind
	See the declaration
*/
PidCodification::LineKindEntry PidCodification::lineKind(const QString &code) const
{
	const QString word = PipeProperties::lineKindToString(code);
	for (const LineKindEntry &entry : line_kinds) {
		if (entry.code == word) {
			return entry;
		}
	}
	LineKindEntry main = isoLineKinds().constFirst();
	for (const LineKindEntry &entry : line_kinds) {
		if (entry.code == PipeProperties::Main) {
			main = entry;
		}
	}
	main.code = word;
	return main;
}

/**
	@brief PidCodification::lineKindName
	See the declaration
*/
QString PidCodification::lineKindName(const QString &code) const
{
	const QString word = PipeProperties::lineKindToString(code);
	if (word == PipeProperties::Main) {
		return QCoreApplication::translate("PidCodification", "Main flow line");
	}
	if (word == PipeProperties::Secondary) {
		return QCoreApplication::translate("PidCodification", "Secondary flow line");
	}
	if (word == PipeProperties::Auxiliary) {
		return QCoreApplication::translate("PidCodification", "Auxiliary system line");
	}
	for (const LineKindEntry &entry : line_kinds) {
		if (entry.code == word && !entry.name.trimmed().isEmpty()) {
			return entry.name.trimmed();
		}
	}
	return word;
}

/**
	@brief PidCodification::setLineKind
	See the declaration
*/
bool PidCodification::setLineKind(const LineKindEntry &entry)
{
	const QString code = entry.code.trimmed();
	if (!ElementOptions::isValidName(code) || !isLineWidth(entry.width)) {
		return false;
	}
	LineKindEntry kept = entry;
	kept.code = code;
	kept.name = isIsoLineKind(code) ? QString() : entry.name.trimmed();
	for (LineKindEntry &known : line_kinds) {
		if (known.code == code) {
			known = kept;
			return true;
		}
	}
	line_kinds << kept;
	return true;
}

/**
	@brief PidCodification::removeLineKind
	See the declaration
*/
bool PidCodification::removeLineKind(const QString &code)
{
	if (isIsoLineKind(code)) {
		return false;
	}
	for (int i = 0 ; i < line_kinds.size() ; ++i) {
		if (line_kinds.at(i).code == code) {
			line_kinds.removeAt(i);
			return true;
		}
	}
	return false;
}

/**
	@brief PidCodification::dashPattern
	See the declaration. The dashes are those QElectroTech drew before for
	a dashed or a dash-dotted line, whose gaps can be told at a normal zoom.
*/
QList<qreal> PidCodification::dashPattern(LinePattern pattern)
{
	switch (pattern) {
		case LinePattern::Dashes:      return {5, 4};
		case LinePattern::ShortDashes: return {2, 2};
		case LinePattern::DashDot:     return {6, 4, 1, 4};
		default:                       return {};
	}
}

QList<PidCodification::LinePattern> PidCodification::linePatterns()
{
	return {LinePattern::Solid, LinePattern::Dashes, LinePattern::ShortDashes, LinePattern::DashDot};
}

QString PidCodification::linePatternToString(LinePattern pattern)
{
	switch (pattern) {
		case LinePattern::Dashes:      return QStringLiteral("dashes");
		case LinePattern::ShortDashes: return QStringLiteral("short_dashes");
		case LinePattern::DashDot:     return QStringLiteral("dash_dot");
		default:                       return QStringLiteral("solid");
	}
}

bool PidCodification::linePatternFromString(const QString &word, LinePattern &pattern)
{
	for (LinePattern known : linePatterns()) {
		if (word.trimmed().toLower() == linePatternToString(known)) {
			pattern = known;
			return true;
		}
	}
	return false;
}

QString PidCodification::linePatternName(LinePattern pattern)
{
	switch (pattern) {
		case LinePattern::Dashes:      return QCoreApplication::translate("PidCodification", "Dashes");
		case LinePattern::ShortDashes: return QCoreApplication::translate("PidCodification", "Short dashes");
		case LinePattern::DashDot:     return QCoreApplication::translate("PidCodification", "Dash and dot");
		default:                       return QCoreApplication::translate("PidCodification", "Solid");
	}
}

/**
	@brief PidCodification::setOptionDefault
	See the declaration
*/
bool PidCodification::setOptionDefault(const QString &family, const QString &option,
									   const QString &value)
{
	if (!ElementOptions::isValidName(family) || !ElementOptions::isValidName(option)
		|| (!value.isEmpty() && !ElementOptions::isValidName(value))) {
		return false;
	}
	if (value.isEmpty())
	{
		auto it = option_defaults.find(family);
		if (it != option_defaults.end()) {
			it.value().remove(option);
				//No family left without an option
			if (it.value().isEmpty()) {
				option_defaults.erase(it);
			}
		}
		return true;
	}
	option_defaults[family].insert(option, value);
	return true;
}

/**
	@brief PidCodification::setOptionDefaults
	See the declaration
*/
bool PidCodification::setOptionDefaults(const QString &family, const ElementOptions::Choices &choices)
{
	const QMap<QString, ElementOptions::Choices> before = option_defaults;
	for (auto it = choices.cbegin() ; it != choices.cend() ; ++it) {
		setOptionDefault(family, it.key(), it.value());
	}
	return option_defaults != before;
}

/**
	@brief PidCodification::newChoices
	See the declaration
*/
ElementOptions::Choices PidCodification::newChoices(const QString &family,
													const ElementOptions &options) const
{
	ElementOptions::Choices choices;
	if (family.isEmpty()) {
		return choices;
	}
	const ElementOptions::Choices defaults = option_defaults.value(family);
	for (const ElementOptions::Option &option : options.options())
	{
		const QString value = defaults.value(option.name);
			//Its own default needs no writing
		if (option.hasValue(value) && value != option.default_value) {
			choices.insert(option.name, value);
		}
	}
	return choices;
}
