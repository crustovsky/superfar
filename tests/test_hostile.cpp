// Core against names and content chosen to break things: bytes that are
// not UTF-8, newlines and tabs in names, the longest name a filesystem
// takes, names that look like options or shell syntax, symlink loops,
// a dangling link, a folder that cannot be read, a sparse file past
// 4 GiB and an archive whose members try to climb out. All against a
// real temp directory through raw POSIX calls, so the bytes stay bytes.
#include "core/archive.h"
#include "core/pages.h"
#include "core/apps.h"
#include "core/dirsize.h"
#include "core/finder.h"
#include "core/jobs/extract.h"
#include "core/jobs/transfer.h"
#include "core/listing.h"
#include "core/location.h"
#include "core/metadata.h"
#include "core/reader.h"
#include "core/rename.h"
#include "core/sort.h"

#include <QDir>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const QByteArray kLatin1 = QByteArray("latin1-\xe9\xff.txt");
const QByteArray kNewline = QByteArray("line\nbreak.txt");
const QByteArray kTab = QByteArray("tab\there.txt");
const QByteArray kLong = QByteArray(251, 'x') + ".txt";
const QByteArray kSpaces = QByteArray("   ");
const QByteArray kDash = QByteArray("-rf");
const QByteArray kOption = QByteArray("--strip-components=1");
const QByteArray kShell = QByteArray("it's $(touch pwned) `id` \"q\".txt");
const QByteArray kNfc = QByteArray("caf\xc3\xa9.txt");
const QByteArray kNfd = QByteArray("cafe\xcc\x81.txt");

QByteArray join(const QByteArray& dir, const QByteArray& name) { return dir + '/' + name; }

void makeFile(const QByteArray& path, const QByteArray& content)
{
    const int fd = ::open(path.constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    QVERIFY2(fd >= 0, path.constData());
    QCOMPARE(::write(fd, content.constData(), static_cast<size_t>(content.size())), static_cast<ssize_t>(content.size()));
    ::close(fd);
}

QByteArray readFile(const QByteArray& path)
{
    const int fd = ::open(path.constData(), O_RDONLY);
    if (fd < 0)
        return QByteArray();
    char buf[4096];
    const ssize_t n = ::read(fd, buf, sizeof buf);
    ::close(fd);
    return n > 0 ? QByteArray(buf, static_cast<qsizetype>(n)) : QByteArray();
}

// Builds the fixture under root and lists the names that a listing of
// root must contain.
void makeFixture(const QByteArray& root, QList<QByteArray>* out)
{
    QList<QByteArray>& names = *out;
    for (const QByteArray& n : {kLatin1, kNewline, kTab, kLong, kSpaces, kDash, kOption, kShell, kNfc, kNfd}) {
        makeFile(join(root, n), "content of " + n);
        names << n;
    }
    makeFile(join(root, "empty"), QByteArray());
    names << "empty";
    QVERIFY(::mkdir(join(root, "plain").constData(), 0755) == 0);
    makeFile(join(root, "plain/ok.txt"), "fine");
    names << "plain";
    QVERIFY(::symlink("loop", join(root, "loop").constData()) == 0);
    names << "loop";
    QVERIFY(::mkdir(join(root, "a").constData(), 0755) == 0);
    QVERIFY(::symlink("../a", join(root, "a/back").constData()) == 0);
    names << "a";
    QVERIFY(::symlink("/nonexistent/target", join(root, "dangle").constData()) == 0);
    names << "dangle";
    QVERIFY(::mkdir(join(root, "unreadable").constData(), 0755) == 0);
    makeFile(join(root, "unreadable/secret"), "s");
    QVERIFY(::chmod(join(root, "unreadable").constData(), 0) == 0);
    names << "unreadable";
}

// A sparse file past 4 GiB, beside the fixture: g_file_copy writes holes
// out as zeros (Nautilus has the same), so it stays out of the copy test.
void makeSparse(const QByteArray& path)
{
    const int fd = ::open(path.constData(), O_WRONLY | O_CREAT, 0644);
    QVERIFY(fd >= 0);
    QVERIFY(::ftruncate(fd, 4LL * 1024 * 1024 * 1024 + 7) == 0);
    ::close(fd);
}

void unlockFixture(const QByteArray& root)
{
    ::chmod(join(root, "unreadable").constData(), 0755);  // so the temp dir can be removed
}

std::vector<core::Entry> list(const core::Location& dir)
{
    core::Listing listing;
    std::vector<core::Entry> all;
    QObject::connect(&listing, &core::Listing::batch, [&](quint64, std::vector<core::Entry> b) {
        all.insert(all.end(), b.begin(), b.end());
    });
    QSignalSpy done(&listing, &core::Listing::finished);
    QSignalSpy failed(&listing, &core::Listing::failed);
    listing.start(dir, 1);
    while (done.isEmpty() && failed.isEmpty())
        QTest::qWait(10);
    return all;
}

void runJob(core::Job& job)
{
    QSignalSpy done(&job, &core::Job::finished);
    job.start();
    while (done.isEmpty())
        QTest::qWait(5);
}

} // namespace

class HostileTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void listingKeepsBytes();
    void sortSurvives();
    void detailsOnBrokenEntries();
    void sparseFileReads();
    void finderStopsAtLoops();
    void sizesStopAtLoops();
    void renamePlanRejectsSlashes();
    void shellQuoteRoundTrip();
    void copyKeepsNamesAndLinks();
    void archiveMembersStayInside();
    void pdfToolOutput();

private:
    QTemporaryDir tmp_;
    QByteArray root_;
    QByteArray sparse_;
    QList<QByteArray> names_;
};

void HostileTest::initTestCase()
{
    QVERIFY(tmp_.isValid());
    root_ = QFile::encodeName(tmp_.path()) + "/hostile";
    QVERIFY(::mkdir(root_.constData(), 0755) == 0);
    makeFixture(root_, &names_);
    sparse_ = QFile::encodeName(tmp_.path()) + "/sparse.bin";
    makeSparse(sparse_);
}

void HostileTest::cleanupTestCase()
{
    unlockFixture(root_);
}

void HostileTest::listingKeepsBytes()
{
    const auto entries = list(core::Location::fromPath(root_));
    QCOMPARE(static_cast<int>(entries.size()), names_.size());
    for (const core::Entry& e : entries) {
        QVERIFY2(names_.contains(e.name), e.name.constData());
        QVERIFY2(!e.display.isEmpty(), e.name.constData());
        if (e.name == "loop" || e.name == "dangle")
            QVERIFY(e.symlink);
        if (e.name == "plain" || e.name == "unreadable")
            QVERIFY(e.isDir());
        // The raw name reaches the filesystem again unchanged.
        const core::Location child = core::Location::fromPath(root_).child(e.name);
        QCOMPARE(child.basename(), e.name);
    }
    // NFC and NFD are two files, not one.
    int cafes = 0;
    for (const core::Entry& e : entries)
        cafes += e.name == kNfc || e.name == kNfd;
    QCOMPARE(cafes, 2);
}

void HostileTest::sortSurvives()
{
    auto entries = list(core::Location::fromPath(root_));
    for (core::Entry& e : entries)
        core::computeSortKey(e);
    core::sortByName(entries);
    QCOMPARE(static_cast<int>(entries.size()), names_.size());
    // Directories first, then everything else; nothing lost or doubled.
    bool seenFile = false;
    for (const core::Entry& e : entries) {
        if (e.isDir())
            QVERIFY(!seenFile);
        else
            seenFile = true;
    }
    core::sortByName(entries, false);
    QCOMPARE(static_cast<int>(entries.size()), names_.size());
}

