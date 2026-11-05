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
#include "pipepropertieswidget.h"

#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../conductorproperties.h"
#include "../diagram.h"
#include "../qetgraphicsitem/conductor.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

/**
	@brief PipePropertiesWidget::PipePropertiesWidget
	@param pipes : the pipes to edit
	@param parent
*/
PipePropertiesWidget::PipePropertiesWidget(const QList<Conductor *> &pipes, QWidget *parent) :
	PropertiesEditorWidget(parent)
{
	m_live_edit = true;
	buildUi();
	setPipes(pipes);
}

/**
	@brief PipePropertiesWidget::setPipes
	Edit @p pipes instead
	@param pipes
*/
void PipePropertiesWidget::setPipes(const QList<Conductor *> &pipes)
{
	for (const QPointer<Conductor> &pipe : std::as_const(m_pipes)) {
		if (pipe) {
			disconnect(pipe, &Conductor::propertiesChange, this, &PipePropertiesWidget::updateUi);
		}
	}
	m_pipes.clear();
	for (Conductor *pipe : pipes) {
		if (pipe) {
			m_pipes << pipe;
				//Undone, redone or changed elsewhere: the fields follow
			connect(pipe, &Conductor::propertiesChange, this, &PipePropertiesWidget::updateUi);
		}
	}
	updateUi();
}

/**
	@brief PipePropertiesWidget::pipes
	@return the pipes edited
*/
QList<Conductor *> PipePropertiesWidget::pipes() const
{
	QList<Conductor *> list;
	for (const QPointer<Conductor> &pipe : m_pipes) {
		if (pipe) list << pipe;
	}
	return list;
}

/**
	@brief PipePropertiesWidget::buildUi
	The fields, in the order of a line designation
*/
void PipePropertiesWidget::buildUi()
{
	auto *layout = new QFormLayout(this);
	m_count = new QLabel(this);
	layout->addRow(m_count);

	addTextField(QStringLiteral("line_number"), tr("Line number:"),
				 [](const PipeProperties &p) { return p.line_number; },
				 [](PipeProperties &p, const QString &t) { p.line_number = t; });

	m_fluid = addListField(QStringLiteral("fluid"), tr("Fluid:"),
						   [](PipeProperties &p, const QString &text) { p.fluid = text; });
	m_fluid->addItems(PipeProperties::defaultFluids());

	QLineEdit *diameter = addTextField(QStringLiteral("nominal_diameter"), tr("Nominal diameter (DN):"),
				 [](const PipeProperties &p) {
					 return p.nominal_diameter > 0 ? QString::number(p.nominal_diameter) : QString();
				 },
				 [](PipeProperties &p, const QString &t) { p.nominal_diameter = qMax(0, t.toInt()); });
	diameter->setValidator(new QIntValidator(0, 100000, diameter));

	m_material = addListField(QStringLiteral("material"), tr("Material:"),
							  [](PipeProperties &p, const QString &text) { p.material = text; });
	addTextField(QStringLiteral("flow"), tr("Flow:"),
				 [](const PipeProperties &p) { return p.flow; },
				 [](PipeProperties &p, const QString &t) { p.flow = t; });
	addTextField(QStringLiteral("piping_class"), tr("Piping class:"),
				 [](const PipeProperties &p) { return p.piping_class; },
				 [](PipeProperties &p, const QString &t) { p.piping_class = t; });
	addTextField(QStringLiteral("nominal_pressure"), tr("Nominal pressure:"),
				 [](const PipeProperties &p) { return p.nominal_pressure; },
				 [](PipeProperties &p, const QString &t) { p.nominal_pressure = t; });
	addTextField(QStringLiteral("spool"), tr("Spool number:"),
				 [](const PipeProperties &p) { return p.spool; },
				 [](PipeProperties &p, const QString &t) { p.spool = t; });
	addTextField(QStringLiteral("external_finish"), tr("External finish:"),
				 [](const PipeProperties &p) { return p.external_finish; },
				 [](PipeProperties &p, const QString &t) { p.external_finish = t; });

	m_kind = new QComboBox(this);
	m_kind->setObjectName(QStringLiteral("line_kind"));
	m_kind->addItem(tr("Main flow line"), int(PipeProperties::Main));
	m_kind->addItem(tr("Secondary flow line"), int(PipeProperties::Secondary));
	m_kind->addItem(tr("Auxiliary system line"), int(PipeProperties::Auxiliary));
	layout->addRow(tr("Kind of line:"), m_kind);
	connect(m_kind, &QComboBox::activated, this, [this](int index) {
		const auto kind = PipeProperties::LineKind(m_kind->itemData(index).toInt());
		applyChange([kind](PipeProperties &p) { p.line_kind = kind; });
	});
}

/**
	@brief PipePropertiesWidget::addListField
	Add a list in which free text may be typed too; once an entry is picked
	or a text typed, does @p set to the properties of every pipe edited
	@return the list
*/
QComboBox *PipePropertiesWidget::addListField(
		const QString &name, const QString &label,
		const std::function<void(PipeProperties &, const QString &)> &set)
{
	auto *list = new QComboBox(this);
	list->setObjectName(name);
	list->setEditable(true);
	list->setInsertPolicy(QComboBox::NoInsert);
	static_cast<QFormLayout *>(layout())->addRow(label, list);
	auto apply = [this, list, set]() {
		const QString text = list->currentText().trimmed();
		applyChange([set, text](PipeProperties &p) { set(p, text); });
	};
	connect(list, &QComboBox::activated, this, apply);
	connect(list->lineEdit(), &QLineEdit::editingFinished, this, [list, apply]() {
		if (list->lineEdit()->isModified()) {
			list->lineEdit()->setModified(false);
			apply();
		}
	});
	return list;
}

