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
#include "../placerename.h"
#include "../qetgraphicsitem/conductor.h"
#include "../qetgraphicsitem/element.h"
#include "../qetproject.h"

namespace
{
	QString unitOf(const Element *element)
	{
		return element->elementInformations().value(PidTag::unitKey()).toString();
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
		//A project with codes keeps its decimal separator, whatever the
		//language of who opens it next
	if (m_new_codification.hasCodes()
		&& m_new_codification.decimal == PidCodification::Decimal::Language) {
		m_new_codification.decimal = PidCodification::languageDecimal();
	}
	if (m_old_codification.text_sizes != m_new_codification.text_sizes) {
		findLabels();
	}
	if (!changes.isEmpty()) {
		findCodeUsers(changes);
	}
	setObsolete(m_old_codification == m_new_codification
				&& m_pipes.isEmpty() && m_renames.empty());
}

ChangePidCodificationCommand::~ChangePidCodificationCommand() = default;

/**
	@brief ChangePidCodificationCommand::usersOf
	See the declaration
*/
int ChangePidCodificationCommand::usersOf(QETProject *project, PidCodification::List list,
										  const QString &code)
{
		//The letters of the instruments are a help to type: no tag bears them
	if (!project || code.isEmpty() || list == PidCodification::List::Letters) {
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
		if (list != PidCodification::List::Units) {
			continue;
		}
		for (const Element *element : folio->elements()) {
			if (unitOf(element) == code) {
				++users;
			}
		}
	}
	return users;
}

/**
	@brief ChangePidCodificationCommand::findCodeUsers
	Find what bears a code given another, all folios built for that: the
	pipe lines of a fluid or a material. A functional unit is a plant of
	QElectroTech: it is renamed as the window of plants and locations does
	(PlaceRename), the title blocks, the structure boxes, the symbols and
	the wire counters which hold it, the labels of the symbols following.
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
	}
		//Each rename read before any is done: U01 and U02 may swap
	for (auto it = changes.units.cbegin() ; it != changes.units.cend() ; ++it)
	{
		PlaceRename::Request request;
		request.plant = it.key();
		request.new_name = it.value();
		if (QUndoCommand *rename = PlaceRename::command(m_project, request)) {
			m_renames.emplace_back(rename);
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
	for (auto it = m_renames.rbegin() ; it != m_renames.rend() ; ++it) {
		(*it)->undo();
	}
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
		//The symbols renamed write their labels again (Element::setElementInformations())
	for (const std::unique_ptr<QUndoCommand> &rename : m_renames) {
		rename->redo();
	}
	for (const LabelSize &label : std::as_const(m_labels)) {
		if (label.text) {
			setSize(label.text, label.new_size);
		}
	}
}
