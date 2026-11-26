// SPDX-License-Identifier: GPL-2.0-or-later
#include "dataBase/projectdatabase.h"
#include "pidlists.h"
#include "qetapptest.h"
#include "qetproject.h"
#include "qettesttimeout.h"
#include "reports/projectreports.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

// The equipment list of a P&ID (step 9l): the placed symbols of the families
// equipment, valve and instrument that have a tag, one row each, read from
// the project database through two TEMP views (PidLists) -- the same rows
// from a .qet, a .qetz and a .qetdb, folios built or not; what each symbol
// holds besides (its "pid_" data, its own choices) one row per value, which
// the list of Project > Reports turns into one column each. Nothing of it is
// ever saved in a file. An electrical symbol, a fitting and a symbol without
// a tag are in none of them.
//
// Runs the real binary (--query, --resave) on a small project written here,
// and reads the same file in the test for the list of the reports.
namespace {

const QString pump_uuid = QStringLiteral("{77777777-7777-4777-8777-777777777701}");
const QString valve_uuid = QStringLiteral("{77777777-7777-4777-8777-777777777702}");
const QString bubble_uuid = QStringLiteral("{77777777-7777-4777-8777-777777777703}");
const QString spare_uuid = QStringLiteral("{77777777-7777-4777-8777-777777777704}");
const QString tee_uuid = QStringLiteral("{77777777-7777-4777-8777-777777777705}");
const QString box_uuid = QStringLiteral("{77777777-7777-4777-8777-777777777706}");

	//A symbol of the project's collection: @p attributes on its definition
	//(its family, its role), @p options the <options> it declares
QString definition(int n, const QString &name, const QString &attributes, const QString &options = QString())
{
	return QStringLiteral(
				"<element name=\"%2.elmt\">"
				"<definition width=\"20\" hotspot_y=\"10\" version=\"0.100.0\" type=\"element\""
				" link_type=\"simple\" hotspot_x=\"10\" height=\"20\"%3>"
				"<uuid uuid=\"{77777777-7777-4777-8777-7777777777a%1}\"/>"
				"<names><name lang=\"en\">%2</name></names>%4"
				"<kindInformations/><informations/>"
				"<description><rect x=\"-5\" y=\"-6\" width=\"10\" height=\"12\" antialias=\"true\""
				" style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
				"</description></definition></element>").arg(n).arg(name, attributes, options);
}

	//An <option> a definition declares, with the values it may take
QString option(const QString &name, const QStringList &values)
{
	QString text = QStringLiteral("<option name=\"%1\" default=\"%2\"><names><name lang=\"en\">%1</name></names>")
				   .arg(name, values.first());
	for (const QString &value : values)
		text += QStringLiteral("<value name=\"%1\"><names><name lang=\"en\">%1</name></names></value>").arg(value);
	return text + QStringLiteral("</option>");
}

