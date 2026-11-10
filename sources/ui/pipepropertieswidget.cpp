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

namespace {

	/// The one size the panel shows of the pipe @p p, the one its material
	/// names it by (PipeProperties::writtenSize())
PipeDiameter writtenSize(const PipeProperties &p, const PidCodification &codes)
{
	return p.writtenSize(codes.outsideDiameterFor(p.material));
}

	/// The DN of the pipe @p p: its own; without one, the DN the sizes of
	/// its material give its outside diameter (those of the other materials
	/// when it has none), the one of @p chosen (by outside diameter, in
	/// tenths) among several; else none
PipeDiameter nominalFor(const PipeProperties &p, const PidCodification &codes,
						const QMap<int, PipeDiameter> &chosen)
{
	if (p.nominal.isSet() || !p.outside.isSet()) {
		return p.nominal;
	}
	const QList<PipeDiameter> joined = codes.nominalsOf(p.outside, p.material);
	return joined.size() == 1 ? joined.first() : chosen.value(p.outside.tenths());
}

	/// The DN the pipe @p p has once given the material of code @p code: its
	/// DN (nominalFor()), the size of the line, as this material has it
	/// (PidCodification::nominalIn(): DN 65 becomes DN 60 in a material
	/// which has that one only, when the project says them equal)
PipeDiameter nominalInMaterial(const PipeProperties &p, const QString &code,
							   const PidCodification &codes,
							   const QMap<int, PipeDiameter> &chosen)
{
	return codes.nominalIn(nominalFor(p, codes, chosen), code);
}

	/// Give the pipe @p p the material of code @p code. Its DN, the size of
	/// the line, stays (nominalInMaterial()); its outside diameter is the
	/// one of the new material: none for a material named by its DN or
	/// which the project has not; else the one it had when the new material
	/// has it for this DN, the only one it has for this DN, or the one of
	/// @p outside_of (by DN, in tenths) among several. A pipe which has no
	/// DN and gets none keeps its sizes.
void giveMaterial(PipeProperties &p, const QString &code, const PidCodification &codes,
				  const QMap<int, PipeDiameter> &nominal_of,
				  const QMap<int, PipeDiameter> &outside_of)
{
		//Its DN first: without one, it comes from the material it leaves
	const PipeDiameter dn = nominalInMaterial(p, code, codes, nominal_of);
	p.material = code;
	if (!dn.isSet()) {
		return;
	}
	p.nominal = dn;
	if (!codes.outsideDiameterFor(code)) {
		p.outside = PipeDiameter();
		return;
	}
	const QList<PipeDiameter> outsides = codes.outsidesOf(dn, code);
	if (!outsides.contains(p.outside)) {
		p.outside = outsides.size() == 1 ? outsides.first() : outside_of.value(dn.tenths());
	}
}

}

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
	m_diameter->setToolTip(tr("A DN (DN 100) or an outside diameter (Ø 110): the DN is the size of "
							  "the line, the material chooses which one is written. A plastic pipe "
							  "has both."));
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
	const PidCodification::SizeRule rule = codes.pipeSizes(material, sizes);
		//No material, or one the project has not: a DN, the size of a line
		//whatever its material
	if (rule == PidCodification::SizeRule::NoMaterial && sizes) {
		*sizes = codes.nominalSizes();
	}
	return rule;
}

