// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest>

#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "elementscollectioncache.h"

// Every QElectroTech on a computer shares one elements_cache.sqlite. The cache
// used PRAGMA locking_mode = EXCLUSIVE, so a QElectroTech that had once read
// the file kept it locked until it closed, and every write from a second one
// waited SQLite's 5 s busy timeout and failed -- on the GUI thread, twice per
// symbol shown in the collection (#1571). Two caches on the same file in one
// process lock each other exactly as two processes do.
class tst_elementcachelock : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;
	const QUuid m_uuid = QUuid::createUuid();

private slots:
	void secondCacheWritesWithoutWaiting()
	{
		QVERIFY(m_dir.isValid());
		const QString path = m_dir.filePath(QStringLiteral("elements_cache.sqlite"));

		ElementsCollectionCache first(path);
		QVERIFY(first.cacheName(QStringLiteral("embed://a.elmt"), m_uuid));
		QVERIFY(first.fetchNameFromCache(QStringLiteral("embed://a.elmt"), m_uuid));
		QVERIFY(!first.fetchNameFromCache(QStringLiteral("embed://missing.elmt"), m_uuid));

		ElementsCollectionCache second(path);
		QElapsedTimer timer;
		timer.start();
		QVERIFY(second.cacheName(QStringLiteral("embed://b.elmt"), m_uuid));
		QVERIFY(second.cachePixmap(QStringLiteral("embed://b.elmt"), m_uuid));
		QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed()) + " ms"));

		// and each one sees what the other wrote
		QVERIFY(second.fetchNameFromCache(QStringLiteral("embed://a.elmt"), m_uuid));
		QVERIFY(first.fetchNameFromCache(QStringLiteral("embed://b.elmt"), m_uuid));
		QVERIFY(first.cacheName(QStringLiteral("embed://c.elmt"), m_uuid));
	}

		// A cache whose previews were drawn another way (its mark in the
		// table meta another value) is dropped once when opened: the
		// statement reading the mark used to hold the file, and the drop
		// failed with no word while the new mark was written (8b)
	void aCacheDrawnAnotherWayIsDropped()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("elements_cache.sqlite"));
		{
			ElementsCollectionCache cache(path);
			QVERIFY(cache.cacheName(QStringLiteral("embed://old.elmt"), m_uuid));
		}
		{
			QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
														 QStringLiteral("tst_elementcachelock_raw"));
			raw.setDatabaseName(path);
			QVERIFY(raw.open());
			QVERIFY(QSqlQuery(raw).exec(QStringLiteral(
				"REPLACE INTO meta (key, value) VALUES ('pixmaps', 'transparent')")));
			raw.close();
		}
		QSqlDatabase::removeDatabase(QStringLiteral("tst_elementcachelock_raw"));

		ElementsCollectionCache reopened(path);
		QVERIFY(!reopened.fetchNameFromCache(QStringLiteral("embed://old.elmt"), m_uuid));
			// and kept, once dropped
		QVERIFY(reopened.cacheName(QStringLiteral("embed://new.elmt"), m_uuid));
		ElementsCollectionCache again(path);
		QVERIFY(again.fetchNameFromCache(QStringLiteral("embed://new.elmt"), m_uuid));
	}
};

QTEST_MAIN(tst_elementcachelock)
#include "tst_elementcachelock.moc"
