/* Unit tests for batch DELETE operations (Database::remove, Index::removeRecords)
   Tests the batched IN-clause removal from PR #3 */
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "database.h"
#include "colordescindex.h"
#include "dctfeaturesindex.h"
#include "cvfeaturesindex.h"

class TestBatchRemove : public QObject {
  Q_OBJECT

 private:
  int _connId = 0;

  /// RAII wrapper that sets up Database + indexes in a temporary directory
  struct TestEnv {
    QTemporaryDir tmpDir;
    Database* database = nullptr;
    ColorDescIndex* colorIdx = nullptr;
    DctFeaturesIndex* dctIdx = nullptr;
    CvFeaturesIndex* cvIdx = nullptr;

    void init(bool color = true, bool dct = true, bool cv = true) {
      Q_ASSERT(tmpDir.isValid());
      database = new Database(tmpDir.path());
      if (color) {
        colorIdx = new ColorDescIndex;
        database->addIndex(colorIdx);
      }
      if (dct) {
        dctIdx = new DctFeaturesIndex;
        database->addIndex(dctIdx);
      }
      if (cv) {
        cvIdx = new CvFeaturesIndex;
        database->addIndex(cvIdx);
      }
      database->setup();
    }

    ~TestEnv() {
      delete database;
      delete colorIdx;
      delete dctIdx;
      delete cvIdx;
      Database::disconnectAll();
    }
  };

  /// Execute a SELECT COUNT(*) and return the result
  int countRows(const QString& dbFile, const QString& table) {
    QString name = QString("cnt_%1").arg(_connId++);
    int n = 0;
    {
      QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
      db.setDatabaseName(dbFile);
      db.open();
      QSqlQuery q(db);
      q.exec("SELECT COUNT(*) FROM " + table);
      if (q.next()) n = q.value(0).toInt();
    }
    QSqlDatabase::removeDatabase(name);
    return n;
  }

  /// Check if a specific id exists in a table
  bool idExists(const QString& dbFile, const QString& table,
                const QString& idCol, int id) {
    QString name = QString("chk_%1").arg(_connId++);
    bool exists = false;
    {
      QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
      db.setDatabaseName(dbFile);
      db.open();
      QSqlQuery q(db);
      q.exec(QString("SELECT COUNT(*) FROM %1 WHERE %2=%3")
                 .arg(table, idCol)
                 .arg(id));
      if (q.next()) exists = q.value(0).toInt() > 0;
    }
    QSqlDatabase::removeDatabase(name);
    return exists;
  }

  /// Insert N rows into the media table (ids 1..count)
  void insertMedia(Database& db, int count) {
    QString name = QString("ins_m_%1").arg(_connId++);
    {
      QSqlDatabase conn = QSqlDatabase::addDatabase("QSQLITE", name);
      conn.setDatabaseName(db.dbPath(0));
      conn.open();
      QSqlQuery q(conn);
      for (int i = 1; i <= count; i++)
        q.exec(QString("INSERT INTO media "
                       "(id,type,path,width,height,md5,phash_dct) "
                       "VALUES (%1,1,'/test/%1.jpg',100,100,'md5_%1',%1)")
                   .arg(i));
    }
    QSqlDatabase::removeDatabase(name);
  }

  /// Insert N rows into the color table (media_id 1..count)
  void insertColorRows(Database& db, int count) {
    QString name = QString("ins_c_%1").arg(_connId++);
    {
      QSqlDatabase conn = QSqlDatabase::addDatabase("QSQLITE", name);
      conn.setDatabaseName(db.dbPath(SearchParams::AlgoColor));
      conn.open();
      QSqlQuery q(conn);
      for (int i = 1; i <= count; i++) {
        q.prepare("INSERT INTO color (media_id, color_desc) "
                  "VALUES (:id, :d)");
        q.bindValue(":id", i);
        q.bindValue(":d", QByteArray(64, char(i & 0xFF)));
        q.exec();
      }
    }
    QSqlDatabase::removeDatabase(name);
  }

