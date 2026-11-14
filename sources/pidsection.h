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
#ifndef PIDSECTION_H
#define PIDSECTION_H

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QString>
#include <QWidget>

/**
	The sections of the P&ID panels: the P&ID tab of a symbol, the page
	of a pipe. A title and a line, never folded: nothing is hidden.
*/
namespace PidSection {

	/// The name of the label of a title, to find it
inline QString titleName() { return QStringLiteral("pid_section_title"); }

/**
	@brief title
	@return a row with @p text in bold and a line up to the right edge,
	child of @p parent; its label is named titleName()
*/
inline QWidget *title(const QString &text, QWidget *parent)
{
	auto *row = new QWidget(parent);
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	auto *label = new QLabel(text, row);
	label->setObjectName(titleName());
	QFont font = label->font();
	font.setBold(true);
	label->setFont(font);
	auto *line = new QFrame(row);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	layout->addWidget(label);
	layout->addWidget(line, 1);
	return row;
}

} // namespace PidSection

#endif // PIDSECTION_H
