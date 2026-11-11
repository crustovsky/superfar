// Jobs and the monitor against a real temp directory.
#include "core/admin.h"
#include "core/archive.h"
#include "core/fileops.h"
#include "core/jobs/attributes.h"
#include "core/jobs/extract.h"
#include "core/jobs/pack.h"
#include "core/jobs/queue.h"
#include "core/jobs/remove.h"
#include "core/jobs/transfer.h"
#include "core/jobs/undo.h"
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

// Takes an entry back and returns the entry for the other direction.
core::UndoEntry takeBack(core::UndoEntry entry, size_t expectLog = 0, bool redo = false)
{
    core::UndoJob job(std::move(entry), redo);
    job.setUnattended(true);
    runJob(job);
    [&] { QCOMPARE(job.log().size(), expectLog); }();
    return {job.what(), std::move(job.undoItems())};
}

core::UndoEntry entryOf(core::Job& job)
{
    return {QStringLiteral("test"), std::move(job.undoItems())};
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
    void undoStack();
    void undoCopy();
    void undoCopyIntoExistingFolder();
    void undoMove();
    void undoMoveAcrossFilesystems();
    void undoTrash();
    void undoOverwrite();
    void overwriteWithoutTrash();
    void undoLeavesChangedItems();
    void undoAttributes();
    void undoSingleOperations();
    void undoPackAndExtract();
    void undoOfAnAbortedJob();
    void permanentDeleteRecordsNothing();
    void queuedJobsWaitTheirTurn();
    void deniedItemsOfferAdministrator();
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

void JobsTest::undoStack()
{
    core::UndoStack stack;
    using Side = core::UndoStack::Side;
    auto entry = [](const QString& what) {
        return core::UndoEntry{what, {core::UndoItem::trashed(core::Location::fromPath("/nowhere"))}};
    };
    QVERIFY(!stack.take(Side::Undo));
    stack.record(entry("a"));
    stack.record(entry("b"));
    QCOMPARE(stack.take(Side::Undo)->what, QStringLiteral("b"));
    stack.put(Side::Redo, entry("b"));
    // The outcome of a redo goes back without touching what else can be redone.
    stack.put(Side::Redo, entry("c"));
    stack.put(Side::Undo, entry("c"));
    QCOMPARE(stack.count(Side::Redo), 2u);
    // A new operation: nothing to redo any more.
    stack.record(entry("d"));
    QCOMPARE(stack.count(Side::Redo), 0u);
    QCOMPARE(stack.take(Side::Undo)->what, QStringLiteral("d"));
    // An entry without items is not one, and the stack is bounded.
    stack.record({QStringLiteral("empty"), {}});
    QCOMPARE(stack.take(Side::Undo)->what, QStringLiteral("c"));
    for (int i = 0; i < 60; ++i)
        stack.record(entry(QString::number(i)));
    QCOMPARE(stack.count(Side::Undo), core::UndoStack::kLimit);
    QCOMPARE(stack.take(Side::Undo)->what, QStringLiteral("59"));
}

void JobsTest::undoCopy()
{
    QTemporaryDir tmp;
    makeTree(tmp.path() + "/src");
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));
    core::TransferJob job({loc(tmp.path() + "/src"), loc(tmp.path() + "/src/a.txt")}, loc(tmp.path() + "/dst"), false);
    runJob(job);
    QVERIFY(job.log().empty());
    // The folder the copy made is one item, whatever is inside it.
    QCOMPARE(job.undoItems().size(), 2u);
    QCOMPARE(read(tmp.path() + "/dst/src/sub/deep/c.txt"), QByteArray("c"));

    core::UndoEntry redo = takeBack(entryOf(job));
    QVERIFY(!QFile::exists(tmp.path() + "/dst/src"));
    QVERIFY(!QFile::exists(tmp.path() + "/dst/a.txt"));
    QCOMPARE(read(tmp.path() + "/src/sub/deep/c.txt"), QByteArray("c"));  // the sources are not the undo's business

    core::UndoEntry undo = takeBack(std::move(redo), 0, true);
    QCOMPARE(read(tmp.path() + "/dst/src/sub/deep/c.txt"), QByteArray("c"));
    QCOMPARE(read(tmp.path() + "/dst/a.txt"), QByteArray("aaa"));
    QVERIFY(QFileInfo(tmp.path() + "/dst/src/link").isSymLink());

    takeBack(std::move(undo));
    QVERIFY(!QFile::exists(tmp.path() + "/dst/src"));
}

