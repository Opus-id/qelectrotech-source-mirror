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

#include <QSet>
#include <QStringList>

namespace
{
	const QString block_tag = QStringLiteral("pid_codification");
	const QString colored_labels_word = QStringLiteral("colored_labels");
	const QString nozzle_texts_word = QStringLiteral("nozzle_texts_on_paper");
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
			block.appendChild(e);
		}
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
	return code == other.code && name == other.name && family == other.family;
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
	if (!hasCodes()) {
		result.fluids = other.fluids;
		result.materials = other.materials;
		result.functional_units = other.functional_units;
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
	for (auto it = tag_models.cbegin() ; it != tag_models.cend() ; ++it) {
		text += QStringLiteral("tag_model;") + it.key() + QLatin1Char(';') + it.value() + QLatin1Char('\n');
	}
	return text;
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
		QStringList fields = line.split(separator);
		for (QString &field : fields) {
			field = field.trimmed();
			if (field.size() >= 2 && field.startsWith(QLatin1Char('"')) && field.endsWith(QLatin1Char('"'))) {
				field = field.mid(1, field.size() - 2).replace(QStringLiteral("\"\""), QStringLiteral("\""));
			}
		}
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
			if (!yes && value != QLatin1String("false")) {
				++left_out;
			} else if (fields.value(1) == colored_labels_word) {
				codification.colored_labels = yes;
			} else if (fields.value(1) == nozzle_texts_word) {
				codification.nozzle_texts_on_paper = yes;
			} else {
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
		if ((!is_fluid && !is_material && !is_unit) || entry.code.isEmpty())
		{
				//A first line naming the columns is not a line left out
			if (i > 0 || is_fluid || is_material || is_unit) {
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
	if (skipped) {
		*skipped = left_out;
	}
	return codification;
}

bool PidCodification::operator==(const PidCodification &other) const
{
	return fluids == other.fluids && materials == other.materials
			&& functional_units == other.functional_units
			&& tag_models == other.tag_models
			&& fluid_family_colors == other.fluid_family_colors
			&& object_colors == other.object_colors
			&& status_colors == other.status_colors
			&& text_sizes == other.text_sizes
			&& colored_labels == other.colored_labels
			&& nozzle_texts_on_paper == other.nozzle_texts_on_paper;
}