  /// Insert N rows into the kphash table (media_id 1..count)
  void insertKphashRows(Database& db, int count) {
    QString name = QString("ins_k_%1").arg(_connId++);
    {
      QSqlDatabase conn = QSqlDatabase::addDatabase("QSQLITE", name);
      conn.setDatabaseName(db.dbPath(SearchParams::AlgoDCTFeatures));
      conn.open();
      QSqlQuery q(conn);
      for (int i = 1; i <= count; i++) {
        q.prepare("INSERT INTO kphash (media_id, hashes) "
                  "VALUES (:id, :h)");
        q.bindValue(":id", i);
        q.bindValue(":h", QByteArray(int(sizeof(uint64_t) * 4),
                                     char(i & 0xFF)));
        q.exec();
      }
    }
    QSqlDatabase::removeDatabase(name);
  }

  /// Insert N rows into the matrix table (media_id 1..count)
  void insertMatrixRows(Database& db, int count) {
    QString name = QString("ins_x_%1").arg(_connId++);
    {
      QSqlDatabase conn = QSqlDatabase::addDatabase("QSQLITE", name);
      conn.setDatabaseName(db.dbPath(SearchParams::AlgoCVFeatures));
      conn.open();
      QSqlQuery q(conn);
      for (int i = 1; i <= count; i++) {
        q.prepare("INSERT INTO matrix "
                  "(media_id, rows, cols, type, stride, data) "
                  "VALUES (:mid, 32, 32, 5, 128, :d)");
        q.bindValue(":mid", i);
        q.bindValue(":d", QByteArray(32 * 128, char(i & 0xFF)));
        q.exec();
      }
    }
    QSqlDatabase::removeDatabase(name);
  }

  /// Insert N rows into all four tables
  void insertAllTables(Database& db, int count) {
    insertMedia(db, count);
    insertColorRows(db, count);
    insertKphashRows(db, count);
    insertMatrixRows(db, count);
  }

  /// Open a QSqlDatabase for use in removeRecords() calls
  QSqlDatabase openConn(const QString& dbFile) {
    QString name = QString("op_%1").arg(_connId++);
    QSqlDatabase conn = QSqlDatabase::addDatabase("QSQLITE", name);
    conn.setDatabaseName(dbFile);
    if (!conn.open())
      qFatal("openConn: %s", qPrintable(conn.lastError().text()));
    return conn;
  }

  void closeConn(QSqlDatabase& conn) {
    QString name = conn.connectionName();
    conn.close();
    conn = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
  }

 private slots:
  // Database::remove() tests
  void removeSingle();
  void removeMultiple();
  void removeAll();
  void removeCrossBatch();
  void removeIdZero();
  void removeEmpty();

  // Individual Index::removeRecords() tests
  void colorDescRemoveRecords();
  void dctFeaturesRemoveRecords();
  void cvFeaturesRemoveRecords();
  void multiBatchRemoveRecords();
  void convenienceOverload();

  // Integration tests
  void removeConsistency();
  void removeLargeBatch();
};

// --- Database::remove() tests ---

void TestBatchRemove::removeSingle() {
  TestEnv env;
  env.init();
  insertMedia(*env.database, 5);

  QCOMPARE(countRows(env.database->dbPath(0), "media"), 5);

  env.database->remove(3);

  QCOMPARE(countRows(env.database->dbPath(0), "media"), 4);
  QVERIFY(!idExists(env.database->dbPath(0), "media", "id", 3));
  QVERIFY(idExists(env.database->dbPath(0), "media", "id", 1));
  QVERIFY(idExists(env.database->dbPath(0), "media", "id", 5));
}

void TestBatchRemove::removeMultiple() {
  TestEnv env;
  env.init();
  insertMedia(*env.database, 10);

  const QVector<int> toRemove = {2, 4, 6, 8, 10};
  env.database->remove(toRemove);

  QCOMPARE(countRows(env.database->dbPath(0), "media"), 5);
  for (int id : toRemove)
    QVERIFY(!idExists(env.database->dbPath(0), "media", "id", id));
  for (int id : {1, 3, 5, 7, 9})
    QVERIFY(idExists(env.database->dbPath(0), "media", "id", id));
}

void TestBatchRemove::removeAll() {
  TestEnv env;
  env.init();
  insertMedia(*env.database, 20);

  QVector<int> ids;
  for (int i = 1; i <= 20; i++) ids.append(i);
  env.database->remove(ids);

  QCOMPARE(countRows(env.database->dbPath(0), "media"), 0);
}

