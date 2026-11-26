// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include "qettesttimeout.h"

// What was chosen on a placed P&ID symbol (the values of its options, the
// sizes of its nozzles) is in the project database, in the rows of
// element_value, with the family and the role its definition gives it:
// the same rows from the document alone, from built folios and from a
// .qetdb, so a .qetdb keeps the choices and a list can tell a pump from a
// valve. An electrical symbol has none of these rows.
//
// Runs the real binary (--query, --resave) on a small project written here.
namespace {

const QString pump_uuid = QStringLiteral("{66666666-6666-4666-8666-666666666661}");
const QString box_uuid = QStringLiteral("{66666666-6666-4666-8666-666666666662}");

QString project()
{
	const QString style = QStringLiteral("line-style:normal;line-weight:normal;filling:none;color:black");
	return QStringLiteral(
				"<project title=\"choices\" version=\"0.100.1\">"
				"<diagram title=\"f\" cols=\"8\" rows=\"4\" colsize=\"60\" rowsize=\"80\">"
				"<elements>"
				"<element type=\"embed://import/pump.elmt\" uuid=\"%1\" x=\"100\" y=\"100\" z=\"10\""
				" orientation=\"0\"><terminals/><inputs/>"
				"<options><option name=\"motor\" value=\"variable\"/></options>"
				"<pid_nozzles same_size=\"false\"><nozzle dn=\"100\" name=\"N1\"/></pid_nozzles>"
				"</element>"
				"<element type=\"embed://import/box.elmt\" uuid=\"%2\" x=\"300\" y=\"100\" z=\"10\""
				" orientation=\"0\"><terminals/><inputs/></element>"
				"</elements><conductors/></diagram>"
				"<collection><category name=\"import\">"
				"<names><name lang=\"en\">Imported</name></names>"
				"<element name=\"pump.elmt\">"
				"<definition width=\"60\" hotspot_y=\"20\" version=\"0.100.0\" type=\"element\""
				" link_type=\"simple\" hotspot_x=\"30\" height=\"40\" pid_family=\"equipment\" pid_role=\"pump\">"
				"<uuid uuid=\"{66666666-6666-4666-8666-66666666aaaa}\"/>"
				"<names><name lang=\"en\">pump</name></names>"
				"<options><option name=\"motor\" default=\"none\">"
				"<names><name lang=\"en\">Motor</name></names>"
				"<value name=\"none\"><names><name lang=\"en\">No motor</name></names></value>"
				"<value name=\"variable\"><names><name lang=\"en\">Variable speed</name></names></value>"
				"</option></options>"
				"<kindInformations/><informations/>"
				"<description>"
				"<line x1=\"-20\" y1=\"0\" x2=\"20\" y2=\"0\" end1=\"none\" end2=\"none\" length1=\"1.5\""
				" length2=\"1.5\" antialias=\"false\" style=\"%3\"/>"
				"<terminal uuid=\"{66666666-6666-4666-8666-66666666bbb1}\" name=\"N1\" x=\"-20\" y=\"0\""
				" orientation=\"w\" type=\"Generic\" medium=\"pipe\" pid_connection=\"plain_end\" pid_dn=\"80\""
				" show_name=\"true\"/>"
				"<terminal uuid=\"{66666666-6666-4666-8666-66666666bbb2}\" name=\"N2\" x=\"20\" y=\"0\""
				" orientation=\"e\" type=\"Generic\" medium=\"pipe\" pid_connection=\"plain_end\" pid_dn=\"80\""
				" show_name=\"true\"/>"
				"</description></definition></element>"
				"<element name=\"box.elmt\">"
				"<definition width=\"30\" hotspot_y=\"21\" version=\"0.100.0\" type=\"element\""
				" link_type=\"simple\" hotspot_x=\"15\" height=\"42\">"
				"<uuid uuid=\"{66666666-6666-4666-8666-66666666cccc}\"/>"
				"<names><name lang=\"en\">box</name></names>"
				"<kindInformations/><informations/>"
				"<description><rect x=\"-5\" y=\"-6\" width=\"10\" height=\"12\" antialias=\"true\""
				" style=\"%3\"/>"
				"</description></definition></element></category></collection>"
				"</project>").arg(pump_uuid, box_uuid, style);
}

	//Every row of element_value but those every symbol has (section element)
const QString rows_query = QStringLiteral(
		"SELECT element_uuid, section, ord, name, value FROM element_value "
		"WHERE section <> 'element' ORDER BY element_uuid, rowid");

} // namespace

