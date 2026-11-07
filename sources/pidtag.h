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
#ifndef PID_TAG_H
#define PID_TAG_H

#include <QString>

class DiagramContext;

/**
	@brief The PidTag namespace
	The tag of a P&ID symbol is made of fields, which the model of its
	project puts together into its label: the functional unit it belongs
	to, the identifier of its type, given by its symbol (Element::getPrefix()),
	the train it is in and its order, as "{unit} {id} {train}{order:2}".

	The unit, the train and the order are information of the placed element
	(its elementInformations, saved as any other); the identifier comes from
	the symbol, so that changing the type of the element changes it. A model
	belongs to the project (PidCodification::tag_models), one for each family
	of objects: QElectroTech has none, and without one the label is written
	by hand, as before.
*/
namespace PidTag
{
		/// The keys of the fields in the information of an element
	QString unitKey();
	QString trainKey();
	QString orderKey();

		/// @p information holds at least one field of a tag
	bool hasFields(const DiagramContext &information);

		/**
			The label @p model gives: {unit}, {id}, {train} and {order} are
			replaced by the fields, {order:N} by the order with N digits at
			least (zeros in front, when it is a number), and "\n" (backslash
			then n, as written in a file) starts a new line. In each line,
			spaces in a row are made one and those at the ends go, so that a
			missing field leaves no gap. An unknown {word} stays as it is.
		*/
	QString label(const QString &model,
				  const QString &unit,
				  const QString &id,
				  const QString &train,
				  const QString &order);
}

#endif
