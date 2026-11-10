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
// always kept with the number. Each material of a project has its sizes, an
// outside diameter and its DN each, and says which names its pipes; the
// project says which DN are the same size of line. The values are those of no
// company.
class tst_pipediameter : public QObject
{
	Q_OBJECT

		// A material with its sizes: outside diameter in tenths, then DN
	static PidCodification::Entry material(const QString &code, bool outside,
										   const QList<PidCodification::Size> &sizes)
	{
		PidCodification::Entry e;
		e.code = code;
		e.name = code;
		e.outside_diameter = outside;
		for (const PidCodification::Size &size : sizes) {
			e.addSize(size.outside, size.nominal);
		}
		return e;
	}

		// A codification every test of the page starts from: two plastics
		// which do not give Ø 125 the same DN, a steel, a stainless steel
		// named by its outside diameter, a material without sizes, and a
		// cast iron, the only one with DN 60
	static PidCodification table()
	{
		PidCodification codification;
		codification.materials = {
			material(QStringLiteral("M01"), true, {{1100, 100}, {1250, 100}, {4500, 0}}),
			material(QStringLiteral("M02"), false, {{0, 80}, {0, 100}, {0, 125}}),
			material(QStringLiteral("M03"), true, {{889, 80}, {1143, 100}}),
			material(QStringLiteral("M04"), false, {}),
			material(QStringLiteral("M05"), true, {{750, 65}, {1250, 125}}),
			material(QStringLiteral("M06"), false, {{0, 60}, {0, 80}, {0, 100}})};
		codification.addEqualNominals(65, 60);
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

			// ---- the sizes of the materials ----
		void aMaterialKeepsItsSizesInOrder()
		{
			PidCodification::Entry e;
			QVERIFY(e.addSize(1250, 100));
			QVERIFY(e.addSize(1100, 100));
			QVERIFY(e.addSize(0, 150));
				// neither; an outside diameter has one DN; a DN alone once
			QVERIFY(!e.addSize(0, 0));
			QVERIFY(!e.addSize(1250, 125));
			QVERIFY(!e.addSize(0, 150));
			QVERIFY(!e.addSize(-5, 100));
				// a DN may have several outside diameters, or one more with one
			QVERIFY(e.addSize(1600, 150));
			QVERIFY(e.addSize(900, 0));
			QCOMPARE(e.sizes, (QList<PidCodification::Size>{{900, 0}, {1100, 100}, {1250, 100},
															 {0, 150}, {1600, 150}}));
				// what names its pipes: its outside diameters, or its DN
			e.outside_diameter = true;
			QCOMPARE(PidCodification::sizesOf(e).size(), 4);
			e.outside_diameter = false;
			QCOMPARE(PidCodification::sizesOf(e),
					 (QList<PipeDiameter>{PipeDiameter::nominal(100), PipeDiameter::nominal(150)}));
		}

		void aPipeTakesTheSizesOfItsMaterial()
		{
			const PidCodification codification = table();
			QList<PipeDiameter> sizes;
			QCOMPARE(codification.pipeSizes(QStringLiteral("M01"), &sizes), PidCodification::SizeRule::Series);
			QCOMPARE(sizes, (QList<PipeDiameter>{PipeDiameter::outside(110), PipeDiameter::outside(125),
												 PipeDiameter::outside(450)}));
			QVERIFY(codification.acceptsForPipe(PipeDiameter::outside(110), QStringLiteral("M01")));
			QVERIFY(!codification.acceptsForPipe(PipeDiameter::outside(90), QStringLiteral("M01")));
			QVERIFY(!codification.acceptsForPipe(PipeDiameter::nominal(100), QStringLiteral("M01")));
			QVERIFY(codification.acceptsForPipe(PipeDiameter::outside(88.9), QStringLiteral("M03")));
			QVERIFY(codification.acceptsForPipe(PipeDiameter(), QStringLiteral("M01")));
				// without sizes, without material, none
			QCOMPARE(codification.pipeSizes(QStringLiteral("M04"), &sizes), PidCodification::SizeRule::NoSeries);
			QVERIFY(sizes.isEmpty());
			QCOMPARE(codification.pipeSizes(QString(), &sizes), PidCodification::SizeRule::NoMaterial);
			QCOMPARE(codification.pipeSizes(QStringLiteral("M99"), &sizes), PidCodification::SizeRule::NoMaterial);
			QCOMPARE(codification.materialsWithoutSizes(), QStringList{QStringLiteral("M04")});
				// a project which lists no material: the DN of ISO 6708
			QCOMPARE(PidCodification().pipeSizes(QStringLiteral("M01"), &sizes), PidCodification::SizeRule::Iso);
			QCOMPARE(sizes.size(), PipeProperties::preferredDiameters().size());
		}

		void aNozzleTakesTheSizesOfAllMaterials()
		{
				// the DN of all, a flange having the DN of its line; the
				// outside diameters of the materials named by them
			const QList<PipeDiameter> expected{
				PipeDiameter::nominal(60), PipeDiameter::nominal(65), PipeDiameter::nominal(80),
				PipeDiameter::nominal(100), PipeDiameter::nominal(125),
				PipeDiameter::outside(75), PipeDiameter::outside(88.9), PipeDiameter::outside(110),
				PipeDiameter::outside(114.3), PipeDiameter::outside(125), PipeDiameter::outside(450)};
				// then the pipe threads, which are fixed
			QCOMPARE(table().nozzleSizes(), expected + PipeDiameter::threadSeries());
			QVERIFY(!table().acceptsForNozzle(PipeDiameter::outside(90)));
				// a project without material: ISO 6708, any outside diameter
			QVERIFY(PidCodification().acceptsForNozzle(PipeDiameter::outside(90)));
			QVERIFY(PidCodification().acceptsForNozzle(PipeDiameter::nominal(100)));
			QVERIFY(!PidCodification().acceptsForNozzle(PipeDiameter::nominal(123)));
		}

		void anOutsideDiameterIsComparedByItsDn_data()
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
				// no material comes with them: two plastics do not agree on Ø 125
			QTest::newRow("one of two") << PipeDiameter::outside(125) << PipeDiameter::nominal(125) << unknown;
			QTest::newRow("the other of two") << PipeDiameter::outside(125) << PipeDiameter::nominal(100) << unknown;
			QTest::newRow("neither of two") << PipeDiameter::outside(125) << PipeDiameter::nominal(80) << different;
			QTest::newRow("not joined") << PipeDiameter::outside(110) << PipeDiameter::nominal(80) << different;
			QTest::newRow("no DN known") << PipeDiameter::outside(450) << PipeDiameter::nominal(400) << unknown;
			QTest::newRow("in no material") << PipeDiameter::outside(90) << PipeDiameter::nominal(80) << unknown;
			QTest::newRow("none") << PipeDiameter() << PipeDiameter::nominal(80) << same;
				// DN 60 and DN 65, which the project says equal
			QTest::newRow("equal DN") << PipeDiameter::nominal(60) << PipeDiameter::nominal(65) << same;
			QTest::newRow("equal DN, other way") << PipeDiameter::nominal(65) << PipeDiameter::nominal(60) << same;
			QTest::newRow("Ø of an equal DN") << PipeDiameter::outside(75) << PipeDiameter::nominal(60) << same;
			QTest::newRow("not said equal") << PipeDiameter::nominal(100) << PipeDiameter::nominal(125) << different;
		}

