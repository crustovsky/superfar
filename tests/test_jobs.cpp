// Jobs and the monitor against a real temp directory.
#include "core/archive.h"
#include "core/fileops.h"
#include "core/jobs/attributes.h"
#include "core/jobs/pack.h"
#include "core/jobs/remove.h"
#include "core/jobs/transfer.h"
#include "core/location.h"
#include "core/monitor.h"

#include <QSet>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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
    void packArchive();
    void attributesOfATree();
    void attributesRefusedAndLogged();
    void copyIntoItself();
    void moveSameFilesystem();
    void conflictAnswers();
    void unattendedSkipsAndLogs();
    void abortMidway();
    void removePermanent();
    void trashOrDelete();
    void monitor();
    void mkdir();
};

void JobsTest::copyIntoItself()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + "/root";
    makeTree(root);
    // A folder into its own subfolder: refused, nothing created.
    core::TransferJob into({loc(root)}, loc(root + "/sub"), false);
    into.setUnattended(true);
    runJob(into);
    QCOMPARE(into.log().size(), 1u);
    QVERIFY(!QFile::exists(root + "/sub/root"));
    // A folder into its own parent is the folder onto itself: refused, intact.
    core::TransferJob onto({loc(root)}, loc(tmp.path()), false);
    onto.setUnattended(true);
    runJob(onto);
    QCOMPARE(onto.log().size(), 1u);
    QCOMPARE(read(root + "/a.txt"), QByteArray("aaa"));
    // A file onto itself likewise.
    core::TransferJob file({loc(root + "/a.txt")}, loc(root), false);
    file.setUnattended(true);
    runJob(file);
    QCOMPARE(file.log().size(), 1u);
    QCOMPARE(read(root + "/a.txt"), QByteArray("aaa"));
    // Moving onto itself is refused the same way.
    core::TransferJob mv({loc(root)}, loc(tmp.path()), true);
    mv.setUnattended(true);
    runJob(mv);
    QCOMPARE(mv.log().size(), 1u);
    QCOMPARE(read(root + "/sub/b.txt"), QByteArray("bbbb"));
}

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
    core::setMode(dir.child("renamed.txt"), 0600, 0777, [&](const QString& e) { error = e; });
    while (error == QStringLiteral("pending"))
        QTest::qWait(5);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(QFile::permissions(tmp.path() + "/renamed.txt") & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());
}

void JobsTest::packArchive()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + "/src";
    makeTree(root);
    write(root + "/-dash.txt", "dash");  // a name, not an option
    const QByteArray latin1 = QByteArray("caf\xe9.txt");
    {
        // Raw bytes: a QString would turn them into U+FFFD.
        const int fd = ::open((QFile::encodeName(root) + "/" + latin1).constData(), O_WRONLY | O_CREAT, 0644);
        QVERIFY(fd >= 0);
        QVERIFY(::write(fd, "bytes", 5) == 5);
        ::close(fd);
    }
    QVERIFY(QDir().mkpath(tmp.path() + "/out"));

    auto listBack = [](const core::Location& archive) {
        core::ArchiveLister lister;
        QList<QByteArray> paths;
        QSet<QByteArray> links;
        bool done = false;
        QObject::connect(&lister, &core::ArchiveLister::listed,
                         [&](quint64, std::vector<core::ArchiveEntry> e, core::ArchiveDropped) {
                             for (const auto& a : e) {
                                 paths << a.path;
                                 if (a.symlink)
                                     links.insert(a.path);
                             }
                             done = true;
                         });
        QObject::connect(&lister, &core::ArchiveLister::failed, [&](quint64, const QString&) { done = true; });
        lister.start(archive, 1);
        while (!done)
            QTest::qWait(5);
        return std::make_pair(paths, links);
    };

    struct Format {
        const char* ext;
        const char* best;
    };
    for (const Format& f : {Format{"zip", "zip:compression-level=9"}, Format{"tar.gz", "gzip:compression-level=9"},
                            Format{"tar.xz", "xz:compression-level=9"}, Format{"tar.zst", "zstd:compression-level=19"},
                            Format{"7z", "7zip:compression=lzma2"}}) {
        std::vector<QByteArray> names = {"a.txt", "sub", "link", "-dash.txt"};
        // zip and 7z keep names in Unicode: libarchive refuses to read a
        // zip member whose name is not UTF-8 and rewrites the 7z one. The
        // tar formats carry the bytes through.
        const bool zip = QByteArray(f.ext) == "zip";
        const bool unicodeNames = zip || QByteArray(f.ext) == "7z";
        if (!unicodeNames)
            names.push_back(latin1);
        const core::Location archive = loc(tmp.path() + "/out/t." + QString::fromLatin1(f.ext));
        core::PackJob job(loc(root), names, archive, QByteArray(f.best));
        runJob(job);
        if (!job.log().empty() && job.log().front().message.contains(QLatin1String("not installed")))
            QSKIP("bsdtar not available");
        QVERIFY2(job.log().empty(), job.log().empty() ? "" : qPrintable(job.log().front().message));
        QVERIFY(QFile::exists(QFile::decodeName(archive.path())));
        const auto [paths, links] = listBack(archive);
        for (const char* expected : {"a.txt", "sub", "sub/b.txt", "sub/deep/c.txt", "link", "-dash.txt"})
            QVERIFY2(paths.contains(expected), qPrintable(QString::fromLatin1("%1 lacks %2").arg(f.ext, expected)));
        if (!unicodeNames)
            QVERIFY2(paths.contains(latin1), f.ext);
        QVERIFY2(links.contains("link") || zip || QByteArray(f.ext) == "7z", f.ext);  // tar keeps the link as a link
    }

    // The archive is there already: the job asks, Rename makes "t (2).zip",
    // Skip leaves it alone and logs the skip.
    const core::Location zip = loc(tmp.path() + "/out/t.zip");
    {
        core::PackJob job(loc(root), {"a.txt"}, zip, {});
        QSignalSpy asked(&job, &core::Job::conflict);
        connect(&job, &core::Job::conflict, &job, [&job] { job.answerConflict(core::ConflictAction::Rename, false); });
        runJob(job);
        QCOMPARE(asked.count(), 1);
        QVERIFY(job.log().empty());
        QVERIFY(QFile::exists(tmp.path() + "/out/t (2).zip"));
    }
    {
        core::PackJob job(loc(root), {"a.txt"}, zip, {});
        connect(&job, &core::Job::conflict, &job, [&job] { job.answerConflict(core::ConflictAction::Skip, false); });
        runJob(job);
        QCOMPARE(job.log().size(), 1u);
        QCOMPARE(job.log().front().kind, core::LogEntry::Kind::Skipped);
    }
    // Not local: refused up front, nothing written.
    {
        core::PackJob job(core::Location::fromUri(QStringLiteral("sftp://nowhere/x")), {"a"}, zip, {});
        runJob(job);
        QCOMPARE(job.log().size(), 1u);
        QCOMPARE(job.log().front().kind, core::LogEntry::Kind::Failed);
    }
}