void JobsTest::undoCopyIntoExistingFolder()
{
    // A merge: only what the copy added goes, what was there stays.
    QTemporaryDir tmp;
    makeTree(tmp.path() + "/src");
    QVERIFY(QDir().mkpath(tmp.path() + "/dst/src/sub"));
    write(tmp.path() + "/dst/src/mine.txt", "mine");
    write(tmp.path() + "/dst/src/sub/kept.txt", "kept");
    core::TransferJob job({loc(tmp.path() + "/src")}, loc(tmp.path() + "/dst"), false);
    runJob(job);
    QCOMPARE(read(tmp.path() + "/dst/src/sub/deep/c.txt"), QByteArray("c"));
    takeBack(entryOf(job));
    QCOMPARE(QDir(tmp.path() + "/dst/src").entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
             QStringList({"mine.txt", "sub"}));
    QCOMPARE(QDir(tmp.path() + "/dst/src/sub").entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
             QStringList({"kept.txt"}));
}

void JobsTest::undoMove()
{
    QTemporaryDir tmp;
    makeTree(tmp.path() + "/src");
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));
    core::TransferJob job({loc(tmp.path() + "/src")}, loc(tmp.path() + "/dst"), true);
    runJob(job);
    QVERIFY(!QFile::exists(tmp.path() + "/src"));
    core::UndoEntry redo = takeBack(entryOf(job));
    QCOMPARE(read(tmp.path() + "/src/sub/b.txt"), QByteArray("bbbb"));
    QVERIFY(!QFile::exists(tmp.path() + "/dst/src"));
    // Something took the old place meanwhile: unattended, the undo
    // leaves both alone and says so.
    core::UndoEntry undo = takeBack(std::move(redo), 0, true);
    write(tmp.path() + "/src", "in the way");
    takeBack(std::move(undo), 1);
    QCOMPARE(read(tmp.path() + "/src"), QByteArray("in the way"));
    QCOMPARE(read(tmp.path() + "/dst/src/a.txt"), QByteArray("aaa"));
}

void JobsTest::undoMoveAcrossFilesystems()
{
    // Across filesystems a folder is moved file by file and removed at
    // the end; undo has to make it again, empty folders included.
    QTemporaryDir tmp;
    QTemporaryDir other(QStringLiteral("/dev/shm/superfar-test-XXXXXX"));
    struct stat a {}, b {};
    if (!other.isValid() || ::stat(QFile::encodeName(tmp.path()).constData(), &a) != 0
        || ::stat(QFile::encodeName(other.path()).constData(), &b) != 0 || a.st_dev == b.st_dev)
        QSKIP("needs a second filesystem at /dev/shm");
    makeTree(tmp.path() + "/src");
    QVERIFY(QDir().mkpath(tmp.path() + "/src/empty"));
    core::TransferJob job({loc(tmp.path() + "/src")}, loc(other.path()), true);
    runJob(job);
    QVERIFY(job.log().empty());
    QVERIFY(!QFile::exists(tmp.path() + "/src"));
    QCOMPARE(read(other.path() + "/src/sub/deep/c.txt"), QByteArray("c"));

    core::UndoEntry redo = takeBack(entryOf(job));
    QCOMPARE(read(tmp.path() + "/src/sub/deep/c.txt"), QByteArray("c"));
    QCOMPARE(read(tmp.path() + "/src/a.txt"), QByteArray("aaa"));
    QVERIFY(QDir(tmp.path() + "/src/empty").exists());
    QVERIFY(QFileInfo(tmp.path() + "/src/link").isSymLink());
    QVERIFY(!QFile::exists(other.path() + "/src"));

    takeBack(std::move(redo), 0, true);
    QVERIFY(!QFile::exists(tmp.path() + "/src"));
    QCOMPARE(read(other.path() + "/src/sub/deep/c.txt"), QByteArray("c"));
    QVERIFY(QDir(other.path() + "/src/empty").exists());
}

