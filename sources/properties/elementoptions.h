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
#ifndef ELEMENT_OPTIONS_H
#define ELEMENT_OPTIONS_H

#include "../NameList/nameslist.h"

#include <QDomDocument>
#include <QDomElement>
#include <QList>
#include <QMap>
#include <QString>

/**
	@brief The ElementOptions class
	The options a symbol offers, such as the motor of a pump (none, fixed
	speed, variable speed): each placed element chooses a value of each
	option, and the parts of the drawing show or hide after it. The type of
	the device stays the symbol itself (see Element::changeLocation()); an
	option is what may vary on a device of that type.

	In a definition (.elmt), an <options> block declares them:
	@code
	<options>
	    <option name="motor" default="none">
	        <names><name lang="en">Motor</name></names>
	        <value name="none"><names><name lang="en">No motor</name></names></value>
	        <value name="fixed">...</value>
	    </option>
	</options>
	@endcode
	and a part of the drawing may carry show_if="motor=fixed,variable": it is
	drawn only when the motor is fixed or variable. Several conditions,
	separated by ";", must all hold. A part without show_if is always drawn.

	A placed element writes the values it was given in its own <options>
	block (choicesToXml()); a value not given is the default.

	A version of QElectroTech that knows nothing of this draws every part.
*/
class ElementOptions
{
	public:
			/// The value chosen for each option, by option name. Sorted,
			/// so that the same choices always give the same key().
		using Choices = QMap<QString, QString>;

		struct Value
		{
			QString name;
			NamesList names;
		};

		struct Option
		{
			QString name;
			QString default_value;
			NamesList names;
			QList<Value> values;

				/// Whether @p value is one of the values of this option
			bool hasValue(const QString &value) const;
		};

		bool isEmpty() const { return m_options.isEmpty(); }
		QList<Option> options() const { return m_options; }
			/// The option named @p name, or nullptr
		const Option *option(const QString &name) const;

			/// Reads the <options> block of @p definition. An option or a
			/// value without a usable name, or named twice, is left out; an
			/// option without any value too; a default that is not one of
			/// the values is the first value.
		void fromXml(const QDomElement &definition);
			/// The <options> block, to append to a definition
		QDomElement toXml(QDomDocument &document) const;

			/// The default value of every option
		Choices defaults() const;
			/// The value of every option once @p chosen is applied: the one
			/// chosen when it is one of the values of the option, else the
			/// default. A choice for an option that does not exist is left
			/// out.
		Choices resolved(const Choices &chosen) const;

			/// Whether a part whose show_if attribute is @p show_if is drawn
			/// with the values @p resolved. A condition on an option @p
			/// resolved does not hold, or one that cannot be read, is left
			/// aside: the part is drawn, as a version of QElectroTech that
			/// knows nothing of options draws it.
		static bool isShown(const QString &show_if, const Choices &resolved);
			/// Whether @p name may name an option or a value: letters,
			/// digits, "_" and "-", so that show_if can be read back
		static bool isValidName(const QString &name);
			/// A text that tells @p choices apart, the same for the same ones
		static QString key(const Choices &choices);

			/// The choices of a placed element, from its <options> block
		static Choices choicesFromXml(const QDomElement &element);
			/// The <options> block of a placed element, or a null element
			/// when @p choices is empty
		static QDomElement choicesToXml(QDomDocument &document, const Choices &choices);

		bool operator==(const ElementOptions &other) const;
		bool operator!=(const ElementOptions &other) const { return !(*this == other); }

	private:
		QList<Option> m_options;
};

#endif