class tst_symbolchoicerows : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;
	int m_run = 0;

	struct Result {
		int code = -1;
		QString out, err;
	};

	Result run(const QStringList &args, bool eager = false)
	{
		const QString home = m_dir.filePath(QStringLiteral("home%1").arg(m_run++));
		QDir().mkpath(home);
		QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		env.insert(QStringLiteral("HOME"), home);
		env.insert(QStringLiteral("XDG_CONFIG_HOME"), home + QStringLiteral("/config"));
		env.insert(QStringLiteral("XDG_DATA_HOME"), home + QStringLiteral("/data"));
		env.remove(QStringLiteral("QET_EAGER_FOLIOS"));
		if (eager) env.insert(QStringLiteral("QET_EAGER_FOLIOS"), QStringLiteral("1"));
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

	// The rows the query gives for @p file, without the project column, sorted
	QStringList rowsOf(const QString &file, bool eager = false)
	{
		const Result r = run({QStringLiteral("--query"), rows_query, file}, eager);
		if (r.code != 0) return {QStringLiteral("failed: ") + r.err.right(400)};
		QStringList rows = r.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts).mid(1);
		for (QString &row : rows) row = row.section(QLatin1Char(','), 1).trimmed();
		rows.sort();
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

	static QStringList expectedRows()
	{
		QStringList rows{
			pump_uuid + QStringLiteral(",options/option,0,name,motor"),
			pump_uuid + QStringLiteral(",options/option,0,value,variable"),
			pump_uuid + QStringLiteral(",pid_nozzles,0,same_size,false"),
			pump_uuid + QStringLiteral(",pid_nozzles/nozzle,0,dn,100"),
			pump_uuid + QStringLiteral(",pid_nozzles/nozzle,0,name,N1"),
			pump_uuid + QStringLiteral(",pid_symbol,0,family,equipment"),
			pump_uuid + QStringLiteral(",pid_symbol,0,role,pump")};
		rows.sort();
		return rows;
	}

	// The placed <element> of uuid @p uuid in the project file @p path
	static QDomElement placed(QDomDocument &document, const QString &path, const QString &uuid)
	{
		QFile f(path);
		if (!f.open(QIODevice::ReadOnly) || !document.setContent(&f)) return QDomElement();
		const QDomNodeList elements = document.elementsByTagName(QStringLiteral("element"));
		for (int i = 0 ; i < elements.size() ; ++i)
			if (elements.at(i).toElement().attribute(QStringLiteral("uuid")) == uuid)
				return elements.at(i).toElement();
		return QDomElement();
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		QVERIFY(QFile::exists(QStringLiteral(QET_TEST_BINARY_PATH)));
		QVERIFY(!writeProject().isEmpty());
	}

	// From the document alone, no folio built: the choices as the file
	// holds them, the family and the role from the definition; nothing for
	// the electrical symbol
	void theChoicesAreRowsFromTheDocument()
	{
		QCOMPARE(rowsOf(writeProject()), expectedRows());
	}

	// From the built symbols: the same rows
	void theChoicesAreTheSameRowsOnceBuilt()
	{
		QCOMPARE(rowsOf(writeProject(), true), expectedRows());
	}

	// A .qetdb is the project database: it keeps the choices (it used to
	// lose them), and gives them back in a .qet, which never holds the
	// family nor the role
	void aQetdbKeepsTheChoices()
	{
		const QString qetdb = m_dir.filePath(QStringLiteral("out.qetdb"));
		const QString back = m_dir.filePath(QStringLiteral("back.qet"));
		const Result saved = run({QStringLiteral("--resave"), writeProject(), qetdb});
		QVERIFY2(saved.code == 0, qPrintable(saved.err.right(400)));
			// read itself, then opened as a project, lazily and built
		QCOMPARE(rowsOf(qetdb), expectedRows());
		QCOMPARE(rowsOf(qetdb, true), expectedRows());

		const Result again = run({QStringLiteral("--resave"), qetdb, back});
		QVERIFY2(again.code == 0, qPrintable(again.err.right(400)));
		QDomDocument document;
		const QDomElement pump = placed(document, back, pump_uuid);
		QVERIFY(!pump.isNull());
		const QDomElement option = pump.firstChildElement(QStringLiteral("options"))
								   .firstChildElement(QStringLiteral("option"));
		QCOMPARE(option.attribute(QStringLiteral("name")), QStringLiteral("motor"));
		QCOMPARE(option.attribute(QStringLiteral("value")), QStringLiteral("variable"));
		const QDomElement nozzles = pump.firstChildElement(QStringLiteral("pid_nozzles"));
		QCOMPARE(nozzles.attribute(QStringLiteral("same_size")), QStringLiteral("false"));
		const QDomElement nozzle = nozzles.firstChildElement(QStringLiteral("nozzle"));
		QCOMPARE(nozzle.attribute(QStringLiteral("name")), QStringLiteral("N1"));
		QCOMPARE(nozzle.attribute(QStringLiteral("dn")), QStringLiteral("100"));
			// the rows of the family and the role are for the lists only
		QVERIFY(pump.firstChildElement(QStringLiteral("pid_symbol")).isNull());
		QFile f(back);
		QVERIFY(f.open(QIODevice::ReadOnly));
		QVERIFY(!f.readAll().contains("pid_symbol"));

			// the electrical symbol: written as it was, no block of its own
		QDomDocument other;
		const QDomElement box = placed(other, back, box_uuid);
		QVERIFY(!box.isNull());
		QVERIFY(box.firstChildElement(QStringLiteral("options")).isNull());
		QVERIFY(box.firstChildElement(QStringLiteral("pid_nozzles")).isNull());
			// and the .qet written back gives the same rows again
		QCOMPARE(rowsOf(back), expectedRows());
	}
};

QTEST_GUILESS_MAIN(tst_symbolchoicerows)
#include "tst_symbolchoicerows.moc"
