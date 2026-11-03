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
#include "terminaldata.h"

#include "../qetapp.h"
#include "../utils/qetutils.h"

#include <QGraphicsObject>
#include <QRegularExpression>

#include <QDebug>

TerminalData::TerminalData():
	PropertiesInterface()
{
	init();
}

TerminalData::TerminalData(QGraphicsObject *parent):
	PropertiesInterface(),
	q(parent)
{
	init();
}

void TerminalData::init()
{
	m_label_font = QETApp::diagramTextsFont();
}

TerminalData::~TerminalData()
{

}

/**
	@brief TerminalData::setParent
	@param parent
*/
void TerminalData::setParent(QGraphicsObject* parent)
{
	q = parent;
}

/**
	@brief TerminalData::toSettings
	Save properties to settings file.

	QString is used to prefix a word before the name of each parameter
	@param settings UNUSED
	@param prefix UNUSED
*/
void TerminalData::toSettings(QSettings &settings, const QString prefix) const

{
	Q_UNUSED(settings)
	Q_UNUSED(prefix)
}

/**
	@brief TerminalData::fromSettings
	load properties to settings file.

	QString is used to prefix a word before the name of each parameter
	@param settings UNUSED
	@param prefix UNUSED
*/
void TerminalData::fromSettings(const QSettings &settings, const QString prefix)
{
	Q_UNUSED(settings)
	Q_UNUSED(prefix)
}

/**
	@brief TerminalData::toXml
	Save properties to xml element
	write the name, number, position and orientation of the terminal
	to xml_element

	@note This method is only called from the PartTerminal
	and should never be called from the Terminal class
	@param xml_document
	@return xml_element : DomElement with
	the name, number, position and orientation of the terminal
*/
QDomElement TerminalData::toXml(QDomDocument &xml_document) const
{
	QDomElement xml_element = xml_document.createElement("terminal");

	qreal x = (qRound(q->scenePos().x() * 100.0) / 100.0);
	qreal y = (qRound(q->scenePos().y() * 100.0) / 100.0);

	xml_element.setAttribute("x", QString("%1").arg(x));
	xml_element.setAttribute("y", QString("%1").arg(y));

	xml_element.setAttribute("uuid", m_uuid.toString());
	xml_element.setAttribute("name", m_name);

	xml_element.setAttribute("orientation",
				 Qet::orientationToString(m_orientation));

	xml_element.setAttribute("type", typeToString(m_type));

		//Written only for a medium other than electrical: an electrical
		//terminal is saved exactly as it always was
	if (m_medium != Electrical)
		xml_element.setAttribute("medium", mediumToString(m_medium));

	if (m_show_name) {
		xml_element.setAttribute("show_name", "true");
		xml_element.setAttribute("label_x", QString::number(m_label_pos.x()));
		xml_element.setAttribute("label_y", QString::number(m_label_pos.y()));
		xml_element.setAttribute("label_font", QETUtils::fontToString(m_label_font));
		xml_element.setAttribute("label_rotation", QString::number(m_label_rotation));
		xml_element.setAttribute("label_halign", static_cast<int>(m_label_halignment));
		xml_element.setAttribute("label_valign", static_cast<int>(m_label_valignment));
		if (m_label_frame)
			xml_element.setAttribute("label_frame", "true");
		if (m_label_color != QColor(Qt::black))
			xml_element.setAttribute("label_color", m_label_color.name());
	}

	if (!m_potential.isEmpty())
		xml_element.setAttribute("potential", m_potential);

	// Save master label override settings
	if (m_use_master_label) {
		xml_element.setAttribute("use_master_label", "true");
		xml_element.setAttribute("master_label_index", m_master_label_index);
	}

	return(xml_element);
}

/**
	@brief TerminalData::fromXml
	load properties to xml element

	@note This method is only called from the PartTerminal
	and should never be called from the Terminal class
	@param xml_element
	@return true if succeeded / false if the attribute is not real
*/
bool TerminalData::fromXml (const QDomElement &xml_element)
{
	qreal term_x = 0.0;
	qreal term_y = 0.0;

	// reads the position of the terminal
	// lit la position de la borne
	if (!QET::attributeIsAReal(xml_element, "x", &term_x))
		return false;

	if (!QET::attributeIsAReal(xml_element, "y", &term_y))
		return false;

	m_pos = QPointF(term_x, term_y);

	//emit posFromXML(QPointF(term_x, term_y));

	QString uuid = xml_element.attribute("uuid");
	// update part and add uuid, which is used in the new version
	// to connect terminals together
	// if the attribute not exists, means, the element is created with an
	// older version of qet. So use the legacy approach
	// to identify terminals
	if (!uuid.isEmpty())
	m_uuid = QUuid(uuid);

	m_name = xml_element.attribute("name");

	// read the orientation of the terminal
	// lit l'orientation de la borne
	m_orientation = Qet::orientationFromString(
				xml_element.attribute("orientation"));

	m_type = typeFromString(xml_element.attribute("type"));
	m_medium = mediumFromString(xml_element.attribute("medium"));

	m_show_name = (xml_element.attribute("show_name") == QLatin1String("true"));
	if (m_show_name) {
		qreal lx = xml_element.attribute("label_x", "0").toDouble();
		qreal ly = xml_element.attribute("label_y", "0").toDouble();
		m_label_pos = QPointF(lx, ly);

		QString font_str = xml_element.attribute("label_font");
		if (!font_str.isEmpty())
			QETUtils::fontFromString(m_label_font, font_str);

		m_label_rotation = xml_element.attribute("label_rotation", "0").toDouble();

		int ha = xml_element.attribute("label_halign", "0").toInt();
		m_label_halignment = static_cast<Qt::Alignment>(ha);

		int va = xml_element.attribute("label_valign", "0").toInt();
		m_label_valignment = static_cast<Qt::Alignment>(va);

		m_label_frame = (xml_element.attribute("label_frame") == QLatin1String("true"));

		QString color_str = xml_element.attribute("label_color");
		if (!color_str.isEmpty())
			m_label_color = QColor(color_str);
	}

	m_potential = xml_element.attribute("potential");

	// Read master label override settings
	m_use_master_label = (xml_element.attribute("use_master_label") == QLatin1String("true"));
	m_master_label_index = xml_element.attribute("master_label_index", "0").toInt();

	return true;
}

