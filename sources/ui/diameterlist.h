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

#include "../pipeproperties.h"

#include <QComboBox>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

namespace DiameterList {

/**
	@brief setUp
	Make of @p list the field of a nominal diameter: the preferred values to
	pick from, and any other whole number to type, the standard only giving
	preferred ones. Empty is a value: no DN. That is why this is no
	QIntValidator, for which an empty field is not finished and which would
	keep the value before it.
	@param list
*/
inline void setUp(QComboBox *list)
{
	list->setEditable(true);
	list->setInsertPolicy(QComboBox::NoInsert);
		//Otherwise 12 typed becomes 125 when the field is left
	list->setCompleter(nullptr);
	list->addItem(QString());
	for (int dn : PipeProperties::preferredDiameters()) {
		list->addItem(QString::number(dn));
	}
	list->setValidator(new QRegularExpressionValidator(
						   QRegularExpression(QStringLiteral("[1-9][0-9]{0,4}|")), list));
}

} // namespace DiameterList

#endif // DIAMETERLIST_H