void JobsTest::undoTrash()
{
    QTemporaryDir tmp;
    makeTree(tmp.path() + "/src");
    write(tmp.path() + "/single", "x");
    core::RemoveJob job({loc(tmp.path() + "/src"), loc(tmp.path() + "/single")}, false);
    runJob(job);
    QVERIFY(job.log().empty());
    QVERIFY(!QFile::exists(tmp.path() + "/src"));
    // The same name trashed again later: each undo brings its own back.
    write(tmp.path() + "/single", "second");  // within the same second
    core::RemoveJob again({loc(tmp.path() + "/single")}, false);
    runJob(again);

    core::UndoEntry redo = takeBack(entryOf(again));
    QCOMPARE(read(tmp.path() + "/single"), QByteArray("second"));
    takeBack(std::move(redo), 0, true);
    QVERIFY(!QFile::exists(tmp.path() + "/single"));

    takeBack(entryOf(job));
    QCOMPARE(read(tmp.path() + "/single"), QByteArray("x"));
    QCOMPARE(read(tmp.path() + "/src/sub/deep/c.txt"), QByteArray("c"));
}

void JobsTest::undoOverwrite()
{
    QTemporaryDir tmp;
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));
    write(tmp.path() + "/f.txt", "new");
    write(tmp.path() + "/dst/f.txt", "old");
    core::Conflict seen;
    core::TransferJob job({loc(tmp.path() + "/f.txt")}, loc(tmp.path() + "/dst"), false);
    connect(&job, &core::Job::conflict, [&](const core::Conflict& c) {
        seen = c;
        job.answerConflict(core::ConflictAction::Overwrite, false);
    });
    runJob(job);
    QVERIFY(job.log().empty());
    QVERIFY(!seen.cannotUndo);
    QCOMPARE(read(tmp.path() + "/dst/f.txt"), QByteArray("new"));

    core::UndoEntry redo = takeBack(entryOf(job));
    QCOMPARE(read(tmp.path() + "/dst/f.txt"), QByteArray("old"));
    takeBack(std::move(redo), 0, true);
    QCOMPARE(read(tmp.path() + "/dst/f.txt"), QByteArray("new"));

    // A move over a file likewise: the source goes back, the old target returns.
    write(tmp.path() + "/g.txt", "moved");
    write(tmp.path() + "/dst/g.txt", "was here");
    core::TransferJob mv({loc(tmp.path() + "/g.txt")}, loc(tmp.path() + "/dst"), true);
    mv.setConflictPolicy(core::ConflictAction::Overwrite);
    runJob(mv);
    QCOMPARE(read(tmp.path() + "/dst/g.txt"), QByteArray("moved"));
    takeBack(entryOf(mv));
    QCOMPARE(read(tmp.path() + "/g.txt"), QByteArray("moved"));
    QCOMPARE(read(tmp.path() + "/dst/g.txt"), QByteArray("was here"));
}

void JobsTest::overwriteWithoutTrash()
{
    if (::geteuid() == 0)
        QSKIP("root can always write to the trash");
    // The trash refuses: as a policy the overwrite still happens, is
    // logged, and is not something undo will pretend to take back.
    const QString trash = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME")) + "/Trash";
    QVERIFY(QDir().mkpath(trash + "/files"));
    QVERIFY(QDir().mkpath(trash + "/info"));
    QCOMPARE(::chmod(QFile::encodeName(trash + "/files").constData(), 0500), 0);
    QCOMPARE(::chmod(QFile::encodeName(trash + "/info").constData(), 0500), 0);
    QTemporaryDir tmp;
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));
    write(tmp.path() + "/f.txt", "new");
    write(tmp.path() + "/dst/f.txt", "old");
    write(tmp.path() + "/fresh.txt", "fresh");
    core::TransferJob job({loc(tmp.path() + "/f.txt"), loc(tmp.path() + "/fresh.txt")}, loc(tmp.path() + "/dst"), false);
    job.setConflictPolicy(core::ConflictAction::Overwrite);
    runJob(job);
    ::chmod(QFile::encodeName(trash + "/files").constData(), 0700);
    ::chmod(QFile::encodeName(trash + "/info").constData(), 0700);
    QCOMPARE(read(tmp.path() + "/dst/f.txt"), QByteArray("new"));
    QCOMPARE(job.log().size(), 1u);
    QCOMPARE(job.log().front().kind, core::LogEntry::Kind::Overwritten);
    QCOMPARE(job.undoItems().size(), 1u);  // the file that replaced nothing
    takeBack(entryOf(job));
    QCOMPARE(read(tmp.path() + "/dst/f.txt"), QByteArray("new"));
    QVERIFY(!QFile::exists(tmp.path() + "/dst/fresh.txt"));
}

