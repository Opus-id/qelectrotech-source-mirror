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

#include "../diagram.h"
#include "../qetgraphicsitem/pipeline.h"
#include "../undocommand/changepipepropertiescommand.h"

#include <QComboBox>
#include <QFormLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>

/**
	@brief PipePropertiesWidget::PipePropertiesWidget
	@param pipes : the pipe lines to edit
	@param parent
*/
PipePropertiesWidget::PipePropertiesWidget(const QList<PipeLine *> &pipes, QWidget *parent) :
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
void PipePropertiesWidget::setPipes(const QList<PipeLine *> &pipes)
{
	for (const QPointer<PipeLine> &pipe : std::as_const(m_pipes)) {
		if (pipe) {
			disconnect(pipe, &PipeLine::propertiesChange, this, &PipePropertiesWidget::updateUi);
		}
	}
	m_pipes.clear();
	for (PipeLine *pipe : pipes) {
		if (pipe) {
			m_pipes << pipe;
				//Undone, redone or changed elsewhere: the fields follow
			connect(pipe, &PipeLine::propertiesChange, this, &PipePropertiesWidget::updateUi);
		}
	}
	updateUi();
}

/**
	@brief PipePropertiesWidget::pipes
	@return the pipe lines edited
*/
QList<PipeLine *> PipePropertiesWidget::pipes() const
{
	QList<PipeLine *> list;
	for (const QPointer<PipeLine> &pipe : m_pipes) {
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

	m_fluid = new QComboBox(this);
	m_fluid->setObjectName(QStringLiteral("fluid"));
	m_fluid->setEditable(true);
	m_fluid->setInsertPolicy(QComboBox::NoInsert);
	m_fluid->addItems(PipeProperties::defaultFluids());
	layout->addRow(tr("Fluid:"), m_fluid);
	auto apply_fluid = [this]() {
		const QString fluid = m_fluid->currentText().trimmed();
		applyChange([fluid](PipeProperties &p) { p.fluid = fluid; });
	};
	connect(m_fluid, &QComboBox::activated, this, apply_fluid);
	connect(m_fluid->lineEdit(), &QLineEdit::editingFinished, this, [this, apply_fluid]() {
		if (m_fluid->lineEdit()->isModified()) {
			m_fluid->lineEdit()->setModified(false);
			apply_fluid();
		}
	});

	QLineEdit *diameter = addTextField(QStringLiteral("nominal_diameter"), tr("Nominal diameter (DN):"),
				 [](const PipeProperties &p) {
					 return p.nominal_diameter > 0 ? QString::number(p.nominal_diameter) : QString();
				 },
				 [](PipeProperties &p, const QString &t) { p.nominal_diameter = qMax(0, t.toInt()); });
	diameter->setValidator(new QIntValidator(0, 100000, diameter));

	addTextField(QStringLiteral("material"), tr("Material:"),
				 [](const PipeProperties &p) { return p.material; },
				 [](PipeProperties &p, const QString &t) { p.material = t; });
	addTextField(QStringLiteral("flow"), tr("Flow:"),
				 [](const PipeProperties &p) { return p.flow; },
				 [](PipeProperties &p, const QString &t) { p.flow = t; });
	addTextField(QStringLiteral("piping_class"), tr("Piping class:"),
				 [](const PipeProperties &p) { return p.piping_class; },
				 [](PipeProperties &p, const QString &t) { p.piping_class = t; });
	addTextField(QStringLiteral("nominal_pressure"), tr("Nominal pressure:"),
				 [](const PipeProperties &p) { return p.nominal_pressure; },
				 [](PipeProperties &p, const QString &t) { p.nominal_pressure = t; });

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
	@brief PipePropertiesWidget::addTextField
	Add a text field which shows @p get of the properties and, once edited,
	does @p set to those of every pipe line edited
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
	Do @p change to the properties of every pipe line edited, in one undo
	step, if it changes anything
	@param change
*/
void PipePropertiesWidget::applyChange(const std::function<void(PipeProperties &)> &change)
{
	auto *undo = new ChangePipePropertiesCommand();
	Diagram *diagram = nullptr;
	for (PipeLine *pipe : pipes())
	{
		PipeProperties properties = pipe->properties();
		change(properties);
		undo->addChange(pipe, properties);
		if (!diagram) {
			diagram = qobject_cast<Diagram *>(pipe->scene());
		}
	}
	if (undo->isEmpty() || !diagram) {
		delete undo;
		return;
	}
	diagram->undoStack().push(undo);
}

/**
	@brief PipePropertiesWidget::showText
	Show in @p edit the value @p get gives for the pipe lines, or nothing
	and "(several values)" when they differ
*/
void PipePropertiesWidget::showText(QLineEdit *edit,
									const std::function<QString(const PipeProperties &)> &get)
{
	const QList<PipeLine *> list = pipes();
	QString value;
	bool same = true;
	for (int i = 0 ; i < list.size() ; ++i)
	{
		const QString v = get(list.at(i)->properties());
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
	Show the properties of the pipe lines edited
*/
void PipePropertiesWidget::updateUi()
{
	const QList<PipeLine *> list = pipes();
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

	const QSignalBlocker blocker(m_kind);
	const PipeProperties::LineKind kind = list.first()->properties().line_kind;
	bool same_kind = true;
	for (PipeLine *pipe : list) {
		if (pipe->properties().line_kind != kind) {
			same_kind = false;
		}
	}
	m_kind->setCurrentIndex(same_kind ? m_kind->findData(int(kind)) : -1);
}

/**
	@brief PipePropertiesWidget::setLiveEdit
	A pipe line has no dialog: its changes are always applied at once
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
