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
#ifndef ELEMENTNOZZLESWIDGET_H
#define ELEMENTNOZZLESWIDGET_H

#include "abstractelementpropertieseditorwidget.h"
#include "../pidcodification.h"
#include "../pipeconnection.h"
#include "../pipediameter.h"

#include <QList>
#include <QMap>
#include <QString>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class Terminal;

/**
	@brief The ElementNozzlesWidget class
	The tab of the properties of a placed element where its nozzles are given
	a connection and a diameter: per named nozzle, a list of connections and
	a field, empty for the diameter of the symbol or else of the pipe
	connected there, shown in grey, and whether the mark of its connection is
	drawn: as the project says, always or never. A DN or an outside diameter of the lists
	of the project only, of a kind the connection takes. Live, a value is
	applied when the field is left or the connection picked, with undo.
*/
class ElementNozzlesWidget : public AbstractElementPropertiesEditorWidget
{
		Q_OBJECT
	public:
		explicit ElementNozzlesWidget(Element *element, QWidget *parent = nullptr);

			/// Whether @p element has a named nozzle, so that this tab is
			/// worth showing
		static bool hasNozzles(const Element *element);

		void setElement(Element *element) override;
		void apply() override;
		QUndoCommand *associatedUndo() const override;
		QString title() const override;
		void updateUi() override;
		bool setLiveEdit(bool live_edit) override;

			/// The diameters the fields give: the one of the symbol is none,
			/// so that the element is saved as one that changed nothing
		NozzleDiameters chosen() const;
			/// The connections the lists give: the one of the symbol is not
			/// kept, as for the diameters
		NozzleConnections chosenConnections() const;
			/// Whether the nozzles draw the mark of their connection, as the
			/// lists say: as the project is not kept
		NozzleMarks chosenMarks() const;
			/// The field of the nozzle @p name, or nullptr
		QLineEdit *fieldFor(const QString &name) const;
			/// The list of connections of the nozzle @p name, or nullptr
		QComboBox *connectionFor(const QString &name) const;
			/// The list saying whether the nozzle @p name draws the mark of
			/// its connection, or nullptr
		QComboBox *markFor(const QString &name) const;
			/// What was said of the last value refused or changed
		QString message() const;

	private:
		struct Field {
			QString name;
			PipeDiameter symbol_diameter;
			PipeConnection::Type symbol_connection;
			QComboBox *connection;
			QComboBox *mark;
			QComboBox *list;
			QLineEdit *edit;
			const Terminal *terminal;
		};
		void buildFields();
		PidCodification projectCodification() const;
			/// The connection picked in the list of @p field
		static PipeConnection::Type pickedConnection(const Field &field);
			/// The sizes of the project the connection of @p field takes
		QList<PipeDiameter> sizesFor(const Field &field) const;
			/// The value of @p field, if the project and the connection
			/// accept it; else gives the field its value back and says why
		bool checkField(const Field &field);
			/// Another connection picked for @p field: its diameter, if the
			/// connection takes it not, becomes the size the table of the
			/// project joins to it, if there is one only, else none
		void connectionPicked(const Field &field);
		void showField(const Field &field);

		QFormLayout *m_layout = nullptr;
		QLabel *m_message = nullptr;
		QList<Field> m_fields;
};

#endif // ELEMENTNOZZLESWIDGET_H
