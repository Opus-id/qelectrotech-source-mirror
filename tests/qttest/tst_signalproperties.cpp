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
#include "signalproperties.h"

#include <QtTest>
#include <QDomDocument>

/**
	SignalProperties -- the data of a signal line of a P&ID: for now its
	type of signal. Checked here: every type is written and read back, the
	words written in the files are the ones of the file format, an unknown
	or missing type is read as undefined, and the comparison sees the type.
*/
class tst_signalproperties : public QObject
{
	Q_OBJECT

	static QDomElement signal(const QString &attributes)
	{
		static QDomDocument doc;
		doc.setContent(QStringLiteral("<signal %1/>").arg(attributes));
		return doc.documentElement();
	}

	static void fillTypes()
	{
		QTest::addColumn<int>("type");
		QTest::addColumn<QString>("word");
		QTest::newRow("undefined")
			<< int(SignalProperties::Undefined) << "undefined";
		QTest::newRow("pneumatic")
			<< int(SignalProperties::Pneumatic) << "pneumatic";
		QTest::newRow("electrical")
			<< int(SignalProperties::Electrical) << "electrical";
		QTest::newRow("hydraulic")
			<< int(SignalProperties::Hydraulic) << "hydraulic";
		QTest::newRow("capillary")
			<< int(SignalProperties::Capillary) << "capillary";
		QTest::newRow("guided electromagnetic")
			<< int(SignalProperties::GuidedElectromagnetic) << "guided_electromagnetic";
		QTest::newRow("data link")
			<< int(SignalProperties::DataLink) << "data_link";
	}

private slots:
	void defaultIsUndefined()
	{
		const SignalProperties p;
		QCOMPARE(int(p.signal_type), int(SignalProperties::Undefined));
	}

	void everyTypeComesBack_data() { fillTypes(); }
	void everyTypeComesBack()
	{
		QFETCH(int, type);
		SignalProperties saved;
		saved.signal_type = SignalProperties::SignalType(type);
		QDomDocument doc;
		QDomElement e = doc.createElement(QStringLiteral("signal"));
		saved.toXml(e);

			// Start from another type, to see the value is read, not kept
		SignalProperties loaded;
		loaded.signal_type = SignalProperties::SignalType((type + 1) % 7);
		loaded.fromXml(e);
		QCOMPARE(loaded, saved);
	}

		// The words are part of the file format: a file written today must
		// read the same in ten years, so they are checked one by one
	void theWordsInTheFilesAreTheFormatOnes_data() { fillTypes(); }
	void theWordsInTheFilesAreTheFormatOnes()
	{
		QFETCH(int, type);
		QFETCH(QString, word);
		SignalProperties p;
		p.signal_type = SignalProperties::SignalType(type);
		QDomDocument doc;
		QDomElement e = doc.createElement(QStringLiteral("signal"));
		p.toXml(e);
		QCOMPARE(e.attribute(QStringLiteral("signal_type")), word);
	}

	void missingTypeIsUndefined()
	{
		SignalProperties p;
		p.signal_type = SignalProperties::Pneumatic;
		p.fromXml(signal(QString()));
		QCOMPARE(int(p.signal_type), int(SignalProperties::Undefined));
	}

	void unknownTypeIsUndefined_data()
	{
		QTest::addColumn<QString>("value");
		QTest::newRow("another word") << "wireless";
		QTest::newRow("empty")        << "";
		QTest::newRow("capitals")     << "PNEUMATIC";
		QTest::newRow("a number")     << "3";
	}

	void unknownTypeIsUndefined()
	{
		QFETCH(QString, value);
		SignalProperties p;
		p.signal_type = SignalProperties::Hydraulic;
		p.fromXml(signal(QStringLiteral("signal_type=\"%1\"").arg(value)));
		QCOMPARE(int(p.signal_type), int(SignalProperties::Undefined));
	}

	void comparisonSeesTheType()
	{
		SignalProperties a, b;
		QVERIFY(a == b);
		QVERIFY(!(a != b));
		b.signal_type = SignalProperties::Capillary;
		QVERIFY(a != b);
		QVERIFY(!(a == b));
	}
};

QTEST_GUILESS_MAIN(tst_signalproperties)
#include "tst_signalproperties.moc"