		void anOutsideDiameterIsComparedByItsDn()
		{
			QFETCH(PipeDiameter, a);
			QFETCH(PipeDiameter, b);
			QFETCH(int, match);
			QCOMPARE(int(table().compare(a, b)), match);
		}

			// The DN is the size of a line whatever its material: each
			// material gives the DN of its outside diameters
		void theDnOfALineGoesWithAnyMaterial()
		{
			const PidCodification codification = table();
			const PipeDiameter dn60 = PipeDiameter::nominal(60), dn65 = PipeDiameter::nominal(65),
					dn80 = PipeDiameter::nominal(80), dn100 = PipeDiameter::nominal(100),
					dn125 = PipeDiameter::nominal(125);
			const QString pe = QStringLiteral("M01"), steel = QStringLiteral("M02"),
					pvc = QStringLiteral("M05"), iron = QStringLiteral("M06");
				// the DN of an outside diameter in its material: one
			QCOMPARE(codification.nominalsOf(PipeDiameter::outside(125), pe), QList<PipeDiameter>{dn100});
			QCOMPARE(codification.nominalsOf(PipeDiameter::outside(125), pvc), QList<PipeDiameter>{dn125});
			QCOMPARE(codification.nominalOf(PipeDiameter::outside(125), pe), dn100);
				// without material, or in one which has not this outside
				// diameter: what the others say, one, several, none
			QCOMPARE(codification.nominalsOf(PipeDiameter::outside(110)), QList<PipeDiameter>{dn100});
			QCOMPARE(codification.nominalsOf(PipeDiameter::outside(125)),
					 (QList<PipeDiameter>{dn100, dn125}));
			QCOMPARE(codification.nominalsOf(PipeDiameter::outside(125), steel),
					 (QList<PipeDiameter>{dn100, dn125}));
			QVERIFY(!codification.nominalOf(PipeDiameter::outside(125)).isSet());
			QVERIFY(codification.nominalsOf(PipeDiameter::outside(450)).isEmpty());
			QVERIFY(codification.nominalsOf(PipeDiameter::outside(90)).isEmpty());
			QVERIFY(codification.nominalsOf(dn100).isEmpty());
				// as the material has it: DN 65 is DN 60 in cast iron
			QCOMPARE(codification.nominalsOf(PipeDiameter::outside(75), iron), QList<PipeDiameter>{dn60});
			QCOMPARE(codification.nominalsOf(PipeDiameter::outside(75), steel), QList<PipeDiameter>{dn65});
				// without material: the DN of all the materials
			QCOMPARE(codification.nominalSizes(), (QList<PipeDiameter>{dn60, dn65, dn80, dn100, dn125}));
			QCOMPARE(codification.lineSizes(QString()), codification.nominalSizes());
			QCOMPARE(codification.lineSizes(QStringLiteral("M99")), codification.nominalSizes());
				// those of ISO 6708 when the project has none
			PidCodification plastics;
			plastics.materials = {material(pe, true, {{1100, 0}, {1250, 0}})};
			QCOMPARE(plastics.nominalSizes().size(), PipeProperties::preferredDiameters().size());
			QCOMPARE(PidCodification().nominalSizes().size(), PipeProperties::preferredDiameters().size());
				// of a material: the DN of its sizes
			QCOMPARE(codification.lineSizes(steel), (QList<PipeDiameter>{dn80, dn100, dn125}));
			QCOMPARE(codification.lineSizes(pe), QList<PipeDiameter>{dn100});
			QCOMPARE(codification.lineSizes(QStringLiteral("M03")), (QList<PipeDiameter>{dn80, dn100}));
			QCOMPARE(codification.lineSizes(iron), (QList<PipeDiameter>{dn60, dn80, dn100}));
			QVERIFY(codification.lineSizes(QStringLiteral("M04")).isEmpty());
				// the outside diameters of a DN: several, one, none for a
				// material named by its DN
			QCOMPARE(codification.outsidesOf(dn100, pe),
					 (QList<PipeDiameter>{PipeDiameter::outside(110), PipeDiameter::outside(125)}));
			QCOMPARE(codification.outsidesOf(dn100, QStringLiteral("M03")),
					 QList<PipeDiameter>{PipeDiameter::outside(114.3)});
			QVERIFY(codification.outsidesOf(dn100, steel).isEmpty());
			QVERIFY(codification.outsidesOf(dn80, pe).isEmpty());
		}