void HostileTest::detailsOnBrokenEntries()
{
    for (const char* name : {"loop", "dangle", "unreadable", "sparse.bin"}) {
        core::Details got;
        bool done = false;
        const QByteArray path = QByteArray(name) == "sparse.bin" ? sparse_ : join(root_, name);
        core::queryDetails(core::Location::fromPath(path), [&](const core::Details& d) {
            got = d;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        if (QByteArray(name) == "sparse.bin") {
            QVERIFY(got.ok);
            QCOMPARE(got.size, 4ull * 1024 * 1024 * 1024 + 7);
        }
        if (QByteArray(name) == "dangle") {
            QVERIFY(got.symlink);
            QCOMPARE(got.symlinkTarget, QStringLiteral("/nonexistent/target"));
        }
    }
}

void HostileTest::sparseFileReads()
{
    core::Reader reader;
    QSignalSpy opened(&reader, &core::Reader::opened);
    QSignalSpy data(&reader, &core::Reader::data);
    QSignalSpy failed(&reader, &core::Reader::failed);
    reader.open(core::Location::fromPath(sparse_));
    QTRY_VERIFY(!opened.isEmpty() || !failed.isEmpty());
    QVERIFY(failed.isEmpty());
    QCOMPARE(opened.first().first().toULongLong(), 4ull * 1024 * 1024 * 1024 + 7);
    // A window past 4 GiB: offsets must not wrap at 32 bits.
    reader.read(4ull * 1024 * 1024 * 1024, 64);
    QTRY_VERIFY(!data.isEmpty() || !failed.isEmpty());
    QVERIFY(failed.isEmpty());
    QCOMPARE(data.first().at(0).toULongLong(), 4ull * 1024 * 1024 * 1024);
    QCOMPARE(data.first().at(1).toByteArray(), QByteArray(7, '\0'));
}

void HostileTest::finderStopsAtLoops()
{
    core::Finder finder;
    std::vector<core::Entry> found;
    connect(&finder, &core::Finder::matches, [&](quint64, std::vector<core::Entry> e) {
        found.insert(found.end(), e.begin(), e.end());
    });
    QSignalSpy finished(&finder, &core::Finder::finished);
    finder.start(core::Location::fromPath(root_), QStringLiteral("txt"), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);  // a followed loop would never end
    QList<QByteArray> names;
    for (const core::Entry& e : found)
        names << e.name;
    QVERIFY(names.contains(kNewline));
    QVERIFY(names.contains(kLatin1));
    QVERIFY(names.contains("plain/ok.txt"));
}

void HostileTest::sizesStopAtLoops()
{
    core::DirSizePool pool;
    core::DirSizer sizer(&pool);
    core::DirSize last;
    bool complete = false;
    connect(&sizer, &core::DirSizer::result, [&](quint64, const QByteArray&, const core::DirSize& s) {
        last = s;
        complete = s.complete;
    });
    sizer.request(core::Location::fromPath(QFile::encodeName(tmp_.path())), "hostile", 1, true);
    QTRY_VERIFY_WITH_TIMEOUT(complete, 10000);
    QVERIFY(last.ok);
    QCOMPARE(last.direct, static_cast<quint64>(names_.size()));
    if (::geteuid() != 0)
        QCOMPARE(last.unreadable, 1u);
    QCOMPARE(last.dirs, 3u);  // plain, a, unreadable; the loop and a/back are not followed
}

void HostileTest::renamePlanRejectsSlashes()
{
    core::RenameRule rule;
    rule.mode = core::RenameRule::Mode::Replace;
    rule.pattern = QStringLiteral("break");
    rule.replacement = QStringLiteral("a/b");
    const QStringList names = {QString::fromUtf8(kNewline), QStringLiteral("x.txt"), QStringLiteral("y.txt")};
    const auto plan = core::planRename(rule, names, {QStringLiteral("x.txt"), QStringLiteral("y.txt"), QStringLiteral("z")});
    QVERIFY(plan.clash[0]);  // a slash can never be a name
    QVERIFY(!plan.clash[1]);
    QVERIFY(!plan.clash[2]);

    // Two sources mapped onto one target clash; one onto an untouched file too.
    rule.pattern = QStringLiteral("?.txt");
    rule.replacement = QStringLiteral("z");
    rule.mode = core::RenameRule::Mode::Wildcard;
    rule.ignoreExtension = false;
    const auto plan2 = core::planRename(rule, {QStringLiteral("x.txt"), QStringLiteral("y.txt")}, {QStringLiteral("z")});
    QVERIFY(plan2.clash[0]);
    QVERIFY(plan2.clash[1]);
}

void HostileTest::shellQuoteRoundTrip()
{
    // The quoted name through a real shell comes back byte for byte, and
    // the command substitution inside it never runs.
    for (const QByteArray& name : {kShell, kNewline, kTab, kSpaces, kDash, kLatin1}) {
        const QByteArray script = "printf '%s' " + core::shellQuote(name);
        const QByteArray out = join(root_, "quote.out");
        const int fd = ::open(out.constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        QVERIFY(fd >= 0);
        const pid_t pid = ::fork();
        QVERIFY(pid >= 0);
        if (pid == 0) {
            ::dup2(fd, 1);
            ::execl("/bin/sh", "sh", "-c", script.constData(), static_cast<char*>(nullptr));
            ::_exit(127);
        }
        int status = 0;
        ::waitpid(pid, &status, 0);
        ::close(fd);
        QCOMPARE(readFile(out), name);
    }
    QVERIFY(!QFile::exists(QFile::decodeName(join(root_, "pwned"))));
    QVERIFY(!QFile::exists(QStringLiteral("pwned")));
}

void HostileTest::copyKeepsNamesAndLinks()
{
    const QByteArray dest = QFile::encodeName(tmp_.path()) + "/copy";
    QVERIFY(::mkdir(dest.constData(), 0755) == 0);
    core::TransferJob job({core::Location::fromPath(root_)}, core::Location::fromPath(dest), false);
    job.setUnattended(true);
    runJob(job);
    const QByteArray copied = dest + "/hostile";
    for (const QByteArray& n : {kLatin1, kNewline, kTab, kLong, kSpaces, kDash, kOption, kShell, kNfc, kNfd})
        QCOMPARE(readFile(join(copied, n)), "content of " + n);
    QCOMPARE(readFile(join(copied, "plain/ok.txt")), QByteArray("fine"));
    // Links are copied as links (rule 15), loops and dangling ones included.
    char target[64];
    ssize_t n = ::readlink(join(copied, "loop").constData(), target, sizeof target);
    QCOMPARE(QByteArray(target, static_cast<qsizetype>(std::max<ssize_t>(n, 0))), QByteArray("loop"));
    n = ::readlink(join(copied, "dangle").constData(), target, sizeof target);
    QCOMPARE(QByteArray(target, static_cast<qsizetype>(std::max<ssize_t>(n, 0))), QByteArray("/nonexistent/target"));
    n = ::readlink(join(copied, "a/back").constData(), target, sizeof target);
    QCOMPARE(QByteArray(target, static_cast<qsizetype>(std::max<ssize_t>(n, 0))), QByteArray("../a"));
    // The unreadable folder is the one failure, logged, and nothing else stopped.
    if (::geteuid() != 0) {
        QVERIFY(!job.log().empty());
        bool logged = false;
        for (const core::LogEntry& e : job.log())
            logged = logged || e.path.contains(QStringLiteral("unreadable"));
        QVERIFY(logged);
    }
}

void HostileTest::archiveMembersStayInside()
{
    // The parser drops what bsdtar refuses to extract and shows the rest
    // as it would land.
    const QByteArray sample =
        "-rw-------  0 0      0           3 Oct 30 16:03 ../escape.txt\n"
        "-rw-------  0 0      0           3 Oct 30 16:03 /abs.txt\n"
        "-rw-------  0 0      0           3 Oct 30 16:03 ok/../../up.txt\n"
        "-rw-------  0 0      0           2 Oct 30 16:03 ./ok/inner.txt\n"
        "-rw-------  0 0      0           4 Oct 30 16:03 -rf\n"
        "-rw-------  0 0      0           1 Oct 30 16:03 caf\\303\\251 \\\\ \\n\\t.txt\n";
    core::ArchiveDropped dropped;
    const auto parsed = core::parseArchiveListing(sample, 2026, &dropped);
    QCOMPARE(dropped.absolute, 1);  // the preview warns about these
    QCOMPARE(dropped.climbing, 2);
    QList<QByteArray> raw;
    for (const auto& e : parsed)
        raw << e.path;
    // bsdtar's escapes are undone: UTF-8 bytes, a backslash, a newline and a tab.
    QCOMPARE(raw, QList<QByteArray>({"abs.txt", "ok/inner.txt", "-rf", QByteArray("caf\xc3\xa9 \\ \n\t.txt")}));

    // A real archive with a member that climbs out, made with bsdtar -P.
    const QByteArray base = QFile::encodeName(tmp_.path());
    QVERIFY(::mkdir(join(base, "arc").constData(), 0755) == 0);
    QVERIFY(::mkdir(join(base, "arc/in").constData(), 0755) == 0);
    QVERIFY(::mkdir(join(base, "arc/in/ok").constData(), 0755) == 0);
    makeFile(join(base, "arc/escape.txt"), "out");
    makeFile(join(base, "arc/in/ok/inner.txt"), "in");
    makeFile(join(base, "arc/in/ok/" + kNewline), "nl");
    makeFile(join(base, "arc/in/-rf"), "dash");
    makeFile(join(base, "arc/in/ok/" + kNfc), "utf8");
    QProcess tar;
    tar.setWorkingDirectory(QFile::decodeName(join(base, "arc/in")));
    // bsdtar takes no "--" in its file list; "./-rf" names the dash file.
    tar.start(QStringLiteral("bsdtar"), {QStringLiteral("-cPf"), QStringLiteral("../hostile.tar"), QStringLiteral("../escape.txt"),
                                         QStringLiteral("ok"), QStringLiteral("./-rf")});
    if (!tar.waitForFinished(10000) || tar.exitCode() != 0)
        QSKIP("bsdtar not available");
    const core::Location archive = core::Location::fromPath(join(base, "arc/hostile.tar"));

    core::ArchiveLister lister;
    std::vector<core::ArchiveEntry> listed;
    bool done = false;
    connect(&lister, &core::ArchiveLister::listed,
            [&](quint64, std::vector<core::ArchiveEntry> e, core::ArchiveDropped) { listed = e; done = true; });
    connect(&lister, &core::ArchiveLister::failed, [&](quint64, const QString& m) { QFAIL(qPrintable(m)); });
    lister.start(archive, 1);
    QTRY_VERIFY(done);
    QList<QByteArray> members;
    for (const auto& e : listed)
        members << e.path;
    QVERIFY(!members.contains("../escape.txt"));
    QVERIFY(!members.contains(".."));
    QVERIFY(members.contains("ok/" + kNewline));
    QVERIFY(members.contains("ok/" + kNfc));
    QVERIFY(members.contains("-rf"));

    // Extracting everything the listing shows lands inside the target and
    // nowhere else; the newline name and the dash name come through.
    const QByteArray out = join(base, "arc/out");
    QVERIFY(::mkdir(out.constData(), 0755) == 0);
    std::vector<QByteArray> all(members.begin(), members.end());
    core::ExtractJob job(archive, all, core::Location::fromPath(out), 0);
    runJob(job);
    QVERIFY2(job.log().empty(), job.log().empty() ? "" : qPrintable(job.log().front().message));
    QCOMPARE(readFile(join(out, "ok/inner.txt")), QByteArray("in"));
    QCOMPARE(readFile(join(out, "ok/" + kNewline)), QByteArray("nl"));
    QCOMPARE(readFile(join(out, "ok/" + kNfc)), QByteArray("utf8"));
    QCOMPARE(readFile(join(out, "-rf")), QByteArray("dash"));
    QCOMPARE(readFile(join(base, "arc/escape.txt")), QByteArray("out"));  // the original, untouched

    // Asked for the climbing member directly, the job fails it and writes nothing outside.
    core::ExtractJob bad(archive, {QByteArray("../escape.txt")}, core::Location::fromPath(out), 0);
    runJob(bad);
    QVERIFY(!bad.log().empty());
    QVERIFY(!QFile::exists(QFile::decodeName(join(base, "escape.txt"))));
}

void HostileTest::pdfToolOutput()
{
    // What the tools print is parsed, never trusted: absurd sizes and
    // counts give an empty result, not an allocation.
    QCOMPARE(core::parsePdfInfo("Pages: -5\nPage size: -1 x 0 pts\n").pages, 0);
    QCOMPARE(core::parsePdfInfo("Pages: 99999999999999999999\n").pages, 0);
    QCOMPARE(core::parsePdfInfo(QByteArray("\0\xff\n:::\n", 8)).widthPt, 0.0);
    QVERIFY(core::parsePpm("").rgb.isEmpty());
    QVERIFY(core::parsePpm("P6").rgb.isEmpty());
    QVERIFY(core::parsePpm("P6\n99999999999 99999999999\n255\n").rgb.isEmpty());
    QVERIFY(core::parsePpm("P6\n40000 40000\n255\nxyz").rgb.isEmpty());
    QVERIFY(core::parsePpm("P6\n0 0\n255\n").rgb.isEmpty());
    QVERIFY(core::parsePpm("P6\n1 1\n65535\n" + QByteArray(6, 'a')).rgb.isEmpty());
    QVERIFY(core::parsePpm("P6\n-1 1\n255\n" + QByteArray(3, 'a')).rgb.isEmpty());
    QVERIFY(core::parsePpm("P6 1 1 255 " + QByteArray(3, 'a')).width == 1);
}

QTEST_GUILESS_MAIN(HostileTest)
#include "test_hostile.moc"
