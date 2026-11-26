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
#include "pidlists.h"

#include "dataBase/projectdatabase.h"
#include "pidcodification.h"
#include "pidtag.h"

#include <QDebug>
#include <QDomDocument>
#include <QPair>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

namespace
{
		/// The information @p name of the placed symbol e, '' for none
	QString information(const QString &name)
	{
		return QStringLiteral("COALESCE((SELECT i.value FROM main.element_information i "
							  "WHERE i.element_uuid = e.uuid AND i.name = '%1' LIMIT 1), '')").arg(name);
	}

		/// The value @p name of the placed symbol e in @p section of
		/// element_value, '' for none
	QString ownValue(const QString &section, const QString &name)
	{
		return QStringLiteral("COALESCE((SELECT v.value FROM main.element_value v "
							  "WHERE v.element_uuid = e.uuid AND v.section = '%1' AND v.name = '%2' LIMIT 1), '')")
				.arg(section, name);
	}

		/// The codification the project database @p db holds: the project
		/// keeps its <pid_codification> block there whole, among the blocks
		/// under the project (QETProject::setPidCodification())
	PidCodification codificationOf(QSqlDatabase db)
	{
		PidCodification codification;
		QSqlQuery query(db);
		query.prepare(QStringLiteral("SELECT content FROM main.foreign_block WHERE owner = :owner ORDER BY ord"));
		query.bindValue(QStringLiteral(":owner"), QString::fromLatin1(projectDataBase::project_owner));
		if (!query.exec()) return codification;
		while (query.next()) {
			const QString block = query.value(0).toString();
			if (!block.startsWith(QLatin1String("<pid_codification"))) continue;
			QDomDocument document;
			if (!document.setContent(QStringLiteral("<project>") + block + QStringLiteral("</project>")))
				continue;
			codification.fromXml(document.documentElement());
			break;
		}
		return codification;
	}
}

QString PidLists::equipmentView()     { return QStringLiteral("qet_pid_equipment"); }
QString PidLists::equipmentDataView() { return QStringLiteral("qet_pid_equipment_data"); }
QString PidLists::codesTable()        { return QStringLiteral("qet_pid_codes"); }

QStringList PidLists::equipmentColumns()
{
	return {QStringLiteral("uuid"), QStringLiteral("label"), QStringLiteral("unit"),
			QStringLiteral("unit_name"), QStringLiteral("train"), QStringLiteral("identifier"),
			QStringLiteral("number"), QStringLiteral("family"), QStringLiteral("role"),
			QStringLiteral("type"), QStringLiteral("description"), QStringLiteral("folio_number"),
			QStringLiteral("folio"), QStringLiteral("position"), QStringLiteral("folio_uuid"),
			QStringLiteral("class")};
}

QStringList PidLists::equipmentDataColumns()
{
	return {QStringLiteral("uuid"), QStringLiteral("name"), QStringLiteral("value")};
}

bool PidLists::namedIn(const QString &sql)
{
	static const QRegularExpression objects(
			QStringLiteral("\\bqet_pid_(equipment|equipment_data|codes)\\b"),
			QRegularExpression::CaseInsensitiveOption);
	return objects.match(sql).hasMatch();
}