			// DN 60 exists for cast iron only, where the others have DN 65:
			// the project says them the same size of line
		void equalDnAreTheSameSizeOfLine()
		{
			PidCodification codification = table();
			const PipeDiameter dn60 = PipeDiameter::nominal(60), dn65 = PipeDiameter::nominal(65);
			QVERIFY(codification.sameNominal(60, 65));
			QVERIFY(codification.sameNominal(65, 60));
			QVERIFY(codification.sameNominal(80, 80));
			QVERIFY(!codification.sameNominal(80, 100));
				// said once, of two DN
			QVERIFY(!codification.addEqualNominals(60, 65));
			QVERIFY(!codification.addEqualNominals(80, 80));
			QVERIFY(!codification.addEqualNominals(0, 80));
			QVERIFY(codification.addEqualNominals(125, 100));
			QCOMPARE(codification.equal_nominals,
					 (QList<QPair<int, int>>{{60, 65}, {100, 125}}));
				// a line keeps its DN in a material which has it, else takes
				// the equal one the material has
			QCOMPARE(codification.nominalIn(dn65, QStringLiteral("M06")), dn60);
			QCOMPARE(codification.nominalIn(dn60, QStringLiteral("M05")), dn65);
			QCOMPARE(codification.nominalIn(dn60, QStringLiteral("M06")), dn60);
				// neither in the material, no material, no size: as it is
			QCOMPARE(codification.nominalIn(dn60, QStringLiteral("M03")), dn60);
			QCOMPARE(codification.nominalIn(dn65, QString()), dn65);
			QCOMPARE(codification.nominalIn(dn65, QStringLiteral("M04")), dn65);
				// nothing is equal without the project saying so
			QVERIFY(!PidCodification().sameNominal(60, 65));
			QVERIFY(PidCodification().equal_nominals.isEmpty());
			PidCodification alone;
			alone.addEqualNominals(60, 65);
			QVERIFY(alone.hasCodes());
			QVERIFY(!alone.isEmpty());
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

			// Several files read as one: the codes, then the sizes
		void severalFilesAreReadAsOne()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString codes = dir.filePath(QStringLiteral("codes.csv"));
			const QString table = dir.filePath(QStringLiteral("table.csv"));
			QFile a(codes);
			QVERIFY(a.open(QIODevice::WriteOnly));
			a.write("\xEF\xBB\xBFkind;code;name;family\nmaterial;M01;PE;od\n");
			a.close();
			QFile b(table);
			QVERIFY(b.open(QIODevice::WriteOnly));
			a.close();
			b.write("\xEF\xBB\xBFkind;code;name;family\nsize;M01;110;100\nsize;M01;90;80\ndn_equal;60;65");
			b.close();
			int skipped = -1;
			const PidCodification read = PidCodification::fromCsv(
						PidCodification::textOfFiles({codes, table}), &skipped);
			QCOMPARE(skipped, 0);
			QCOMPARE(read.materials.size(), 1);
			QCOMPARE(read.materials.first().sizes,
					 (QList<PidCodification::Size>{{900, 80}, {1100, 100}}));
			QCOMPARE(read.equal_nominals.size(), 1);
				// the sizes alone have no material to go to
			QVERIFY(PidCodification::fromCsv(PidCodification::textOfFiles({table}), &skipped)
					.materials.isEmpty());
			QCOMPARE(skipped, 2);
			QStringList unreadable;
			PidCodification::textOfFiles({dir.filePath(QStringLiteral("none.csv"))}, &unreadable);
			QCOMPARE(unreadable.size(), 1);
		}

