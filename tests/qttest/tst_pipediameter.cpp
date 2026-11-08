// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "pidcodification.h"
#include "pipediameter.h"
#include "pipeproperties.h"
#include "qetproject.h"
#include "ui/configpage/pidconfigpage.h"
#include "undocommand/changepidcodificationcommand.h"

#include <QComboBox>
#include <QDomDocument>
#include <QFile>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUndoStack>

Q_DECLARE_METATYPE(PipeDiameter)

// The size of a pipe or a nozzle: a DN, as for a metal pipe or a flange, or
// an outside diameter (Ø) to the tenth, as for a plastic pipe. The kind is
// always kept with the number. Each material of a project has its series of
// sizes and says which kind names its pipes; a table joins outside diameters
// to DN. The values are those of no company.
class tst_pipediameter : public QObject
{
	Q_OBJECT

	static PidCodification::Entry material(const QString &code, bool outside, const QList<int> &series)
	{
		PidCodification::Entry e;
		e.code = code;
		e.name = code;
		e.outside_diameter = outside;
		e.series = series;
		return e;
	}

		// A codification every test of the page starts from
	static PidCodification table()
	{
		PidCodification codification;
		codification.materials = {material(QStringLiteral("M01"), true, {1100, 1250}),
								  material(QStringLiteral("M02"), false, {800, 1000, 1250}),
								  material(QStringLiteral("M03"), true, {889, 1143}),
								  material(QStringLiteral("M04"), false, {})};
		codification.outside_diameters.insert(1100, {100});
		codification.outside_diameters.insert(1250, {100, 125});
		codification.outside_diameters.insert(889, {80});
		codification.outside_diameters.insert(4500, {});
		codification.decimal = PidCodification::Decimal::Point;
		return codification;
	}

	private slots:
			// ---- the value ----
		void aDiameterIsReadFromWhatIsTyped_data()
		{
			QTest::addColumn<QString>("typed");
			QTest::addColumn<int>("bare");
			QTest::addColumn<int>("kind");
			QTest::addColumn<int>("tenths");
			QTest::addColumn<bool>("ok");
			const int dn = PipeDiameter::Nominal, od = PipeDiameter::Outside, none = PipeDiameter::None;
			QTest::newRow("DN with a space") << "DN 100" << od << dn << 1000 << true;
			QTest::newRow("dn stuck")        << "dn100"  << od << dn << 1000 << true;
			QTest::newRow("Ø")               << "Ø 110"  << dn << od << 1100 << true;
			QTest::newRow("ø stuck")         << "ø110"   << dn << od << 1100 << true;
			QTest::newRow("D")               << "D110"   << dn << od << 1100 << true;
			QTest::newRow("comma")           << "Ø 88,9" << dn << od << 889 << true;
			QTest::newRow("point")           << "88.9"   << od << od << 889 << true;
			QTest::newRow("bare as a DN")    << "100"    << dn << dn << 1000 << true;
			QTest::newRow("bare as a Ø")     << "110"    << od << od << 1100 << true;
			QTest::newRow("DN with a tenth") << "DN 88,9" << od << none << 0 << false;
			QTest::newRow("two decimals")    << "88,95"  << od << none << 0 << false;
			QTest::newRow("empty")           << "  "     << dn << none << 0 << true;
			QTest::newRow("zero")            << "0"      << dn << none << 0 << false;
			QTest::newRow("sign alone")      << "Ø"      << dn << none << 0 << false;
			QTest::newRow("text")            << "PN16"   << dn << none << 0 << false;
		}

		void aDiameterIsReadFromWhatIsTyped()
		{
			QFETCH(QString, typed);
			QFETCH(int, bare);
			QFETCH(int, kind);
			QFETCH(int, tenths);
			QFETCH(bool, ok);
			bool read_ok = !ok;
			const PipeDiameter d = PipeDiameter::fromText(typed, PipeDiameter::Kind(bare), &read_ok);
			QCOMPARE(int(d.kind()), kind);
			QCOMPARE(d.tenths(), tenths);
			QCOMPARE(read_ok, ok);
		}