	//A placed symbol: @p information is name then value, in pairs
QString placed(const QString &uuid, const QString &type, int x, const QString &prefix,
			   const QStringList &information, const QString &own = QString())
{
	QString text = QStringLiteral("<element type=\"embed://import/%1.elmt\" uuid=\"%2\" x=\"%3\" y=\"100\" z=\"10\""
								  " orientation=\"0\" prefix=\"%4\"><terminals/><inputs/>")
				   .arg(type, uuid).arg(x).arg(prefix);
	if (!information.isEmpty()) {
		text += QStringLiteral("<elementInformations>");
		for (int i = 0 ; i + 1 < information.size() ; i += 2)
			text += QStringLiteral("<elementInformation name=\"%1\" show=\"%2\">%3</elementInformation>")
					.arg(information.at(i), information.at(i) == QLatin1String("label")
						 ? QStringLiteral("1") : QStringLiteral("0"), information.at(i + 1));
		text += QStringLiteral("</elementInformations>");
	}
	return text + own + QStringLiteral("</element>");
}

QString project()
{
	return QStringLiteral(
				"<project title=\"equipment\" version=\"0.100.1\">"
				"<pid_codification><functional_unit code=\"U01\" name=\"Lift station\"/></pid_codification>"
				"<diagram title=\"f\" cols=\"12\" rows=\"4\" colsize=\"60\" rowsize=\"80\">"
				"<elements>")
			//a pump with a tag, data and a choice
		+ placed(pump_uuid, QStringLiteral("pump_centrifugal"), 100, QStringLiteral("PC"),
				 {QStringLiteral("label"), QStringLiteral("U01 PC 101"),
				  QStringLiteral("pid_unit"), QStringLiteral("U01"),
				  QStringLiteral("pid_train"), QStringLiteral("1"),
				  QStringLiteral("pid_order"), QStringLiteral("1"),
				  QStringLiteral("description"), QStringLiteral("Lift pump"),
				  QStringLiteral("pid_power"), QStringLiteral("5.5")},
				 QStringLiteral("<options><option name=\"motor\" value=\"variable\"/></options>"))
			//a valve of a unit the codification does not name
		+ placed(valve_uuid, QStringLiteral("valve_ball"), 200, QStringLiteral("VBO"),
				 {QStringLiteral("label"), QStringLiteral("U02 VBO 201"),
				  QStringLiteral("pid_unit"), QStringLiteral("U02"),
				  QStringLiteral("pid_train"), QStringLiteral("2"),
				  QStringLiteral("pid_order"), QStringLiteral("1")},
				 QStringLiteral("<options><option name=\"command\" value=\"piston_single\"/></options>"))
			//an instrument: its letters are its identifier
		+ placed(bubble_uuid, QStringLiteral("instrument_bubble"), 300, QString(),
				 {QStringLiteral("label"), QStringLiteral("U01 LIT 101"),
				  QStringLiteral("pid_unit"), QStringLiteral("U01"),
				  QStringLiteral("pid_train"), QStringLiteral("1"),
				  QStringLiteral("pid_order"), QStringLiteral("1"),
				  QStringLiteral("pid_letters"), QStringLiteral("LIT")})
			//a pump without a tag, a fitting and an electrical symbol with a
			//label: in no list
		+ placed(spare_uuid, QStringLiteral("pump_centrifugal"), 400, QStringLiteral("PC"),
				 {QStringLiteral("pid_power"), QStringLiteral("11")})
		+ placed(tee_uuid, QStringLiteral("fitting_tee"), 500, QString(),
				 {QStringLiteral("label"), QStringLiteral("T1")})
		+ placed(box_uuid, QStringLiteral("box"), 600, QString(),
				 {QStringLiteral("label"), QStringLiteral("K1")})
		+ QStringLiteral("</elements><conductors/></diagram>"
						 "<collection><category name=\"import\">"
						 "<names><name lang=\"en\">Imported</name></names>")
		+ definition(1, QStringLiteral("pump_centrifugal"),
					 QStringLiteral(" pid_family=\"equipment\" pid_role=\"pump\" pid_class=\"rotating\""),
					 QStringLiteral("<options>")
					 + option(QStringLiteral("motor"), {QStringLiteral("none"), QStringLiteral("variable")})
					 + QStringLiteral("</options>"))
		+ definition(2, QStringLiteral("valve_ball"), QStringLiteral(" pid_family=\"valve\""),
					 QStringLiteral("<options>")
					 + option(QStringLiteral("command"), {QStringLiteral("manual"), QStringLiteral("piston_single")})
					 + QStringLiteral("</options>"))
		+ definition(3, QStringLiteral("instrument_bubble"), QStringLiteral(" pid_family=\"instrument\""))
		+ definition(4, QStringLiteral("fitting_tee"), QStringLiteral(" pid_family=\"pipe\""))
		+ definition(5, QStringLiteral("box"), QString())
		+ QStringLiteral("</category></collection></project>");
}