void JobsTest::undoLeavesChangedItems()
{
    QTemporaryDir tmp;
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));
    write(tmp.path() + "/a.txt", "a");
    write(tmp.path() + "/b.txt", "b");
    core::TransferJob job({loc(tmp.path() + "/a.txt"), loc(tmp.path() + "/b.txt")}, loc(tmp.path() + "/dst"), false);
    runJob(job);
    write(tmp.path() + "/dst/a.txt", "worked on since");
    core::UndoJob undo(entryOf(job), false);
    runJob(undo);
    QCOMPARE(undo.log().size(), 1u);
    QCOMPARE(undo.log().front().kind, core::LogEntry::Kind::Skipped);
    QCOMPARE(read(tmp.path() + "/dst/a.txt"), QByteArray("worked on since"));
    QVERIFY(!QFile::exists(tmp.path() + "/dst/b.txt"));
    QCOMPARE(undo.undoItems().size(), 1u);  // redo knows of b alone
}

void JobsTest::undoAttributes()
{
    QTemporaryDir tmp;
    const QString root = tmp.path() + "/tree";
    makeTree(root);
    auto mode = [&](const QString& rel) {
        struct stat st {};
        return ::lstat(QFile::encodeName(root + rel).constData(), &st) == 0 ? quint32(st.st_mode & 07777) : ~0u;
    };
    QCOMPARE(::chmod(QFile::encodeName(root + "/a.txt").constData(), 0600), 0);
    QCOMPARE(::chmod(QFile::encodeName(root + "/sub").constData(), 0700), 0);
    QCOMPARE(::chmod(QFile::encodeName(root + "/sub/b.txt").constData(), 0640), 0);
    const quint32 deep = mode("/sub/deep");

    core::AttributeChange change;
    change.mode = 0755;
    change.recursive = true;
    core::AttributesJob job({loc(root)}, change);
    runJob(job);
    QCOMPARE(mode("/sub"), 0755u);
    QCOMPARE(mode("/a.txt"), 0644u);

    // One item changed again by hand: it is left as it is now.
    QCOMPARE(::chmod(QFile::encodeName(root + "/sub/b.txt").constData(), 0444), 0);
    core::UndoEntry redo = takeBack(entryOf(job), 1);
    QCOMPARE(mode("/a.txt"), 0600u);
    QCOMPARE(mode("/sub"), 0700u);
    QCOMPARE(mode("/sub/deep"), deep);
    QCOMPARE(mode("/sub/b.txt"), 0444u);

    takeBack(std::move(redo), 0, true);
    QCOMPARE(mode("/a.txt"), 0644u);
    QCOMPARE(mode("/sub"), 0755u);
    QCOMPARE(mode("/sub/b.txt"), 0444u);
}

void JobsTest::undoSingleOperations()
{
    // Rename, new folder and new file are not jobs; the window records
    // them with the same items.
    QTemporaryDir tmp;
    write(tmp.path() + "/old.txt", "content");
    QVERIFY(QFile::rename(tmp.path() + "/old.txt", tmp.path() + "/new.txt"));
    QVERIFY(QDir().mkpath(tmp.path() + "/made"));
    core::UndoEntry entry{QStringLiteral("test"),
                          {core::UndoItem::moved(loc(tmp.path() + "/old.txt"), loc(tmp.path() + "/new.txt")),
                           core::UndoItem::created(loc(tmp.path() + "/made"))}};
    core::UndoEntry redo = takeBack(std::move(entry));
    QCOMPARE(read(tmp.path() + "/old.txt"), QByteArray("content"));
    QVERIFY(!QFile::exists(tmp.path() + "/new.txt"));
    QVERIFY(!QFile::exists(tmp.path() + "/made"));
    takeBack(std::move(redo), 0, true);
    QCOMPARE(read(tmp.path() + "/new.txt"), QByteArray("content"));
    QVERIFY(QDir(tmp.path() + "/made").exists());

    // A swap through temporary names, as the multi-rename panel does
    // it, goes back step by step.
    write(tmp.path() + "/x", "x");
    write(tmp.path() + "/y", "y");
    std::vector<core::UndoItem> steps;
    auto step = [&](const QString& from, const QString& to) {
        QVERIFY(QFile::rename(tmp.path() + from, tmp.path() + to));
        steps.push_back(core::UndoItem::moved(loc(tmp.path() + from), loc(tmp.path() + to)));
    };
    step("/x", "/t1");
    step("/y", "/t2");
    step("/t1", "/y");
    step("/t2", "/x");
    QCOMPARE(read(tmp.path() + "/x"), QByteArray("y"));
    takeBack({QStringLiteral("swap"), std::move(steps)});
    QCOMPARE(read(tmp.path() + "/x"), QByteArray("x"));
    QCOMPARE(read(tmp.path() + "/y"), QByteArray("y"));
}

