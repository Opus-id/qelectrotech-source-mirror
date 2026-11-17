// SPDX-License-Identifier: GPL-2.0-or-later
#include "signalproperties.h"

#include <QtTest>
#include <QDomDocument>

// SignalProperties -- the data of a signal line of a P&ID: for now its type
// of signal. Checked here: every type is written and read back, the words
// written in the files are the ones of the file format, the undefined type
// writes nothing, an unknown or missing type is read as undefined, and the
// comparison sees the type.
class tst_signalproperties : public QObject
{
	Q_OBJECT

		// The conductor of a file with @p attributes
	static QDomElement conductor(QDomDocument &doc, const QString &attributes)
	{
		doc.setContent(QStringLiteral("<conductor %1/>").arg(attributes));
		return doc.documentElement();
	}

		// What @p p writes of @p name, an empty string when it does not
	static QString written(const SignalProperties &p, const QString &name)
	{
		for (const auto &attribute : p.attributes()) {
			if (attribute.first == name) return attribute.second;
		}
		return QString();
	}

	static void fillTypes()
	{
		QTest::addColumn<int>("type");
		QTest::addColumn<QString>("word");
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
		QVERIFY(p.isDefault());
	}

		// Written only when set: a signal line whose type is not chosen adds
		// nothing to its conductor
	void theUndefinedTypeWritesNothing()
	{
		QVERIFY(SignalProperties().attributes().isEmpty());
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
		QCOMPARE(p.attributes().size(), 1);
		QCOMPARE(written(p, QStringLiteral("pid_signal_type")), word);
		QCOMPARE(SignalProperties::typeToString(p.signal_type), word);
		QCOMPARE(int(SignalProperties::typeFromString(word)), type);
	}

	void everyTypeComesBack_data() { fillTypes(); }
	void everyTypeComesBack()
	{
		QFETCH(int, type);
		QFETCH(QString, word);
		SignalProperties saved;
		saved.signal_type = SignalProperties::SignalType(type);
		QDomDocument doc;
		const QDomElement e = conductor(
					doc, QStringLiteral("pid_signal_type=\"%1\"").arg(word));

			// Start from another type, to see the value is read, not kept
		SignalProperties loaded;
		loaded.signal_type = SignalProperties::SignalType((type + 1) % 7);
		loaded.fromXml(e);
		QCOMPARE(loaded, saved);
	}

	void missingTypeIsUndefined()
	{
		SignalProperties p;
		p.signal_type = SignalProperties::Pneumatic;
		QDomDocument doc;
		p.fromXml(conductor(doc, QString()));
		QCOMPARE(int(p.signal_type), int(SignalProperties::Undefined));
	}

	void unknownTypeIsUndefined_data()
	{
		QTest::addColumn<QString>("value");
		QTest::newRow("another word") << "wireless";
		QTest::newRow("empty")        << "";
		QTest::newRow("capitals")     << "PNEUMATIC";
		QTest::newRow("a number")     << "3";
			// never written, read all the same
		QTest::newRow("the word undefined") << "undefined";
	}

	void unknownTypeIsUndefined()
	{
		QFETCH(QString, value);
		SignalProperties p;
		p.signal_type = SignalProperties::Hydraulic;
		QDomDocument doc;
		p.fromXml(conductor(doc, QStringLiteral("pid_signal_type=\"%1\"").arg(value)));
		QCOMPARE(int(p.signal_type), int(SignalProperties::Undefined));
	}

		// The attribute of the frozen branch, without prefix, is not read
	void theOldNameIsNotRead()
	{
		SignalProperties p;
		QDomDocument doc;
		p.fromXml(conductor(doc, QStringLiteral("signal_type=\"pneumatic\"")));
		QCOMPARE(int(p.signal_type), int(SignalProperties::Undefined));
	}

		// Every type is offered once, the undefined one first, with a name
	void everyTypeIsOfferedWithAName()
	{
		const QList<SignalProperties::SignalType> types = SignalProperties::types();
		QCOMPARE(types.size(), 7);
		QCOMPARE(int(types.first()), int(SignalProperties::Undefined));
		QSet<int> seen;
		QSet<QString> names;
		for (SignalProperties::SignalType type : types) {
			seen << int(type);
			QVERIFY(!SignalProperties::typeName(type).isEmpty());
			names << SignalProperties::typeName(type);
		}
		QCOMPARE(seen.size(), 7);
		QCOMPARE(names.size(), 7);
	}

	void comparisonSeesTheType()
	{
		SignalProperties a, b;
		QVERIFY(a == b);
		QVERIFY(!(a != b));
		b.signal_type = SignalProperties::Capillary;
		QVERIFY(a != b);
		QVERIFY(!(a == b));
		QVERIFY(!b.isDefault());
	}
};

QTEST_GUILESS_MAIN(tst_signalproperties)
#include "tst_signalproperties.moc"
