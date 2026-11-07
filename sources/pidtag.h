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

#include <QList>
#include <QString>

class DiagramContext;
class Element;
class QETProject;

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

		/// @p model shows the train ({train} or {train:N}): then a tag with
		/// an order always has a train, 0 when there is none, so that all
		/// the tags of the project have as many digits
	bool showsTrain(const QString &model);

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

		/**
			The order the next element of @p unit, @p id and @p train gets in
			@p project: one more than the highest order these hold, 1 when
			there is none. A number freed by a deletion is not given again
			while a higher one is held: it may stand on a document already
			sent. Orders which are no number do not count, and "05" is 5;
			no train counts as train 0.
			@p besides, the element being numbered, is left out. The folios
			not built yet (lazy folios) count, read from the project database.
			No counter is kept anywhere: the project is read each time.
		*/
	int nextOrder(QETProject *project,
				  const QString &unit,
				  const QString &id,
				  const QString &train,
				  const Element *besides = nullptr);

		/**
			After a paste or the copy of a folio: a pasted element whose tag
			(unit, identifier, train, order) another element of @p project
			holds gets the next order (nextOrder()), when @p renumber; one
			which holds it alone keeps it (a cut and paste). Folio by folio,
			then from left to right and from top to bottom. Every pasted
			element with a tag and a model then writes its label again, which
			the "erase the label on copy" preference may have emptied. Only
			for elements that are not in an undo stack yet: nothing here is
			undone on its own.
			With @p fresh (the copies of a "Create similar", new devices),
			every pasted element with a tag gets the next order, the one
			without an order too, whatever @p renumber says, and its texts
			take their place around their anchor as the label grows.
			@return how many elements were given another order
		*/
	int numberPasted(QETProject *project,
					 const QList<Element *> &pasted,
					 bool renumber = true,
					 bool fresh = false);
}

#endif