void JobsTest::attributesOfATree()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + "/tree";
    makeTree(root);  // a.txt, sub/b.txt, sub/deep/c.txt, link -> a.txt
    write(tmp.path() + "/outside.txt", "o");
    write(root + "/sub/run.sh", "#!/bin/sh\n");
    QVERIFY(QFile::link("../../outside.txt", root + "/sub/escape"));
    auto mode = [&](const QString& rel) {
        struct stat st {};
        return ::lstat(QFile::encodeName(tmp.path() + rel).constData(), &st) == 0 ? quint32(st.st_mode & 07777) : ~0u;
    };
    auto chmodTo = [&](const QString& rel, quint32 m) { QCOMPARE(::chmod(QFile::encodeName(tmp.path() + rel).constData(), m), 0); };
    chmodTo("/tree", 02700);
    chmodTo("/tree/sub", 0700);
    chmodTo("/tree/sub/deep", 0700);
    chmodTo("/tree/a.txt", 0600);
    chmodTo("/tree/sub/b.txt", 0600);
    chmodTo("/tree/sub/run.sh", 0700);
    chmodTo("/outside.txt", 0600);

    // Not recursive: the folder alone, its nine bits; setgid stays.
    {
        core::AttributeChange change;
        change.mode = 0750;
        core::AttributesJob job({loc(root)}, change);
        runJob(job);
        QVERIFY(job.log().empty());
    }
    QCOMPARE(mode("/tree"), 02750u);
    QCOMPARE(mode("/tree/sub"), 0700u);
    QCOMPARE(mode("/tree/a.txt"), 0600u);

    // Recursive rwxr-xr-x: folders take it, files take execute only
    // where they had it, a link below is neither changed nor followed.
    {
        core::AttributeChange change;
        change.mode = 0755;
        change.recursive = true;
        core::AttributesJob job({loc(root)}, change);
        QSignalSpy progress(&job, &core::Job::progress);
        runJob(job);
        QVERIFY(job.log().empty());
        const auto last = progress.last().first().value<core::JobProgress>();
        QCOMPARE(last.itemsTotal, 9u);  // tree, a.txt, link, sub, b.txt, run.sh, escape, deep, c.txt
        QCOMPARE(last.itemsDone, 9u);
    }
    QCOMPARE(mode("/tree"), 02755u);
    QCOMPARE(mode("/tree/sub"), 0755u);
    QCOMPARE(mode("/tree/sub/deep"), 0755u);
    QCOMPARE(mode("/tree/a.txt"), 0644u);
    QCOMPARE(mode("/tree/sub/b.txt"), 0644u);
    QCOMPARE(mode("/tree/sub/run.sh"), 0755u);
    QCOMPARE(mode("/outside.txt"), 0600u);

    // Only the bits the mask names: others lose read, the rest stays.
    {
        core::AttributeChange change;
        change.mode = 0;
        change.modeMask = 0004;
        change.recursive = true;
        core::AttributesJob job({loc(root + "/sub")}, change);
        runJob(job);
    }
    QCOMPARE(mode("/tree/sub"), 0751u);
    QCOMPARE(mode("/tree/sub/b.txt"), 0640u);
    QCOMPARE(mode("/tree/sub/run.sh"), 0751u);
    QCOMPARE(mode("/tree/a.txt"), 0644u);  // not below sub

    // A link named itself: its mode means nothing, so it is skipped and
    // the log says so; the target is not touched.
    {
        core::AttributeChange change;
        change.mode = 0777;
        core::AttributesJob job({loc(root + "/link"), loc(root + "/gone")}, change);
        runJob(job);
        QCOMPARE(job.log().size(), 2u);  // the link skipped, the missing item failed
    }
    QCOMPARE(mode("/tree/a.txt"), 0644u);

    // The group, down the tree, to another group of this user (or any
    // group for root); the links change hands themselves.
    std::optional<quint32> other;
    struct stat st {};
    QCOMPARE(::stat(QFile::encodeName(root).constData(), &st), 0);
    if (::geteuid() == 0) {
        other = st.st_gid == 1 ? 2 : 1;
    } else {
        std::vector<gid_t> groups(static_cast<size_t>(std::max(0, ::getgroups(0, nullptr))));
        const int n = ::getgroups(static_cast<int>(groups.size()), groups.data());
        for (int i = 0; i < n; ++i)
            if (groups[static_cast<size_t>(i)] != st.st_gid)
                other = groups[static_cast<size_t>(i)];
    }
    if (!other)
        QSKIP("this user has one group only: nothing to change the group to");
    core::AttributeChange change;
    change.owner.gid = other;
    change.recursive = true;
    core::AttributesJob job({loc(root)}, change);
    runJob(job);
    QVERIFY(job.log().empty());
    for (const char* rel : {"/tree", "/tree/a.txt", "/tree/link", "/tree/sub/deep/c.txt", "/tree/sub/escape"}) {
        QCOMPARE(::lstat(QFile::encodeName(tmp.path() + rel).constData(), &st), 0);
        QCOMPARE(quint32(st.st_gid), *other);
    }
    QCOMPARE(::stat(QFile::encodeName(tmp.path() + "/outside.txt").constData(), &st), 0);
    QVERIFY(quint32(st.st_gid) != *other);  // the link's target stays whose it was
    QCOMPARE(mode("/tree"), 02755u);  // a group change alone leaves the mode
}

