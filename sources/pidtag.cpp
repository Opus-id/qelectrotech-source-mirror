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
#include "pidtag.h"

#include "diagramcontext.h"

#include <QRegularExpression>
#include <QStringList>

QString PidTag::unitKey()  { return QStringLiteral("pid_unit"); }
QString PidTag::trainKey() { return QStringLiteral("pid_train"); }
QString PidTag::orderKey() { return QStringLiteral("pid_order"); }

bool PidTag::hasFields(const DiagramContext &information)
{
	for (const QString &key : {unitKey(), trainKey(), orderKey()}) {
		if (!information.value(key).toString().trimmed().isEmpty()) {
			return true;
		}
	}
	return false;
}

QString PidTag::label(const QString &model,
					  const QString &unit,
					  const QString &id,
					  const QString &train,
					  const QString &order)
{
	static const QRegularExpression field(QStringLiteral("\\{(unit|id|train|order)(?::(\\d{1,2}))?\\}"));
	static const QRegularExpression spaces(QStringLiteral(" {2,}"));

	QString result;
	int from = 0;
	auto it = field.globalMatch(model);
	while (it.hasNext())
	{
		const QRegularExpressionMatch match = it.next();
		result += model.mid(from, match.capturedStart() - from);
		from = match.capturedEnd();
		const QString name = match.captured(1);
		QString value = name == QLatin1String("unit")  ? unit.trimmed()
					  : name == QLatin1String("id")    ? id.trimmed()
					  : name == QLatin1String("train") ? train.trimmed()
					  : order.trimmed();
		bool number = false;
		value.toInt(&number);
		if (number && !match.captured(2).isEmpty()) {
			value = value.rightJustified(match.captured(2).toInt(), QLatin1Char('0'));
		}
		result += value;
	}
	result += model.mid(from);

	QStringList lines = result.split(QStringLiteral("\\n"));
	for (QString &line : lines) {
		line = line.replace(spaces, QStringLiteral(" ")).trimmed();
	}
	return lines.join(QLatin1Char('\n'));
}