	//Every column of the list but where the symbol is drawn on its folio
const QString equipment_query = QStringLiteral(
		"SELECT uuid, label, unit, unit_name, train, identifier, number, family, role, type, "
		"description, folio_number, class FROM qet_pid_equipment ORDER BY label");
const QString data_query = QStringLiteral(
		"SELECT uuid, name, value FROM qet_pid_equipment_data ORDER BY uuid, name");

} // namespace

class tst_pidequipmentlist : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;
	int m_run = 0;

	struct Result {
		int code = -1;
		QString out, err;
	};

	Result run(const QStringList &args, const QStringList &set = {})
	{
		const QString home = m_dir.filePath(QStringLiteral("home%1").arg(m_run++));
		QDir().mkpath(home);
		QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		env.insert(QStringLiteral("HOME"), home);
		env.insert(QStringLiteral("XDG_CONFIG_HOME"), home + QStringLiteral("/config"));
		env.insert(QStringLiteral("XDG_DATA_HOME"), home + QStringLiteral("/data"));
		env.remove(QStringLiteral("QET_EAGER_FOLIOS"));
		env.remove(QStringLiteral("QET_QUERY_OPEN_PROJECT"));
		for (const QString &name : set) env.insert(name, QStringLiteral("1"));
		QProcess proc;
		proc.setProcessEnvironment(env);
		proc.start(QStringLiteral(QET_TEST_BINARY_PATH), args);
		Result result;
		if (!proc.waitForFinished(QetTest::timeout(180000))) return result;
		result.code = proc.exitCode();
		result.out = QString::fromUtf8(proc.readAllStandardOutput());
		result.err = QString::fromUtf8(proc.readAllStandardError());
		return result;
	}

	// The rows @p sql gives for @p file, without the project column, in order
	QStringList rowsOf(const QString &sql, const QString &file, const QStringList &set = {})
	{
		const Result r = run({QStringLiteral("--query"), sql, file}, set);
		if (r.code != 0) return {QStringLiteral("failed: ") + r.err.right(400)};
		QStringList rows = r.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts).mid(1);
		for (QString &row : rows) row = row.section(QLatin1Char(','), 1).trimmed();
		return rows;
	}

	QString writeProject()
	{
		const QString in = m_dir.filePath(QStringLiteral("in.qet"));
		if (QFile::exists(in)) return in;
		QFile f(in);
		if (!f.open(QIODevice::WriteOnly)) return QString();
		f.write(project().toUtf8());
		return in;
	}

	static QStringList expectedEquipment()
	{
			//The class last: that of the pump's definition, none for the two others
		return {bubble_uuid + QStringLiteral(",U01 LIT 101,U01,Lift station,1,LIT,1,instrument,,instrument_bubble,,1,"),
				pump_uuid + QStringLiteral(",U01 PC 101,U01,Lift station,1,PC,1,equipment,pump,pump_centrifugal,Lift pump,1,rotating"),
				valve_uuid + QStringLiteral(",U02 VBO 201,U02,,2,VBO,1,valve,,valve_ball,,1,")};
	}

	static QStringList expectedData()
	{
		return {pump_uuid + QStringLiteral(",option_motor,variable"),
				pump_uuid + QStringLiteral(",pid_power,5.5"),
				valve_uuid + QStringLiteral(",option_command,piston_single")};
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		QVERIFY(QFile::exists(QStringLiteral(QET_TEST_BINARY_PATH)));
		QVERIFY(!writeProject().isEmpty());
		QETProject::setBackupEnabled(false);
	}

	// The columns of the two views, in order: renaming or removing one fails
	// here. A column is only ever added (at the end).
	void theColumnsArePinned()
	{
		const QString equipment = QStringLiteral(
				"uuid,label,unit,unit_name,train,identifier,number,family,role,type,description,"
				"folio_number,folio,position,folio_uuid,class");
		QCOMPARE(PidLists::equipmentColumns().join(QLatin1Char(',')), equipment);
		QCOMPARE(PidLists::equipmentDataColumns().join(QLatin1Char(',')), QStringLiteral("uuid,name,value"));
		const Result list = run({QStringLiteral("--query"),
								 QStringLiteral("SELECT * FROM qet_pid_equipment LIMIT 0"), writeProject()});
		QVERIFY2(list.code == 0, qPrintable(list.err.right(400)));
		QCOMPARE(list.out.section(QLatin1Char('\n'), 0, 0).trimmed(), QStringLiteral("project,") + equipment);
		const Result data = run({QStringLiteral("--query"),
								 QStringLiteral("SELECT * FROM qet_pid_equipment_data LIMIT 0"), writeProject()});
		QVERIFY2(data.code == 0, qPrintable(data.err.right(400)));
		QCOMPARE(data.out.section(QLatin1Char('\n'), 0, 0).trimmed(), QStringLiteral("project,uuid,name,value"));
	}

	// From the document alone, no folio built: the three symbols with a tag,
	// the name of the unit from the codification, the letters of the
	// instrument as its identifier; then with the folios built
	void theListFromAQet()
	{
		QCOMPARE(rowsOf(equipment_query, writeProject()), expectedEquipment());
		QCOMPARE(rowsOf(data_query, writeProject()), expectedData());
		QCOMPARE(rowsOf(equipment_query, writeProject(), {QStringLiteral("QET_EAGER_FOLIOS")}), expectedEquipment());
		QCOMPARE(rowsOf(data_query, writeProject(), {QStringLiteral("QET_EAGER_FOLIOS")}), expectedData());
		QCOMPARE(rowsOf(QStringLiteral("SELECT list, code, name FROM qet_pid_codes ORDER BY list, code"), writeProject()),
				 QStringList({QStringLiteral("unit,U01,Lift station")}));
	}

	// The same rows from the two other formats; a .qetdb read itself, then
	// opened as a project
	void theSameListFromAQetzAndAQetdb()
	{
		for (const QString &suffix : {QStringLiteral("qetz"), QStringLiteral("qetdb")}) {
			const QString file = m_dir.filePath(QStringLiteral("out.") + suffix);
			const Result saved = run({QStringLiteral("--resave"), writeProject(), file});
			QVERIFY2(saved.code == 0, qPrintable(suffix + QLatin1Char(' ') + saved.err.right(400)));
			QCOMPARE(rowsOf(equipment_query, file), expectedEquipment());
			QCOMPARE(rowsOf(data_query, file), expectedData());
			QCOMPARE(rowsOf(equipment_query, file, {QStringLiteral("QET_QUERY_OPEN_PROJECT")}), expectedEquipment());
			QCOMPARE(rowsOf(data_query, file, {QStringLiteral("QET_QUERY_OPEN_PROJECT")}), expectedData());
		}
	}

	// The list of Project > Reports: the columns of the view under their own
	// names (an exchange file: never translated), then one column per value
	// the symbols hold, in name order; sorted by unit, train and label
	void theReportHasAColumnPerValue()
	{
		QETProject project(writeProject());
		QCOMPARE(project.state(), QETProject::Ok);
		const ProjectReports::Report report = ProjectReports::report(QStringLiteral("pid_equipment"));
		QCOMPARE(report.source, PidLists::equipmentView());
		const ProjectReports::Table table = ProjectReports::read(&project, report);
		QVERIFY2(table.error.isEmpty(), qPrintable(table.error));
		QStringList headers = PidLists::equipmentColumns();
		headers.removeAll(QStringLiteral("folio_uuid"));
		const int fixed = int(headers.size());
		headers << QStringLiteral("option_command") << QStringLiteral("option_motor") << QStringLiteral("pid_power");
		QCOMPARE(table.headers, headers);
		QCOMPARE(table.rows.size(), 3);
		const int label = int(headers.indexOf(QStringLiteral("label")));
		QCOMPARE(table.rows.at(0).at(label), QStringLiteral("U01 LIT 101"));
		QCOMPARE(table.rows.at(1).at(label), QStringLiteral("U01 PC 101"));
		QCOMPARE(table.rows.at(2).at(label), QStringLiteral("U02 VBO 201"));
		QCOMPARE(table.rows.at(1).at(0), pump_uuid);
		QCOMPARE(table.rows.at(1).mid(fixed), QStringList({QString(), QStringLiteral("variable"), QStringLiteral("5.5")}));
		QCOMPARE(table.rows.at(2).mid(fixed), QStringList({QStringLiteral("piston_single"), QString(), QString()}));
		QCOMPARE(table.rows.at(0).mid(fixed), QStringList({QString(), QString(), QString()}));

			//A place of the electrical structure does not apply to it: the
			//whole list, no error
		const ProjectReports::Table placed = ProjectReports::read(&project, report, {QStringLiteral("P1"), QString()});
		QVERIFY2(placed.error.isEmpty(), qPrintable(placed.error));
		QCOMPARE(placed.rows.size(), 3);

		const QString csv = m_dir.filePath(QStringLiteral("list.csv"));
		QVERIFY(ProjectReports::writeCsv(table, csv));
		QFile f(csv);
		QVERIFY(f.open(QIODevice::ReadOnly));
		const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
		QCOMPARE(lines.size(), 4);
		QCOMPARE(lines.first(), headers.join(QLatin1Char(';')));
		QVERIFY(lines.at(2).startsWith(pump_uuid + QStringLiteral(";U01 PC 101;U01;Lift station;1;PC;1;equipment;pump;"
																  "pump_centrifugal;Lift pump;1;")));
		QVERIFY(lines.at(2).endsWith(QStringLiteral(";;variable;5.5")));
	}

	// Asked for, the views and the table are TEMP: the project database,
	// which a .qetdb is, holds none of them
	void nothingIsKeptInTheDatabase()
	{
		QETProject project(writeProject());
		QCOMPARE(project.state(), QETProject::Ok);
		QSqlQuery list = project.dataBase()->newQuery(QStringLiteral("SELECT COUNT(*) FROM qet_pid_equipment"));
		QVERIFY(list.next());
		QCOMPARE(list.value(0).toInt(), 3);
		const QString count = QStringLiteral("SELECT COUNT(*) FROM %1 WHERE name LIKE 'qet!_pid%' ESCAPE '!'");
		QSqlQuery kept = project.dataBase()->newQuery(count.arg(QStringLiteral("main.sqlite_master")));
		QVERIFY(kept.next());
		QCOMPARE(kept.value(0).toInt(), 0);
		QSqlQuery temporary = project.dataBase()->newQuery(count.arg(QStringLiteral("temp.sqlite_master")));
		QVERIFY(temporary.next());
		QVERIFY(temporary.value(0).toInt() >= 3);

			//and a .qetdb saved by the command line has none either
		const QString file = m_dir.filePath(QStringLiteral("kept.qetdb"));
		const Result saved = run({QStringLiteral("--resave"), writeProject(), file});
		QVERIFY2(saved.code == 0, qPrintable(saved.err.right(400)));
		QCOMPARE(rowsOf(count.arg(QStringLiteral("sqlite_master")), file), QStringList({QStringLiteral("0")}));
	}

	// A change of the codification is in the list at the next read
	void theNameOfTheUnitFollowsTheCodification()
	{
		QETProject project(writeProject());
		QCOMPARE(project.state(), QETProject::Ok);
		PidCodification codification = project.pidCodification();
		QCOMPARE(codification.functional_units.size(), 1);
		codification.functional_units[0].name = QStringLiteral("Pumping");
		project.setPidCodification(codification);
		QSqlQuery names = project.dataBase()->newQuery(QStringLiteral(
				"SELECT DISTINCT unit_name FROM qet_pid_equipment WHERE unit = 'U01'"));
		QVERIFY(names.next());
		QCOMPARE(names.value(0).toString(), QStringLiteral("Pumping"));
	}
};

QET_APP_TEST_MAIN(tst_pidequipmentlist)
#include "tst_pidequipmentlist.moc"
