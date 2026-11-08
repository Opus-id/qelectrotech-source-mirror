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
#include "changepidcodificationcommand.h"

#include "../diagram.h"
#include "../pidtag.h"
#include "../qetgraphicsitem/conductor.h"
#include "../qetgraphicsitem/element.h"
#include "../qetproject.h"

namespace
{
		/// A valve may have a fluid at the head of its tag (an injection of a
		/// reagent), in the place of a functional unit
	bool isValve(const Element *element)
	{
		return element->elementData().m_pid_family == QLatin1String("valve");
	}

	QString unitOf(const Element *element)
	{
		return element->elementInformations().value(PidTag::unitKey()).toString();
	}

	bool isUnit(const PidCodification &codification, const QString &code)
	{
		for (const PidCodification::Entry &entry : codification.functional_units) {
			if (entry.code == code) {
				return true;
			}
		}
		return false;
	}
}

ChangePidCodificationCommand::ChangePidCodificationCommand(
		QETProject *project,
		const PidCodification &new_codification,
		const CodeChanges &changes,
		QUndoCommand *parent) :
	QUndoCommand(parent),
	m_project(project),
	m_old_codification(project->pidCodification()),
	m_new_codification(new_codification)
{
		//No "&" in this text: the Edit menu would read it as a shortcut
	setText(QObject::tr("Change the colors and the codification of the project"));
		//The codes of a project are always in their order, whatever made them
	m_new_codification.sortCodes();
	if (m_old_codification.text_sizes != m_new_codification.text_sizes) {
		findLabels();
	}
	if (!changes.isEmpty()) {
		findCodeUsers(changes);
	}
	setObsolete(m_old_codification == m_new_codification
				&& m_pipes.isEmpty() && m_elements.isEmpty());
}

/**
	@brief ChangePidCodificationCommand::usersOf
	See the declaration
*/
int ChangePidCodificationCommand::usersOf(QETProject *project, PidCodification::List list,
										  const QString &code)
{
	if (!project || code.isEmpty()) {
		return 0;
	}
	int users = 0;
	for (Diagram *folio : project->diagrams())
	{
		folio->ensureLoaded();
		if (list != PidCodification::List::Units)
		{
			for (const Conductor *pipe : folio->conductors())
			{
				if (pipe->medium() != TerminalData::Pipe) {
					continue;
				}
				const PipeProperties &data = pipe->properties().m_pipe;
				if ((list == PidCodification::List::Fluids ? data.fluid : data.material) == code) {
					++users;
				}
			}
		}
		if (list == PidCodification::List::Materials) {
			continue;
		}
		for (const Element *element : folio->elements()) {
			if (unitOf(element) == code
				&& (list == PidCodification::List::Units || isValve(element))) {
				++users;
			}
		}
	}
	return users;
}

/**
	@brief ChangePidCodificationCommand::findCodeUsers
	Find what bears a code given another, all folios built for that. A
	symbol takes the new code of its functional unit; a valve which has no
	unit of that code takes that of its fluid.
*/
void ChangePidCodificationCommand::findCodeUsers(const CodeChanges &changes)
{
	for (Diagram *folio : m_project->diagrams())
	{
		folio->ensureLoaded();
		for (Conductor *pipe : folio->conductors())
		{
			if (pipe->medium() != TerminalData::Pipe) {
				continue;
			}
			const ConductorProperties old_properties = pipe->properties();
			ConductorProperties new_properties = old_properties;
			PipeProperties &data = new_properties.m_pipe;
			data.fluid = changes.fluids.value(data.fluid, data.fluid);
			data.material = changes.materials.value(data.material, data.material);
			if (!(new_properties == old_properties)) {
				m_pipes << PipeChange{pipe, old_properties, new_properties};
			}
		}
		for (Element *element : folio->elements())
		{
			const QString unit = unitOf(element);
			if (unit.isEmpty()) {
				continue;
			}
			QString new_unit = changes.units.value(unit);
			if (new_unit.isEmpty() && isValve(element) && !isUnit(m_old_codification, unit)) {
				new_unit = changes.fluids.value(unit);
			}
			if (new_unit.isEmpty()) {
				continue;
			}
			const DiagramContext old_information = element->elementInformations();
			DiagramContext new_information = old_information;
			new_information.addValue(PidTag::unitKey(), new_unit);
			m_elements << ElementChange{element, old_information, new_information};
		}
	}
}

/**
	@brief ChangePidCodificationCommand::findLabels
	Find the labels of the project which follow their family: those which
	have the size of their family, all folios built for that.
*/
void ChangePidCodificationCommand::findLabels()
{
	for (Diagram *folio : m_project->diagrams())
	{
		folio->ensureLoaded();
		for (const Element *element : folio->elements())
		{
			const int old_size = element->labelSize(m_old_codification);
			const int new_size = element->labelSize(m_new_codification);
			if (old_size == new_size) {
				continue;
			}
			for (DynamicElementTextItem *text : element->labelTexts()) {
				if (text->font().pointSize() == old_size) {
					m_labels << LabelSize{text, old_size, new_size};
				}
			}
		}
	}
}

void ChangePidCodificationCommand::setSize(DynamicElementTextItem *text, int size)
{
	QFont font = text->font();
	font.setPointSize(size);
	text->setFont(font);
}

void ChangePidCodificationCommand::undo()
{
	for (const PipeChange &change : std::as_const(m_pipes)) {
		if (change.pipe) {
			change.pipe->setProperties(change.old_properties);
		}
	}
	for (const ElementChange &change : std::as_const(m_elements)) {
		if (change.element) {
			change.element->setElementInformations(change.old_information);
		}
	}
		//which gives the labels of the symbols the former units back
	if (m_project) {
		m_project->setPidCodification(m_old_codification);
	}
	for (const LabelSize &label : std::as_const(m_labels)) {
		if (label.text) {
			setSize(label.text, label.old_size);
		}
	}
}

void ChangePidCodificationCommand::redo()
{
	if (m_project) {
		m_project->setPidCodification(m_new_codification);
	}
	for (const PipeChange &change : std::as_const(m_pipes)) {
		if (change.pipe) {
			change.pipe->setProperties(change.new_properties);
		}
	}
	for (const ElementChange &change : std::as_const(m_elements)) {
		if (change.element) {
			change.element->setElementInformations(change.new_information);
			change.element->updatePidLabel();
		}
	}
	for (const LabelSize &label : std::as_const(m_labels)) {
		if (label.text) {
			setSize(label.text, label.new_size);
		}
	}
}