		void aDiameterIsWrittenWithItsKind()
		{
			const QLocale point = QLocale::c();
			const QLocale comma(QLocale::French);
			QCOMPARE(PipeDiameter::nominal(100).text(point), QStringLiteral("DN 100"));
			QCOMPARE(PipeDiameter::outside(110).text(comma), QStringLiteral("Ø 110"));
			QCOMPARE(PipeDiameter::outside(88.9).text(comma), QStringLiteral("Ø 88,9"));
			QCOMPARE(PipeDiameter::outside(88.9).text(point), QStringLiteral("Ø 88.9"));
			QCOMPARE(PipeDiameter::outside(1200).text(comma), QStringLiteral("Ø 1200"));
			QVERIFY(PipeDiameter().text().isEmpty());
				// the same number, not the same diameter
			QVERIFY(PipeDiameter::nominal(110) != PipeDiameter::outside(110));
			QVERIFY(!PipeDiameter::nominal(0).isSet());
			QVERIFY(!PipeDiameter::outside(-5).isSet());
			QVERIFY(!PipeDiameter::fromTenths(PipeDiameter::Nominal, 889).isSet());
				// read back as written, in either language
			for (const PipeDiameter &d : {PipeDiameter::nominal(65), PipeDiameter::outside(63),
										  PipeDiameter::outside(88.9)}) {
				QCOMPARE(PipeDiameter::fromText(d.text(comma)), d);
				QCOMPARE(PipeDiameter::fromText(d.text(point)), d);
			}
		}

		void aDiameterIsAnAttributeOfItsKind()
		{
			QDomDocument doc;
			QDomElement e = doc.createElement(QStringLiteral("nozzle"));
			PipeDiameter::outside(88.9).toXml(e, QStringLiteral("dn"), QStringLiteral("od"));
				// always a point in a file
			QCOMPARE(e.attribute(QStringLiteral("od")), QStringLiteral("88.9"));
			QVERIFY(!e.hasAttribute(QStringLiteral("dn")));
			QCOMPARE(PipeDiameter::fromXml(e, QStringLiteral("dn"), QStringLiteral("od")),
					 PipeDiameter::outside(88.9));
			QDomElement none = doc.createElement(QStringLiteral("nozzle"));
			PipeDiameter().toXml(none, QStringLiteral("dn"), QStringLiteral("od"));
			QVERIFY(none.attributes().isEmpty());
		}

			// ---- the series of the materials ----
		void aPipeTakesTheSeriesOfItsMaterial()
		{
			const PidCodification codification = table();
			QList<PipeDiameter> sizes;
			QCOMPARE(codification.pipeSizes(QStringLiteral("M01"), &sizes), PidCodification::SizeRule::Series);
			QCOMPARE(sizes, (QList<PipeDiameter>{PipeDiameter::outside(110), PipeDiameter::outside(125)}));
			QVERIFY(codification.acceptsForPipe(PipeDiameter::outside(110), QStringLiteral("M01")));
			QVERIFY(!codification.acceptsForPipe(PipeDiameter::outside(90), QStringLiteral("M01")));
			QVERIFY(!codification.acceptsForPipe(PipeDiameter::nominal(100), QStringLiteral("M01")));
			QVERIFY(codification.acceptsForPipe(PipeDiameter::outside(88.9), QStringLiteral("M03")));
			QVERIFY(codification.acceptsForPipe(PipeDiameter(), QStringLiteral("M01")));
				// without series, without material, none
			QCOMPARE(codification.pipeSizes(QStringLiteral("M04"), &sizes), PidCodification::SizeRule::NoSeries);
			QVERIFY(sizes.isEmpty());
			QCOMPARE(codification.pipeSizes(QString(), &sizes), PidCodification::SizeRule::NoMaterial);
			QCOMPARE(codification.pipeSizes(QStringLiteral("M99"), &sizes), PidCodification::SizeRule::NoMaterial);
			QCOMPARE(codification.materialsWithoutSeries(), QStringList{QStringLiteral("M04")});
				// a project which lists no material: the DN of ISO 6708
			QCOMPARE(PidCodification().pipeSizes(QStringLiteral("M01"), &sizes), PidCodification::SizeRule::Iso);
			QCOMPARE(sizes.size(), PipeProperties::preferredDiameters().size());
		}

		void aNozzleTakesTheSeriesOfAllMaterials()
		{
			const QList<PipeDiameter> expected{
				PipeDiameter::nominal(80), PipeDiameter::nominal(100), PipeDiameter::nominal(125),
				PipeDiameter::outside(88.9), PipeDiameter::outside(110), PipeDiameter::outside(114.3),
				PipeDiameter::outside(125)};
				// then the pipe threads, which are fixed
			QCOMPARE(table().nozzleSizes(), expected + PipeDiameter::threadSeries());
			QVERIFY(!table().acceptsForNozzle(PipeDiameter::outside(90)));
				// a project without material: ISO 6708, any outside diameter
			QVERIFY(PidCodification().acceptsForNozzle(PipeDiameter::outside(90)));
			QVERIFY(PidCodification().acceptsForNozzle(PipeDiameter::nominal(100)));
			QVERIFY(!PidCodification().acceptsForNozzle(PipeDiameter::nominal(123)));
		}

