// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "diagram.h"
#include "pidcodification.h"
#include "qetproject.h"
#include "qetresult.h"
#include "signalmark.h"
#include "signalproperties.h"
#include "ui/configpage/pidconfigpage.h"

#include <QComboBox>
#include <QDomDocument>
#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>
#include <QUndoStack>

// Which mark the signal lines of each type of signal take: a setting of the
// project, as its colours. By default nothing is said, nor written: the marks
// of SignalMark::defaultMark(). A project says another mark for a type; it is
// saved with it, read from a CSV file which says them all, set in the page
// P&ID of the properties of the project, with undo.
class tst_signalmarksettings : public QObject
{
	Q_OBJECT

	static QDomElement written(const PidCodification &codification, QDomDocument &doc)
	{
		QDomElement holder = doc.createElement(QStringLiteral("project"));
		const QDomElement block = codification.toXml(doc);
		if (!block.isNull()) {
			holder.appendChild(block);
		}
		return holder;
	}

	static bool writeFile(const QString &path, const QByteArray &text)
	{
		QFile file(path);
		return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
	}

	private slots:
			// ---- the setting ----
		void byDefaultNothingIsSaidNorWritten()
		{
			const PidCodification c;
			QVERIFY(c.signal_marks.isEmpty());
			QVERIFY(!c.hasSignalMarks());
			QVERIFY(c.isEmpty());
			for (SignalProperties::SignalType type : SignalProperties::types()) {
				QCOMPARE(int(c.signalMark(type)), int(SignalMark::defaultMark(type)));
			}
			QDomDocument doc;
			QVERIFY(c.toXml(doc).isNull());
			QVERIFY(!c.presentationToCsv().contains(QStringLiteral("signal_mark")));
		}

		void aMarkSetIsKeptUnlessItIsTheDefaultOne()
		{
			PidCodification c;
			c.setSignalMark(SignalProperties::Pneumatic, SignalMark::Bar2);
			QCOMPARE(int(c.signalMark(SignalProperties::Pneumatic)), int(SignalMark::Bar2));
			QCOMPARE(c.signal_marks.value(QStringLiteral("pneumatic")), QStringLiteral("bar2"));
			QVERIFY(c.hasSignalMarks());
			QVERIFY(!c.isEmpty());
			QVERIFY(c != PidCodification());
				// the other types keep theirs
			QCOMPARE(int(c.signalMark(SignalProperties::Hydraulic)), int(SignalMark::LMark));

			c.setSignalMark(SignalProperties::Pneumatic, SignalMark::AMark);
			QVERIFY(c.signal_marks.isEmpty());
			QVERIFY(c == PidCodification());
		}

			// ---- the file of the project ----
		void theMarksAreWrittenInTheBlockOfTheProject()
		{
			PidCodification c;
			c.setSignalMark(SignalProperties::Pneumatic, SignalMark::Bar2);
			c.setSignalMark(SignalProperties::Electrical, SignalMark::Dashes);
			QDomDocument doc;
			const QDomElement holder = written(c, doc);
			const QDomElement block = holder.firstChildElement(QStringLiteral("pid_codification"));
			QVERIFY(!block.isNull());
			QMap<QString, QString> found;
			for (QDomElement e = block.firstChildElement(QStringLiteral("signal_mark")) ; !e.isNull() ;
				 e = e.nextSiblingElement(QStringLiteral("signal_mark"))) {
				found.insert(e.attribute(QStringLiteral("type")), e.attribute(QStringLiteral("mark")));
			}
			QCOMPARE(found.size(), 2);
			QCOMPARE(found.value(QStringLiteral("pneumatic")), QStringLiteral("bar2"));
			QCOMPARE(found.value(QStringLiteral("electrical")), QStringLiteral("dashes"));
				// nothing else: the types left alone are not written
			QCOMPARE(block.childNodes().size(), 2);

			PidCodification read;
			read.fromXml(holder);
			QVERIFY(read == c);
		}