void JobsTest::undoPackAndExtract()
{
    QTemporaryDir tmp;
    makeTree(tmp.path() + "/src");
    QVERIFY(QDir().mkpath(tmp.path() + "/out/src"));
    write(tmp.path() + "/out/src/there.txt", "there");
    const core::Location archive = loc(tmp.path() + "/made.tar.gz");
    core::PackJob pack(loc(tmp.path()), {QByteArray("src")}, archive, {});
    runJob(pack);
    if (!pack.log().empty())
        QSKIP("needs bsdtar");
    QVERIFY(QFile::exists(tmp.path() + "/made.tar.gz"));

    // Into a folder that has src/ already: what the extraction merged
    // into it is not undo's to sort out, the new top-level folder is.
    core::ExtractJob merged(archive, {QByteArray("src/a.txt")}, loc(tmp.path() + "/out"), 0);
    runJob(merged);
    QVERIFY(merged.undoItems().empty());
    QVERIFY(QDir().mkpath(tmp.path() + "/fresh"));
    core::ExtractJob extract(archive, {QByteArray("src/sub/b.txt"), QByteArray("src/a.txt")}, loc(tmp.path() + "/fresh"), 0);
    runJob(extract);
    QCOMPARE(read(tmp.path() + "/fresh/src/sub/b.txt"), QByteArray("bbbb"));
    QCOMPARE(extract.undoItems().size(), 1u);
    takeBack(entryOf(extract));
    QVERIFY(!QFile::exists(tmp.path() + "/fresh/src"));

    core::UndoEntry redo = takeBack(entryOf(pack));
    QVERIFY(!QFile::exists(tmp.path() + "/made.tar.gz"));
    takeBack(std::move(redo), 0, true);
    QVERIFY(QFile::exists(tmp.path() + "/made.tar.gz"));
}

void JobsTest::undoOfAnAbortedJob()
{
    // A job aborted halfway records the part it did; an undo aborted
    // halfway leaves the rest to be undone later.
    QTemporaryDir tmp;
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));
    std::vector<core::Location> sources;
    for (int i = 0; i < 300; ++i) {
        write(tmp.path() + QString("/f%1").arg(i), QByteArray(64, 'x'));
        sources.push_back(loc(tmp.path() + QString("/f%1").arg(i)));
    }
    core::TransferJob job(sources, loc(tmp.path() + "/dst"), false);
    connect(&job, &core::Job::progress, [&](const core::JobProgress& p) {
        if (p.phase == core::JobProgress::Phase::Running && p.itemsDone >= 20)
            job.abort();
    });
    runJob(job);
    QVERIFY(job.aborted());
    const size_t copied = static_cast<size_t>(QDir(tmp.path() + "/dst").entryList(QDir::Files).size());
    QCOMPARE(job.undoItems().size(), copied);

    takeBack(entryOf(job));
    QCOMPARE(QDir(tmp.path() + "/dst").entryList(QDir::Files).size(), 0);

    // Thirty files moved; something sits where f10 was. The undo goes
    // backwards, asks about it, and is aborted there: f11 and up are
    // back, the ten before it wait for the next undo.
    sources.resize(30);
    core::TransferJob mv(sources, loc(tmp.path() + "/dst"), true);
    runJob(mv);
    QCOMPARE(mv.undoItems().size(), 30u);
    write(tmp.path() + "/f10", "in the way");
    core::UndoJob undo(entryOf(mv), false);
    connect(&undo, &core::Job::conflict, [&](const core::Conflict& c) {
        QCOMPARE(c.name, QStringLiteral("f10"));
        undo.abort();
    });
    runJob(undo);
    QVERIFY(undo.aborted());
    QVERIFY(QFile::exists(tmp.path() + "/f29"));
    QVERIFY(!QFile::exists(tmp.path() + "/f9"));
    QCOMPARE(undo.remaining().items.size(), 10u);
    QCOMPARE(undo.undoItems().size(), 19u);
    takeBack(undo.remaining());
    QVERIFY(QFile::exists(tmp.path() + "/f0"));
    QCOMPARE(QDir(tmp.path() + "/dst").entryList(QDir::Files), QStringList({"f10"}));
}

