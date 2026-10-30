// Jobs and the monitor against a real temp directory.
#include "core/fileops.h"
#include "core/jobs/remove.h"
#include "core/jobs/transfer.h"
#include "core/location.h"
#include "core/monitor.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

void write(const QString& path, const QByteArray& content)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

QByteArray read(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

core::Location loc(const QString& path)
{
    return core::Location::fromPath(QFile::encodeName(path));
}

void runJob(core::Job& job)
{
    QSignalSpy done(&job, &core::Job::finished);
    job.start();
    while (done.isEmpty())
        QTest::qWait(5);
}

// a tree: root/a.txt, root/sub/b.txt, root/sub/deep/c.txt, root/link -> a.txt
void makeTree(const QString& root)
{
    QVERIFY(QDir().mkpath(root + "/sub/deep"));
    write(root + "/a.txt", "aaa");
    write(root + "/sub/b.txt", "bbbb");
    write(root + "/sub/deep/c.txt", "c");
    QVERIFY(QFile::link("a.txt", root + "/link"));
}

} // namespace

class JobsTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void createAndRename();
    void copyTree();
    void moveSameFilesystem();
    void conflictAnswers();
    void unattendedSkipsAndLogs();
    void abortMidway();
    void removePermanent();
    void trashOrDelete();
    void monitor();
    void mkdir();
};

void JobsTest::copyTree()
{
    QTemporaryDir tmp;
    makeTree(tmp.path() + "/src");
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));

    core::TransferJob job({loc(tmp.path() + "/src")}, loc(tmp.path() + "/dst"), false);
    core::JobProgress last;
    connect(&job, &core::Job::progress, [&](const core::JobProgress& p) { last = p; });
    runJob(job);

    QVERIFY(!job.aborted());
    QVERIFY(job.log().empty());
    QCOMPARE(read(tmp.path() + "/dst/src/a.txt"), QByteArray("aaa"));
    QCOMPARE(read(tmp.path() + "/dst/src/sub/deep/c.txt"), QByteArray("c"));
    QVERIFY(QFileInfo(tmp.path() + "/dst/src/link").isSymLink());  // the link, not the target
    QCOMPARE(last.phase, core::JobProgress::Phase::Done);
    QCOMPARE(last.itemsTotal, 7u);  // src, a, link, sub, b, deep, c
    QCOMPARE(last.itemsDone, last.itemsTotal);
    QCOMPARE(last.bytesTotal, 13u);  // 3 + 4 + 1, plus the link's own 5 bytes
    QVERIFY(QFile::exists(tmp.path() + "/src/a.txt"));  // copy leaves the source
}

void JobsTest::moveSameFilesystem()
{
    QTemporaryDir tmp;
    makeTree(tmp.path() + "/src");
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));

    core::TransferJob job({loc(tmp.path() + "/src")}, loc(tmp.path() + "/dst"), true);
    runJob(job);

    QVERIFY(!job.aborted());
    QVERIFY(!QFile::exists(tmp.path() + "/src"));
    QCOMPARE(read(tmp.path() + "/dst/src/sub/b.txt"), QByteArray("bbbb"));
}

void JobsTest::conflictAnswers()
{
    QTemporaryDir tmp;
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));
    write(tmp.path() + "/f.txt", "new");
    write(tmp.path() + "/dst/f.txt", "old");

    int asked = 0;
    core::Conflict seen;
    auto copyWith = [&](core::ConflictAction action) -> size_t {
        core::TransferJob job({loc(tmp.path() + "/f.txt")}, loc(tmp.path() + "/dst"), false);
        connect(&job, &core::Job::conflict, [&](const core::Conflict& c) {
            ++asked;
            seen = c;
            job.answerConflict(action, false);
        });
        runJob(job);
        return job.log().size();
    };

    QCOMPARE(copyWith(core::ConflictAction::Skip), 1u);
    QCOMPARE(asked, 1);
    QCOMPARE(seen.name, QStringLiteral("f.txt"));
    QCOMPARE(seen.sourceSize, 3u);
    QCOMPARE(read(tmp.path() + "/dst/f.txt"), QByteArray("old"));

    QCOMPARE(copyWith(core::ConflictAction::Rename), 0u);
    QCOMPARE(read(tmp.path() + "/dst/f (2).txt"), QByteArray("new"));

    QCOMPARE(copyWith(core::ConflictAction::Overwrite), 0u);
    QCOMPARE(read(tmp.path() + "/dst/f.txt"), QByteArray("new"));
}

void JobsTest::unattendedSkipsAndLogs()
{
    QTemporaryDir tmp;
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));
    write(tmp.path() + "/f.txt", "new");
    write(tmp.path() + "/dst/f.txt", "old");

    core::TransferJob job({loc(tmp.path() + "/f.txt")}, loc(tmp.path() + "/dst"), false);
    job.setUnattended(true);
    QSignalSpy asked(&job, &core::Job::conflict);
    runJob(job);
    QVERIFY(asked.isEmpty());
    QCOMPARE(job.log().size(), 1u);
    QCOMPARE(job.log().front().kind, core::LogEntry::Kind::Skipped);
    QCOMPARE(read(tmp.path() + "/dst/f.txt"), QByteArray("old"));
}

