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
#include "../pidcodification.h"
#include "../pidstatus.h"
#include "../qetgraphicsitem/conductor.h"
#include "../qetproject.h"
#include "diameterlist.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
			disconnect(pipe, &Conductor::propertiesChange, this, nullptr);
		}
	}
	m_pipes.clear();
	m_refused.clear();
	for (Conductor *pipe : pipes) {
		if (pipe) {
			m_pipes << pipe;
				//Undone, redone or changed elsewhere: the fields follow, and
				//a value refused before is no longer what matters
			connect(pipe, &Conductor::propertiesChange, this, [this]() {
				m_refused.clear();
				updateUi();
			});
		}
	}

		//The codification of their project, followed when it changes
	QETProject *project = nullptr;
	if (!m_pipes.isEmpty() && m_pipes.first()->diagram()) {
		project = m_pipes.first()->diagram()->project();
	}
	if (project != m_project)
	{
		if (m_project) {
			disconnect(m_project, &QETProject::pidCodificationChanged, this, nullptr);
		}
		m_project = project;
		if (m_project) {
			connect(m_project, &QETProject::pidCodificationChanged, this, [this]() {
				fillLists();
				updateUi();
			});
		}
	}
	fillLists();
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
						   [](PipeProperties &p, const QString &code) { p.fluid = code; });

		//The material first: it gives the sizes
	m_material = addListField(QStringLiteral("material"), tr("Material:"),
							  [](PipeProperties &p, const QString &code) { p.material = code; },
							  [this](const QString &code) { materialEdited(code); });

	m_diameter = new QComboBox(this);
	m_diameter->setObjectName(QStringLiteral("diameter_list"));
	DiameterList::setUp(m_diameter);
	QLineEdit *diameter_edit = m_diameter->lineEdit();
	diameter_edit->setObjectName(QStringLiteral("diameter"));
	m_diameter->setToolTip(tr("The sizes of the series of the material: DN for a metal pipe, outside "
							  "diameter (Ø 110) for a plastic one."));
	layout->addRow(tr("Diameter:"), m_diameter);
	m_diameter_message = new QLabel(this);
	m_diameter_message->setObjectName(QStringLiteral("diameter_message"));
	m_diameter_message->setWordWrap(true);
	m_diameter_message->setStyleSheet(QStringLiteral("color: #c05000"));
	layout->addRow(QString(), m_diameter_message);
	connect(diameter_edit, &QLineEdit::editingFinished, this, [this, diameter_edit]() {
			//Only once the user changed it: leaving a field which shows
			//nothing, for several values, changes nothing
		if (diameter_edit->isModified()) {
			diameter_edit->setModified(false);
			diameterEdited();
		}
	});
		//Picked in the list: as if typed
	connect(m_diameter, &QComboBox::activated, this, &PipePropertiesWidget::diameterEdited);

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

	m_status = new QComboBox(this);
	m_status->setObjectName(QStringLiteral("status"));
	for (const auto &status : PidStatus::statuses()) {
		m_status->addItem(status.second, status.first);
	}
	layout->addRow(tr("Status of works:"), m_status);
	connect(m_status, &QComboBox::activated, this, [this](int index) {
		const QString status = m_status->itemData(index).toString();
		applyChange([status](PipeProperties &p) { p.status = status; });
	});

	m_color = new QPushButton(this);
	m_color->setObjectName(QStringLiteral("color"));
	auto *automatic = new QPushButton(tr("Automatic"), this);
	automatic->setObjectName(QStringLiteral("automatic_color"));
	auto *color_row = new QHBoxLayout();
	color_row->addWidget(m_color);
	color_row->addWidget(automatic);
	layout->addRow(tr("Color:"), color_row);
	connect(m_color, &QPushButton::clicked, this, [this]() {
		const QList<Conductor *> list = pipes();
		const QColor current = list.isEmpty() ? QColor() : list.first()->drawnColor();
		const QColor color = QColorDialog::getColor(current, this, title());
		if (color.isValid()) {
			setManualColor(color);
		}
	});
	connect(automatic, &QPushButton::clicked, this, [this]() { setManualColor(QColor()); });
}