void JobsTest::permanentDeleteRecordsNothing()
{
    QTemporaryDir tmp;
    makeTree(tmp.path() + "/src");
    core::RemoveJob job({loc(tmp.path() + "/src")}, true);
    runJob(job);
    QVERIFY(!QFile::exists(tmp.path() + "/src"));
    QVERIFY(job.undoItems().empty());
}

void JobsTest::queuedJobsWaitTheirTurn()
{
    QTemporaryDir tmp;
    QVERIFY(QDir().mkpath(tmp.path() + "/dst"));
    for (const char* name : {"a", "b", "c", "d", "e", "f"})
        write(tmp.path() + "/" + name, name);
    write(tmp.path() + "/dst/a", "there already");
    write(tmp.path() + "/dst/f", "there already");
    const core::Location dst = loc(tmp.path() + "/dst");
    auto copyOf = [&](const char* name) { return new core::TransferJob({loc(tmp.path() + "/" + name)}, dst, false, this); };
    auto there = [&](const char* name) { return read(tmp.path() + "/dst/" + name) == QByteArray(name); };

    core::JobQueue queue;
    // The first job asks about a and waits for an answer: it is running.
    core::Job* first = copyOf("a");
    QSignalSpy asked(first, &core::Job::conflict);
    queue.add(first, false);
    QTRY_COMPARE(asked.size(), 1);

    // Queued behind it: not started. Started the usual way: beside it.
    core::Job* second = copyOf("b");
    queue.add(second, true);
    core::Job* beside = copyOf("c");
    queue.add(beside, false);
    QTRY_VERIFY(there("c"));
    QTRY_COMPARE(queue.count(), 2);
    QVERIFY(!second->hasStarted());
    QVERIFY(!there("b"));

    // Start now stops the waiting.
    core::Job* eager = copyOf("e");
    queue.add(eager, true);
    QVERIFY(!eager->hasStarted());
    queue.startNow(eager);
    QTRY_VERIFY(there("e"));

    // An aborted queued job ends without having run.
    core::Job* dropped = copyOf("d");
    queue.add(dropped, true);
    QSignalSpy droppedEnded(dropped, &core::Job::finished);
    dropped->abort();
    QTRY_COMPARE(droppedEnded.size(), 1);
    QVERIFY(dropped->aborted());
    QVERIFY(!dropped->hasStarted());
    dropped->start();  // and cannot be started after
    QVERIFY(!dropped->hasStarted());

    // The first one ends: the one queued behind it runs.
    QVERIFY(!second->hasStarted());
    first->answerConflict(core::ConflictAction::Skip, false);
    QTRY_VERIFY(there("b"));
    QTRY_COMPARE(queue.count(), 0);
    QVERIFY(!there("d"));

    // A job that ends by abort lets the next one go as well, and a
    // paused one holds it: it has not ended.
    core::Job* blocked = copyOf("f");
    QSignalSpy blockedAsked(blocked, &core::Job::conflict);
    queue.add(blocked, false);
    QTRY_COMPARE(blockedAsked.size(), 1);
    core::Job* after = copyOf("d");
    queue.add(after, true);
    blocked->setPaused(true);
    QTest::qWait(50);
    QVERIFY(!after->hasStarted());
    blocked->abort();
    QTRY_VERIFY(there("d"));
    QTRY_COMPARE(queue.count(), 0);
    // With nothing ahead a queued job starts at once.
    QFile::remove(tmp.path() + "/dst/b");
    core::Job* alone = copyOf("b");
    queue.add(alone, true);
    QVERIFY(alone->hasStarted());
    QTRY_VERIFY(there("b"));
    QTRY_COMPARE(queue.count(), 0);
}