		void theCodificationIsSavedWithItsSizes()
		{
			const PidCodification codification = table();
			QDomDocument doc;
			QDomElement project = doc.createElement(QStringLiteral("project"));
			project.appendChild(codification.toXml(doc));
			const QDomElement block = project.firstChildElement(QStringLiteral("pid_codification"));
			const QDomElement first = block.firstChildElement(QStringLiteral("material"));
			QCOMPARE(first.attribute(QStringLiteral("diameter")), QStringLiteral("outside"));
			QVERIFY(!first.hasAttribute(QStringLiteral("series")));
			QCOMPARE(block.attribute(QStringLiteral("decimal_separator")), QStringLiteral("point"));
				// a size by element, with what is known of it only
			const QDomNodeList sizes = first.elementsByTagName(QStringLiteral("size"));
			QCOMPARE(sizes.size(), 3);
			QCOMPARE(sizes.at(0).toElement().attribute(QStringLiteral("od")), QStringLiteral("110"));
			QCOMPARE(sizes.at(0).toElement().attribute(QStringLiteral("dn")), QStringLiteral("100"));
			QVERIFY(!sizes.at(2).toElement().hasAttribute(QStringLiteral("dn")));
			const QDomElement steel = first.nextSiblingElement(QStringLiteral("material"));
			QVERIFY(!steel.firstChildElement(QStringLiteral("size")).hasAttribute(QStringLiteral("od")));
				// always a point in a file
			const QDomElement stainless = steel.nextSiblingElement(QStringLiteral("material"));
			QCOMPARE(stainless.firstChildElement(QStringLiteral("size")).attribute(QStringLiteral("od")),
					 QStringLiteral("88.9"));
			QVERIFY(block.firstChildElement(QStringLiteral("outside_diameter")).isNull());
			const QDomElement equal = block.firstChildElement(QStringLiteral("equal_dn"));
			QCOMPARE(equal.attribute(QStringLiteral("a")), QStringLiteral("60"));
			QCOMPARE(equal.attribute(QStringLiteral("b")), QStringLiteral("65"));
			PidCodification back;
			back.fromXml(project);
			QCOMPARE(back, codification);
				// a project without sizes nor equal DN writes none
			PidCodification plain;
			plain.materials = {material(QStringLiteral("M01"), false, {})};
			QDomElement other = doc.createElement(QStringLiteral("project"));
			other.appendChild(plain.toXml(doc));
			QVERIFY(other.elementsByTagName(QStringLiteral("size")).isEmpty());
			QVERIFY(other.elementsByTagName(QStringLiteral("equal_dn")).isEmpty());
		}

