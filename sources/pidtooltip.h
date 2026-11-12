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
#ifndef PIDTOOLTIP_H
#define PIDTOOLTIP_H

#include <QString>
#include <QStringList>

/**
	The tool tips of the P&ID. Qt cuts a tool tip on several lines only
	when it is rich text: a long sentence otherwise runs off the screen.
*/
namespace PidToolTip {

/**
	@brief wrapped
	@return @p text as a tool tip Qt cuts on several lines: rich text, its
	line breaks kept. A text whose lines are all short stays as it is, so
	that "Flange" is still "Flange"; nothing for no text.
*/
inline QString wrapped(const QString &text)
{
	constexpr int longest = 70;
	const QStringList lines = text.split(QLatin1Char('\n'));
	bool long_line = false;
	for (const QString &line : lines) {
		long_line = long_line || line.size() > longest;
	}
	if (!long_line) {
		return text;
	}
	QStringList escaped;
	for (const QString &line : lines) {
		escaped << line.toHtmlEscaped();
	}
	return QStringLiteral("<p>") + escaped.join(QStringLiteral("<br>")) + QStringLiteral("</p>");
}

} // namespace PidToolTip

#endif // PIDTOOLTIP_H
