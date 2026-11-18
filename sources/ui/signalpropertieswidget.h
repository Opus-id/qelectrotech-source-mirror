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
#ifndef SIGNALPROPERTIESWIDGET_H
#define SIGNALPROPERTIESWIDGET_H

#include "../PropertiesEditor/propertieseditorwidget.h"
#include "../signalproperties.h"

#include <QList>
#include <QPointer>
#include <functional>

class Conductor;
class QComboBox;
class QLabel;

/**
	@brief The SignalPropertiesWidget class
	The data of the signal lines selected on a folio, conductors of the
	signal medium (SignalProperties): their type of signal and their status
	of works.
	It stands for the page of a wire, whose fields mean nothing for a signal
	line. A change is applied at once, with undo, to every signal line
	selected: a value which differs from one line to another shows none and
	changes nothing until one is chosen. It stays on the lines selected: it
	is never passed on to the lines connected to them.
*/
class SignalPropertiesWidget : public PropertiesEditorWidget
{
		Q_OBJECT
	public:
		explicit SignalPropertiesWidget(const QList<Conductor *> &lines, QWidget *parent = nullptr);

		void setLines(const QList<Conductor *> &lines);
		QList<Conductor *> lines() const;

		QString title() const override;
		void updateUi() override;
		bool setLiveEdit(bool live_edit) override;

		static void editInDialog(Conductor *line, QWidget *parent = nullptr);

	private:
		void buildUi();
		void applyChange(const std::function<void(SignalProperties &)> &change);

		QList<QPointer<Conductor>> m_lines;
		QComboBox *m_type = nullptr;
		QComboBox *m_status = nullptr;
		QLabel *m_count = nullptr;
};

#endif // SIGNALPROPERTIESWIDGET_H