void TestBatchRemove::removeCrossBatch() {
  TestEnv env;
  env.init();
  const int total = 750;  // crosses the 500-row batch boundary
  insertMedia(*env.database, total);

  QVector<int> ids;
  for (int i = 1; i <= total; i++) ids.append(i);
  env.database->remove(ids);

  QCOMPARE(countRows(env.database->dbPath(0), "media"), 0);
}

void TestBatchRemove::removeIdZero() {
  TestEnv env;
  env.init();
  insertMedia(*env.database, 5);

  // id=0 causes the entire remove call to abort
  const QVector<int> ids = {1, 0, 3};
  env.database->remove(ids);

  // nothing should be deleted
  QCOMPARE(countRows(env.database->dbPath(0), "media"), 5);
}

void TestBatchRemove::removeEmpty() {
  TestEnv env;
  env.init();
  insertMedia(*env.database, 5);

  const QVector<int> empty;
  env.database->remove(empty);

  QCOMPARE(countRows(env.database->dbPath(0), "media"), 5);
}

// --- Individual Index::removeRecords() tests ---

void TestBatchRemove::colorDescRemoveRecords() {
  TestEnv env;
  env.init(true, false, false);
  insertColorRows(*env.database, 10);

  const QString dbFile =
      env.database->dbPath(SearchParams::AlgoColor);
  QCOMPARE(countRows(dbFile, "color"), 10);

  const QVector<QString> batches = {"2,4,6"};
  QSqlDatabase conn = openConn(dbFile);
  env.colorIdx->removeRecords(conn, batches);
  closeConn(conn);

  QCOMPARE(countRows(dbFile, "color"), 7);
  QVERIFY(!idExists(dbFile, "color", "media_id", 2));
  QVERIFY(!idExists(dbFile, "color", "media_id", 4));
  QVERIFY(!idExists(dbFile, "color", "media_id", 6));
  QVERIFY(idExists(dbFile, "color", "media_id", 1));
  QVERIFY(idExists(dbFile, "color", "media_id", 10));
}

void TestBatchRemove::dctFeaturesRemoveRecords() {
  TestEnv env;
  env.init(false, true, false);
  insertKphashRows(*env.database, 10);

  const QString dbFile =
      env.database->dbPath(SearchParams::AlgoDCTFeatures);
  QCOMPARE(countRows(dbFile, "kphash"), 10);

  const QVector<QString> batches = {"1,5,9"};
  QSqlDatabase conn = openConn(dbFile);
  env.dctIdx->removeRecords(conn, batches);
  closeConn(conn);

  QCOMPARE(countRows(dbFile, "kphash"), 7);
  QVERIFY(!idExists(dbFile, "kphash", "media_id", 1));
  QVERIFY(!idExists(dbFile, "kphash", "media_id", 5));
  QVERIFY(!idExists(dbFile, "kphash", "media_id", 9));
  QVERIFY(idExists(dbFile, "kphash", "media_id", 3));
}

void TestBatchRemove::cvFeaturesRemoveRecords() {
  TestEnv env;
  env.init(false, false, true);
  insertMatrixRows(*env.database, 10);

  const QString dbFile =
      env.database->dbPath(SearchParams::AlgoCVFeatures);
  QCOMPARE(countRows(dbFile, "matrix"), 10);

  const QVector<QString> batches = {"3,7"};
  QSqlDatabase conn = openConn(dbFile);
  env.cvIdx->removeRecords(conn, batches);
  closeConn(conn);

  QCOMPARE(countRows(dbFile, "matrix"), 8);
  QVERIFY(!idExists(dbFile, "matrix", "media_id", 3));
  QVERIFY(!idExists(dbFile, "matrix", "media_id", 7));
  QVERIFY(idExists(dbFile, "matrix", "media_id", 1));
  QVERIFY(idExists(dbFile, "matrix", "media_id", 10));
}

void TestBatchRemove::multiBatchRemoveRecords() {
  TestEnv env;
  env.init(true, false, false);
  insertColorRows(*env.database, 15);

  const QString dbFile =
      env.database->dbPath(SearchParams::AlgoColor);
  QCOMPARE(countRows(dbFile, "color"), 15);

  // multiple batch strings, as Database::remove() would build them
  const QVector<QString> batches = {"1,2,3,4,5", "6,7,8,9,10"};
  QSqlDatabase conn = openConn(dbFile);
  env.colorIdx->removeRecords(conn, batches);
  closeConn(conn);

  QCOMPARE(countRows(dbFile, "color"), 5);
  for (int i = 1; i <= 10; i++)
    QVERIFY(!idExists(dbFile, "color", "media_id", i));
  for (int i = 11; i <= 15; i++)
    QVERIFY(idExists(dbFile, "color", "media_id", i));
}

