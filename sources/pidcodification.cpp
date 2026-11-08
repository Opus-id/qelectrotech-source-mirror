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

#include "pipeproperties.h"
#include "qetapp.h"

#include <QFile>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace
{
	const QString block_tag = QStringLiteral("pid_codification");
	const QString colored_labels_word = QStringLiteral("colored_labels");
	const QString nozzle_texts_word = QStringLiteral("nozzle_texts_on_paper");
	const QString nozzle_sizes_word = QStringLiteral("show_nozzle_sizes");
	const QString decimal_word = QStringLiteral("decimal_separator");

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
			if (!entry.series.isEmpty()) {
				e.setAttribute(QStringLiteral("series"), PidCodification::seriesToText(entry.series));
			}
			block.appendChild(e);
		}
	}

		/// The numbers of "25/32/88,9" (or separated by spaces), in tenths of
		/// a millimetre, in order, each once; whole ones only when @p whole;
		/// @p ok false when something else is written
	QList<int> readNumbers(const QString &text, bool whole, bool *ok)
	{
		QList<int> values;
		bool all = true;
		const QStringList parts = QString(text).replace(QLatin1Char('/'), QLatin1Char(' '))
								  .split(QLatin1Char(' '), Qt::SkipEmptyParts);
		for (const QString &part : parts) {
			const int value = PipeDiameter::tenthsOf(part);
			if (!value || (whole && value % 10)) {
				all = false;
			} else if (!values.contains(value)) {
				values << value;
			}
		}
		std::sort(values.begin(), values.end());
		if (ok) *ok = all;
		return values;
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
				//A DN is whole: a series which says otherwise is not read
			bool ok = false;
			entry.series = readNumbers(e.attribute(QStringLiteral("series")), !entry.outside_diameter, &ok);
			if (!ok) {
				entry.series.clear();
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
			&& outside_diameter == other.outside_diameter && series == other.series;
}

QList<PipeDiameter> PidCodification::seriesOf(const Entry &entry)
{
	QList<PipeDiameter> sizes;
	const PipeDiameter::Kind kind = entry.outside_diameter ? PipeDiameter::Outside
														   : PipeDiameter::Nominal;
	for (int tenths : entry.series) {
		const PipeDiameter size = PipeDiameter::fromTenths(kind, tenths);
		if (size.isSet()) {
			sizes << size;
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
		list = seriesOf(*entry);
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
	QList<int> nominal, outside;
	for (const Entry &entry : materials) {
		QList<int> &list = entry.outside_diameter ? outside : nominal;
		for (int tenths : entry.series) {
			if (!list.contains(tenths)) {
				list << tenths;
			}
		}
	}
	std::sort(nominal.begin(), nominal.end());
	std::sort(outside.begin(), outside.end());
	QList<PipeDiameter> sizes;
	for (int tenths : std::as_const(nominal)) {
		const PipeDiameter size = PipeDiameter::fromTenths(PipeDiameter::Nominal, tenths);
		if (size.isSet()) sizes << size;
	}
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
	@brief PidCodification::conversions
	See the declaration
*/
QList<PipeDiameter> PidCodification::conversions(const PipeDiameter &diameter,
												 const QString &material_code) const
{
	QList<PipeDiameter> series;
	if (!diameter.isSet() || pipeSizes(material_code, &series) != SizeRule::Series
		|| series.contains(diameter)) {
		return {};
	}
	QList<PipeDiameter> found;
	for (const PipeDiameter &size : std::as_const(series))
	{
		if (size.kind() != diameter.kind()) {
			if (compare(diameter, size) == DiameterMatch::Same) {
				found << size;
			}
		} else if (diameter.isOutside()) {
				//Two outside diameters joined to the same DN
			const QList<int> from = outside_diameters.value(diameter.tenths());
			const QList<int> to = outside_diameters.value(size.tenths());
			if (std::any_of(from.cbegin(), from.cend(), [&to](int dn) { return to.contains(dn); })) {
				found << size;
			}
		}
	}
	return found;
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

QStringList PidCodification::materialsWithoutSeries() const
{
	QStringList codes;
	for (const Entry &entry : materials) {
		if (entry.series.isEmpty()) {
			codes << entry.code;
		}
	}
	return codes;
}

QList<int> PidCodification::seriesFromText(const QString &text, bool *ok)
{
	return readNumbers(text, false, ok);
}

QString PidCodification::seriesToText(const QList<int> &tenths, const QLocale &locale)
{
	QStringList parts;
	for (int value : tenths) {
		parts << PipeDiameter::number(value, locale);
	}
	return parts.join(QLatin1Char('/'));
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
		return a == b ? DiameterMatch::Same : DiameterMatch::Different;
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
	const PipeDiameter &outside = a.isOutside() ? a : b;
	const PipeDiameter &nominal = a.isOutside() ? b : a;
	const auto it = outside_diameters.constFind(outside.tenths());
	if (it == outside_diameters.constEnd() || it.value().isEmpty()) {
		return DiameterMatch::Unknown;
	}
	return it.value().contains(nominal.tenths() / 10) ? DiameterMatch::Same : DiameterMatch::Different;
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

QList<int> PidCodification::diametersFromText(const QString &text, bool *ok)
{
	QList<int> values = readNumbers(text, true, ok);
	for (int &value : values) {
		value /= 10;
	}
	return values;
}

QString PidCodification::diametersToText(const QList<int> &values)
{
	QStringList parts;
	for (int value : values) {
		parts << QString::number(value);
	}
	return parts.join(QLatin1Char('/'));
}

QList<PidCodification::Entry> &PidCodification::entries(List list)
{
	switch (list) {
		case List::Fluids:    return fluids;
		case List::Materials: return materials;
		case List::Units:     return functional_units;
	}
	return functional_units;
}

const QList<PidCodification::Entry> &PidCodification::entries(List list) const
{
	return const_cast<PidCodification *>(this)->entries(list);
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
	for (List list : {List::Fluids, List::Materials, List::Units}) {
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
			QStringLiteral("instrument"), nozzleTexts()};
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
	}
		//A file of codes may have no diameters, and the reverse
	if (!hasDiameters()) {
		result.outside_diameters = other.outside_diameters;
	}
	if (!hasColors()) {
		result.fluid_family_colors = other.fluid_family_colors;
		result.object_colors = other.object_colors;
		result.status_colors = other.status_colors;
	}
	if (!hasTextSettings()) {
		result.text_sizes = other.text_sizes;
		result.colored_labels = other.colored_labels;
		result.nozzle_texts_on_paper = other.nozzle_texts_on_paper;
		result.show_nozzle_sizes = other.show_nozzle_sizes;
		result.decimal = other.decimal;
		result.tag_models = other.tag_models;
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
	for (auto it = outside_diameters.cbegin() ; it != outside_diameters.cend() ; ++it)
	{
		QDomElement e = document.createElement(QStringLiteral("outside_diameter"));
		e.setAttribute(QStringLiteral("value"), PipeDiameter::fileNumber(it.key()));
		if (!it.value().isEmpty()) {
			e.setAttribute(QStringLiteral("dn"), diametersToText(it.value()));
		}
		block.appendChild(e);
	}
	for (auto it = tag_models.cbegin() ; it != tag_models.cend() ; ++it)
	{
		QDomElement e = document.createElement(QStringLiteral("tag_model"));
		e.setAttribute(QStringLiteral("family"), it.key());
		e.setAttribute(QStringLiteral("value"), it.value());
		block.appendChild(e);
	}
	writeColors(document, block, QStringLiteral("fluid_family"), fluid_family_colors);
	writeColors(document, block, QStringLiteral("object"), object_colors);
	writeColors(document, block, QStringLiteral("status"), status_colors);
	for (auto it = text_sizes.cbegin() ; it != text_sizes.cend() ; ++it)
	{
		QDomElement e = document.createElement(QStringLiteral("text_size"));
		e.setAttribute(QStringLiteral("category"), it.key());
		e.setAttribute(QStringLiteral("points"), it.value());
		block.appendChild(e);
	}
	if (colored_labels) {
		block.setAttribute(colored_labels_word, QStringLiteral("true"));
	}
	if (!nozzle_texts_on_paper) {
		block.setAttribute(nozzle_texts_word, QStringLiteral("false"));
	}
	if (!show_nozzle_sizes) {
		block.setAttribute(nozzle_sizes_word, QStringLiteral("false"));
	}
	if (decimal != Decimal::Language) {
		block.setAttribute(decimal_word, decimalToString(decimal));
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
	outside_diameters.clear();
	const QString od_tag = QStringLiteral("outside_diameter");
	for (QDomElement e = block.firstChildElement(od_tag) ; !e.isNull() ;
		 e = e.nextSiblingElement(od_tag))
	{
		const int value = PipeDiameter::tenthsOf(e.attribute(QStringLiteral("value")));
		if (value && !outside_diameters.contains(value)) {
			outside_diameters.insert(value, diametersFromText(e.attribute(QStringLiteral("dn"))));
		}
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
	colored_labels = block.attribute(colored_labels_word) == QLatin1String("true");
	nozzle_texts_on_paper = block.attribute(nozzle_texts_word) != QLatin1String("false");
	show_nozzle_sizes = block.attribute(nozzle_sizes_word) != QLatin1String("false");
	decimal = decimalFromString(block.attribute(decimal_word));
}

/**
	@brief PidCodification::paletteToCsv
	See the declaration
*/
QString PidCodification::paletteToCsv() const
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
	for (auto it = text_sizes.cbegin() ; it != text_sizes.cend() ; ++it) {
		text += QStringLiteral("text_size;") + it.key() + QLatin1Char(';')
				+ QString::number(it.value()) + QLatin1Char('\n');
	}
	if (colored_labels) {
		text += QStringLiteral("text_option;") + colored_labels_word + QStringLiteral(";true\n");
	}
	if (!nozzle_texts_on_paper) {
		text += QStringLiteral("text_option;") + nozzle_texts_word + QStringLiteral(";false\n");
	}
	if (!show_nozzle_sizes) {
		text += QStringLiteral("text_option;") + nozzle_sizes_word + QStringLiteral(";false\n");
	}
	if (decimal != Decimal::Language) {
		text += QStringLiteral("text_option;") + decimal_word + QLatin1Char(';')
				+ decimalToString(decimal) + QLatin1Char('\n');
	}
	for (auto it = tag_models.cbegin() ; it != tag_models.cend() ; ++it) {
		text += QStringLiteral("tag_model;") + it.key() + QLatin1Char(';') + it.value() + QLatin1Char('\n');
	}
	return text;
}

/**
	@brief PidCodification::toCsv
	See the declaration
*/
QString PidCodification::toCsv() const
{
	return codesToCsv() + paletteToCsv();
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
	for (const Entry &entry : materials) {
		if (!entry.series.isEmpty()) {
			text += QStringLiteral("series;") + csvField(entry.code) + QLatin1Char(';')
					+ seriesToText(entry.series) + QLatin1Char('\n');
		}
	}
	for (auto it = outside_diameters.cbegin() ; it != outside_diameters.cend() ; ++it) {
		text += QStringLiteral("od;") + PipeDiameter::fileNumber(it.key());
		if (!it.value().isEmpty()) {
			text += QLatin1Char(';') + diametersToText(it.value());
		}
		text += QLatin1Char('\n');
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
PidCodification PidCodification::fromCsv(const QString &text, int *skipped)
{
	PidCodification codification;
	int left_out = 0;
	QSet<QString> fluid_codes, material_codes, unit_codes;
		//The series read, by code of material
	QMap<QString, QString> series;
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
			if (fields.value(1) == decimal_word) {
				const Decimal read = decimalFromString(value);
				if (read == Decimal::Language) {
					++left_out;
				} else {
					codification.decimal = read;
				}
			} else if (!yes && value != QLatin1String("false")) {
				++left_out;
			} else if (fields.value(1) == colored_labels_word) {
				codification.colored_labels = yes;
			} else if (fields.value(1) == nozzle_texts_word) {
				codification.nozzle_texts_on_paper = yes;
			} else if (fields.value(1) == nozzle_sizes_word) {
				codification.show_nozzle_sizes = yes;
			} else {
				++left_out;
			}
			continue;
		}
		if (kind == QLatin1String("series") || kind == QStringLiteral("série")
			|| kind == QLatin1String("serie"))
		{
				//Read with its material, which may come after it
			if (fields.value(1).isEmpty() || series.contains(fields.value(1))) {
				++left_out;
			} else {
				series.insert(fields.value(1), fields.value(2));
			}
			continue;
		}
		if (kind == QLatin1String("od") || kind == QLatin1String("outside_diameter")
			|| kind == QStringLiteral("diamètre_extérieur") || kind == QLatin1String("diametre_exterieur"))
		{
			const int value = PipeDiameter::tenthsOf(fields.value(1));
			bool ok = true;
			const QList<int> joined = diametersFromText(fields.value(2), &ok);
			if (!value || !ok || codification.outside_diameters.contains(value)) {
				++left_out;
			} else {
				codification.outside_diameters.insert(value, joined);
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
		const bool is_fluid = kind == QLatin1String("fluid") || kind == QLatin1String("fluide");
		const bool is_material = kind == QLatin1String("material") || kind == QStringLiteral("matériau")
							   || kind == QLatin1String("materiau");
		const bool is_unit = kind == QLatin1String("unit") || kind == QLatin1String("functional_unit")
							 || kind == QStringLiteral("unité") || kind == QLatin1String("unite")
							 || kind == QStringLiteral("unité_fonctionnelle")
							 || kind == QLatin1String("unite_fonctionnelle");
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
		if ((!is_fluid && !is_material && !is_unit) || entry.code.isEmpty())
		{
				//A line naming the columns is not a line left out: the
				//first one, or that of a file read after another
			const bool header = kind == QLatin1String("kind");
			if (!header && (i > 0 || is_fluid || is_material || is_unit)) {
				++left_out;
			}
			continue;
		}
		QSet<QString> &codes = is_fluid ? fluid_codes : is_material ? material_codes : unit_codes;
		if (codes.contains(entry.code)) {
			++left_out;
			continue;
		}
		codes.insert(entry.code);
		(is_fluid ? codification.fluids
				  : is_material ? codification.materials : codification.functional_units) << entry;
	}
		//Each series to its material: a DN is whole
	for (auto it = series.cbegin() ; it != series.cend() ; ++it)
	{
		auto material = std::find_if(codification.materials.begin(), codification.materials.end(),
									 [&it](const Entry &entry) { return entry.code == it.key(); });
		bool ok = false;
		const QList<int> sizes = material == codification.materials.end()
				? QList<int>() : readNumbers(it.value(), !material->outside_diameter, &ok);
		if (!ok || sizes.isEmpty()) {
			++left_out;
		} else {
			material->series = sizes;
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
			&& outside_diameters == other.outside_diameters
			&& tag_models == other.tag_models
			&& fluid_family_colors == other.fluid_family_colors
			&& object_colors == other.object_colors
			&& status_colors == other.status_colors
			&& text_sizes == other.text_sizes
			&& colored_labels == other.colored_labels
			&& nozzle_texts_on_paper == other.nozzle_texts_on_paper
			&& show_nozzle_sizes == other.show_nozzle_sizes
			&& decimal == other.decimal;
}
