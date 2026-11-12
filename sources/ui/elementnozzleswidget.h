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
#include <QSet>
#include <QString>
#include <functional>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QToolButton;
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

	A nozzle which follows its pipe (Terminal::followsPipe(): those of a
	valve, of a tee, a free end) has no size of its own: its field is greyed
	and shows the size of its line; its connection and its drawing stay free.

	Above them, for an element of two nozzles or more, one box: one
	connection (and drawing) for all; and for two nozzles or more with a size
	of their own, another: one size for all. A thread tells its DN in a tool
	tip (EN 10255, PipeDiameter::threadTable()). Checked, the first nozzle gives
	its value to the others, whose lists are greyed. Nothing of them is
	written: they are checked when the nozzles are alike, at the opening of
	the tab, and unchecked when they no longer are (an undo).
	A reducer has no box for one size: its sides differ by nature.

	A side of a reducer given a size of its own gives it to its line: the
	pipes of that side (Terminal::nearestPipes()) and those they continue,
	as the page of a pipe gives a size typed (PipePropertiesWidget::
	sizeLineIn()), in the step which gives the nozzle its size. A size
	emptied changes no pipe; a line changed later never changes the size of
	the reducer (the gap is marked). The size it has, typed again, chosen
	again in the list or validated by Enter, gives the line this size back;
	a field only entered and left changes nothing.

	At the end of the row of a nozzle given a size of its own, the button
	"Follow the pipe" (an icon, so that the panel is not widened) empties it, with undo: the nozzle has the size of its
	line again, and no pipe changes. Not for a nozzle whose symbol gives a
	size: emptied, it would have this one, not the one of its line.
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
			/// The button "Follow the pipe" of the nozzle @p name, or nullptr
			/// for a nozzle which follows its pipe anyway
		QToolButton *followButtonFor(const QString &name) const;
			/// What was said of the last value refused or changed
		QString message() const;
			/// What the mouse resting on the size of the nozzle @p name
			/// shows of a thread: "G 1"1/2 = DN 40 (EN 10255)"; nothing for
			/// another size
		QString threadToolTip(const QString &name) const;
			/// The box giving all the nozzles the connection and the drawing
			/// of the first, nullptr for an element of one nozzle
		QCheckBox *sameConnectionBox() const { return m_same_connection; }
			/// The box giving all the nozzles the size of the first, nullptr
			/// for an element with fewer than two nozzles with a size of
			/// their own: a valve, a tee
		QCheckBox *sameSizeBox() const { return m_same_size; }
			/// What asks which size goes with another when a line is given
			/// the size of a reducer (PipePropertiesWidget::setChooser()), a
			/// list in a dialog unless a test says otherwise
		void setChooser(const std::function<int(const QString &, const QStringList &)> &chooser) {
			m_chooser = chooser;
		}

	protected:
		bool eventFilter(QObject *watched, QEvent *event) override;

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
				/// It has the size of its line (Terminal::followsPipe())
			bool follows;
				/// "Follow the pipe", nullptr when it follows anyway
			QToolButton *follow;
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
			/// @p field is the first nozzle, which gives its values to the
			/// others when a box is checked
		bool isFirst(const Field &field) const;
			/// The first nozzle with a size of its own, which gives it to the
			/// others when their box is checked; nullptr if none
		const Field *firstSized() const;
		bool isFirstSized(const Field &field) const;
			/// The nozzles of the element have one connection and drawing;
			/// those with a size of their own have one size
		bool allSameConnection() const;
		bool allSameSize() const;
			/// Give the others the connection and the drawing, the size of
			/// the first nozzle, in the lists
		void copyConnectionOfFirst();
		void copySizeOfFirst();
			/// A box checked or unchecked
		void sameToggled();
			/// Grey the lists the first nozzle fills
		void updateLinkedFields();
			/// The sides of a reducer which @p diameters give a size of
			/// their own other than the one they have, or the one they have
			/// typed again (m_typed), and which have a line to give it to;
			/// none for any other element
		QList<const Field *> resizedSides(const NozzleDiameters &diameters) const;
			/// "Follow the pipe" clicked: @p field emptied, and those it
			/// gives its size to when the box for one size is checked
		void followPipe(const Field &field);

		QFormLayout *m_layout = nullptr;
			/// The fields given a value by the user since the last one
			/// applied: typed, chosen in the list or validated by Enter;
			/// not a field only entered and left
		mutable QSet<QString> m_typed;
		std::function<int(const QString &, const QStringList &)> m_chooser;
		QLabel *m_message = nullptr;
		QCheckBox *m_same_connection = nullptr;
		QCheckBox *m_same_size = nullptr;
		QList<Field> m_fields;
};

#endif // ELEMENTNOZZLESWIDGET_H
