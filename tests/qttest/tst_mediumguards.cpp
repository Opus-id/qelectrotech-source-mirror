// SPDX-License-Identifier: GPL-2.0-or-later
#include "ElementsCollection/elementslocation.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "autoNum/numerotationcontext.h"
#include "autoNum/renumberconductorscommand.h"
#include "bomexport.h"
#include "conductornumexport.h"
#include "dataBase/projectdatabase.h"
#include "diagram.h"
#include "duplicatelabels.h"
#include "factory/elementfactory.h"
#include "factory/ui/listquerywidget.h"
#include "pidcodification.h"
#include "qetapptest.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "qetresult.h"
#include "qettesttimeout.h"
#include "reports/projectreports.h"
#include "wiringlistdb.h"
#include "wiringlistexport.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QUuid>
#include <QtTest>

/**
	The functions of wires that came with upstream after the medium was
	added leave the pipes alone, as every other function of wires does: a
	potential is electrical, and a pipe or a signal line is never
	part of one (Conductor::relatedPotentialConductors()).
	- Renumber conductors gives no pipe a wire number, and the text of a
	  pipe on a folio not chosen keeps no wire number from being given.
	- The duplicate check never calls the pipes of one line duplicates,
	  and never follows a potential through a pipe.
	- potential_view lists no pipe.
	- The electrical lists leave out pipes and signal lines (wiring list,
	  --export-cables, wire numbers, --export-nets, the wire list of the
	  reports and of a table on a folio) and the symbols of a P&ID (the
	  nomenclature, --export-bom, the parts list of the reports and of a
	  table on a folio); a command says how many it left out. qet_wires
	  keeps every line and tells its medium in its last column (9l-bis).
*/
class tst_mediumguards : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;
	int m_run = 0;

		//A symbol 20 x 40, terminals north and south, of @p medium ("" for
		//electrical); of the P&ID family @p family when one is given
	static Element *symbol(Diagram *diagram, QPointF at, const QString &medium = {},
						   const QString &family = {})
	{
		auto *collection = diagram->project()->embeddedElementCollection();
		QString name = medium.isEmpty() ? QStringLiteral("wire") : medium;
		if (!family.isEmpty()) name += QLatin1Char('_') + family;
		const QString file = QStringLiteral("import/%1.elmt").arg(name);
		if (!collection->exist(file)) {
			const QString attr = medium.isEmpty()
					? QString() : QStringLiteral(" medium=\"%1\"").arg(medium);
			const QString family_attr = family.isEmpty()
					? QString() : QStringLiteral(" pid_family=\"%1\"").arg(family);
			QDomDocument definition;
			definition.setContent(QString(
				R"(<definition type="element" link_type="simple" uuid="%1" width="20" height="40" hotspot_x="10" hotspot_y="20"%5>
<names><name lang="en">Test symbol</name></names>
<description><line x1="0" y1="-10" x2="0" y2="10"/>
<terminal x="0" y="-20" orientation="n" uuid="%2"%4/><terminal x="0" y="20" orientation="s" uuid="%3"%4/>
</description></definition>)").arg(QUuid::createUuid().toString(), QUuid::createUuid().toString(),
								   QUuid::createUuid().toString(), attr, family_attr));
			if (!collection->addElementDefinition(QStringLiteral("import"), name,
												  definition.documentElement()))
				return nullptr;
		}
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(
			ElementsLocation(QStringLiteral("embed://") + file, diagram->project()),
			nullptr, &state);
		if (state || !element) return nullptr;
		diagram->addItem(element);
		element->setPos(at);
		return element;
	}

		//A conductor from @p from to @p to, with the text @p text; its medium
		//is the one of @p from. The text of a pipe is its designation: there
		//its line number, written by the model "{line_number}" (step 7b)
	static Conductor *draw(Terminal *from, Terminal *to, const QString &text)
	{
		auto *c = new Conductor(from, to);
		from->diagram()->addItem(c);
		auto p = c->properties();
		if (c->medium() == TerminalData::Pipe) {
			QETProject *project = from->diagram()->project();
			PidCodification codes = project->pidCodification();
			if (codes.tag_models.value(QStringLiteral("line")).isEmpty()) {
				codes.tag_models.insert(QStringLiteral("line"), QStringLiteral("{line_number}"));
				project->setPidCodification(codes);
			}
			p.m_pipe.line_number = text;
		} else {
			p.text = text;
		}
		c->setProperties(p);
		return c;
	}

		//A conductor of its own at @p at, between two symbols of @p medium
	static Conductor *lone(Diagram *d, QPointF at, const QString &text,
						   const QString &medium = {})
	{
		Element *a = symbol(d, at, medium);
		Element *b = symbol(d, at + QPointF(0, 60), medium);
		if (!a || !b) return nullptr;
		return draw(a->terminals().last(), b->terminals().first(), text);
	}

	static void numbering(QETProject &project)
	{
		NumerotationContext rule;
		rule.addValue(QStringLiteral("string"), QStringLiteral("W"));
		rule.addValue(QStringLiteral("unit"), 1, 1, 1);
		project.addConductorAutoNum(QStringLiteral("wires"), rule);
	}

	static QString text(Conductor *c) {return c->properties().text;}

		//The labels of the wire number groups the duplicate check finds
	static QStringList signalGroups(QETProject &project)
	{
		QStringList labels;
		for (const auto &group : DuplicateLabels::find(&project))
			if (group.kind == DuplicateLabels::Kind::Signal)
				labels << group.label;
		labels.sort();
		return labels;
	}

	static int count(QETProject &project, const QString &sql)
	{
		project.dataBase()->updateDB();
		QString error;
		QSqlQuery query = project.dataBase()->newQuery(sql, &error);
		if (!error.isEmpty() || !query.next()) return -1;
		return query.value(0).toInt();
	}

		//How many rows @p sql gives, -1 on an error
	static int rows(QETProject &project, const QString &sql)
	{
		project.dataBase()->updateDB();
		QString error;
		QSqlQuery query = project.dataBase()->newQuery(sql, &error);
		if (!error.isEmpty()) return -1;
		if (!query.isActive() && !query.exec()) return -1;
		int n = 0;
		while (query.next()) ++n;
		return n;
	}

	static void label(Element *element, const QString &text)
	{
		auto info = element->elementInformations();
		info.addValue(QStringLiteral("label"), text);
		info.remove(QStringLiteral("formula"));
		element->setElementInformations(info);
	}

		//One folio of a project with both: a wire W1 between K1 and K2, a
		//pipe L100 between the pump P01 and the tee T1, a signal line S1
		//between two symbols of signal; @p wire, @p pipe and @p signal_line
		//are what it drew
	struct Mixed { Conductor *wire = nullptr, *pipe = nullptr, *signal_line = nullptr; };
	static Mixed mixed(QETProject &project)
	{
		Mixed m;
		Diagram *d = project.addNewDiagram();
		Element *k1 = symbol(d, {100, 100});
		Element *k2 = symbol(d, {100, 200});
		Element *pump = symbol(d, {300, 100}, QStringLiteral("pipe"), QStringLiteral("equipment"));
		Element *tee = symbol(d, {300, 200}, QStringLiteral("pipe"), QStringLiteral("pipe"));
		Element *s1 = symbol(d, {500, 100}, QStringLiteral("signal"));
		Element *s2 = symbol(d, {500, 200}, QStringLiteral("signal"));
		if (!k1 || !k2 || !pump || !tee || !s1 || !s2) return m;
		label(k1, QStringLiteral("K1"));
		label(k2, QStringLiteral("K2"));
		label(pump, QStringLiteral("P01"));
		label(tee, QStringLiteral("T1"));
		m.wire = draw(k1->terminals().last(), k2->terminals().first(), QStringLiteral("W1"));
		m.pipe = draw(pump->terminals().last(), tee->terminals().first(), QStringLiteral("L100"));
		m.signal_line = draw(s1->terminals().last(), s2->terminals().first(), QStringLiteral("S1"));
		return m;
	}

		//The binary on @p args, in a sandbox of its own: its exit code and
		//what it wrote on its standard output
	QPair<int, QString> run(const QStringList &args)
	{
		const QString home = m_dir.filePath(QStringLiteral("home%1").arg(m_run++));
		QDir().mkpath(home);
		QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		env.insert(QStringLiteral("HOME"), home);
		env.insert(QStringLiteral("XDG_CONFIG_HOME"), home + QStringLiteral("/config"));
		env.insert(QStringLiteral("XDG_DATA_HOME"), home + QStringLiteral("/data"));
		QProcess proc;
		proc.setProcessEnvironment(env);
		proc.start(QStringLiteral(QET_TEST_BINARY_PATH), args);
		if (!proc.waitForFinished(QetTest::timeout(120000))) return {-1, QString()};
		return {proc.exitCode(), QString::fromUtf8(proc.readAllStandardOutput())};
	}