/**
 * @brief TerminalData::typeToString
 * @param type
 * @return type into a QString
 */
QString TerminalData::typeToString(TerminalData::Type type)
{
	switch (type) {
		case Generic:
			return QString("Generic");
		case Inner :
			return QString("Inner");
		case Outer :
			return QString("Outer");
		case No :
			return QString("No");
		case Nc :
			return QString("Nc");
		case Common :
			return QString("Common");
	}
	return QString("Generic");
}

/**
	@brief TerminalData::mediumToString
	@param medium
	@return the word the files use for @p medium
*/
QString TerminalData::mediumToString(TerminalData::Medium medium)
{
	switch (medium) {
		case Electrical: return QStringLiteral("electrical");
		case Pipe:       return QStringLiteral("pipe");
		case Signal:     return QStringLiteral("signal");
		case Drive:      return QStringLiteral("drive");
	}
	return QStringLiteral("electrical");
}

/**
	@brief TerminalData::mediumFromString
	@param string
	@return the medium @p string names. Anything else, the absence of the
	attribute included, is electrical: what QET always made of a terminal,
	and what it keeps making of a word written by a later version that it
	does not know.
*/
TerminalData::Medium TerminalData::mediumFromString(const QString &string)
{
	if (string == QLatin1String("pipe"))   return Pipe;
	if (string == QLatin1String("signal")) return Signal;
	if (string == QLatin1String("drive"))  return Drive;
	return Electrical;
}

/**
	@brief TerminalData::namePrefix
	@param medium
	@return the letter the names of the terminals of @p medium begin with by
	convention: N for a nozzle (N1, N2... as DEXPI numbers them), E for a
	drive (entraînement), S for a signal; none for an electrical terminal,
	whose names are free (1, 2, A1, 13...)
*/
QString TerminalData::namePrefix(TerminalData::Medium medium)
{
	switch (medium) {
		case Electrical: return QString();
		case Pipe:       return QStringLiteral("N");
		case Signal:     return QStringLiteral("S");
		case Drive:      return QStringLiteral("E");
	}
	return QString();
}

/**
	@brief TerminalData::suggestedName
	The name a terminal gets when its medium becomes @p medium in the element
	editor. A name that follows the convention of another medium (a letter
	N, E or S then a number, like E1) or no name at all is replaced by the
	first free name of the new medium, so that a drive E1 turned into a
	nozzle is not left called E1. Any other name, chosen by the author of
	the symbol ("Suction", "1"...), is kept; so is every name when the
	terminal becomes electrical, as electrical names follow no convention.
	@param medium the new medium of the terminal
	@param current its name now
	@param taken the names of the other terminals of the element
	@return the name it should have
*/
QString TerminalData::suggestedName(TerminalData::Medium medium,
				    const QString &current,
				    const QStringList &taken)
{
	const QString prefix = namePrefix(medium);
	if (prefix.isEmpty()) {
		return current;
	}

	static const QRegularExpression conventional(QStringLiteral("^([NES])[0-9]+$"));
	const QRegularExpressionMatch match = conventional.match(current);
	const bool replaceable = current.isEmpty()
			|| (match.hasMatch() && match.captured(1) != prefix);
	if (!replaceable) {
		return current;
	}

	for (int number = 1 ; ; ++number) {
		const QString name = prefix + QString::number(number);
		if (!taken.contains(name)) {
			return name;
		}
	}
}

/**
 * @brief TerminalData::typeFromString
 * @param string
 * @return The type describe in string to TerminalData::Type.
 * if string doesn't describe a type, TerminalData::Generic is returned
 */
TerminalData::Type TerminalData::typeFromString(const QString &string)
{
	if (string.isEmpty() ||
		string == "Generic") {
		return TerminalData::Generic;
	} else if (string == "Inner") {
		return TerminalData::Inner;
	} else if (string == "Outer") {
		return TerminalData::Outer;
	} else if (string == "No") {
		return TerminalData::No;
	} else if (string == "Nc") {
		return TerminalData::Nc;
	} else if (string == "Common") {
		return TerminalData::Common;
	} else {
		qDebug() << "TerminalData::typeFromString, argument string is invalid"
					" failsafe type 'TerminalData::Generic' is returned";
		return TerminalData::Generic;
	}
}
