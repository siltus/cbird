#include <QtTest/QtTest>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUuid>
#include <QtGui/QImage>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

class TestUpdateWorkflows : public QObject {
  Q_OBJECT

  QString _cbird;
  QString _ffmpeg;
  QString _root;

  void run(const QString& program, const QStringList& args, QByteArray& output) {
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("CBIRD_NO_COLORS", "1");
    process.setProcessEnvironment(env);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, args);
    QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
    QVERIFY2(process.waitForFinished(30000), qPrintable(process.errorString()));
    output = process.readAllStandardOutput();
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, output.constData());
  }

  QStringList updateArgs(const QString& root) const {
    return {"-headless", "-args", "none", "-use", root, "-create",
            "-i.fsize", "0", "-i.idxthr", "2", "-i.decthr", "2"};
  }

  void makeVideo(const QString& path, const QString& codec, const QString& format) {
    QByteArray output;
    run(_ffmpeg, {"-hide_banner", "-loglevel", "error", "-nostdin",
                  "-f", "lavfi", "-i", "testsrc2=size=128x96:rate=12",
                  "-t", "1", "-an", "-c:v", codec, "-f", format, path}, output);
    QVERIFY2(QFileInfo(path).size() > 0, output.constData());
  }

  void checkMedia(const QString& root, const QMap<QString, int>& expected) {
    const QString path = QDir(root).filePath("_index/media0.db");
    QVERIFY(QFileInfo::exists(path));
    const QString name = QUuid::createUuid().toString();
    QMap<QString, int> actual;
    QString error;
    {
      auto db = QSqlDatabase::addDatabase("QSQLITE", name);
      db.setDatabaseName(path);
      if (!db.open()) {
        error = db.lastError().text();
      } else {
        QSqlQuery query(db);
        if (!query.exec("select path,type from media")) {
          error = query.lastError().text();
        } else {
          while (query.next())
            actual.insert(query.value(0).toString(), query.value(1).toInt());
        }
      }
      db.close();
    }
    QSqlDatabase::removeDatabase(name);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(actual, expected);
  }

 private slots:
  void initTestCase() {
    _cbird = qEnvironmentVariable("CBIRD_TEST_EXE");
    _ffmpeg = qEnvironmentVariable("CBIRD_TEST_FFMPEG");
    _root = qEnvironmentVariable("CBIRD_TEST_ROOT");
    QVERIFY2(QFileInfo(_cbird).isExecutable(), "Set CBIRD_TEST_EXE to the built cbird executable");
    QVERIFY2(QFileInfo(_ffmpeg).isExecutable(), "Set CBIRD_TEST_FFMPEG to ffmpeg");
    QVERIFY2(QFileInfo(_root).isDir(), "Set CBIRD_TEST_ROOT to an isolated fixture directory");
#ifdef Q_OS_WIN
    QVERIFY2(QDir::fromNativeSeparators(_root).startsWith("D:/", Qt::CaseInsensitive),
             "Windows test fixtures must stay on D:");
#endif
  }

  void privacyOutput_data() {
    QTest::addColumn<bool>("privateLogs");
    QTest::newRow("plain") << false;
    QTest::newRow("pii") << true;
  }

  void privacyOutput() {
    QFETCH(bool, privateLogs);
    QTemporaryDir temp(QDir(_root).filePath("cbird-privacy-XXXXXX"));
    QVERIFY(temp.isValid());
    const QString root = temp.filePath("Private Person");
    QVERIFY(QDir().mkpath(root));
    QImage image(128, 96, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(QDir(root).filePath("Secret Holiday.png")));

    auto args = updateArgs(root);
    args << "-v" << "-i.verbose" << "true" << "-i.algos" << "dct" << "-update";
    if (privateLogs) args.prepend("-pii");
    QByteArray output;
    run(_cbird, args, output);
    QVERIFY(!output.isEmpty());
    if (privateLogs) {
      QVERIFY2(!output.contains("Private"), output.constData());
      QVERIFY2(!output.contains("Person"), output.constData());
      QVERIFY2(!output.contains("Secret"), output.constData());
      QVERIFY2(!output.contains("Holiday"), output.constData());
    } else {
      QVERIFY2(output.contains("./Secret Holiday.png"), output.constData());
    }
    checkMedia(root, {{"Secret Holiday.png", 1}});
  }

  void supportedVideoContainers_data() {
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("codec");
    QTest::addColumn<QString>("format");
    QTest::newRow("mp4") << "clip.mp4" << "mpeg4" << "mp4";
    QTest::newRow("matroska") << "clip.mkv" << "mpeg4" << "matroska";
    QTest::newRow("avi") << "clip.avi" << "mpeg4" << "avi";
    QTest::newRow("quicktime") << "clip.mov" << "mpeg4" << "mov";
    QTest::newRow("mpeg-program") << "clip.mpg" << "mpeg2video" << "mpeg";
    QTest::newRow("mpeg-transport") << "clip.ts" << "mpeg2video" << "mpegts";
    QTest::newRow("realmedia") << "clip.rm" << "rv20" << "rm";
  }

  void supportedVideoContainers() {
    QFETCH(QString, fileName);
    QFETCH(QString, codec);
    QFETCH(QString, format);
    QTemporaryDir temp(QDir(_root).filePath("cbird-container-XXXXXX"));
    QVERIFY(temp.isValid());
    makeVideo(temp.filePath(fileName), codec, format);

    auto args = updateArgs(temp.path());
    args << "-i.algos" << "video" << "-i.ignored" << "true" << "-update";
    QByteArray output;
    run(_cbird, args, output);
    checkMedia(temp.path(), {{fileName, 2}});
    const auto indexes = QDir(temp.filePath("_index/video")).entryList({"*.vdx"}, QDir::Files);
    QCOMPARE(indexes.size(), 1);
  }

  void rejectsSourceFilesWithVideoExtensions() {
    QTemporaryDir temp(QDir(_root).filePath("cbird-nonvideo-XXXXXX"));
    QVERIFY(temp.isValid());
    makeVideo(temp.filePath("clip.mp4"), "mpeg4", "mp4");
    QFile source(temp.filePath("application.ts"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray text("export const title = 'This is TypeScript, not a transport stream';\n");
    QCOMPARE(source.write(text), text.size());
    source.close();

    auto args = updateArgs(temp.path());
    args << "-i.algos" << "video" << "-i.ignored" << "true" << "-update";
    QByteArray output;
    run(_cbird, args, output);
    checkMedia(temp.path(), {{"clip.mp4", 2}});
    QVERIFY2(output.contains("unsupported"), output.constData());
  }

  void mixedQueueAndMissingIndexRecovery() {
    QTemporaryDir temp(QDir(_root).filePath("cbird-mixed-XXXXXX"));
    QVERIFY(temp.isValid());
    const QString original = temp.filePath("video-0.mp4");
    makeVideo(original, "mpeg4", "mp4");
    QMap<QString, int> expected;
    QImage image(128, 96, QImage::Format_RGB32);
    for (int i = 0; i < 8; ++i) {
      const QString videoName = QString("video-%1.mp4").arg(i);
      if (i > 0) QVERIFY(QFile::copy(original, temp.filePath(videoName)));
      expected.insert(videoName, 2);
      const QString imageName = QString("image-%1.png").arg(i);
      image.fill(QColor::fromHsv(i * 30, 200, 200));
      QVERIFY(image.save(temp.filePath(imageName)));
      expected.insert(imageName, 1);
    }

    auto args = updateArgs(temp.path());
    args << "-i.algos" << "dct+video" << "-i.ljf" << "false"
         << "-i.bsize" << "2" << "-update";
    QByteArray output;
    run(_cbird, args, output);
    checkMedia(temp.path(), expected);
    QDir indexes(temp.filePath("_index/video"));
    auto files = indexes.entryList({"*.vdx"}, QDir::Files);
    QCOMPARE(files.size(), 8);

    // Remove only one index created by this test, then exercise routine-update recovery.
    QVERIFY(indexes.remove(files.first()));
    run(_cbird, args, output);
    checkMedia(temp.path(), expected);
    files = indexes.entryList({"*.vdx"}, QDir::Files);
    QCOMPARE(files.size(), 8);
    for (const QString& file : files)
      QVERIFY(QFileInfo(indexes.filePath(file)).size() > 0);
  }
};

QTEST_GUILESS_MAIN(TestUpdateWorkflows)
#include "testupdateworkflows.moc"
