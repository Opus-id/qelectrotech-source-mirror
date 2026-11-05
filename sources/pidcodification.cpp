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
	QSet<QString> fluid_codes, material_codes;
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
		const bool is_fluid = kind == QLatin1String("fluid") || kind == QLatin1String("fluide");
		const bool is_material = kind == QLatin1String("material") || kind == QStringLiteral("matériau")
							   || kind == QLatin1String("materiau");
		Entry entry;
		entry.code = fields.value(1);
		entry.name = fields.value(2);
		entry.family = fields.value(3);
		if ((!is_fluid && !is_material) || entry.code.isEmpty())
		{
				//A first line naming the columns is not a line left out
			if (i > 0 || is_fluid || is_material) {
				++left_out;
			}
			continue;
		}
		QSet<QString> &codes = is_fluid ? fluid_codes : material_codes;
		if (codes.contains(entry.code)) {
			++left_out;
			continue;
		}
		codes.insert(entry.code);
		(is_fluid ? codification.fluids : codification.materials) << entry;
	}
	if (skipped) {
		*skipped = left_out;
	}
	return codification;
}

bool PidCodification::operator==(const PidCodification &other) const
{
	return fluids == other.fluids && materials == other.materials;
}
