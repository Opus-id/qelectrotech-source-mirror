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
#include "../pidcodification.h"
#include "../pipeproperties.h"

#include <QList>
#include <QPointer>
#include <functional>

class Conductor;
class QETProject;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

/**
	@brief The PipePropertiesWidget class
	The data of the pipes selected on a folio, conductors of the pipe medium
	(PipeProperties): line number, fluid, material, size (a DN, and an
	outside diameter for a material named by it, in one field), flow,
	piping class, nominal pressure, spool, external finish and kind of line.
	It stands for the page of a wire, whose fields mean nothing for a pipe.
	Each change is applied at once, with undo, to every pipe selected and to
	that field only: a field whose value differs from one pipe to another
	shows none and changes nothing until it is edited. Nothing is passed on
	to the pipes connected to them.
*/
class PipePropertiesWidget : public PropertiesEditorWidget
{
		Q_OBJECT
	public:
		explicit PipePropertiesWidget(const QList<Conductor *> &pipes, QWidget *parent = nullptr);

		void setPipes(const QList<Conductor *> &pipes);
		QList<Conductor *> pipes() const;

		QString title() const override;
		void updateUi() override;
		bool setLiveEdit(bool live_edit) override;

			/// Give the pipes edited the colour @p color, or none when it is
			/// invalid: they then follow their fluid
		void setManualColor(const QColor &color);

		static void editInDialog(Conductor *pipe, QWidget *parent = nullptr);

	private:
		void buildUi();
		QLineEdit *addTextField(const QString &name, const QString &label,
								const std::function<QString(const PipeProperties &)> &get,
								const std::function<void(PipeProperties &, const QString &)> &set);
		void bindTextField(QLineEdit *edit,
						   const std::function<QString(const PipeProperties &)> &get,
						   const std::function<void(PipeProperties &, const QString &)> &set,
						   bool through_valves = false);
		QComboBox *addListField(const QString &name, const QString &label,
								const std::function<void(PipeProperties &, const QString &)> &set,
								const std::function<void(const QString &)> &apply_code = nullptr);
		void applyChange(const std::function<void(PipeProperties &)> &change,
						 bool through_valves = false);
		void showText(QLineEdit *edit, const std::function<QString(const PipeProperties &)> &get);
		void fillLists();
		static QString codeOf(const QComboBox *list, const QString &text);
			/// The codification of the project, an empty one without it
		PidCodification codification() const;
			/// The sizes offered for the pipes edited, written in @p sizes:
			/// the series of their material; the DN of the project
			/// (PidCodification::nominalSizes()) without material, or with
			/// one the project has not; @p several says they have several
			/// materials, and then none
		PidCodification::SizeRule sizeRule(QList<PipeDiameter> *sizes, bool *several = nullptr) const;
			/// The size typed or picked, a DN or an outside diameter, given
			/// to the pipes: a pipe has a DN, the size of the line, and for
			/// a material named by it an outside diameter. The one typed
			/// gives the other, through the table of the project and the
			/// series of the material; the user chooses among several. A
			/// size out of the series becomes the nearest one. Otherwise
			/// the field gets its value back.
		void diameterEdited();
			/// The material of code @p code given to the pipes, in one step:
			/// their DN stays, their outside diameter is the one the series
			/// of the new material joins to it, which the user chooses
			/// among several, once for a DN; none for a material named by
			/// its DN
		void materialEdited(const QString &code);
			/// The answer to @p question among @p items, -1 for none, which
			/// the dialog offers last as @p none
		int choose(const QString &question, const QStringList &items, const QString &none);
			/// The size chosen among @p sizes, see choose(); none for none
		PipeDiameter chooseSize(const QString &question, const QList<PipeDiameter> &sizes,
								const QString &none);
			/// What is wrong with the diameter shown, empty when nothing
		QString diameterWarning() const;

	public:
			/// The text under the field of the diameter: a value refused, or
			/// one which is not in the lists of the project or which does not
			/// suit the material
		QString diameterMessage() const;
			/// What asks which size goes with another, a list in a dialog
			/// unless a test says otherwise; it answers the index chosen, -1
			/// for none
		void setChooser(const std::function<int(const QString &, const QStringList &)> &chooser) {
			m_chooser = chooser;
		}

	private:
		QComboBox *m_diameter = nullptr;
		QLabel *m_diameter_message = nullptr;
			/// A value typed and refused, said until the next change
		QString m_refused;
		std::function<int(const QString &, const QStringList &)> m_chooser;

		QList<QPointer<Conductor>> m_pipes;
			/// The project of the pipes, for its codification
		QPointer<QETProject> m_project;
			/// Each text field with what it shows of the properties
		QList<QPair<QLineEdit *, std::function<QString(const PipeProperties &)>>> m_text_fields;
		QComboBox *m_fluid = nullptr;
		QComboBox *m_material = nullptr;
		QComboBox *m_kind = nullptr;
		QComboBox *m_status = nullptr;
		QPushButton *m_color = nullptr;
		QLabel *m_count = nullptr;
};

#endif // PIPEPROPERTIESWIDGET_H
