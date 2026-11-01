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
#ifndef PIPEPROPERTIESWIDGET_H
#define PIPEPROPERTIESWIDGET_H

#include "../PropertiesEditor/propertieseditorwidget.h"
#include "../pipeproperties.h"

#include <QList>
#include <QPointer>
#include <functional>

class PipeLine;
class QComboBox;
class QLabel;
class QLineEdit;

/**
	@brief The PipePropertiesWidget class
	The properties of the pipe lines selected on a folio (PipeProperties):
	line number, fluid, nominal diameter, material, flow, piping class,
	nominal pressure and kind of line. Each change is applied at once, with
	undo, to every pipe line selected, and to that field only: a field whose
	value differs from one pipe line to another shows none and changes
	nothing until it is edited. Nothing is passed on to the pipe lines
	connected to them.
*/
class PipePropertiesWidget : public PropertiesEditorWidget
{
		Q_OBJECT
	public:
		explicit PipePropertiesWidget(const QList<PipeLine *> &pipes, QWidget *parent = nullptr);

		void setPipes(const QList<PipeLine *> &pipes);
		QList<PipeLine *> pipes() const;

		QString title() const override;
		void updateUi() override;
		bool setLiveEdit(bool live_edit) override;

	private:
		void buildUi();
		QLineEdit *addTextField(const QString &name, const QString &label,
								const std::function<QString(const PipeProperties &)> &get,
								const std::function<void(PipeProperties &, const QString &)> &set);
		void applyChange(const std::function<void(PipeProperties &)> &change);
		void showText(QLineEdit *edit, const std::function<QString(const PipeProperties &)> &get);

		QList<QPointer<PipeLine>> m_pipes;
			/// Each text field with what it shows of the properties
		QList<QPair<QLineEdit *, std::function<QString(const PipeProperties &)>>> m_text_fields;
		QComboBox *m_fluid = nullptr;
		QComboBox *m_kind = nullptr;
		QLabel *m_count = nullptr;
};

#endif // PIPEPROPERTIESWIDGET_H