void TestBatchRemove::convenienceOverload() {
  TestEnv env;
  env.init(true, false, false);
  insertColorRows(*env.database, 10);

  const QString dbFile =
      env.database->dbPath(SearchParams::AlgoColor);

  // use the QVector<int> convenience overload (Index base class)
  const QVector<int> ids = {2, 5, 8};
  QSqlDatabase conn = openConn(dbFile);
  static_cast<Index*>(env.colorIdx)->removeRecords(conn, ids);
  closeConn(conn);

  QCOMPARE(countRows(dbFile, "color"), 7);
  for (int id : ids)
    QVERIFY(!idExists(dbFile, "color", "media_id", id));
  QVERIFY(idExists(dbFile, "color", "media_id", 1));
  QVERIFY(idExists(dbFile, "color", "media_id", 10));
}

// --- Integration tests ---

void TestBatchRemove::removeConsistency() {
  TestEnv env;
  env.init();
  insertAllTables(*env.database, 20);

  const QString mediaDb = env.database->dbPath(0);
  const QString colorDb =
      env.database->dbPath(SearchParams::AlgoColor);
  const QString kphashDb =
      env.database->dbPath(SearchParams::AlgoDCTFeatures);
  const QString matrixDb =
      env.database->dbPath(SearchParams::AlgoCVFeatures);

  QCOMPARE(countRows(mediaDb, "media"), 20);
  QCOMPARE(countRows(colorDb, "color"), 20);
  QCOMPARE(countRows(kphashDb, "kphash"), 20);
  QCOMPARE(countRows(matrixDb, "matrix"), 20);

  // remove first 10
  QVector<int> ids;
  for (int i = 1; i <= 10; i++) ids.append(i);
  env.database->remove(ids);

  QCOMPARE(countRows(mediaDb, "media"), 10);
  QCOMPARE(countRows(colorDb, "color"), 10);
  QCOMPARE(countRows(kphashDb, "kphash"), 10);
  QCOMPARE(countRows(matrixDb, "matrix"), 10);

  // verify correct IDs survive in all tables
  for (int i = 1; i <= 10; i++) {
    QVERIFY(!idExists(mediaDb, "media", "id", i));
    QVERIFY(!idExists(colorDb, "color", "media_id", i));
    QVERIFY(!idExists(kphashDb, "kphash", "media_id", i));
    QVERIFY(!idExists(matrixDb, "matrix", "media_id", i));
  }
  for (int i = 11; i <= 20; i++) {
    QVERIFY(idExists(mediaDb, "media", "id", i));
    QVERIFY(idExists(colorDb, "color", "media_id", i));
    QVERIFY(idExists(kphashDb, "kphash", "media_id", i));
    QVERIFY(idExists(matrixDb, "matrix", "media_id", i));
  }
}

void TestBatchRemove::removeLargeBatch() {
  TestEnv env;
  env.init();
  const int total = 1500;  // 3 batches of 500
  insertAllTables(*env.database, total);

  QVector<int> ids;
  for (int i = 1; i <= total; i++) ids.append(i);

  QElapsedTimer timer;
  timer.start();
  env.database->remove(ids);
  const qint64 elapsed = timer.elapsed();

  QCOMPARE(countRows(env.database->dbPath(0), "media"), 0);
  QCOMPARE(countRows(env.database->dbPath(SearchParams::AlgoColor),
                     "color"),
           0);
  QCOMPARE(countRows(env.database->dbPath(SearchParams::AlgoDCTFeatures),
                     "kphash"),
           0);
  QCOMPARE(countRows(env.database->dbPath(SearchParams::AlgoCVFeatures),
                     "matrix"),
           0);

  qDebug() << "Removed" << total
           << "entries from all tables in" << elapsed << "ms";
  QVERIFY2(elapsed < 30000,
           qPrintable(QString("took %1ms, expected < 30s").arg(elapsed)));
}

QTEST_MAIN(TestBatchRemove)
#include "testbatchremove.moc"