		void anOutsideDiameterIsComparedWithItsTable_data()
		{
			QTest::addColumn<PipeDiameter>("a");
			QTest::addColumn<PipeDiameter>("b");
			QTest::addColumn<int>("match");
			const int same = int(PidCodification::DiameterMatch::Same);
			const int different = int(PidCodification::DiameterMatch::Different);
			const int unknown = int(PidCodification::DiameterMatch::Unknown);
			QTest::newRow("two DN equal") << PipeDiameter::nominal(100) << PipeDiameter::nominal(100) << same;
			QTest::newRow("two DN") << PipeDiameter::nominal(100) << PipeDiameter::nominal(80) << different;
			QTest::newRow("two Ø") << PipeDiameter::outside(110) << PipeDiameter::outside(125) << different;
			QTest::newRow("joined") << PipeDiameter::outside(110) << PipeDiameter::nominal(100) << same;
			QTest::newRow("joined, other way") << PipeDiameter::nominal(100) << PipeDiameter::outside(110) << same;
			QTest::newRow("tenth joined") << PipeDiameter::outside(88.9) << PipeDiameter::nominal(80) << same;
			QTest::newRow("one of two") << PipeDiameter::outside(125) << PipeDiameter::nominal(125) << same;
			QTest::newRow("not joined") << PipeDiameter::outside(110) << PipeDiameter::nominal(80) << different;
			QTest::newRow("no DN known") << PipeDiameter::outside(450) << PipeDiameter::nominal(400) << unknown;
			QTest::newRow("not in table") << PipeDiameter::outside(90) << PipeDiameter::nominal(80) << unknown;
			QTest::newRow("none") << PipeDiameter() << PipeDiameter::nominal(80) << same;
		}

		void anOutsideDiameterIsComparedWithItsTable()
		{
			QFETCH(PipeDiameter, a);
			QFETCH(PipeDiameter, b);
			QFETCH(int, match);
			QCOMPARE(int(table().compare(a, b)), match);
		}

		void aSizeIsConvertedThroughTheTable()
		{
			const PidCodification codification = table();
				// to the steel: one DN, or two, within its series
			QCOMPARE(codification.conversions(PipeDiameter::outside(110), QStringLiteral("M02")),
					 QList<PipeDiameter>{PipeDiameter::nominal(100)});
			QCOMPARE(codification.conversions(PipeDiameter::outside(125), QStringLiteral("M02")),
					 (QList<PipeDiameter>{PipeDiameter::nominal(100), PipeDiameter::nominal(125)}));
				// to the plastic: the outside diameters joined to DN 100
			QCOMPARE(codification.conversions(PipeDiameter::nominal(100), QStringLiteral("M01")),
					 (QList<PipeDiameter>{PipeDiameter::outside(110), PipeDiameter::outside(125)}));
				// nothing to convert: in the series, no series, nothing joined
			QVERIFY(codification.conversions(PipeDiameter::outside(110), QStringLiteral("M01")).isEmpty());
			QVERIFY(codification.conversions(PipeDiameter::outside(110), QStringLiteral("M03")).isEmpty());
			QVERIFY(codification.conversions(PipeDiameter::outside(110), QStringLiteral("M04")).isEmpty());
			QVERIFY(codification.conversions(PipeDiameter::outside(450), QStringLiteral("M02")).isEmpty());
				// two outside diameters joined to the same DN: a plastic Ø 110
				// is a stainless Ø 114,3
			PidCodification both = codification;
			both.outside_diameters.insert(1143, {100});
			QCOMPARE(both.conversions(PipeDiameter::outside(110), QStringLiteral("M03")),
					 QList<PipeDiameter>{PipeDiameter::outside(114.3)});
		}

		void theNearestSizeIsFound()
		{
			const QList<PipeDiameter> series{PipeDiameter::outside(90), PipeDiameter::outside(110),
											 PipeDiameter::nominal(100)};
			QCOMPARE(PidCodification::nearest(PipeDiameter::outside(93), series), PipeDiameter::outside(90));
			QCOMPARE(PidCodification::nearest(PipeDiameter::outside(104), series), PipeDiameter::outside(110));
				// as near: the greater
			QCOMPARE(PidCodification::nearest(PipeDiameter::outside(100), series), PipeDiameter::outside(110));
				// of its kind only
			QCOMPARE(PidCodification::nearest(PipeDiameter::nominal(65), series), PipeDiameter::nominal(100));
			QVERIFY(!PidCodification::nearest(PipeDiameter::nominal(65), {PipeDiameter::outside(63)}).isSet());
		}

