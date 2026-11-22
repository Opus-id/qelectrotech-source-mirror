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
class ConductorProperties;
class QETProject;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

/**
	@brief The PipePropertiesWidget class
	The data of the pipes selected on a folio, conductors of the pipe medium
	(PipeProperties): line number, fluid, material, size (a DN, and an
	outside diameter for a material named by it, in one field), flow,
	piping class, nominal pressure, spool, external finish, kind of line,
	insulation (its kind, what is written of it, a mark in the middle).
	It stands for the page of a wire, whose fields mean nothing for a pipe.
	Each change is applied at once, with undo, to every pipe selected and to
	that field only: a field whose value differs from one pipe to another
	shows none and changes nothing until it is edited. Two things go on to
	the pipes connected to them. The size: the DN goes on through the valves,
	whatever the material, each pipe reached getting the outside diameter of
	its own material (resizeLine()); reducers stop it. The spec: the
	material, piping class and nominal pressure go on through the valves,
	tees and reducers; spec breaks and equipment stop it (step 5l-1l). The
	fluid, the line number, the kind of line and the flow go on as
	Conductor::inlineRun() says. The other fields stay on the pipes selected.
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
			/// Which pipes a change reaches: those selected; with them, those
			/// their size passes to; those their spec (material, piping
			/// class, nominal pressure) passes to; those their fluid, their
			/// line number or their flow passes to, see Conductor::inlineRun();
			/// those their kind of line passes to, the same as their line
			/// number; those their insulation passes to (step 7d-bis)
		enum class Reach { Selection, Size, Spec, Fluid, LineNumber, LineKind, Flow, Insulation };

		void buildUi();
		QLineEdit *addTextField(const QString &name, const QString &label,
								const std::function<QString(const PipeProperties &)> &get,
								const std::function<void(PipeProperties &, const QString &)> &set,
								Reach reach = Reach::Selection);
		void bindTextField(QLineEdit *edit,
						   const std::function<QString(const PipeProperties &)> &get,
						   const std::function<void(PipeProperties &, const QString &)> &set,
						   Reach reach = Reach::Selection);
		QComboBox *addListField(const QString &name, const QString &label,
								const std::function<void(PipeProperties &, const QString &)> &set,
								const std::function<void(const QString &)> &apply_code = nullptr);
			/// With @p parent, the commands are added to it and nothing is
			/// pushed: the change is part of a step made elsewhere
		void applyChange(const std::function<void(PipeProperties &)> &change,
						 Reach reach = Reach::Selection, QUndoCommand *parent = nullptr);
			/// applyChange() for what a pipe has as a conductor (its text
			/// shown, its designation)
		void applyPropertiesChange(const std::function<void(ConductorProperties &)> &change,
								   Reach reach = Reach::Selection, QUndoCommand *parent = nullptr);
			/// The pipes edited, and those @p reach adds to them
		QList<Conductor *> changedPipes(Reach reach) const;
			/// What is said when a change of the spec, or of the fluid, the
			/// line number or the flow with @p reach, went beyond the pipes selected: how
			/// many other pipes of the line it changed, and the values @p get
			/// gave those it replaced; empty when it changed none. To compute
			/// before the change, with the new @p value.
		QString lineNote(const std::function<QString(const PipeProperties &)> &get,
						 const QString &value, Reach reach = Reach::Spec) const;
			/// Give the line of the pipes edited the DN @p nominal, in one
			/// step: the pipes of the material @p material, when
			/// @p one_material says the pipes edited have this one, take
			/// @p nominal and @p outside as they are; the others the DN as
			/// their material has it and the outside diameter of their
			/// material (kept, the only one, the default one, or the one
			/// the user chooses, once for a material and a DN). No size
			/// empties them all; an outside diameter without DN is not
			/// passed on to another material. What is worth saying is added
			/// to @p notes. @p parent as for applyChange().
		void resizeLine(const PipeDiameter &nominal, const PipeDiameter &outside,
						const QString &material, bool one_material, QStringList &notes,
						QUndoCommand *parent = nullptr);
		void showText(QLineEdit *edit, const std::function<QString(const PipeProperties &)> &get);
		void fillLists();
		static QString codeOf(const QComboBox *list, const QString &text);
			/// The codification of the project, an empty one without it
		PidCodification codification() const;
			/// The sizes offered for the pipes edited, written in @p sizes:
			/// the sizes of their material; the DN of the project
			/// (PidCodification::nominalSizes()) without material, or with
			/// one the project has not; @p several says they have several
			/// materials, and then the DN of the project, a DN for all of
			/// them; none, and NoSeries, when one of them has no sizes
		PidCodification::SizeRule sizeRule(QList<PipeDiameter> *sizes, bool *several = nullptr) const;
			/// The size typed or picked, a DN or an outside diameter, given
			/// to the pipes: a pipe has a DN, the size of the line, and for
			/// a material named by it an outside diameter. The one typed
			/// gives the other, through the sizes of the material; among
			/// several outside diameters of a DN, the default one of the
			/// material is taken, and said, or without one the user
			/// chooses. A size the material has not becomes the nearest
			/// one. Otherwise the field gets its value back.
		void diameterEdited();
			/// What diameterEdited() and sizeLineIn() do: @p text given to
			/// the line, in the step @p parent, or in one of its own without;
			/// false when refused or cancelled, said in m_refused
		bool sizeLine(const QString &text, QUndoCommand *parent);
			/// The material of code @p code given to the pipes and to the
			/// rest of their line (Reach::Spec), in one step: their DN
			/// stays, their outside diameter is the one the sizes of the new
			/// material join to it: among several, its default one, or
			/// without one the one the user chooses, once for a DN; none for
			/// a material named by its DN
		void materialEdited(const QString &code);
			/// The answer to @p question among @p items, -1 for none, which
			/// the dialog offers last as @p none. With @p remember, the
			/// dialog has a box of this text too, unchecked, whose state is
			/// written in @p remembered (false for none).
		int choose(const QString &question, const QStringList &items, const QString &none,
				   const QString &remember = QString(), bool *remembered = nullptr);
			/// The size chosen among @p sizes, see choose(); none for none
		PipeDiameter chooseSize(const QString &question, const QList<PipeDiameter> &sizes,
								const QString &none, const QString &remember = QString(),
								bool *remembered = nullptr);
			/// The outside diameter the user chooses among @p outsides for
			/// the DN @p nominal in the material @p material, none for none.
			/// The answer may be kept as the default one of this DN in the
			/// material: the codification of the project is then changed in
			/// the step of the next applyChange(), and it is said in @p notes.
		PipeDiameter chooseOutside(const PipeDiameter &nominal, const QString &material,
								   const QList<PipeDiameter> &outsides, QStringList &notes);
			/// What is wrong with the diameter shown, empty when nothing
		QString diameterWarning() const;
		void showColor();

	public:
			/// Give the line of the pipes edited the size @p text, read as
			/// when it is typed in the field of the diameter (diameterEdited(),
			/// the same questions), in the step @p parent: the commands are
			/// added to it, nothing is pushed. What is said of it is then in
			/// diameterMessage(). @return false when the size is refused or
			/// the user changed nothing: no command was added then.
		bool sizeLineIn(const QString &text, QUndoCommand *parent);
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
			/// With a chooser (setChooser()), what answers the box of a
			/// question which may be remembered, from its text; unchecked
			/// without it
		void setRememberer(const std::function<bool(const QString &)> &rememberer) {
			m_rememberer = rememberer;
		}

	private:
		QComboBox *m_diameter = nullptr;
		QLabel *m_diameter_message = nullptr;
			/// A value typed and refused, said until the next change
		QString m_refused;
		std::function<int(const QString &, const QStringList &)> m_chooser;
		std::function<bool(const QString &)> m_rememberer;
			/// The outside diameters chosen to be the default ones of their
			/// DN, by material (chooseOutside()), until applyChange()
		QList<QPair<QString, PipeDiameter>> m_new_defaults;

		QList<QPointer<Conductor>> m_pipes;
			/// The project of the pipes, for its codification
		QPointer<QETProject> m_project;
			/// Each text field with what it shows of the properties
		QList<QPair<QLineEdit *, std::function<QString(const PipeProperties &)>>> m_text_fields;
		QComboBox *m_fluid = nullptr;
		QComboBox *m_material = nullptr;
		QComboBox *m_kind = nullptr;
		QComboBox *m_status = nullptr;
		QComboBox *m_insulation_kind = nullptr;
		QCheckBox *m_insulation_mark = nullptr;
		QCheckBox *m_show_designation = nullptr;
		QPushButton *m_color = nullptr;
		QToolButton *m_automatic_color = nullptr;
		QLabel *m_count = nullptr;
			/// The direction of flow, found from the symbols (step 8a-bis)
		QLabel *m_flow_direction = nullptr;
};

#endif // PIPEPROPERTIESWIDGET_H
