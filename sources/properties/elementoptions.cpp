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
#include "elementoptions.h"

#include <QRegularExpression>
#include <QSet>

namespace
{
	const QString options_tag = QStringLiteral("options");
	const QString option_tag = QStringLiteral("option");
	const QString value_tag = QStringLiteral("value");
}

/**
	@brief ElementOptions::Option::hasValue
	@param value
	@return true if @p value is one of the values of this option
*/
bool ElementOptions::Option::hasValue(const QString &value) const
{
	for (const Value &v : values) {
		if (v.name == value) return true;
	}
	return false;
}

/**
	@brief ElementOptions::option
	@param name
	@return the option named @p name, or nullptr
*/
const ElementOptions::Option *ElementOptions::option(const QString &name) const
{
	for (const Option &o : m_options) {
		if (o.name == name) return &o;
	}
	return nullptr;
}

/**
	@brief ElementOptions::isValidName
	@param name
	@return true if @p name is made of letters, digits, "_" and "-" only
*/
bool ElementOptions::isValidName(const QString &name)
{
	static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]+$"));
	return valid.match(name).hasMatch();
}

/**
	@brief ElementOptions::fromXml
	Read the options declared by the <options> block of @p definition.
	@param definition : the <definition> element of a symbol
*/
void ElementOptions::fromXml(const QDomElement &definition)
{
	m_options.clear();
	const QDomElement block = definition.firstChildElement(options_tag);
	QSet<QString> option_names;
	for (QDomElement o = block.firstChildElement(option_tag) ; !o.isNull() ;
		 o = o.nextSiblingElement(option_tag))
	{
		Option option;
		option.name = o.attribute(QStringLiteral("name"));
		if (!isValidName(option.name) || option_names.contains(option.name)) {
			continue;
		}
		option.names.fromXml(o);
		QSet<QString> value_names;
		for (QDomElement v = o.firstChildElement(value_tag) ; !v.isNull() ;
			 v = v.nextSiblingElement(value_tag))
		{
			Value value;
			value.name = v.attribute(QStringLiteral("name"));
			if (!isValidName(value.name) || value_names.contains(value.name)) {
				continue;
			}
			value.names.fromXml(v);
			value_names.insert(value.name);
			option.values << value;
		}
		if (option.values.isEmpty()) {
			continue;
		}
		const QString default_value = o.attribute(QStringLiteral("default"));
		option.default_value = option.hasValue(default_value)
				? default_value : option.values.first().name;
		option_names.insert(option.name);
		m_options << option;
	}
}

/**
	@brief ElementOptions::toXml
	@param document
	@return the <options> block declaring these options, in their order
*/
QDomElement ElementOptions::toXml(QDomDocument &document) const
{
	QDomElement block = document.createElement(options_tag);
	for (const Option &option : m_options)
	{
		QDomElement o = document.createElement(option_tag);
		o.setAttribute(QStringLiteral("name"), option.name);
		o.setAttribute(QStringLiteral("default"), option.default_value);
		if (!option.names.isEmpty()) {
			o.appendChild(option.names.toXml(document));
		}
		for (const Value &value : option.values)
		{
			QDomElement v = document.createElement(value_tag);
			v.setAttribute(QStringLiteral("name"), value.name);
			if (!value.names.isEmpty()) {
				v.appendChild(value.names.toXml(document));
			}
			o.appendChild(v);
		}
		block.appendChild(o);
	}
	return block;
}

/**
	@brief ElementOptions::defaults
	@return the default value of every option
*/
ElementOptions::Choices ElementOptions::defaults() const
{
	return resolved(Choices());
}

/**
	@brief ElementOptions::resolved
	@param chosen : the values chosen, by option name
	@return the value of every option: the one chosen when it is one of its
	values, else its default
*/
ElementOptions::Choices ElementOptions::resolved(const Choices &chosen) const
{
	Choices choices;
	for (const Option &option : m_options)
	{
		const QString value = chosen.value(option.name);
		choices.insert(option.name, option.hasValue(value) ? value : option.default_value);
	}
	return choices;
}