/**
	@brief PipePropertiesWidget::diameterEdited
	See the declaration. A number alone is of the kind the material names
	its pipes by.
*/
void PipePropertiesWidget::diameterEdited()
{
	const QString text = m_diameter->lineEdit()->text().trimmed();
	const QList<Conductor *> list = pipes();
	QList<PipeDiameter> sizes;
	bool several = false;
	sizeRule(&sizes, &several);
	if (list.isEmpty() || several) {
		updateUi();
		return;
	}
	const PidCodification codes = codification();
	const QLocale locale = codes.locale();
	const QString material = list.first()->properties().m_pipe.material;
	const bool known = codes.material(material);
	const bool by_outside = codes.outsideDiameterFor(material);
		//What all the pipes edited have, none when they differ
	auto shared = [&list](PipeDiameter PipeProperties::*size) {
		const PipeDiameter value = list.first()->properties().m_pipe.*size;
		for (Conductor *pipe : list) {
			if (pipe->properties().m_pipe.*size != value) {
				return PipeDiameter();
			}
		}
		return value;
	};
	auto nearest_note = [&](const PipeDiameter &from, const PipeDiameter &to) {
		return (known ? tr("%1 → %2: the nearest size of the material.")
					  : tr("%1 → %2: the nearest DN the project offers."))
				.arg(from.text(locale), to.text(locale));
	};
	const QString out_of_series = tr("%1 is not a size of the material (P&ID properties of "
									 "the project, Codification): nothing changed.").arg(text);
		//DN 65 typed for a material which has DN 60, said equal
	auto as_the_material_has_it = [&](const PipeDiameter &dn, QStringList &said) {
		const PipeDiameter own = codes.nominalIn(dn, material);
		if (own != dn) {
			said << tr("%1 → %2: the same size of line, as the material %3 has it.")
					.arg(dn.text(locale), own.text(locale), material);
		}
		return own;
	};

	bool ok = false;
	const PipeDiameter typed = PipeDiameter::fromText(
				text, by_outside ? PipeDiameter::Outside : PipeDiameter::Nominal, &ok);
	PipeDiameter nominal, outside;
	QStringList notes;
	QString refusal;
	bool cancelled = false;
	if (!ok || typed.isThread())
	{
		refusal = tr("%1 is not a size a pipe line may have: nothing changed.").arg(text);
	}
	else if (!typed.isSet())
	{
			//Emptied: no size
	}
	else if (by_outside && typed.isOutside())
	{
			//The outside diameter of a plastic pipe, one of its sizes, which
			//gives its DN
		outside = typed;
		if (!sizes.contains(outside)) {
			outside = PidCodification::nearest(typed, sizes);
			if (outside.isSet()) {
				notes << nearest_note(typed, outside);
			} else {
				refusal = out_of_series;
			}
		}
		if (outside.isSet()) {
			const QList<PipeDiameter> joined = codes.nominalsOf(outside, material);
			const PipeDiameter kept = shared(&PipeProperties::nominal);
			if (joined.size() == 1) {
				nominal = joined.first();
			} else if (kept.isSet() && joined.contains(kept)) {
				nominal = kept;
			} else if (joined.size() > 1) {
				nominal = chooseSize(tr("%1 may be joined to several DN. Which one is the size of "
										"the line?").arg(outside.text(locale)),
									 joined, tr("Leave the DN empty"));
			}
		}
	}
	else if (by_outside)
	{
			//The DN of a plastic pipe: its outside diameter is the one the
			//material has for it
		nominal = as_the_material_has_it(typed, notes);
		const QList<PipeDiameter> allowed = codes.lineSizes(material);
		if (!allowed.isEmpty() && !allowed.contains(nominal)) {
			nominal = PidCodification::nearest(typed, allowed);
			notes << tr("%1 → %2: the nearest DN of the sizes of the material.")
					 .arg(typed.text(locale), nominal.text(locale));
		}
		const QList<PipeDiameter> outsides = codes.outsidesOf(nominal, material);
		const PipeDiameter kept = shared(&PipeProperties::outside);
		if (outsides.size() == 1) {
			outside = outsides.first();
		} else if (kept.isSet() && outsides.contains(kept)) {
			outside = kept;
		} else if (outsides.size() > 1) {
			outside = chooseSize(tr("Which outside diameter for %1 in the material %2?")
								 .arg(nominal.text(locale), material),
								 outsides, tr("Leave it empty"));
		}
	}
	else
	{
			//A pipe named by its DN, or without material: a DN only; an
			//outside diameter typed gives the DN the materials have for it
		nominal = typed;
		if (typed.isOutside()) {
			const QList<PipeDiameter> joined = codes.nominalsOf(typed, material);
			nominal = PipeDiameter();
			if (joined.isEmpty()) {
				refusal = tr("%1: no material of the project has a DN for it: nothing changed.")
						  .arg(typed.text(locale));
			} else if (joined.size() == 1) {
				nominal = joined.first();
			} else {
				nominal = chooseSize(tr("%1 may be joined to several DN. Which one is the size of "
										"the line?").arg(typed.text(locale)),
									 joined, tr("Change nothing"));
				cancelled = !nominal.isSet();
			}
			if (nominal.isSet()) {
				notes << tr("%1 → %2: its DN in the sizes of the materials.")
						 .arg(typed.text(locale), nominal.text(locale));
			}
		}
		else {
			nominal = as_the_material_has_it(typed, notes);
		}
		if (nominal.isSet() && !sizes.contains(nominal)) {
			const PipeDiameter near = PidCodification::nearest(nominal, sizes);
			if (near.isSet()) {
				notes << nearest_note(nominal, near);
				nominal = near;
			} else {
				refusal = out_of_series;
			}
		}
	}

	if (!refusal.isEmpty() || cancelled) {
		m_refused = refusal;
	} else {
		m_refused.clear();
			//The pipes continued through valves get the same sizes
		applyChange([nominal, outside](PipeProperties &p) {
			p.nominal = nominal;
			p.outside = outside;
		}, true);
			//Said after the change, which clears what was said
		m_refused = notes.join(QLatin1Char('\n'));
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
	const bool by_outside = codes.outsideDiameterFor(code);
		//What is asked once, whatever the number of pipes: the DN of an
		//outside diameter the materials give several, by outside diameter;
		//the outside diameter of a DN the material has several for, by DN
	QMap<int, PipeDiameter> nominal_of, outside_of;
	for (Conductor *pipe : pipes())
	{
		const PipeProperties p = pipe->properties().m_pipe;
		if (!p.nominal.isSet() && p.outside.isSet() && !nominal_of.contains(p.outside.tenths())) {
			const QList<PipeDiameter> joined = codes.nominalsOf(p.outside, p.material);
			if (joined.size() > 1) {
				nominal_of.insert(p.outside.tenths(),
								  chooseSize(tr("%1 may be joined to several DN. Which one is the "
												"size of the line?").arg(p.outside.text(locale)),
											 joined, tr("Keep it as it is")));
			}
		}
		const PipeDiameter dn = nominalInMaterial(p, code, codes, nominal_of);
		if (!by_outside || !dn.isSet() || outside_of.contains(dn.tenths())) {
			continue;
		}
		const QList<PipeDiameter> outsides = codes.outsidesOf(dn, code);
		if (outsides.size() > 1 && !outsides.contains(p.outside)) {
			outside_of.insert(dn.tenths(),
							  chooseSize(tr("Which outside diameter for %1 in the material %2?")
										 .arg(dn.text(locale), code),
										 outsides, tr("Leave it empty")));
		}
	}
		//What is said of it: a DN found, an outside diameter replaced
	QStringList notes;
	for (Conductor *pipe : pipes())
	{
		const PipeProperties before = pipe->properties().m_pipe;
		PipeProperties after = before;
		giveMaterial(after, code, codes, nominal_of, outside_of);
		QString note;
		if (!before.nominal.isSet() && after.nominal.isSet()) {
			note = tr("%1 → %2: its DN in the sizes of the materials.")
				   .arg(before.outside.text(locale), after.nominal.text(locale));
		} else if (before.nominal.isSet() && after.nominal != before.nominal) {
			note = tr("%1 → %2: the same size of line, as the material %3 has it.")
				   .arg(before.nominal.text(locale), after.nominal.text(locale), code);
		} else if (before.outside.isSet() && after.outside.isSet() && before.outside != after.outside) {
			note = tr("%1 → %2: the outside diameter of %3 in the material %4.")
				   .arg(before.outside.text(locale), after.outside.text(locale),
						after.nominal.text(locale), code);
		}
		if (!note.isEmpty() && !notes.contains(note)) {
			notes << note;
		}
	}
	applyChange([code, codes, nominal_of, outside_of](PipeProperties &p) {
		giveMaterial(p, code, codes, nominal_of, outside_of);
	});
		//Said after the change, which clears what was said
	m_refused = notes.join(QLatin1Char('\n'));
	updateUi();
}

/**
	@brief PipePropertiesWidget::choose
	See the declaration
*/
int PipePropertiesWidget::choose(const QString &question, const QStringList &items,
								 const QString &none)
{
	if (m_chooser) {
		return m_chooser(question, items);
	}
	QStringList offered = items;
	offered << none;
	bool ok = false;
	const QString picked = QInputDialog::getItem(this, title(), question, offered, 0, false, &ok);
	return ok ? int(items.indexOf(picked)) : -1;
}

/**
	@brief PipePropertiesWidget::chooseSize
	See the declaration
*/
PipeDiameter PipePropertiesWidget::chooseSize(const QString &question, const QList<PipeDiameter> &sizes,
											  const QString &none)
{
	const QLocale locale = codification().locale();
	QStringList items;
	for (const PipeDiameter &size : sizes) {
		items << size.text(locale);
	}
	const int index = choose(question, items, none);
	return index >= 0 && index < sizes.size() ? sizes.at(index) : PipeDiameter();
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
		const PipeProperties p = pipe->properties().m_pipe;
		const PidCodification::Entry *material = codes.material(p.material);
		if (material && material->outside_diameter)
		{
				//A pipe named by its outside diameter has both sizes
			if (p.outside.isSet()) {
				if (!codes.acceptsForPipe(p.outside, p.material)) {
					return tr("%1 is not a size of the material %2.")
							.arg(p.outside.text(locale), p.material);
				}
				if (!p.nominal.isSet()) {
					return tr("%1 has no DN: the sizes of the material %2 give it none.")
							.arg(p.outside.text(locale), p.material);
				}
			} else if (p.nominal.isSet()) {
				return codes.outsidesOf(p.nominal, p.material).isEmpty()
						? tr("%1: the material %2 has no outside diameter for this DN.")
						  .arg(p.nominal.text(locale), p.material)
						: tr("%1: the outside diameter of the material %2 is not chosen yet.")
						  .arg(p.nominal.text(locale), p.material);
			}
			continue;
		}
		const PipeDiameter size = p.lineSize();
		if (!size.isSet() || codes.acceptsForPipe(size, p.material)) {
			continue;
		}
		if (material) {
			return size.isOutside()
					? tr("%1: the material %2 names its pipes by their DN.")
					  .arg(size.text(locale), p.material)
					: tr("%1 is not a size of the material %2.")
					  .arg(size.text(locale), p.material);
		}
		if (codes.materials.isEmpty()) {
			return tr("%1 is not a DN of ISO 6708.").arg(size.text(locale));
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

		//The series of the material, or the DN of the project without
		//material; none, greyed, for a material without series or several
		//materials
	QList<PipeDiameter> sizes;
	bool several = false;
	const PidCodification::SizeRule rule = sizeRule(&sizes, &several);
	const QLocale locale = codification().locale();
	DiameterList::fill(m_diameter, sizes, locale);
	m_diameter->setEnabled(!sizes.isEmpty());
	const PidCodification codes = codification();
	showText(m_diameter->lineEdit(), [locale, codes](const PipeProperties &p) {
		return writtenSize(p, codes).text(locale);
	});
		//What was just done or refused, then what is wrong now
	QStringList lines;
	if (!m_refused.isEmpty()) {
		lines << m_refused;
	}
	if (several) {
		lines << tr("Several materials: give the pipes one material to give them a size.");
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