		void theSizesAreReadFromAFileOfCodes()
		{
			const PidCodification codification = table();
			int skipped = -1;
			QCOMPARE(PidCodification::fromCsv(codification.toCsv(), &skipped), codification);
			QCOMPARE(skipped, 0);

			const PidCodification read = PidCodification::fromCsv(QStringLiteral(
				"kind;code;name;family\n"
				"size;M01;32;25\n"
				"material;M01;PE;Ø\n"
				"material;M02;Steel\n"
				"material;M03;Stainless;od\n"
				"size;M01;25;20\n"
				"size;M02;;50\n"
				"size;M02;;65\n"
				"size;M03;88,9;80\n"
				"size;M09;;100\n"
				"material;M04;Iron\n"
				"size;M04;;88,9\n"
				"size;M01;25;32\n"
				"size;M02;;\n"
				"size;M03;x;80\n"
				"taille;M04;;60\n"
				"series;M02;50 65 80\n"
				"od;88,9;80\n"
				"dn_equal;60;65\n"
				"dn_equivalent;65;60\n"
				"dn_equal;60;60\n"
				"text_option;decimal_separator;comma\n"), &skipped);
				// M09 unknown, a DN with a tenth, an outside diameter read
				// already, a size with neither, one not readable, two lines
				// of a former version, two DN said equal already, one DN
			QCOMPARE(skipped, 9);
			QCOMPARE(read.material(QStringLiteral("M01"))->sizes,
					 (QList<PidCodification::Size>{{250, 20}, {320, 25}}));
			QCOMPARE(read.material(QStringLiteral("M02"))->sizes,
					 (QList<PidCodification::Size>{{0, 50}, {0, 65}}));
			QCOMPARE(read.material(QStringLiteral("M03"))->sizes,
					 (QList<PidCodification::Size>{{889, 80}}));
			QCOMPARE(read.material(QStringLiteral("M04"))->sizes,
					 (QList<PidCodification::Size>{{0, 60}}));
			QVERIFY(read.outsideDiameterFor(QStringLiteral("M01")));
			QVERIFY(read.materials.first().family.isEmpty());
			QCOMPARE(read.equal_nominals, (QList<QPair<int, int>>{{60, 65}}));
			QCOMPARE(read.decimal, PidCodification::Decimal::Comma);
		}