/**
	@brief ElementOptions::isShown
	@param show_if : the show-if attribute of a part, "option=value,value"
	conditions separated by ";"
	@param resolved : the value of every option of the symbol, see resolved()
	@return true if the part is drawn: every condition that can be read and
	names an option of @p resolved holds
*/
bool ElementOptions::isShown(const QString &show_if, const Choices &resolved)
{
	const QStringList conditions = show_if.split(QLatin1Char(';'), Qt::SkipEmptyParts);
	for (const QString &condition : conditions)
	{
		const int equal = condition.indexOf(QLatin1Char('='));
		if (equal <= 0) {
			continue;
		}
		const QString name = condition.left(equal).trimmed();
		if (!resolved.contains(name)) {
			continue;
		}
		QStringList values;
		for (const QString &value : condition.mid(equal + 1).split(QLatin1Char(','))) {
			values << value.trimmed();
		}
		if (!values.contains(resolved.value(name))) {
			return false;
		}
	}
	return true;
}

/**
	@brief ElementOptions::key
	@param choices
	@return "option=value" for each choice, separated by ";", in the order
	of the option names
*/
QString ElementOptions::key(const Choices &choices)
{
	QStringList parts;
	for (auto it = choices.cbegin() ; it != choices.cend() ; ++it) {
		parts << it.key() + QLatin1Char('=') + it.value();
	}
	return parts.join(QLatin1Char(';'));
}

/**
	@brief ElementOptions::choicesFromXml
	@param element : a placed <element>
	@return the values its <options> block gives, by option name. A choice
	whose names cannot be read is left out.
*/
ElementOptions::Choices ElementOptions::choicesFromXml(const QDomElement &element)
{
	Choices choices;
	const QDomElement block = element.firstChildElement(options_tag);
	for (QDomElement o = block.firstChildElement(option_tag) ; !o.isNull() ;
		 o = o.nextSiblingElement(option_tag))
	{
		const QString name = o.attribute(QStringLiteral("name"));
		const QString value = o.attribute(QStringLiteral("value"));
		if (isValidName(name) && isValidName(value)) {
			choices.insert(name, value);
		}
	}
	return choices;
}

/**
	@brief ElementOptions::choicesToXml
	@param document
	@param choices
	@return the <options> block of a placed element, one <option> per
	choice in the order of the option names; a null element when there is
	no choice, so that an element without any is written as before
*/
QDomElement ElementOptions::choicesToXml(QDomDocument &document, const Choices &choices)
{
	if (choices.isEmpty()) {
		return QDomElement();
	}
	QDomElement block = document.createElement(options_tag);
	for (auto it = choices.cbegin() ; it != choices.cend() ; ++it)
	{
		QDomElement o = document.createElement(option_tag);
		o.setAttribute(QStringLiteral("name"), it.key());
		o.setAttribute(QStringLiteral("value"), it.value());
		block.appendChild(o);
	}
	return block;
}

/**
	@brief ElementOptions::operator ==
	@param other
	@return true if both declare the same options, values, names and
	defaults, in the same order
*/
bool ElementOptions::operator==(const ElementOptions &other) const
{
	if (m_options.size() != other.m_options.size()) return false;
	for (int i = 0 ; i < m_options.size() ; ++i)
	{
		const Option &a = m_options.at(i);
		const Option &b = other.m_options.at(i);
		if (a.name != b.name || a.default_value != b.default_value
			|| a.names != b.names || a.values.size() != b.values.size()) {
			return false;
		}
		for (int j = 0 ; j < a.values.size() ; ++j) {
			if (a.values.at(j).name != b.values.at(j).name
				|| a.values.at(j).names != b.values.at(j).names) {
				return false;
			}
		}
	}
	return true;
}