/**
	@brief PipePropertiesWidget::addTextField
	Add a text field which shows @p get of the properties and, once edited,
	does @p set to those of every pipe edited
	@return the field
*/
QLineEdit *PipePropertiesWidget::addTextField(
		const QString &name, const QString &label,
		const std::function<QString(const PipeProperties &)> &get,
		const std::function<void(PipeProperties &, const QString &)> &set)
{
	auto *edit = new QLineEdit(this);
	edit->setObjectName(name);
	static_cast<QFormLayout *>(layout())->addRow(label, edit);
	m_text_fields.append({edit, get});
		//Only once the user changed it: leaving a field which shows
		//nothing, for several values, changes nothing
	connect(edit, &QLineEdit::editingFinished, this, [this, edit, set]() {
		if (!edit->isModified()) {
			return;
		}
		edit->setModified(false);
		const QString text = edit->text().trimmed();
		applyChange([set, text](PipeProperties &p) { set(p, text); });
	});
	return edit;
}

/**
	@brief PipePropertiesWidget::applyChange
	Do @p change to the data of every pipe edited, in one undo step, if it
	changes anything
	@param change
*/
void PipePropertiesWidget::applyChange(const std::function<void(PipeProperties &)> &change)
{
	auto *undo = new QUndoCommand(tr("Change the properties of a pipe line"));
	Diagram *diagram = nullptr;
	for (Conductor *pipe : pipes())
	{
		ConductorProperties properties = pipe->properties();
		change(properties.m_pipe);
		if (properties == pipe->properties()) {
			continue;
		}
		QVariant old_value, new_value;
		old_value.setValue(pipe->properties());
		new_value.setValue(properties);
		new QPropertyUndoCommand(pipe, "properties", old_value, new_value, undo);
		if (!diagram) {
			diagram = pipe->diagram();
		}
	}
	if (!undo->childCount() || !diagram) {
		delete undo;
		return;
	}
	diagram->undoStack().push(undo);
}

/**
	@brief PipePropertiesWidget::showText
	Show in @p edit the value @p get gives for the pipes, or nothing and
	"(several values)" when they differ
*/
void PipePropertiesWidget::showText(QLineEdit *edit,
									const std::function<QString(const PipeProperties &)> &get)
{
	const QList<Conductor *> list = pipes();
	QString value;
	bool same = true;
	for (int i = 0 ; i < list.size() ; ++i)
	{
		const QString v = get(list.at(i)->properties().m_pipe);
		if (i == 0) {
			value = v;
		} else if (v != value) {
			same = false;
			break;
		}
	}
	const QSignalBlocker blocker(edit);
	edit->setText(same ? value : QString());
	edit->setPlaceholderText(same ? QString() : tr("(several values)"));
	edit->setModified(false);
}

/**
	@brief PipePropertiesWidget::updateUi
	Show the data of the pipes edited
*/
void PipePropertiesWidget::updateUi()
{
	const QList<Conductor *> list = pipes();
	m_count->setText(tr("%n pipe line(s) selected", "", int(list.size())));
	m_count->setVisible(list.size() > 1);
	setEnabled(!list.isEmpty());
	if (list.isEmpty()) {
		return;
	}

	for (const auto &field : std::as_const(m_text_fields)) {
		showText(field.first, field.second);
	}
	showText(m_fluid->lineEdit(), [](const PipeProperties &p) { return p.fluid; });
	showText(m_material->lineEdit(), [](const PipeProperties &p) { return p.material; });

	const QSignalBlocker blocker(m_kind);
	const PipeProperties::LineKind kind = list.first()->properties().m_pipe.line_kind;
	bool same_kind = true;
	for (Conductor *pipe : list) {
		if (pipe->properties().m_pipe.line_kind != kind) {
			same_kind = false;
		}
	}
	m_kind->setCurrentIndex(same_kind ? m_kind->findData(int(kind)) : -1);
}

/**
	@brief PipePropertiesWidget::setLiveEdit
	The data of a pipe are always applied at once, in the dock as in the
	dialog of editInDialog()
	@return true
*/
bool PipePropertiesWidget::setLiveEdit(bool live_edit)
{
	Q_UNUSED(live_edit)
	m_live_edit = true;
	return true;
}

/**
	@brief PipePropertiesWidget::title
	@return the title of this editor
*/
QString PipePropertiesWidget::title() const
{
	return tr("Pipe line");
}

/**
	@brief PipePropertiesWidget::editInDialog
	Edit @p pipe in a dialog of its own: what a double click on a conductor
	opens. Each change is applied at once, so there is only a button to close
	it, and undo to go back.
	@param pipe
	@param parent
*/
void PipePropertiesWidget::editInDialog(Conductor *pipe, QWidget *parent)
{
	QDialog dialog(parent);
	auto *widget = new PipePropertiesWidget({pipe}, &dialog);
	dialog.setWindowTitle(widget->title());
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto *layout = new QVBoxLayout(&dialog);
	layout->addWidget(widget);
	layout->addWidget(buttons);
	dialog.exec();
}
