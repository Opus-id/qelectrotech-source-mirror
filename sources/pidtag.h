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

#include <QHash>
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
	(its elementInformations, saved as any other). The functional unit is the
	plant of QElectroTech (QETInformation::ELMT_PLANT, the "=" of IEC 81346),
	the value the symbol holds itself: what a structure box or the folio
	gives is only offered (ElementTagWidget), never read into the tag, so
	that a tag never changes when its symbol is moved. The identifier comes from
	the symbol, so that changing the type of the element changes it. An
	instrument has letters of identification instead ("PT", "LIC"), typed
	on the placed element (lettersKey()), as its symbol, a bubble, may be
	any instrument: typed, they are its identifier. A model
	belongs to the project (PidCodification::tag_models), one for each family
	of objects: QElectroTech has none, and without one the label is written
	by hand, as before.
*/
namespace PidTag
{
		/// The keys of the fields in the information of an element; the
		/// unit is the plant of QElectroTech (QETInformation::ELMT_PLANT)
	QString unitKey();
	QString trainKey();
	QString orderKey();
		/// The letters of identification of an instrument
	QString lettersKey();

		/// The family of the symbols which have letters (ElementData::m_pid_family)
	QString instrumentFamily();

		/// @p information holds at least one field of a tag
	bool hasFields(const DiagramContext &information);

		/// The identifier of the tag of an element of @p information whose
		/// symbol gives @p prefix: its letters when it has some, else @p prefix
	QString identifier(const DiagramContext &information, const QString &prefix);
		/// The identifier of the tag of @p element (identifier() above)
	QString identifier(const Element *element);

		/// @p model shows the train ({train} or {train:N}): then a tag with
		/// an order always has a train, 0 when there is none, so that all
		/// the tags of the project have as many digits
	bool showsTrain(const QString &model);
		/// @p model shows the functional unit ({unit}): the label already
		/// says the plant, which the reference designation of IEC 81346
		/// would write a second time (Element::designationForDisplay())
	bool showsUnit(const QString &model);

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
			The label @p model gives without its identifier ({id} left out),
			for the lower half of an instrument bubble (ISA 5.1 annex B.3.1:
			the letters above, the rest below): "{unit} {id} {train}{order:2}"
			gives "U01 105". The separators the identifier leaves at the ends
			("-", ".", "_", "/") go with it.
		*/
	QString labelWithoutIdentifier(const QString &model,
								   const QString &unit,
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
			The instruments (@p family instrumentFamily()) are numbered by
			loop (ANSI/ISA-5.1, annex A.6), a number being that of a loop which
			several instruments share (PT 105, PIC 105): their order is
			counted by unit and train whatever their letters (in series), or
			by unit, train and measured variable, the first letter, when the
			project says so (PidCodification::parallel_numbering).
		*/
	int nextOrder(QETProject *project,
				  const QString &unit,
				  const QString &id,
				  const QString &train,
				  const Element *besides = nullptr,
				  const QString &family = QString());

		/**
			After a paste or the copy of a folio: a pasted element whose tag
			(unit, identifier, train, order) another element of @p project
			holds gets the next order (nextOrder(), by loop for an
			instrument: the instruments of a loop copied whole get the same
			new order), when @p renumber; one
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

		/// Which elements newOrders() gives an order
	enum class Numbering {
		Clashing,   ///< as numberPasted(): an order another element holds
		Fresh,      ///< as numberPasted() with fresh: every element
		Unnumbered, ///< the elements without an order; the others keep theirs
		All         ///< every element again, the orders it had being freed
	};

		/**
			The orders numberPasted() would give @p elements, by @p how,
			nothing being written: the element and its new order, for
			those which get one. The elements are taken folio by folio, then
			from left to right and from top to bottom, each after the highest
			order the other elements of @p project hold in its unit, identifier
			and train (its loop, for an instrument). With All, instruments
			which had the same order still share one (a loop stays a loop).
			Clashing and Fresh take the elements which have a model of tag
			and a field (numberPasted()); Unnumbered and All those with a
			model of tag, a field or not: "Number" of a selection.
		*/
	QHash<Element *, QString> newOrders(QETProject *project,
										const QList<Element *> &elements,
										Numbering how);
}

#endif