void JobsTest::deniedItemsOfferAdministrator()
{
    if (::geteuid() == 0)
        QSKIP("root is never denied");
    QVERIFY(core::admin::available());  // the stand-in backend file, see main()
    QTemporaryDir tmp;
    QVERIFY(QDir().mkpath(tmp.path() + "/ro"));
    write(tmp.path() + "/f.txt", "f");
    write(tmp.path() + "/ro/kept.txt", "kept");
    QCOMPARE(::chmod(QFile::encodeName(tmp.path() + "/ro").constData(), 0555), 0);
    struct Unlock {
        QByteArray path;
        ~Unlock() { ::chmod(path.constData(), 0755); }
    } unlock{QFile::encodeName(tmp.path() + "/ro")};

    // The answers given here are never "as administrator": that would
    // ask the developer's own polkit agent. What is checked is the offer.
    auto offered = [&](core::Job& job, core::ErrorAction answer = core::ErrorAction::Skip) {
        QList<bool> offers;
        connect(&job, &core::Job::failure, [&](const core::Failure& f) {
            offers << f.offerAdmin;
            job.answerError(answer, false);
        });
        runJob(job);
        return offers;
    };
    // A copy into a folder that is not ours to write to, a move out of
    // it, a delete in it, a chmod of what is not ours: denied, local.
    {
        core::TransferJob job({loc(tmp.path() + "/f.txt")}, loc(tmp.path() + "/ro"), false);
        QCOMPARE(offered(job), QList<bool>({true}));
        QCOMPARE(job.log().size(), 1u);
    }
    {
        core::TransferJob job({loc(tmp.path() + "/ro/kept.txt")}, loc(tmp.path()), true);
        QCOMPARE(offered(job), QList<bool>({true}));
        QVERIFY(QFile::exists(tmp.path() + "/ro/kept.txt"));
    }
    {
        core::RemoveJob job({loc(tmp.path() + "/ro/kept.txt")}, true);
        QCOMPARE(offered(job), QList<bool>({true}));
    }
    {
        // A folder: it cannot be made there, and is asked about once.
        makeTree(tmp.path() + "/tree");
        core::TransferJob job({loc(tmp.path() + "/tree")}, loc(tmp.path() + "/ro"), false);
        QCOMPARE(offered(job), QList<bool>({true}));
        QVERIFY(!QFile::exists(tmp.path() + "/ro/tree"));
    }
    {
        core::AttributeChange change;
        change.mode = 0700;
        core::AttributesJob job({loc(QStringLiteral("/etc/hostname"))}, change);
        if (QFile::exists(QStringLiteral("/etc/hostname")))
            QCOMPARE(offered(job), QList<bool>({true}));
    }
    // A failure that is not about permission carries no offer.
    {
        core::TransferJob job({loc(tmp.path() + "/gone.txt")}, loc(tmp.path()), false);
        QCOMPARE(offered(job), QList<bool>({false}));
    }
    // Unattended never asks, so never escalates: the item is a failure
    // in the log, as before.
    {
        core::TransferJob job({loc(tmp.path() + "/f.txt")}, loc(tmp.path() + "/ro"), false);
        job.setUnattended(true);
        QCOMPARE(offered(job), QList<bool>());
        QCOMPARE(job.log().size(), 1u);
        QCOMPARE(job.log().front().kind, core::LogEntry::Kind::Failed);
    }
    // "Skip, for all" is a policy like any other.
    {
        write(tmp.path() + "/g.txt", "g");
        core::TransferJob job({loc(tmp.path() + "/f.txt"), loc(tmp.path() + "/g.txt")}, loc(tmp.path() + "/ro"), false);
        int asked = 0;
        connect(&job, &core::Job::failure, [&](const core::Failure&) {
            ++asked;
            job.answerError(core::ErrorAction::Skip, true);
        });
        runJob(job);
        QCOMPARE(asked, 1);
        QCOMPARE(job.log().size(), 2u);
    }
}

int main(int argc, char** argv)
{
    // A home and a trash of the test's own, set before GLib reads them.
    // The home trash serves files on the home's filesystem, so with the
    // home beside the temp folders the trash works wherever those are.
    QTemporaryDir home;
    qputenv("HOME", QFile::encodeName(home.path()));
    qputenv("XDG_DATA_HOME", QFile::encodeName(home.path() + "/data"));
    // GVfs's admin backend counts as installed (its file is there):
    // the offer to go on as administrator does not depend on the
    // machine the tests run on.
    QDir().mkpath(home.path() + "/sys/gvfs/mounts");
    if (QFile backend(home.path() + "/sys/gvfs/mounts/admin.mount"); !backend.open(QIODevice::WriteOnly))
        return 1;
    qputenv("XDG_DATA_DIRS", QFile::encodeName(home.path() + "/sys") + ":/usr/local/share:/usr/share");
    QCoreApplication app(argc, argv);
    JobsTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "test_jobs.moc"
