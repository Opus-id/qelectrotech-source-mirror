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
#ifndef DIAMETERLIST_H
#define DIAMETERLIST_H

#include "../pidcodification.h"
#include "../pipediameter.h"
#include "../pipeproperties.h"

#include <QComboBox>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>

namespace DiameterList {

/**
	@brief isoSizes
	@return the DN of ISO 6708, as diameters
*/
inline QList<PipeDiameter> isoSizes()
{
	QList<PipeDiameter> list;
	for (int value : PipeProperties::preferredDiameters()) {
		list << PipeDiameter::nominal(value);
	}
	return list;
}

/**
	@brief fill
	Offer in @p list "nothing", then @p diameters, keeping the text shown
*/
inline void fill(QComboBox *list, const QList<PipeDiameter> &diameters,
				 const QLocale &locale = QLocale::c())
{
	const QSignalBlocker blocker(list);
	const QString text = list->lineEdit() ? list->lineEdit()->text() : QString();
	list->clear();
	list->addItem(QString());
	for (const PipeDiameter &diameter : diameters) {
		list->addItem(diameter.text(locale));
	}
	if (list->lineEdit()) {
		list->lineEdit()->setText(text);
	}
}

/**
	@brief setUp
	Make of @p list the field of a diameter: the values to pick from, and
	others to type, "DN 100", "Ø 88,9" or a number alone; the field that takes
	it tells whether it is allowed. Empty is a value: no diameter. That is
	why this is no QIntValidator, for which an empty field is not finished and
	which would keep the value before it.
	@param list
	@param diameters : those offered, by default the DN of ISO 6708
*/
inline void setUp(QComboBox *list, const QList<PipeDiameter> &diameters = isoSizes(),
				  const QLocale &locale = QLocale::c())
{
	list->setEditable(true);
	list->setInsertPolicy(QComboBox::NoInsert);
		//Otherwise 12 typed becomes 125 when the field is left
	list->setCompleter(nullptr);
	fill(list, diameters, locale);
	list->setValidator(new QRegularExpressionValidator(
						   QRegularExpression(QStringLiteral("\\s*(([Dd][Nn]|[\\x{00D8}\\x{00F8}Dd])\\s*)?"
															 "([1-9][0-9]{0,4}([.,][0-9])?)?\\s*")),
						   list));
}

} // namespace DiameterList

#endif // DIAMETERLIST_H