			// A file may say them all (fromCsv()): those which are the
			// default ones are not written in the project
		void aDefaultMarkSaidIsNotWritten()
		{
			const PidCodification all_default = PidCodification::fromCsv(QStringLiteral(
				"signal_mark;pneumatic;a_mark\nsignal_mark;capillary;cross\n"));
			QCOMPARE(all_default.signal_marks.size(), 2);
			QVERIFY(!all_default.hasSignalMarks());
			QDomDocument doc;
			QVERIFY(all_default.toXml(doc).isNull());

			PidCodification mixed = PidCodification::fromCsv(QStringLiteral(
				"signal_mark;pneumatic;a_mark\nsignal_mark;capillary;none\n"));
			QVERIFY(mixed.hasSignalMarks());
			QDomDocument doc2;
			const QDomElement block = written(mixed, doc2).firstChildElement();
			QCOMPARE(block.childNodes().size(), 1);
			QCOMPARE(block.firstChildElement().attribute(QStringLiteral("type")),
					 QStringLiteral("capillary"));
			mixed.dropDefaultSignalMarks();
			QCOMPARE(mixed.signal_marks.keys(), QStringList{QStringLiteral("capillary")});
		}

			// What a later version wrote: kept, and written back; meanwhile
			// the lines take the default mark
		void aWordOfALaterVersionIsKept()
		{
			QDomDocument doc;
			QVERIFY(doc.setContent(QStringLiteral(
				"<project><pid_codification>"
				"<signal_mark type=\"pneumatic\" mark=\"diamond\"/>"
				"<signal_mark type=\"mechanical\" mark=\"bar2\"/>"
				"<signal_mark type=\"hydraulic\" mark=\"bar3\"/>"
				"</pid_codification></project>")));
			PidCodification c;
			c.fromXml(doc.documentElement());
			QCOMPARE(c.signal_marks.size(), 3);
			QCOMPARE(int(c.signalMark(SignalProperties::Pneumatic)), int(SignalMark::AMark));
			QCOMPARE(int(c.signalMark(SignalProperties::Hydraulic)), int(SignalMark::Bar3));

			QDomDocument out;
			PidCodification back;
			back.fromXml(written(c, out));
			QVERIFY(back == c);
			QCOMPARE(back.signal_marks.value(QStringLiteral("pneumatic")), QStringLiteral("diamond"));
			QCOMPARE(back.signal_marks.value(QStringLiteral("mechanical")), QStringLiteral("bar2"));
		}

		void theMarksAreSavedWithTheProject_data()
		{
			QTest::addColumn<QString>("suffix");
			QTest::newRow(".qet") << QStringLiteral(".qet");
			QTest::newRow(".qetz") << QStringLiteral(".qetz");
			QTest::newRow(".qetdb") << QStringLiteral(".qetdb");
		}