			// A file of codes replaces the equal DN of the project with its
			// codes; a file which says the equal DN only keeps the codes
		void aFileOfEqualDnKeepsTheCodesOfTheProject()
		{
			PidCodification codes;
			codes.materials = {material(QStringLiteral("M09"), false, {{0, 100}})};
			const PidCodification merged = codes.completedWith(table());
			QCOMPARE(merged.materials.size(), 1);
			QVERIFY(merged.equal_nominals.isEmpty());

			PidCodification equal;
			equal.addEqualNominals(100, 110);
			const PidCodification other = equal.completedWith(table());
			QCOMPARE(other.materials, table().materials);
			QCOMPARE(other.equal_nominals, (QList<QPair<int, int>>{{100, 110}}));

			PidCodification colours;
			colours.object_colors.insert(QStringLiteral("valve"), QColor(Qt::blue));
			QCOMPARE(colours.completedWith(table()).equal_nominals, table().equal_nominals);
		}

			// ---- the page of the project ----
		void thePageShowsTheSizesOfTheMaterials()
		{
			QETProject project;
			project.undoStack()->push(new ChangePidCodificationCommand(&project, table()));
			PidConfigPage page(&project);
			QTableWidget *materials = page.codeTable(PidCodification::List::Materials);
			QCOMPARE(materials->rowCount(), 6);
			QCOMPARE(materials->item(0, 2)->checkState(), Qt::Checked);
				// the first, the last, how many; not typed in
			QCOMPARE(materials->item(0, 3)->text(), QStringLiteral("Ø 110 … Ø 450 (3)"));
			QCOMPARE(materials->item(1, 3)->text(), QStringLiteral("DN 80 … DN 125 (3)"));
			QVERIFY(!(materials->item(0, 3)->flags() & Qt::ItemIsEditable));
				// a material without sizes is in orange
			QVERIFY(materials->item(3, 3)->text().isEmpty());
			QVERIFY(materials->item(3, 0)->background() != QBrush());
			QVERIFY(materials->item(0, 0)->background() == QBrush());

				// the separator of the project
			auto *decimal = page.findChild<QComboBox *>(QStringLiteral("decimal_separator"));
			decimal->setCurrentIndex(decimal->findData(int(PidCodification::Decimal::Comma)));
			emit decimal->activated(decimal->currentIndex());
			QCOMPARE(materials->item(2, 3)->text(), QStringLiteral("Ø 88,9 … Ø 114,3 (2)"));

				// named by its DN instead: its sizes stay whole
			materials->item(2, 2)->setCheckState(Qt::Unchecked);
			QCOMPARE(materials->item(2, 3)->text(), QStringLiteral("DN 80 … DN 100 (2)"));
				// a code changed keeps its sizes
			materials->item(0, 0)->setText(QStringLiteral("P01"));

			page.applyProjectConf();
			const PidCodification &applied = project.pidCodification();
			QCOMPARE(applied.material(QStringLiteral("M03"))->sizes,
					 table().material(QStringLiteral("M03"))->sizes);
			QVERIFY(!applied.outsideDiameterFor(QStringLiteral("M03")));
			QVERIFY(!applied.material(QStringLiteral("M01")));
			QCOMPARE(applied.material(QStringLiteral("P01"))->sizes,
					 table().material(QStringLiteral("M01"))->sizes);
			QCOMPARE(applied.equal_nominals, table().equal_nominals);
			QCOMPARE(applied.decimal, PidCodification::Decimal::Comma);
			project.undoStack()->undo();
			QVERIFY(project.pidCodification().outsideDiameterFor(QStringLiteral("M03")));
			QVERIFY(project.pidCodification().material(QStringLiteral("M01")));
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