void JobsTest::attributesRefusedAndLogged()
{
    if (::geteuid() == 0)
        QSKIP("root is refused nothing");
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    makeTree(tmp.path() + "/tree");
    core::AttributeChange change;
    change.owner.uid = 0;  // giving files to root is root's to do
    change.recursive = true;

    // Asked: the first refusal comes as a question; Skip for all ends
    // the job with every item in the log.
    {
        core::AttributesJob job({loc(tmp.path() + "/tree")}, change);
        int asked = 0;
        connect(&job, &core::Job::failure, &job, [&](const core::Failure& f) {
            ++asked;
            QVERIFY(!f.message.isEmpty());
            job.answerError(core::ErrorAction::Skip, true);
        });
        runJob(job);
        QCOMPARE(asked, 1);
        QCOMPARE(job.log().size(), 7u);  // tree, a.txt, link, sub, b.txt, deep, c.txt
    }
    // Unattended: no question at all, the same log.
    {
        core::AttributesJob job({loc(tmp.path() + "/tree")}, change);
        job.setUnattended(true);
        QSignalSpy asked(&job, &core::Job::failure);
        runJob(job);
        QVERIFY(asked.isEmpty());
        QCOMPARE(job.log().size(), 7u);
        QCOMPARE(job.log().front().kind, core::LogEntry::Kind::Failed);
    }
    // Abort at the first question stops there.
    {
        core::AttributesJob job({loc(tmp.path() + "/tree")}, change);
        connect(&job, &core::Job::failure, &job, [&] { job.answerError(core::ErrorAction::Abort, false); });
        runJob(job);
        QVERIFY(job.aborted());
    }
}

QTEST_GUILESS_MAIN(JobsTest)
#include "test_jobs.moc"
