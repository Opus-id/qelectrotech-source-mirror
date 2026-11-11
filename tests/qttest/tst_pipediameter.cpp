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
#include <QGroupBox>
#include <QLineEdit>
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

			// A DN which has several outside diameters in a material may have
			// a default one, taken without a question: one at most by DN
		void aDnMayHaveADefaultOutsideDiameter()
		{
			PidCodification::Entry e;
			e.code = QStringLiteral("M01");
			e.name = e.code;
			e.outside_diameter = true;
			QVERIFY(e.addSize(1100, 100));
			QVERIFY(e.addSize(1250, 100, true));
			QVERIFY(e.addSize(1600, 150));
			QVERIFY(e.addSize(4500, 0));
			QCOMPARE(e.outsideCount(100), 2);
			PidCodification codification;
			codification.materials = {e};
			PidCodification::Entry &material = codification.materials.first();
			const QString code = QStringLiteral("M01");
			const PipeDiameter dn100 = PipeDiameter::nominal(100), dn150 = PipeDiameter::nominal(150);
			QCOMPARE(codification.defaultOutside(dn100, code), PipeDiameter::outside(125));
			QCOMPARE(codification.outsideOf(dn100, code), PipeDiameter::outside(125));
				// the only one of a DN is taken, and is no default
			QCOMPARE(codification.outsideOf(dn150, code), PipeDiameter::outside(160));
			QVERIFY(!codification.defaultOutside(dn150, code).isSet());

				// written in a file of codes and in the project, where it is only
			const QString csv = codification.toCsv();
			QVERIFY(csv.contains(QStringLiteral("size;M01;125;100;default\n")));
			QVERIFY(csv.contains(QStringLiteral("size;M01;110;100\n")));
			int skipped = -1;
			QCOMPARE(PidCodification::fromCsv(csv, &skipped), codification);
			QCOMPARE(skipped, 0);
			QDomDocument doc;
			QDomElement project = doc.createElement(QStringLiteral("project"));
			project.appendChild(codification.toXml(doc));
			const QDomNodeList sizes = project.elementsByTagName(QStringLiteral("size"));
			QCOMPARE(sizes.size(), 4);
			QVERIFY(!sizes.at(0).toElement().hasAttribute(QStringLiteral("default")));
			QCOMPARE(sizes.at(1).toElement().attribute(QStringLiteral("default")), QStringLiteral("true"));
			PidCodification back;
			back.fromXml(project);
			QCOMPARE(back, codification);

				// one by DN: the one before is so no more
			QVERIFY(material.setDefaultSize(1100));
			QCOMPARE(codification.defaultOutside(dn100, code), PipeDiameter::outside(110));
			QVERIFY(!material.sizes.at(1).by_default);
				// none: the user chooses
			QVERIFY(material.setDefaultSize(1100, false));
			QVERIFY(!codification.outsideOf(dn100, code).isSet());
				// a size which is not there, or without a DN
			QVERIFY(!material.setDefaultSize(999));
			QVERIFY(!material.setDefaultSize(4500));
				// the one left alone for its DN is no default any more
			QVERIFY(material.setDefaultSize(1250));
			QVERIFY(material.removeSize(1100, 100));
			QVERIFY(!material.removeSize(1100, 100));
			QCOMPARE(material.sizes.first(), (PidCodification::Size{1250, 100, false}));
			QCOMPARE(codification.outsideOf(dn100, code), PipeDiameter::outside(125));

				// the French word; a word not known, a default without both
				// values: lines left out
			const PidCodification read = PidCodification::fromCsv(QStringLiteral(
				"material;M01;PE;od\n"
				"size;M01;110;100;défaut\n"
				"size;M01;125;100\n"
				"size;M01;160;150;maybe\n"
				"size;M01;200;;default\n"), &skipped);
			QCOMPARE(skipped, 2);
			QCOMPARE(read.materials.first().sizes.size(), 2);
			QCOMPARE(read.defaultOutside(dn100, code), PipeDiameter::outside(110));
				// a material named by its DN gives its pipes no outside diameter
			material.outside_diameter = false;
			QVERIFY(!codification.outsideOf(dn100, code).isSet());
		}

			// The sizes of a material the file does not list are lines left
			// out, unless they are asked for the materials of a page
		void theSizesAloneAreKeptForWhoAsksThem()
		{
			const QString text = QStringLiteral("size;M01;125;100;default\n"
												"size;M01;110;100\n"
												"size;M01;110;80\n"
												"size;M07;;80\n"
												"dn_equal;60;65\n");
			int skipped = -1;
			QMap<QString, QList<PidCodification::Size>> loose;
			const PidCodification read = PidCodification::fromCsv(text, &skipped, &loose);
				// an outside diameter has one DN
			QCOMPARE(skipped, 1);
			QVERIFY(read.materials.isEmpty());
			QCOMPARE(read.equal_nominals.size(), 1);
			QCOMPARE(loose.keys(), (QStringList{QStringLiteral("M01"), QStringLiteral("M07")}));
			QCOMPARE(loose.value(QStringLiteral("M01")),
					 (QList<PidCodification::Size>{{1100, 100, false}, {1250, 100, true}}));
			QVERIFY(PidCodification::fromCsv(text, &skipped).materials.isEmpty());
			QCOMPARE(skipped, 4);
		}

			// The DN which are the same size of line are typed together
		void equalDnAreTypedTogether()
		{
			PidCodification codification;
			QVERIFY(codification.equalNominalsText().isEmpty());
			QVERIFY(codification.setEqualNominalsText(QStringLiteral("125/130, 60/65")));
			QCOMPARE(codification.equal_nominals, (QList<QPair<int, int>>{{60, 65}, {125, 130}}));
			QCOMPARE(codification.equalNominalsText(), QStringLiteral("60/65, 125/130"));
				// three of them, with what a user may type around
			QVERIFY(codification.setEqualNominalsText(QStringLiteral(" DN 60 / 65 ; 70/65  60/65")));
			QCOMPARE(codification.equalNominalsText(), QStringLiteral("60/65/70"));
			QVERIFY(codification.sameNominal(60, 70));
				// what cannot be read changes nothing
			for (const QString &wrong : {QStringLiteral("60"), QStringLiteral("60/60"),
										 QStringLiteral("60/x"), QStringLiteral("60/65,5"),
										 QStringLiteral("60/0"), QStringLiteral("60/65.5")}) {
				QVERIFY2(!codification.setEqualNominalsText(wrong), qPrintable(wrong));
				QCOMPARE(codification.equalNominalsText(), QStringLiteral("60/65/70"));
			}
			QVERIFY(codification.setEqualNominalsText(QStringLiteral("  ")));
			QVERIFY(codification.equal_nominals.isEmpty());
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
			QCOMPARE(codification.nominalsOf(PipeDiameter::outside(75)), QList<PipeDiameter>{dn65});
				// without material: the DN of all the materials, equal ones
				// as one, the usual one
			QCOMPARE(codification.nominalSizes(), (QList<PipeDiameter>{dn65, dn80, dn100, dn125}));
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
			QCOMPARE(codification.equalNominals(65), (QList<int>{60, 65}));
			QCOMPARE(codification.equalNominals(80), QList<int>{80});
				// the usual one: the DN most materials have, the greater of
				// two as usual; DN 60 is of cast iron only
			QCOMPARE(codification.usualNominal(60), 65);
			QCOMPARE(codification.usualNominal(65), 65);
			QCOMPARE(codification.usualNominal(80), 80);
			PidCodification irons = codification;
			irons.materials << material(QStringLiteral("M07"), false, {{0, 60}});
			QCOMPARE(irons.usualNominal(65), 60);
				// without material no DN is chosen among them: the usual one
				// stands for all, and they are written together
			QCOMPARE(codification.nominalIn(dn65, QString()), dn65);
			QCOMPARE(codification.nominalIn(dn60, QString()), dn65);
			QCOMPARE(codification.nominalIn(dn60, QStringLiteral("M99")), dn65);
			QCOMPARE(codification.text(dn65, QString()), QStringLiteral("DN 60/65"));
			QCOMPARE(codification.text(dn60, QStringLiteral("M99")), QStringLiteral("DN 60/65"));
			QCOMPARE(codification.text(dn60, QStringLiteral("M06")), QStringLiteral("DN 60"));
			QCOMPARE(codification.text(dn65, QStringLiteral("M05")), QStringLiteral("DN 65"));
			QCOMPARE(codification.text(PipeDiameter::nominal(80), QString()), QStringLiteral("DN 80"));
			QCOMPARE(codification.text(PipeDiameter::outside(88.9), QString()), QStringLiteral("Ø 88.9"));
			QVERIFY(codification.text(PipeDiameter(), QString()).isEmpty());
				// read back as written, in any order
			QCOMPARE(codification.nominalOfEqualText(QStringLiteral("DN 60/65")), dn65);
			QCOMPARE(codification.nominalOfEqualText(QStringLiteral("65/60")), dn65);
			QVERIFY(!codification.nominalOfEqualText(QStringLiteral("60/80")).isSet());
			QVERIFY(!codification.nominalOfEqualText(QStringLiteral("DN 60")).isSet());
			QVERIFY(!codification.nominalOfEqualText(QStringLiteral("60/x")).isSet());
			QVERIFY(!codification.nominalOfEqualText(QStringLiteral("1/2")).isSet());
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
				// neither in the material, no size: as it is
			QCOMPARE(codification.nominalIn(dn60, QStringLiteral("M03")), dn60);
			QCOMPARE(codification.nominalIn(dn65, QStringLiteral("M04")), dn65);
				// equal one after the other
			QVERIFY(codification.addEqualNominals(65, 70));
			QCOMPARE(codification.equalNominals(70), (QList<int>{60, 65, 70}));
			QVERIFY(codification.sameNominal(60, 70));
			QCOMPARE(codification.text(dn65, QString()), QStringLiteral("DN 60/65/70"));
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

			// The sizes of the material of the current line are typed in the
			// list beside the materials
		void theSizesOfAMaterialAreTypedInThePage()
		{
			QETProject project;
			project.undoStack()->push(new ChangePidCodificationCommand(&project, table()));
			PidConfigPage page(&project);
			QTableWidget *materials = page.codeTable(PidCodification::List::Materials);
			QTableWidget *sizes = page.sizeTable();
			auto *box = page.findChild<QGroupBox *>(QStringLiteral("size_box"));
			QVERIFY(sizes && box);
				// its columns: the DN, the size of the line, comes first
			const int dn = 0, od = 1;
			QCOMPARE(sizes->horizontalHeaderItem(dn)->text(), QStringLiteral("DN"));
			auto text = [sizes](int row, int column) { return sizes->item(row, column)->text(); };
			auto hasBox = [sizes](int row) {
				return sizes->item(row, 2)->data(Qt::CheckStateRole).isValid();
			};

				// those of the first material at first, in order
			QVERIFY(box->isEnabled());
			QVERIFY(box->title().contains(QStringLiteral("M01")));
			QCOMPARE(sizes->rowCount(), 3);
			QCOMPARE(text(0, od), QStringLiteral("110"));
			QCOMPARE(text(0, dn), QStringLiteral("100"));
			QCOMPARE(text(2, od), QStringLiteral("450"));
			QVERIFY(text(2, dn).isEmpty());
				// DN 100 has two outside diameters: a box each, none checked
			QVERIFY(hasBox(0) && hasBox(1) && !hasBox(2));
			QCOMPARE(sizes->item(0, 2)->checkState(), Qt::Unchecked);

				// a steel: a DN each
			materials->setCurrentCell(1, 0);
			QVERIFY(box->title().contains(QStringLiteral("M02")));
			QCOMPARE(sizes->rowCount(), 3);
			QVERIFY(text(0, od).isEmpty());
			QCOMPARE(text(0, dn), QStringLiteral("80"));
			QVERIFY(!hasBox(0));
				// a size added, as it is typed
			page.addSize();
			QCOMPARE(sizes->rowCount(), 4);
			sizes->item(3, dn)->setText(QStringLiteral("DN 150"));
			QCOMPARE(text(3, dn), QStringLiteral("150"));
			QCOMPARE(materials->item(1, 3)->text(), QStringLiteral("DN 80 … DN 150 (4)"));
			QVERIFY(page.codeMessage().isEmpty());
				// its outside diameter, to the tenth, with the separator of the page
			sizes->item(3, od)->setText(QStringLiteral("168,3"));
			QCOMPARE(text(3, od), QStringLiteral("168.3"));
				// what is no size gets its value back, and it is said
			sizes->item(3, dn)->setText(QStringLiteral("abc"));
			QCOMPARE(text(3, dn), QStringLiteral("150"));
			QVERIFY(!page.codeMessage().isEmpty());
			sizes->item(3, dn)->setText(QStringLiteral("150,5"));
			QCOMPARE(text(3, dn), QStringLiteral("150"));
			sizes->item(3, od)->setText(QStringLiteral("DN 200"));
			QCOMPARE(text(3, od), QStringLiteral("168.3"));
				// a DN alone is listed once
			page.addSize();
			sizes->item(4, dn)->setText(QStringLiteral("100"));
			QVERIFY(text(4, dn).isEmpty());
			QVERIFY(!page.codeMessage().isEmpty());
				// a size removed; the line not filled is no size
			sizes->clearSelection();
			sizes->selectRow(0);
			page.removeSelectedSizes();
			QCOMPARE(sizes->rowCount(), 4);
			QCOMPARE(materials->item(1, 3)->text(), QStringLiteral("DN 100 … DN 150 (3)"));

				// a plastic: the default outside diameter of a DN, one by DN
			materials->setCurrentCell(0, 0);
			QCOMPARE(sizes->rowCount(), 3);
			sizes->item(1, 2)->setCheckState(Qt::Checked);
			QCOMPARE(sizes->item(1, 2)->checkState(), Qt::Checked);
			sizes->item(0, 2)->setCheckState(Qt::Checked);
			QCOMPARE(sizes->item(0, 2)->checkState(), Qt::Checked);
			QCOMPARE(sizes->item(1, 2)->checkState(), Qt::Unchecked);
				// an outside diameter has one DN
			page.addSize();
			sizes->item(3, od)->setText(QStringLiteral("125"));
			QVERIFY(text(3, od).isEmpty());
			sizes->item(3, od)->setText(QStringLiteral("Ø 140"));
			sizes->item(3, dn)->setText(QStringLiteral("125"));
			QCOMPARE(text(3, od), QStringLiteral("140"));
			QVERIFY(!hasBox(3));
				// a line which lacks what names the pipes is in orange
			page.addSize();
			sizes->item(4, dn)->setText(QStringLiteral("200"));
			QVERIFY(sizes->item(4, od)->background() != QBrush());
			QVERIFY(sizes->item(0, od)->background() == QBrush());
				// named by its DN instead: no default to choose, nothing lacks
			materials->item(0, 2)->setCheckState(Qt::Unchecked);
			QVERIFY(!hasBox(0));
			QVERIFY(sizes->item(4, od)->background() == QBrush());
			materials->item(0, 2)->setCheckState(Qt::Checked);
			QVERIFY(hasBox(0));
			QCOMPARE(sizes->item(0, 2)->checkState(), Qt::Checked);

				// the equal DN of the project, typed together
			auto *equal = page.findChild<QLineEdit *>(QStringLiteral("equal_dn"));
			QVERIFY(equal);
			QCOMPARE(equal->text(), QStringLiteral("60/65"));
			equal->setText(QStringLiteral("60/65; 100/110"));
			emit equal->editingFinished();
			QCOMPARE(equal->text(), QStringLiteral("60/65, 100/110"));
			equal->setText(QStringLiteral("60"));
			emit equal->editingFinished();
			QCOMPARE(equal->text(), QStringLiteral("60/65, 100/110"));
			QVERIFY(!page.codeMessage().isEmpty());

				// nothing before the page is applied; then in one step
			QCOMPARE(project.pidCodification(), table());
			const int steps = project.undoStack()->count();
			page.applyProjectConf();
			QCOMPARE(project.undoStack()->count(), steps + 1);
			const PidCodification applied = project.pidCodification();
			QCOMPARE(applied.material(QStringLiteral("M02"))->sizes,
					 (QList<PidCodification::Size>{{0, 100}, {0, 125}, {1683, 150}}));
			QCOMPARE(applied.material(QStringLiteral("M01"))->sizes,
					 (QList<PidCodification::Size>{{1100, 100, true}, {1250, 100}, {1400, 125},
												   {0, 200}, {4500, 0}}));
			QCOMPARE(applied.outsideOf(PipeDiameter::nominal(100), QStringLiteral("M01")),
					 PipeDiameter::outside(110));
			QCOMPARE(applied.equal_nominals, (QList<QPair<int, int>>{{60, 65}, {100, 110}}));
				// the page shows the project again, in order
			QCOMPARE(sizes->rowCount(), 5);
			QCOMPARE(text(3, dn), QStringLiteral("200"));
			project.undoStack()->undo();
			QCOMPARE(project.pidCodification(), table());
			QCOMPARE(sizes->rowCount(), 3);
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