bool PidLists::refresh(QSqlDatabase db)
{
	QSqlQuery query(db);
	bool ok = true;
	const auto run = [&](const QString &sql) {
		if (!query.exec(sql)) {
			qDebug() << "PidLists::refresh" << query.lastError();
			ok = false;
		}
	};

	run(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS %1 ("
					   "list TEXT NOT NULL, code TEXT NOT NULL, name TEXT, "
					   "PRIMARY KEY (list, code))").arg(codesTable()));

		//One row per placed symbol of a family that has a tag, once it has
		//one. The inner SELECT reads the fields, the outer one names the
		//unit and chooses the identifier (PidTag::identifier()).
		//type: what follows the last '/' of the symbol's location, without
		//its extension -- RTRIM() takes from its end every character that
		//is no '/', which leaves the folders.
	run(QStringLiteral("DROP VIEW IF EXISTS temp.%1").arg(equipmentView()));
	run(QStringLiteral(
			"CREATE TEMP VIEW %1 AS SELECT "
			"s.uuid AS uuid, s.label AS label, s.unit AS unit, "
			"COALESCE((SELECT c.name FROM temp.%2 c WHERE c.list = 'unit' AND c.code = s.unit), '') AS unit_name, "
			"s.train AS train, "
			"CASE WHEN s.letters <> '' THEN s.letters ELSE s.prefix END AS identifier, "
			"s.number AS number, s.family AS family, s.role AS role, s.type AS type, "
			"s.description AS description, s.folio_number AS folio_number, s.folio AS folio, "
			"s.position AS position, s.folio_uuid AS folio_uuid, s.class AS class "
			"FROM (SELECT e.uuid AS uuid, ei.label AS label, ")
		.arg(equipmentView(), codesTable())
		+ information(PidTag::unitKey()) + QStringLiteral(" AS unit, ")
		+ information(PidTag::trainKey()) + QStringLiteral(" AS train, ")
		+ information(PidTag::lettersKey()) + QStringLiteral(" AS letters, ")
		+ information(PidTag::orderKey()) + QStringLiteral(" AS number, ")
		+ ownValue(QStringLiteral("element"), QStringLiteral("prefix")) + QStringLiteral(" AS prefix, ")
		+ ownValue(QStringLiteral("pid_symbol"), QStringLiteral("family")) + QStringLiteral(" AS family, ")
		+ ownValue(QStringLiteral("pid_symbol"), QStringLiteral("role")) + QStringLiteral(" AS role, ")
		+ ownValue(QStringLiteral("pid_symbol"), QStringLiteral("class")) + QStringLiteral(" AS class, ")
		+ QStringLiteral(
			"COALESCE(REPLACE(REPLACE(e.definition, "
				"RTRIM(e.definition, REPLACE(e.definition, '/', '')), ''), '.elmt', ''), '') AS type, "
			"COALESCE(ei.description, '') AS description, "
			"d.pos AS folio_number, di.folio AS folio, e.pos AS position, e.diagram_uuid AS folio_uuid "
			"FROM main.element e "
			"JOIN main.element_info ei ON ei.element_uuid = e.uuid "
			"LEFT JOIN main.diagram d ON d.uuid = e.diagram_uuid "
			"LEFT JOIN main.diagram_info di ON di.diagram_uuid = e.diagram_uuid) s "
			"WHERE s.family IN ('equipment', 'valve', 'instrument') "
			"AND TRIM(COALESCE(s.label, '')) <> ''"));

		//What each of them holds besides: its "pid_" information but the
		//fields of its tag, which the view above has, then its own choices,
		//the two rows of an <option> (name, value) made one
	run(QStringLiteral("DROP VIEW IF EXISTS temp.%1").arg(equipmentDataView()));
	run(QStringLiteral(
			"CREATE TEMP VIEW %1 AS "
			"SELECT i.element_uuid AS uuid, i.name AS name, i.value AS value "
			"FROM main.element_information i "
			"WHERE i.name LIKE 'pid\\_%' ESCAPE '\\' "
			"AND i.name NOT IN ('%3', '%4', '%5', '%6') "
			"AND TRIM(COALESCE(i.value, '')) <> '' "
			"AND i.element_uuid IN (SELECT uuid FROM temp.%2) "
			"UNION ALL "
			"SELECT n.element_uuid, 'option_' || n.value, v.value "
			"FROM main.element_value n JOIN main.element_value v "
			"ON v.element_uuid = n.element_uuid AND v.section = n.section AND v.ord = n.ord "
			"AND v.name = 'value' "
			"WHERE n.section = 'options/option' AND n.name = 'name' "
			"AND TRIM(COALESCE(v.value, '')) <> '' "
			"AND n.element_uuid IN (SELECT uuid FROM temp.%2)")
		.arg(equipmentDataView(), equipmentView(), PidTag::unitKey(), PidTag::trainKey(),
			 PidTag::orderKey(), PidTag::lettersKey()));

		//The codes, as the project has them now
	const PidCodification codification = codificationOf(db);
	const QList<QPair<QString, QList<PidCodification::Entry>>> lists{
		{QStringLiteral("unit"), codification.functional_units},
		{QStringLiteral("fluid"), codification.fluids},
		{QStringLiteral("material"), codification.materials},
		{QStringLiteral("letters"), codification.instrument_letters}};
	const bool own_transaction = db.transaction();
	run(QStringLiteral("DELETE FROM temp.%1").arg(codesTable()));
	query.prepare(QStringLiteral("INSERT OR IGNORE INTO temp.%1 (list, code, name) "
								 "VALUES (:list, :code, :name)").arg(codesTable()));
	for (const auto &list : lists) {
		for (const PidCodification::Entry &entry : list.second) {
			query.bindValue(QStringLiteral(":list"), list.first);
			query.bindValue(QStringLiteral(":code"), entry.code);
			query.bindValue(QStringLiteral(":name"), entry.name);
			if (!query.exec()) {
				qDebug() << "PidLists::refresh insert" << query.lastError();
				ok = false;
			}
		}
	}
	if (own_transaction && !db.commit()) {
		qDebug() << "PidLists::refresh commit" << db.lastError();
		ok = false;
	}
	return ok;
}