		void theMarksAreSavedWithTheProject()
		{
			QFETCH(QString, suffix);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("marks") + suffix);
			PidCodification c;
			c.setSignalMark(SignalProperties::Pneumatic, SignalMark::Bar2);
			c.setSignalMark(SignalProperties::Undefined, SignalMark::None);
			{
				QETProject project;
				project.addNewDiagram();
				project.setPidCodification(c);
				project.setFilePath(path);
				QVERIFY(project.write().isOk());
			}
			QETProject back(path);
			QVERIFY(back.pidCodification() == c);
			QCOMPARE(int(back.pidCodification().signalMark(SignalProperties::Pneumatic)),
					 int(SignalMark::Bar2));
		}

			// ---- the CSV files ----
		void aCsvFileSaysTheMarks()
		{
			int skipped = 0;
			const PidCodification c = PidCodification::fromCsv(QStringLiteral(
				"kind;key;value\n"
				"signal_mark;pneumatic;bar2\n"
				"marque_signal;electrical;dashes\n"
				"signal_mark;Hydraulic;L_MARK\n"		// case does not matter
				"signal_mark;pneumatic;bar3\n"			// said twice
				"signal_mark;mechanical;bar1\n"			// a type not known
				"signal_mark;capillary;diamond\n"		// a mark not known
				"signal_mark;data_link;\n"), &skipped);	// no mark
			QCOMPARE(skipped, 4);
			QCOMPARE(c.signal_marks.size(), 3);
			QCOMPARE(int(c.signalMark(SignalProperties::Pneumatic)), int(SignalMark::Bar2));
			QCOMPARE(int(c.signalMark(SignalProperties::Electrical)), int(SignalMark::Dashes));
			QCOMPARE(int(c.signalMark(SignalProperties::Hydraulic)), int(SignalMark::LMark));
			QCOMPARE(int(c.signalMark(SignalProperties::Capillary)), int(SignalMark::Cross));
				// nothing but marks: no colours, no texts
			QVERIFY(!c.hasColors() && !c.hasTextSettings() && !c.hasCodes());
			QVERIFY(!c.isEmpty());
		}

			// The marks are part of the presentation: written with it, only
			// those which are not the default ones, apart from the codes
		void theMarksGoWithThePresentation()
		{
			PidCodification c;
			c.setSignalMark(SignalProperties::Pneumatic, SignalMark::Bar2);
			c.setSignalMark(SignalProperties::Electrical, SignalMark::Dashes);
			const QString presentation = c.presentationToCsv();
			QVERIFY(presentation.contains(QStringLiteral("signal_mark;pneumatic;bar2\n")));
			QVERIFY(presentation.contains(QStringLiteral("signal_mark;electrical;dashes\n")));
			QCOMPARE(presentation.count(QStringLiteral("signal_mark")), 2);
			QVERIFY(!c.codesToCsv().contains(QStringLiteral("signal_mark")));

			int skipped = 0;
			QVERIFY(PidCodification::fromCsv(c.toCsv(), &skipped) == c);
			QCOMPARE(skipped, 0);
				// the default presentation brings them to a new project
			QVERIFY(PidCodification().completedWith(PidCodification::fromCsv(presentation)) == c);
		}

			// A file which has marks says them all: a type it leaves out
			// takes the default one again. A file without any leaves those of
			// the project alone, and a file of marks alone leaves the rest.
		void aFileWithMarksReplacesThemAll()
		{
			PidCodification project;
			project.setSignalMark(SignalProperties::Pneumatic, SignalMark::Bar2);
			project.setSignalMark(SignalProperties::Capillary, SignalMark::None);
			project.object_colors.insert(QStringLiteral("signal"), QColor(0, 0, 255));
			project.text_sizes.insert(QStringLiteral("valve"), 8);

			const PidCodification marks = PidCodification::fromCsv(QStringLiteral(
				"signal_mark;electrical;dashes\n"));
			const PidCodification after = marks.completedWith(project);
			QCOMPARE(int(after.signalMark(SignalProperties::Electrical)), int(SignalMark::Dashes));
			QCOMPARE(int(after.signalMark(SignalProperties::Pneumatic)), int(SignalMark::AMark));
			QCOMPARE(int(after.signalMark(SignalProperties::Capillary)), int(SignalMark::Cross));
			QCOMPARE(after.object_colors, project.object_colors);
			QCOMPARE(after.text_sizes, project.text_sizes);

			const PidCodification colors = PidCodification::fromCsv(QStringLiteral(
				"object_color;valve;#808000\n"));
			const PidCodification kept = colors.completedWith(project);
			QCOMPARE(kept.signal_marks, project.signal_marks);
			QCOMPARE(kept.object_colors.value(QStringLiteral("valve")), QColor(0x80, 0x80, 0));
		}

			// ---- the page P&ID of the properties of the project ----
		void thePageShowsTheMarkOfEachType()
		{
			QETProject project;
			PidCodification c;
			c.setSignalMark(SignalProperties::Pneumatic, SignalMark::Bar2);
			project.setPidCodification(c);

			PidConfigPage page(&project);
			for (SignalProperties::SignalType type : SignalProperties::types())
			{
				auto *marks = page.findChild<QComboBox *>(
							QStringLiteral("signal_mark_") + SignalProperties::typeToString(type));
				QVERIFY2(marks, qPrintable(SignalProperties::typeToString(type)));
					// the whole catalogue, each mark with its picture and its name
				QCOMPARE(marks->count(), int(SignalMark::all().size()));
				for (int i = 0 ; i < marks->count() ; ++i) {
					QVERIFY(!marks->itemIcon(i).isNull());
					QVERIFY(!marks->itemText(i).isEmpty());
				}
				QCOMPARE(marks->currentData().toString(),
						 SignalMark::toString(project.pidCodification().signalMark(type)));
			}
		}

		void aMarkChosenInThePageReachesTheProjectWithUndo()
		{
			QETProject project;
			PidConfigPage page(&project);
			auto *electrical = page.findChild<QComboBox *>(QStringLiteral("signal_mark_electrical"));
			QVERIFY(electrical);
			QCOMPARE(electrical->currentData().toString(), QStringLiteral("e_mark"));

				// as the user does: a choice in the list
			const int dashes = electrical->findData(QStringLiteral("dashes"));
			QVERIFY(dashes >= 0);
			electrical->setCurrentIndex(dashes);
			emit electrical->activated(dashes);
				// nothing reaches the project before the page is applied
			QVERIFY(project.pidCodification().signal_marks.isEmpty());
			const int steps = project.undoStack()->count();
			page.applyProjectConf();
			QCOMPARE(project.undoStack()->count(), steps + 1);
			QCOMPARE(int(project.pidCodification().signalMark(SignalProperties::Electrical)),
					 int(SignalMark::Dashes));

			project.undoStack()->undo();
			QVERIFY(project.pidCodification().signal_marks.isEmpty());
			QCOMPARE(electrical->currentData().toString(), QStringLiteral("e_mark"));
			project.undoStack()->redo();
			QCOMPARE(electrical->currentData().toString(), QStringLiteral("dashes"));

				// the default one chosen again: nothing is kept
			page.setSignalMark(SignalProperties::Electrical, SignalMark::EMark);
			page.applyProjectConf();
			QVERIFY(project.pidCodification().signal_marks.isEmpty());
			QVERIFY(project.pidCodification().isEmpty());
		}

		void aButtonGivesTheDefaultMarksBack()
		{
			QETProject project;
			PidCodification c;
			c.setSignalMark(SignalProperties::Pneumatic, SignalMark::Bar2);
			c.setSignalMark(SignalProperties::DataLink, SignalMark::None);
				// a type of a later version: not this page's to take away
			c.signal_marks.insert(QStringLiteral("mechanical"), QStringLiteral("bar1"));
			project.setPidCodification(c);

			PidConfigPage page(&project);
			auto *reset = page.findChild<QPushButton *>(QStringLiteral("signal_marks_reset"));
			auto *pneumatic = page.findChild<QComboBox *>(QStringLiteral("signal_mark_pneumatic"));
			QVERIFY(reset && pneumatic);
			QCOMPARE(pneumatic->currentData().toString(), QStringLiteral("bar2"));
			reset->click();
			QCOMPARE(pneumatic->currentData().toString(), QStringLiteral("a_mark"));
			page.applyProjectConf();
			QCOMPARE(project.pidCodification().signal_marks.keys(),
					 QStringList{QStringLiteral("mechanical")});
		}

			// A mark of a later version: the list shows the mark drawn, the
			// default one, and the word stays in the project as long as the
			// list is not touched
		void aMarkOfALaterVersionStaysUnlessItsListIsTouched()
		{
			QETProject project;
			PidCodification c;
			c.signal_marks.insert(QStringLiteral("pneumatic"), QStringLiteral("diamond"));
			project.setPidCodification(c);

			PidConfigPage page(&project);
			auto *pneumatic = page.findChild<QComboBox *>(QStringLiteral("signal_mark_pneumatic"));
			QVERIFY(pneumatic);
			QCOMPARE(pneumatic->currentData().toString(), QStringLiteral("a_mark"));
			page.setSignalMark(SignalProperties::Hydraulic, SignalMark::Bar3);
			page.applyProjectConf();
			QCOMPARE(project.pidCodification().signal_marks.value(QStringLiteral("pneumatic")),
					 QStringLiteral("diamond"));
			QCOMPARE(int(project.pidCodification().signalMark(SignalProperties::Hydraulic)),
					 int(SignalMark::Bar3));
		}

			// A file of marks alone, such as those of another standard: the
			// page says what it read, takes them all, and keeps its colours
		void aFileOfMarksIsImportedInThePage()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString other_marks = dir.filePath(QStringLiteral("other.csv"));
			QVERIFY(writeFile(other_marks, "kind;key;value\n"
										   "signal_mark;undefined;bar1\n"
										   "signal_mark;pneumatic;bar2\n"
										   "signal_mark;electrical;dashes\n"));
			const QString default_marks = dir.filePath(QStringLiteral("default.csv"));
			QVERIFY(writeFile(default_marks, "signal_mark;pneumatic;a_mark\n"));

			QETProject project;
			PidCodification c;
			c.setSignalMark(SignalProperties::Capillary, SignalMark::None);
			c.object_colors.insert(QStringLiteral("signal"), QColor(0, 0, 255));
			project.setPidCodification(c);

			PidConfigPage page(&project);
			QVERIFY(page.importCsv(other_marks));
			QVERIFY2(page.codeMessage().contains(QStringLiteral("marks of the signal lines")),
					 qPrintable(page.codeMessage()));
			QVERIFY(!page.codeMessage().contains(QStringLiteral("fluids")));
			QVERIFY(!page.codeMessage().contains(QStringLiteral("presentation")));
			QCOMPARE(page.findChild<QComboBox *>(QStringLiteral("signal_mark_pneumatic"))
					 ->currentData().toString(), QStringLiteral("bar2"));
				// the type the file leaves out: the default one again
			QCOMPARE(page.findChild<QComboBox *>(QStringLiteral("signal_mark_capillary"))
					 ->currentData().toString(), QStringLiteral("cross"));
			page.applyProjectConf();
			const PidCodification applied = project.pidCodification();
			QCOMPARE(int(applied.signalMark(SignalProperties::Pneumatic)), int(SignalMark::Bar2));
			QCOMPARE(int(applied.signalMark(SignalProperties::Electrical)), int(SignalMark::Dashes));
				// the default mark the file said is not kept
			QCOMPARE(applied.signal_marks.size(), 2);
			QCOMPARE(applied.object_colors.value(QStringLiteral("signal")), QColor(0, 0, 255));

				// a file which says default marks only: back to them
			QVERIFY(page.importCsv(default_marks));
			page.applyProjectConf();
			QVERIFY(project.pidCodification().signal_marks.isEmpty());
			QCOMPARE(project.pidCodification().object_colors.value(QStringLiteral("signal")),
					 QColor(0, 0, 255));
		}

		void aProjectOpenedReadOnlyShowsItsMarksGreyed()
		{
			QETProject project;
			project.setReadOnly(true);
			PidConfigPage page(&project);
			QVERIFY(!page.findChild<QComboBox *>(QStringLiteral("signal_mark_pneumatic"))->isEnabled());
			QVERIFY(!page.findChild<QPushButton *>(QStringLiteral("signal_marks_reset"))->isEnabled());
		}
};

QET_APP_TEST_MAIN(tst_signalmarksettings)
#include "tst_signalmarksettings.moc"