/**
	@brief PipePropertiesWidget::setManualColor
	See the declaration
*/
void PipePropertiesWidget::setManualColor(const QColor &color)
{
	applyChange([color](PipeProperties &p) { p.color = color; });
}

/**
	@brief PipePropertiesWidget::addListField
	Add a list of the codes of the codification of the project ("W01 - Raw
	water"), in which free text may be typed too; once a code is picked or a
	text typed, does @p set to the properties of every pipe edited with the
	code, or the text when it is none; or @p apply_code when it is given
	@return the list
*/
QComboBox *PipePropertiesWidget::addListField(
		const QString &name, const QString &label,
		const std::function<void(PipeProperties &, const QString &)> &set,
		const std::function<void(const QString &)> &apply_code)
{
	auto *list = new QComboBox(this);
	list->setObjectName(name);
	list->setEditable(true);
	list->setInsertPolicy(QComboBox::NoInsert);
	static_cast<QFormLayout *>(layout())->addRow(label, list);
	auto apply = [this, list, set, apply_code]() {
		const QString code = codeOf(list, list->currentText());
		if (apply_code) {
			apply_code(code);
		} else {
			applyChange([set, code](PipeProperties &p) { set(p, code); });
		}
			//Even when nothing changed: "(None)" picked on an empty field
			//must not stay written in it
		updateUi();
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
	@brief PipePropertiesWidget::codeOf
	@param list : a list made by addListField()
	@param text : a text shown or typed in it
	@return the code of the entry shown as @p text, or of the entry whose code
	is @p text; else @p text itself, trimmed, which is free text
*/
QString PipePropertiesWidget::codeOf(const QComboBox *list, const QString &text)
{
	const QString trimmed = text.trimmed();
	for (int i = 0 ; i < list->count() ; ++i)
	{
		if (list->itemText(i) == trimmed || list->itemData(i).toString() == trimmed) {
			return list->itemData(i).toString();
		}
	}
	return trimmed;
}

PidCodification PipePropertiesWidget::codification() const
{
	return m_project ? m_project->pidCodification() : PidCodification();
}

/**
	@brief PipePropertiesWidget::sizeRule
	See the declaration
*/
PidCodification::SizeRule PipePropertiesWidget::sizeRule(QList<PipeDiameter> *sizes,
														  bool *several) const
{
	const PidCodification codes = codification();
	const QList<Conductor *> list = pipes();
	QString material;
	bool same = true;
	for (int i = 0 ; i < list.size() ; ++i) {
		const QString code = list.at(i)->properties().m_pipe.material;
		if (i == 0) {
			material = code;
		} else if (code != material) {
			same = false;
		}
	}
	if (several) {
		*several = !same && !codes.materials.isEmpty();
	}
	if (!same && !codes.materials.isEmpty()) {
		if (sizes) sizes->clear();
		return PidCodification::SizeRule::NoMaterial;
	}
	return codes.pipeSizes(material, sizes);
}

/**
	@brief PipePropertiesWidget::diameterEdited
	See the declaration. A number alone is of the kind of the series.
*/
void PipePropertiesWidget::diameterEdited()
{
	const QString text = m_diameter->lineEdit()->text().trimmed();
	QList<PipeDiameter> sizes;
	sizeRule(&sizes);
	const PipeDiameter::Kind kind = sizes.isEmpty() ? PipeDiameter::Nominal : sizes.first().kind();
	bool ok = false;
	PipeDiameter diameter = PipeDiameter::fromText(text, kind, &ok);
		//A size the series has not: the nearest one of its kind
	QString note;
	if (ok && diameter.isSet() && !sizes.contains(diameter)) {
		const PipeDiameter near = PidCodification::nearest(diameter, sizes);
		if (near.isSet()) {
			note = tr("%1 → %2: the nearest size of the series of the material.")
				   .arg(text, near.text(codification().locale()));
		}
		diameter = near;
		ok = near.isSet();
	}
	if (!ok) {
		m_refused = tr("%1 is not in the series of the material (P&ID properties of the project, "
					   "Codification): nothing changed.").arg(text);
	} else {
		m_refused.clear();
		applyChange([diameter](PipeProperties &p) { p.diameter = diameter; }, true);
			//Said after the change, which clears what was said
		m_refused = note;
	}
		//Written as the lists write it, or the value back
	updateUi();
}

/**
	@brief PipePropertiesWidget::materialEdited
	See the declaration
*/
void PipePropertiesWidget::materialEdited(const QString &code)
{
	const PidCodification codes = codification();
	const QLocale locale = codes.locale();
		//What a diameter becomes when the table gives several sizes
	QMap<QPair<int, int>, PipeDiameter> chosen;
	for (Conductor *pipe : pipes())
	{
		const PipeDiameter diameter = pipe->properties().m_pipe.diameter;
		const QPair<int, int> key(diameter.kind(), diameter.tenths());
		const QList<PipeDiameter> found = codes.conversions(diameter, code);
		if (found.size() < 2 || chosen.contains(key)) {
			continue;
		}
		QStringList items;
		for (const PipeDiameter &size : found) {
			items << size.text(locale);
		}
		const int index = choose(tr("%1 may become one of these sizes of the material %2. Which one?")
								 .arg(diameter.text(locale), code), items);
		chosen.insert(key, index >= 0 && index < found.size() ? found.at(index) : diameter);
	}
		//What each pipe gets, and what is said of it
	QStringList notes;
	auto convert = [codes, code, chosen, locale, &notes](const PipeDiameter &diameter) {
		QList<PipeDiameter> series;
		if (!diameter.isSet() || codes.pipeSizes(code, &series) != PidCodification::SizeRule::Series
			|| series.contains(diameter)) {
			return diameter;
		}
		const QList<PipeDiameter> found = codes.conversions(diameter, code);
		PipeDiameter result = diameter;
		QString note;
		if (found.size() == 1) {
			result = found.first();
			note = tr("%1 → %2, as the table of the diameters joins them.");
		} else if (found.size() > 1) {
			result = chosen.value(qMakePair(int(diameter.kind()), diameter.tenths()), diameter);
			note = result != diameter ? tr("%1 → %2, as chosen.") : QString();
		} else if (const PipeDiameter near = PidCodification::nearest(diameter, series) ; near.isSet()) {
			result = near;
			note = tr("%1 → %2: the nearest size of the series of the material.");
		}
		if (result == diameter) {
			note = tr("%1 kept: no size of the series of the material matches it.")
				   .arg(diameter.text(locale));
		} else {
			note = note.arg(diameter.text(locale), result.text(locale));
		}
		if (!notes.contains(note)) {
			notes << note;
		}
		return result;
	};
	QMap<QPair<int, int>, PipeDiameter> results;
	for (Conductor *pipe : pipes()) {
		const PipeDiameter diameter = pipe->properties().m_pipe.diameter;
		results.insert(qMakePair(int(diameter.kind()), diameter.tenths()), convert(diameter));
	}
	applyChange([code, results](PipeProperties &p) {
		p.material = code;
		p.diameter = results.value(qMakePair(int(p.diameter.kind()), p.diameter.tenths()), p.diameter);
	});
		//Said after the change, which clears what was said
	m_refused = notes.join(QLatin1Char('\n'));
	updateUi();
}

/**
	@brief PipePropertiesWidget::choose
	See the declaration
*/
int PipePropertiesWidget::choose(const QString &question, const QStringList &items)
{
	if (m_chooser) {
		return m_chooser(question, items);
	}
	QStringList offered = items;
	offered << tr("Keep it as it is");
	bool ok = false;
	const QString picked = QInputDialog::getItem(this, title(), question, offered, 0, false, &ok);
	return ok ? int(items.indexOf(picked)) : -1;
}

/**
	@brief PipePropertiesWidget::diameterWarning
	See the declaration
*/
QString PipePropertiesWidget::diameterWarning() const
{
	const PidCodification codes = codification();
	const QLocale locale = codes.locale();
	for (Conductor *pipe : pipes())
	{
		const PipeProperties &p = pipe->properties().m_pipe;
		if (!p.diameter.isSet() || codes.acceptsForPipe(p.diameter, p.material)) {
			continue;
		}
		const PidCodification::Entry *material = codes.material(p.material);
		if (material && material->outside_diameter != p.diameter.isOutside()) {
			return material->outside_diameter
					? tr("%1: the material %2 names its pipes by their outside diameter.")
					  .arg(p.diameter.text(locale), p.material)
					: tr("%1: the material %2 names its pipes by their DN.")
					  .arg(p.diameter.text(locale), p.material);
		}
		if (material) {
			return tr("%1 is not in the series of the material %2.").arg(p.diameter.text(locale), p.material);
		}
		if (codes.materials.isEmpty()) {
			return tr("%1 is not a DN of ISO 6708.").arg(p.diameter.text(locale));
		}
	}
	return QString();
}

/**
	@brief PipePropertiesWidget::diameterMessage
	See the declaration
*/
QString PipePropertiesWidget::diameterMessage() const
{
	return m_diameter_message->text();
}

/**
	@brief PipePropertiesWidget::fillLists
	Offer in the lists of fluids and materials "(None)", to clear the field,
	then the codification of the project; without one, the default fluids as
	free text, and no material
*/
void PipePropertiesWidget::fillLists()
{
	const PidCodification codification = m_project ? m_project->pidCodification()
												   : PidCodification();
	auto fill = [this](QComboBox *list, const QList<PidCodification::Entry> &entries,
					   const QStringList &fallback) {
		const QSignalBlocker blocker(list);
		list->clear();
		list->addItem(tr("(None)"), QString());
		for (const PidCodification::Entry &entry : entries) {
			list->addItem(entry.name.isEmpty() ? entry.code
											   : entry.code + QStringLiteral(" – ") + entry.name,
						  entry.code);
		}
		if (entries.isEmpty()) {
			for (const QString &text : fallback) {
				list->addItem(text, text);
			}
		}
	};
	fill(m_fluid, codification.fluids, PipeProperties::defaultFluids());
	fill(m_material, codification.materials, QStringList());
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
	bindTextField(edit, get, set);
	return edit;
}

/**
	@brief PipePropertiesWidget::bindTextField
	Make @p edit show @p get of the properties and, once edited, do @p set to
	those of every pipe edited
*/
void PipePropertiesWidget::bindTextField(
		QLineEdit *edit,
		const std::function<QString(const PipeProperties &)> &get,
		const std::function<void(PipeProperties &, const QString &)> &set,
		bool through_valves)
{
	m_text_fields.append({edit, get});
		//Only once the user changed it: leaving a field which shows
		//nothing, for several values, changes nothing
	connect(edit, &QLineEdit::editingFinished, this, [this, edit, set, through_valves]() {
		if (!edit->isModified()) {
			return;
		}
		edit->setModified(false);
		const QString text = edit->text().trimmed();
		applyChange([set, text](PipeProperties &p) { set(p, text); }, through_valves);
	});
}

/**
	@brief PipePropertiesWidget::applyChange
	Do @p change to the data of every pipe edited, in one undo step, if it
	changes anything
	@param change
	@param through_valves : also to the pipes they continue through valves,
	see Conductor::inlineRun()
*/
void PipePropertiesWidget::applyChange(const std::function<void(PipeProperties &)> &change,
									   bool through_valves)
{
	QList<Conductor *> changed = pipes();
	if (through_valves) {
		for (Conductor *pipe : pipes()) {
			for (Conductor *next : pipe->inlineRun()) {
				if (!changed.contains(next)) {
					changed << next;
				}
			}
		}
	}
	auto *undo = new QUndoCommand(tr("Change the properties of a pipe line"));
	Diagram *diagram = nullptr;
	for (Conductor *pipe : changed)
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
		//A code shown with its name, as the list offers it; no code, nothing
	auto shown = [](const QComboBox *list, const QString &code) {
		if (code.isEmpty()) {
			return QString();
		}
		const int index = list->findData(code);
		return index >= 0 ? list->itemText(index) : code;
	};
	showText(m_fluid->lineEdit(), [this, shown](const PipeProperties &p) { return shown(m_fluid, p.fluid); });
	showText(m_material->lineEdit(), [this, shown](const PipeProperties &p) { return shown(m_material, p.material); });

		//The series of the material; none, greyed, without one
	QList<PipeDiameter> sizes;
	bool several = false;
	const PidCodification::SizeRule rule = sizeRule(&sizes, &several);
	const QLocale locale = codification().locale();
	DiameterList::fill(m_diameter, sizes, locale);
	m_diameter->setEnabled(!sizes.isEmpty());
	showText(m_diameter->lineEdit(), [locale](const PipeProperties &p) { return p.diameter.text(locale); });
		//What was just done or refused, then what is wrong now
	QStringList lines;
	if (!m_refused.isEmpty()) {
		lines << m_refused;
	}
	if (several) {
		lines << tr("Several materials: give the pipes one material to give them a size.");
	} else if (rule == PidCodification::SizeRule::NoMaterial) {
		lines << tr("Choose first a material of the codification of the project: it gives the sizes.");
	} else if (rule == PidCodification::SizeRule::NoSeries) {
		lines << tr("The material %1 has no series (P&ID properties of the project, Codification).")
				 .arg(list.first()->properties().m_pipe.material);
	}
	const QString warning = diameterWarning();
	if (!warning.isEmpty()) {
		lines << warning;
	}
	const QString message = lines.join(QLatin1Char('\n'));
	m_diameter_message->setText(message);
	m_diameter_message->setVisible(!message.isEmpty());

	const QSignalBlocker blocker(m_kind);
	const PipeProperties::LineKind kind = list.first()->properties().m_pipe.line_kind;
	bool same_kind = true;
	for (Conductor *pipe : list) {
		if (pipe->properties().m_pipe.line_kind != kind) {
			same_kind = false;
		}
	}
	m_kind->setCurrentIndex(same_kind ? m_kind->findData(int(kind)) : -1);

	const QSignalBlocker status_blocker(m_status);
	const QString status = list.first()->properties().m_pipe.status;
	bool same_status = true;
	for (Conductor *pipe : list) {
		if (pipe->properties().m_pipe.status != status) {
			same_status = false;
		}
	}
	m_status->setCurrentIndex(-1);
	for (int i = 0 ; same_status && i < m_status->count() ; ++i) {
		if (m_status->itemData(i).toString() == status) {
			m_status->setCurrentIndex(i);
		}
	}

	const QColor color = list.first()->properties().m_pipe.color;
	bool same_color = true;
	for (Conductor *pipe : list) {
		if (pipe->properties().m_pipe.color != color) {
			same_color = false;
		}
	}
	m_color->setText(!same_color ? tr("(several values)")
					 : color.isValid() ? color.name() : tr("Automatic"));
	m_color->setStyleSheet(same_color && color.isValid()
						   ? QStringLiteral("background-color: %1; color: %2")
							 .arg(color.name(), color.lightness() < 128 ? QStringLiteral("white")
																		 : QStringLiteral("black"))
						   : QString());
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