			// Several files read as one: the codes, then the diameters
		void severalFilesAreReadAsOne()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString codes = dir.filePath(QStringLiteral("codes.csv"));
			const QString table = dir.filePath(QStringLiteral("table.csv"));
			QFile a(codes);
			QVERIFY(a.open(QIODevice::WriteOnly));
			a.write("\xEF\xBB\xBFkind;code;name;family\nmaterial;M01;PE;od\nseries;M01;90/110\n");
			a.close();
			QFile b(table);
			QVERIFY(b.open(QIODevice::WriteOnly));
			a.close();
			b.write("\xEF\xBB\xBFkind;code;name;family\nod;110;100\nod;90;80");
			b.close();
			int skipped = -1;
			const PidCodification read = PidCodification::fromCsv(
						PidCodification::textOfFiles({codes, table}), &skipped);
			QCOMPARE(skipped, 0);
			QCOMPARE(read.materials.size(), 1);
			QCOMPARE(read.outside_diameters.size(), 2);
			QStringList unreadable;
			PidCodification::textOfFiles({dir.filePath(QStringLiteral("none.csv"))}, &unreadable);
			QCOMPARE(unreadable.size(), 1);
		}

		void theCodificationIsSavedWithItsSeries()
		{
			const PidCodification codification = table();
			QDomDocument doc;
			QDomElement project = doc.createElement(QStringLiteral("project"));
			project.appendChild(codification.toXml(doc));
			const QDomElement block = project.firstChildElement(QStringLiteral("pid_codification"));
			const QDomElement first = block.firstChildElement(QStringLiteral("material"));
			QCOMPARE(first.attribute(QStringLiteral("diameter")), QStringLiteral("outside"));
			QCOMPARE(first.attribute(QStringLiteral("series")), QStringLiteral("110/125"));
			QCOMPARE(block.attribute(QStringLiteral("decimal_separator")), QStringLiteral("point"));
			bool tenth = false;
			for (QDomElement e = block.firstChildElement(QStringLiteral("outside_diameter")) ; !e.isNull() ;
				 e = e.nextSiblingElement(QStringLiteral("outside_diameter"))) {
				if (e.attribute(QStringLiteral("value")) == QLatin1String("88.9")) tenth = true;
			}
			QVERIFY(tenth);
			PidCodification back;
			back.fromXml(project);
			QCOMPARE(back, codification);
		}

		void theSeriesAreReadFromAFileOfCodes()
		{
			const PidCodification codification = table();
			int skipped = -1;
			QCOMPARE(PidCodification::fromCsv(codification.toCsv(), &skipped), codification);
			QCOMPARE(skipped, 0);

			const PidCodification read = PidCodification::fromCsv(QStringLiteral(
				"kind;code;name;family\n"
				"series;M01;25/32/40\n"
				"material;M01;PE;Ø\n"
				"material;M02;Steel\n"
				"material;M03;Stainless;od\n"
				"series;M02;50 65 80\n"
				"series;M03;60,3/88,9\n"
				"series;M09;100\n"
				"material;M04;Iron\n"
				"series;M04;88,9\n"
				"od;88,9;80\n"
				"od;125;100/125\n"
				"od;450\n"
				"od;90;80/DN\n"
				"text_option;decimal_separator;comma\n"), &skipped);
				// M09 unknown, a DN with a tenth, a DN not readable
			QCOMPARE(skipped, 3);
			QCOMPARE(read.material(QStringLiteral("M01"))->series, (QList<int>{250, 320, 400}));
			QCOMPARE(read.material(QStringLiteral("M02"))->series, (QList<int>{500, 650, 800}));
			QCOMPARE(read.material(QStringLiteral("M03"))->series, (QList<int>{603, 889}));
			QVERIFY(read.material(QStringLiteral("M04"))->series.isEmpty());
			QVERIFY(read.outsideDiameterFor(QStringLiteral("M01")));
			QVERIFY(read.materials.first().family.isEmpty());
			QCOMPARE(read.outside_diameters.value(889), QList<int>{80});
			QCOMPARE(read.outside_diameters.value(1250), (QList<int>{100, 125}));
			QVERIFY(read.outside_diameters.contains(4500));
			QVERIFY(!read.outside_diameters.contains(900));
			QCOMPARE(read.decimal, PidCodification::Decimal::Comma);
		}

			// A file of codes without table keeps that of the project, and
			// the reverse
		void aFileWithoutTableKeepsThatOfTheProject()
		{
			PidCodification codes;
			codes.materials = {material(QStringLiteral("M09"), false, {1000})};
			const PidCodification merged = codes.completedWith(table());
			QCOMPARE(merged.materials.size(), 1);
			QCOMPARE(merged.outside_diameters, table().outside_diameters);

			PidCodification diameters;
			diameters.outside_diameters.insert(630, {50});
			const PidCodification other = diameters.completedWith(table());
			QCOMPARE(other.materials, table().materials);
			QCOMPARE(other.outside_diameters.size(), 1);
		}

			// ---- the page of the project ----
		void thePageEditsTheSeriesAndTheTable()
		{
			QETProject project;
			project.undoStack()->push(new ChangePidCodificationCommand(&project, table()));
			PidConfigPage page(&project);
			QTableWidget *od = page.outsideDiameterTable();
			QTableWidget *materials = page.codeTable(PidCodification::List::Materials);
			QCOMPARE(od->rowCount(), 4);
			QCOMPARE(materials->item(0, 2)->checkState(), Qt::Checked);
			QCOMPARE(materials->item(0, 3)->text(), QStringLiteral("110/125"));
				// a material without series is in orange
			QVERIFY(materials->item(3, 0)->background() != QBrush());
			QVERIFY(materials->item(0, 0)->background() == QBrush());

				// a series: a tenth for an outside diameter only
			materials->item(1, 3)->setText(QStringLiteral("80/88,9"));
			QVERIFY(!page.codeMessage().isEmpty());
			QCOMPARE(materials->item(1, 3)->text(), QStringLiteral("80/100/125"));
			materials->item(3, 3)->setText(QStringLiteral("150 / 100"));
			QCOMPARE(materials->item(3, 3)->text(), QStringLiteral("100/150"));
			QVERIFY(materials->item(3, 0)->background() == QBrush());

				// an outside diameter to the tenth and the DN it is joined to
			page.addDiameter(od);
			const int row = od->rowCount() - 1;
			od->item(row, 1)->setText(QStringLiteral("100 / 90"));
			od->item(row, 0)->setText(QStringLiteral("114,3"));
			bool found = false;
			for (int r = 0 ; r < od->rowCount() ; ++r) {
				if (od->item(r, 0)->text() == QStringLiteral("114.3")) {
					found = true;
					QCOMPARE(od->item(r, 1)->text(), QStringLiteral("90/100"));
					od->item(r, 1)->setText(QStringLiteral("80, DN"));
					QVERIFY(!page.codeMessage().isEmpty());
					QCOMPARE(od->item(r, 1)->text(), QStringLiteral("90/100"));
				}
			}
			QVERIFY(found);

				// the separator of the project
			auto *decimal = page.findChild<QComboBox *>(QStringLiteral("decimal_separator"));
			decimal->setCurrentIndex(decimal->findData(int(PidCodification::Decimal::Comma)));
			emit decimal->activated(decimal->currentIndex());
			QCOMPARE(materials->item(2, 3)->text(), QStringLiteral("88,9/114,3"));

			page.applyProjectConf();
			const PidCodification &applied = project.pidCodification();
			QCOMPARE(applied.material(QStringLiteral("M04"))->series, (QList<int>{1000, 1500}));
			QCOMPARE(applied.outside_diameters.value(1143), (QList<int>{90, 100}));
			QCOMPARE(applied.decimal, PidCodification::Decimal::Comma);
			project.undoStack()->undo();
			QVERIFY(project.pidCodification().material(QStringLiteral("M04"))->series.isEmpty());
		}

			// A project with codes keeps its separator, whatever the language
			// of who opens it next
		void aProjectWithCodesKeepsItsSeparator()
		{
			QETProject project;
			PidCodification codes = table();
			codes.decimal = PidCodification::Decimal::Language;
			project.undoStack()->push(new ChangePidCodificationCommand(&project, codes));
			QCOMPARE(project.pidCodification().decimal, PidCodification::languageDecimal());
				// a project without codes writes nothing
			QETProject plain;
			PidCodification colours;
			colours.object_colors.insert(QStringLiteral("valve"), QColor(Qt::blue));
			plain.undoStack()->push(new ChangePidCodificationCommand(&plain, colours));
			QCOMPARE(plain.pidCodification().decimal, PidCodification::Decimal::Language);
		}
};

QET_APP_TEST_MAIN(tst_pipediameter)
#include "tst_pipediameter.moc"