private slots:
	void renumberGivesNoPipeAWireNumber()
	{
		QETProject project;
		numbering(project);
		Diagram *chosen = project.addNewDiagram();
		Diagram *other = project.addNewDiagram();
		Conductor *pipe = lone(chosen, {100, 100}, QStringLiteral("L100"), QStringLiteral("pipe"));
		Conductor *wire = lone(chosen, {300, 100}, QStringLiteral("x"));
			//On the folio not chosen, a pipe whose text looks like a wire
			//number: it reserves nothing
		Conductor *far_pipe = lone(other, {100, 100}, QStringLiteral("W1"), QStringLiteral("pipe"));
		QVERIFY(pipe && wire && far_pipe);
		QCOMPARE(pipe->medium(), TerminalData::Pipe);
		QCOMPARE(wire->medium(), TerminalData::Electrical);

		auto plan = RenumberConductorsCommand::plan(&project, QStringLiteral("wires"), {chosen});
		QCOMPARE(plan.potentials, 1);
		for (const auto &change : std::as_const(plan.changes))
			QCOMPARE(change.wire.data(), wire);
		project.undoStack()->push(new RenumberConductorsCommand(
				&project, QStringLiteral("wires"), std::move(plan), QStringLiteral("renumber")));
		QCOMPARE(text(wire), QStringLiteral("W1"));
		QCOMPARE(text(pipe), QStringLiteral("L100"));
		QCOMPARE(text(far_pipe), QStringLiteral("W1"));
	}

	void theDuplicateCheckLeavesPipesAlone()
	{
		QETProject project;
		Diagram *d = project.addNewDiagram();
			//Two pipes of one line: the same number, no duplicate
		QVERIFY(lone(d, {100, 100}, QStringLiteral("L100"), QStringLiteral("pipe")));
		QVERIFY(lone(d, {300, 100}, QStringLiteral("L100"), QStringLiteral("pipe")));
			//Two wires apart with one number: still a duplicate
		QVERIFY(lone(d, {500, 100}, QStringLiteral("W7")));
		QVERIFY(lone(d, {700, 100}, QStringLiteral("W7")));
		QCOMPARE(signalGroups(project), QStringList{QStringLiteral("W7")});
	}

		//Two wires numbered alike, joined only through a pipe drawn to one of
		//them by hand: two potentials, so a duplicate
	void theDuplicateCheckNeverGoesThroughAPipe()
	{
		QETProject project;
		Diagram *d = project.addNewDiagram();
		Element *e1 = symbol(d, {100, 100});
		Element *e2 = symbol(d, {100, 200});
		Element *e3 = symbol(d, {300, 100});
		Element *p = symbol(d, {300, 300}, QStringLiteral("pipe"));
		QVERIFY(e1 && e2 && e3 && p);
		Conductor *w1 = draw(e1->terminals().last(), e2->terminals().first(), QStringLiteral("W8"));
		Conductor *pipe = draw(p->terminals().first(), e1->terminals().last(), QString());
		Conductor *w2 = draw(e3->terminals().last(), p->terminals().first(), QStringLiteral("W8"));
		QCOMPARE(w1->medium(), TerminalData::Electrical);
		QCOMPARE(pipe->medium(), TerminalData::Pipe);
		QCOMPARE(w2->medium(), TerminalData::Electrical);
		QCOMPARE(signalGroups(project), QStringList{QStringLiteral("W8")});
	}

	void potentialViewListsNoPipe()
	{
		QETProject project;
		Diagram *d = project.addNewDiagram();
		Conductor *pipe = lone(d, {100, 100}, QStringLiteral("L100"), QStringLiteral("pipe"));
		Conductor *wire = lone(d, {300, 100}, QStringLiteral("W1"));
		QVERIFY(pipe && wire);
		QCOMPARE(count(project, QStringLiteral("SELECT COUNT(*) FROM potential_view")), 1);
		QCOMPARE(count(project, QStringLiteral("SELECT COUNT(*) FROM potential_member WHERE wire_uuid = '%1'")
					   .arg(pipe->uuid().toString())), 0);
		QCOMPARE(count(project, QStringLiteral("SELECT COUNT(*) FROM potential_member WHERE wire_uuid = '%1'")
					   .arg(wire->uuid().toString())), 1);
	}

		//qet_wires keeps every line, and its last column says which are no
		//wires: empty for a wire, as the file writes none
	void qetWiresTellsTheMedium()
	{
		QETProject project;
		const Mixed m = mixed(project);
		QVERIFY(m.wire && m.pipe && m.signal_line);
		QCOMPARE(m.signal_line->medium(), TerminalData::Signal);
		QCOMPARE(count(project, QStringLiteral("SELECT COUNT(*) FROM qet_wires")), 3);
		const QString medium = QStringLiteral(
			"SELECT COUNT(*) FROM qet_wires WHERE wire_uuid = '%1' AND COALESCE(medium, '') = '%2'");
		QCOMPARE(count(project, medium.arg(m.wire->uuid().toString(), QString())), 1);
		QCOMPARE(count(project, medium.arg(m.pipe->uuid().toString(), QStringLiteral("pipe"))), 1);
		QCOMPARE(count(project, medium.arg(m.signal_line->uuid().toString(), QStringLiteral("signal"))), 1);
	}

		//The wiring list (window and --export-wiring), the CSV of
		//--export-cables, the wire numbers, the wire list of the reports and
		//of a table on a folio: the wire alone
	void wireListsLeaveOtherMediaOut()
	{
		QETProject project;
		const Mixed m = mixed(project);
		QVERIFY(m.wire && m.pipe && m.signal_line);

		int other_media = -1;
		QString error;
		const QVector<WiringListRow> list = wiringListRows(&project, nullptr, &error, &other_media);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(list.size(), 1);
		QCOMPARE(list.first().wire_number, QStringLiteral("W1"));
		QCOMPARE(other_media, 2);

		int csv_other_media = -1;
		const QString csv = WiringListExport(&project).toCsvString(nullptr, &csv_other_media);
		QCOMPARE(csv.trimmed().split(QLatin1Char('\n')).size(), 2);	//its header, the wire
		QCOMPARE(csv_other_media, 2);

		ConductorNumExport numbers(&project);
		const QString nums = numbers.wiresNum();
		QVERIFY2(nums.contains(QStringLiteral("W1")), qPrintable(nums));
		QVERIFY2(!nums.contains(QStringLiteral("L100")) && !nums.contains(QStringLiteral("S1")), qPrintable(nums));
		QCOMPARE(numbers.otherMediaCount(), 2);

		const auto table = ProjectReports::read(&project, ProjectReports::report(QStringLiteral("wires")));
		QVERIFY2(table.error.isEmpty(), qPrintable(table.error));
		QCOMPARE(table.rows.size(), 1);
		QCOMPARE(table.rows.first().first(), QStringLiteral("W1"));

		QCOMPARE(rows(project, ListQueryWidget::queryStr(ListQueryWidget::Wiring)), 1);
	}

		//The nomenclature (window, --export-bom, a nomenclature table), the
		//parts list of the reports and of a table on a folio: K1 and K2,
		//neither the pump nor the tee
	void electricalNomenclatureLeavesPidSymbolsOut()
	{
		QETProject project;
		const Mixed m = mixed(project);
		QVERIFY(m.wire && m.pipe && m.signal_line);

		QCOMPARE(count(project, QStringLiteral("SELECT COUNT(*) FROM element_nomenclature_view "
											   "WHERE label IN ('P01', 'T1')")), 0);
		QCOMPARE(count(project, QStringLiteral("SELECT COUNT(*) FROM element_nomenclature_view "
											   "WHERE label IN ('K1', 'K2')")), 2);
		QCOMPARE(rows(project, BomExport::defaultQuery()), 4);	//K1, K2 and the two of signal

		const auto table = ProjectReports::read(&project, ProjectReports::report(QStringLiteral("parts")));
		QVERIFY2(table.error.isEmpty(), qPrintable(table.error));
		QCOMPARE(table.rows.size(), 2);
		for (const QStringList &row : table.rows)
			QVERIFY2(row.first().endsWith(QStringLiteral("K1")) || row.first().endsWith(QStringLiteral("K2")),
					 qPrintable(row.first()));

		QCOMPARE(rows(project, ListQueryWidget::queryStr(ListQueryWidget::Device)), 2);
			//The views other tools read keep them all
		QCOMPARE(count(project, QStringLiteral("SELECT COUNT(*) FROM qet_device_list "
											   "WHERE label IN ('P01', 'T1')")), 2);
	}

		//The commands write the lists without them and say how many they
		//left out; a net is never a pipe
	void commandsSayWhatTheyLeftOut()
	{
		QVERIFY(m_dir.isValid());
		QVERIFY(QFile::exists(QStringLiteral(QET_TEST_BINARY_PATH)));
		QETProject::setBackupEnabled(false);
		const QString path = m_dir.filePath(QStringLiteral("mixed.qet"));
		{
			QETProject project;
			const Mixed m = mixed(project);
			QVERIFY(m.wire && m.pipe && m.signal_line);
			project.setFilePath(path);
			QVERIFY(project.write().isOk());
		}
		const QString lines = QStringLiteral("2 line(s) of another medium (pipes, signal lines) left out.");

		auto result = run({QStringLiteral("--export-wiring"), path, m_dir.filePath(QStringLiteral("wiring.csv"))});
		QCOMPARE(result.first, 0);
		QVERIFY2(result.second.contains(QStringLiteral("Exported 1 conductor(s)")), qPrintable(result.second));
		QVERIFY2(result.second.contains(lines), qPrintable(result.second));

		result = run({QStringLiteral("--export-cables"), path, m_dir.filePath(QStringLiteral("cables.csv"))});
		QCOMPARE(result.first, 0);
		QVERIFY2(result.second.contains(lines), qPrintable(result.second));

		result = run({QStringLiteral("--export-wires"), path, m_dir.filePath(QStringLiteral("wires.csv"))});
		QCOMPARE(result.first, 0);
		QVERIFY2(result.second.contains(lines), qPrintable(result.second));

		const QString nets = m_dir.filePath(QStringLiteral("nets.json"));
		result = run({QStringLiteral("--export-nets"), path, nets});
		QCOMPARE(result.first, 0);
		QVERIFY2(result.second.contains(lines), qPrintable(result.second));
		QFile file(nets);
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QJsonArray list = QJsonDocument::fromJson(file.readAll()).object()
				.value(QStringLiteral("list")).toArray();
		QCOMPARE(list.size(), 1);
		QCOMPARE(list.first().toObject().value(QStringLiteral("wire_no")).toString(), QStringLiteral("W1"));

		result = run({QStringLiteral("--export-bom"), path, m_dir.filePath(QStringLiteral("bom.csv"))});
		QCOMPARE(result.first, 0);
		QVERIFY2(result.second.contains(QStringLiteral("Exported 4 component(s)")), qPrintable(result.second));
		QVERIFY2(result.second.contains(QStringLiteral("2 P&ID symbol(s) left out.")), qPrintable(result.second));

			//An electrical project: no word of it
		const QString electrical = m_dir.filePath(QStringLiteral("electrical.qet"));
		{
			QETProject project;
			Diagram *d = project.addNewDiagram();
			QVERIFY(lone(d, {100, 100}, QStringLiteral("W1")));
			project.setFilePath(electrical);
			QVERIFY(project.write().isOk());
		}
		result = run({QStringLiteral("--export-wiring"), electrical, m_dir.filePath(QStringLiteral("wiring2.csv"))});
		QCOMPARE(result.first, 0);
		QVERIFY2(!result.second.contains(QStringLiteral("left out")), qPrintable(result.second));
		result = run({QStringLiteral("--export-bom"), electrical, m_dir.filePath(QStringLiteral("bom2.csv"))});
		QCOMPARE(result.first, 0);
		QVERIFY2(!result.second.contains(QStringLiteral("left out")), qPrintable(result.second));
	}
};

QET_APP_TEST_MAIN(tst_mediumguards)
#include "tst_mediumguards.moc"
