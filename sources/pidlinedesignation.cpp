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
#include "pidlinedesignation.h"

#include "pidcodification.h"
#include "pipeproperties.h"

#include <QRegularExpression>
#include <QStringList>

QString PidLineDesignation::modelKey()
{
	return QStringLiteral("line");
}

QString PidLineDesignation::text(const QString &model,
								 const PipeProperties &pipe,
								 const PidCodification &codification)
{
	if (model.trimmed().isEmpty()) {
		return QString();
	}
	static const QRegularExpression field(QStringLiteral(
			"\\{(fluid|line_number|material|piping_class|pn|flow|size|dn)\\}"));
	static const QRegularExpression spaces(QStringLiteral(" {2,}"));

		//The size as the drawing writes it: the material chooses
	const PipeDiameter written = pipe.writtenSize(codification.outsideDiameterFor(pipe.material));
	const QString size = pipe.hasSize() ? codification.text(written, pipe.material)
										: QStringLiteral("DN ?");
		//The DN alone, as the size writes it ("60/65" for equal DN)
	QString dn = QStringLiteral("?");
	if (pipe.nominal.isSet()) {
		dn = codification.text(pipe.nominal, pipe.material);
		const QString prefix = QStringLiteral("DN ");
		if (dn.startsWith(prefix)) {
			dn = dn.mid(prefix.size());
		}
	}

	QString result;
	int from = 0;
	auto it = field.globalMatch(model);
	while (it.hasNext())
	{
		const QRegularExpressionMatch match = it.next();
		result += model.mid(from, match.capturedStart() - from);
		from = match.capturedEnd();
		const QString name = match.captured(1);
		const QString value =
				name == QLatin1String("fluid")        ? pipe.fluid
			  : name == QLatin1String("line_number")  ? pipe.line_number
			  : name == QLatin1String("material")     ? pipe.material
			  : name == QLatin1String("piping_class") ? pipe.piping_class
			  : name == QLatin1String("pn")           ? pipe.nominal_pressure
			  : name == QLatin1String("flow")         ? pipe.flow
			  : name == QLatin1String("size")         ? size
			  : dn;
		result += value.trimmed();
	}
	result += model.mid(from);

	QStringList lines = result.split(QStringLiteral("\\n"));
	for (QString &line : lines) {
		line = line.replace(spaces, QStringLiteral(" ")).trimmed();
	}
	return lines.join(QLatin1Char('\n'));
}
