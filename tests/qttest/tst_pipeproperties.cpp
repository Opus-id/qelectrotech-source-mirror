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
#include "pipeproperties.h"

#include <QtTest>
#include <QDomDocument>

/**
	PipeProperties -- the data of a pipe line of a P&ID: nominal diameter,
	material, fluid and flow. Checked here: what toXml() writes comes back
	through fromXml(), an unreadable or missing attribute leaves the
	property unset, and the comparison operators see every field.
*/
class tst_pipeproperties : public QObject
{
	Q_OBJECT

	static QDomElement pipe(const QString &attributes)
	{
		static QDomDocument doc;
		doc.setContent(QStringLiteral("<pipe %1/>").arg(attributes));
		return doc.documentElement();
	}

	static PipeProperties sample()
	{
		PipeProperties p;
		p.nominal_diameter = 100;
		p.material = QStringLiteral("PVC");
		p.fluid = QStringLiteral("Raw water");
		p.flow = QStringLiteral("120 m3/h");
		return p;
	}

private slots:
	void defaultsAreUnset()
	{
		const PipeProperties p;
		QCOMPARE(p.nominal_diameter, 0);
		QVERIFY(p.material.isEmpty());
		QVERIFY(p.fluid.isEmpty());
		QVERIFY(p.flow.isEmpty());
	}

	void savedPropertiesComeBack()
	{
		const PipeProperties saved = sample();
		QDomDocument doc;
		QDomElement e = doc.createElement(QStringLiteral("pipe"));
		saved.toXml(e);

		PipeProperties loaded;
		loaded.fromXml(e);
		QCOMPARE(loaded, saved);
	}

	void textWithSpecialCharactersComesBack()
	{
		PipeProperties saved = sample();
		saved.material = QStringLiteral("Acier \"inox\" <316L> & co");
		saved.flow = QStringLiteral("3,5 l/s");
		QDomDocument doc;
		QDomElement e = doc.createElement(QStringLiteral("pipe"));
		saved.toXml(e);

			// Through a real text round trip, not only the DOM tree
		QDomDocument reread;
		doc.appendChild(e);
		reread.setContent(doc.toString());

		PipeProperties loaded;
		loaded.fromXml(reread.documentElement());
		QCOMPARE(loaded, saved);
	}

	void missingAttributesLeaveEverythingUnset()
	{
			// A pipe read from an element with no attribute must not keep
			// what the object held before.
		PipeProperties p = sample();
		p.fromXml(pipe(QString()));
		QCOMPARE(p, PipeProperties());
	}

	void unreadableDiameterIsUnset_data()
	{
		QTest::addColumn<QString>("dn");
		QTest::newRow("text")      << "wide";
		QTest::newRow("empty")     << "";
		QTest::newRow("zero")      << "0";
		QTest::newRow("negative")  << "-50";
		QTest::newRow("decimal")   << "12.5";
		QTest::newRow("too large") << "99999999999999999999";
	}

	void unreadableDiameterIsUnset()
	{
		QFETCH(QString, dn);
		PipeProperties p = sample();
		p.fromXml(pipe(QStringLiteral("dn=\"%1\"").arg(dn)));
		QCOMPARE(p.nominal_diameter, 0);
	}

	void validDiameterIsRead()
	{
		PipeProperties p;
		p.fromXml(pipe(QStringLiteral("dn=\"250\"")));
		QCOMPARE(p.nominal_diameter, 250);
	}

	void comparisonSeesEveryField()
	{
		const PipeProperties base = sample();
		QVERIFY(base == sample());
		QVERIFY(!(base != sample()));

		PipeProperties other = sample();
		other.nominal_diameter = 150;
		QVERIFY(base != other);

		other = sample();
		other.material = QStringLiteral("Steel");
		QVERIFY(base != other);

		other = sample();
		other.fluid = QStringLiteral("Sludge");
		QVERIFY(base != other);

		other = sample();
		other.flow = QStringLiteral("80 m3/h");
		QVERIFY(base != other);
	}

	void defaultFluidsAreUsable()
	{
		QStringList fluids = PipeProperties::defaultFluids();
		QVERIFY(!fluids.isEmpty());
		QVERIFY(!fluids.contains(QString()));
		QCOMPARE(fluids.removeDuplicates(), 0);
	}
};

QTEST_GUILESS_MAIN(tst_pipeproperties)
#include "tst_pipeproperties.moc"
