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

#include <QList>
#include <QMap>
#include <QString>

class QFormLayout;
class QLineEdit;

/**
	@brief The ElementNozzlesWidget class
	The tab of the properties of a placed element where its nozzles are given
	a nominal diameter: one field per named nozzle, empty for the one of the
	symbol or else of the pipe connected there, shown in grey. Live, a value
	is applied when the field is left, with undo.
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
		QMap<QString, int> chosen() const;
			/// The field of the nozzle @p name, or nullptr
		QLineEdit *fieldFor(const QString &name) const;

	private:
		void buildFields();

		struct Field {
			QString name;
			int symbol_dn;
			QLineEdit *edit;
		};
		QFormLayout *m_layout = nullptr;
		QList<Field> m_fields;
};

#endif // ELEMENTNOZZLESWIDGET_H