void JobsTest::abortMidway()
{
    QTemporaryDir tmp;
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));
    for (int i = 0; i < 300; ++i)
        write(tmp.path() + QString("/f%1").arg(i), QByteArray(1024, 'x'));
    std::vector<core::Location> sources;
    for (int i = 0; i < 300; ++i)
        sources.push_back(loc(tmp.path() + QString("/f%1").arg(i)));

    core::TransferJob job(sources, loc(tmp.path() + "/dst"), false);
    connect(&job, &core::Job::progress, [&](const core::JobProgress& p) {
        if (p.phase == core::JobProgress::Phase::Running && p.itemsDone >= 5)
            job.abort();
    });
    runJob(job);
    QVERIFY(job.aborted());
    const int copied = QDir(tmp.path() + "/dst").entryList(QDir::Files).size();
    QVERIFY(copied >= 5);
    QVERIFY(copied < 300);
}

void JobsTest::removePermanent()
{
    QTemporaryDir tmp;
    makeTree(tmp.path() + "/src");
    write(tmp.path() + "/single", "x");

    core::RemoveJob job({loc(tmp.path() + "/src"), loc(tmp.path() + "/single")}, true);
    core::JobProgress last;
    connect(&job, &core::Job::progress, [&](const core::JobProgress& p) { last = p; });
    runJob(job);
    QVERIFY(!job.aborted());
    QVERIFY(job.log().empty());
    QVERIFY(!QFile::exists(tmp.path() + "/src"));
    QVERIFY(!QFile::exists(tmp.path() + "/single"));
    QCOMPARE(last.itemsTotal, 8u);
}

void JobsTest::trashOrDelete()
{
    // The temp dir may sit on a filesystem without a trash folder; either
    // way the item must be gone, by trash or by the offered deletion.
    QTemporaryDir tmp;
    makeTree(tmp.path() + "/src");

    core::RemoveJob job({loc(tmp.path() + "/src")}, false);
    bool offered = false;
    connect(&job, &core::Job::failure, [&](const core::Failure& f) {
        offered = f.offerDelete;
        job.answerError(f.offerDelete ? core::ErrorAction::DeletePermanently : core::ErrorAction::Skip, false);
    });
    runJob(job);
    QVERIFY(!job.aborted());
    QVERIFY2(!QFile::exists(tmp.path() + "/src"), offered ? "deleted" : "trashed");
}

void JobsTest::monitor()
{
    QTemporaryDir tmp;
    core::Monitor mon;
    QSignalSpy changed(&mon, &core::Monitor::changed);
    mon.watch(loc(tmp.path()));
    QVERIFY(mon.active());
    write(tmp.path() + "/new", "x");
    QVERIFY(changed.wait(2000));
    // A burst becomes one signal after the quiet period.
    for (int i = 0; i < 20; ++i)
        write(tmp.path() + QString("/b%1").arg(i), "y");
    QVERIFY(changed.wait(2000));
    QTest::qWait(600);
    QCOMPARE(changed.size(), 2);
}

void JobsTest::mkdir()
{
    QTemporaryDir tmp;
    QString error = QStringLiteral("pending");
    core::makeDirectory(loc(tmp.path() + "/made"), [&](const QString& e) { error = e; });
    while (error == QStringLiteral("pending"))
        QTest::qWait(5);
    QVERIFY(error.isEmpty());
    QVERIFY(QFileInfo(tmp.path() + "/made").isDir());

    error = QStringLiteral("pending");
    core::makeDirectory(loc(tmp.path() + "/made"), [&](const QString& e) { error = e; });
    while (error == QStringLiteral("pending"))
        QTest::qWait(5);
    QVERIFY(!error.isEmpty());  // exists already
}

void JobsTest::createAndRename()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const core::Location dir = loc(tmp.path());

    QString error = QStringLiteral("pending");
    core::createFile(dir.child("new.txt"), [&](const QString& e) { error = e; });
    while (error == QStringLiteral("pending"))
        QTest::qWait(5);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(QFile::exists(tmp.path() + "/new.txt"));

    // Creating again fails rather than truncating.
    error = QStringLiteral("pending");
    core::createFile(dir.child("new.txt"), [&](const QString& e) { error = e; });
    while (error == QStringLiteral("pending"))
        QTest::qWait(5);
    QVERIFY(!error.isEmpty());

    error = QStringLiteral("pending");
    QByteArray actual;
    core::renameEntry(dir.child("new.txt"), QStringLiteral("renamed.txt"),
                      [&](const QString& e, const QByteArray& name, const QString& display) {
                          error = e;
                          actual = name;
                          QCOMPARE(display, QStringLiteral("renamed.txt"));
                      });
    while (error == QStringLiteral("pending"))
        QTest::qWait(5);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(actual, QByteArray("renamed.txt"));
    QVERIFY(!QFile::exists(tmp.path() + "/new.txt"));
    QVERIFY(QFile::exists(tmp.path() + "/renamed.txt"));

    error = QStringLiteral("pending");
    core::setMode(dir.child("renamed.txt"), 0600, [&](const QString& e) { error = e; });
    while (error == QStringLiteral("pending"))
        QTest::qWait(5);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(QFile::permissions(tmp.path() + "/renamed.txt") & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());
}

QTEST_GUILESS_MAIN(JobsTest)
#include "test_jobs.moc"
