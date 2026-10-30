// Headless tests for core against a real temp directory (never a mock).
#include "core/dirsize.h"
#include "core/listing.h"
#include "core/location.h"
#include "core/metadata.h"
#include "core/sort.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

namespace {

void touch(const QString& path, const QByteArray& content = {})
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

// Runs a listing to completion and returns all entries.
std::vector<core::Entry> list(const core::Location& dir, quint64 generation = 1)
{
    core::Listing listing;
    std::vector<core::Entry> all;
    QObject::connect(&listing, &core::Listing::batch, [&](quint64, std::vector<core::Entry> b) {
        all.insert(all.end(), b.begin(), b.end());
    });
    QSignalSpy done(&listing, &core::Listing::finished);
    QSignalSpy failed(&listing, &core::Listing::failed);
    listing.start(dir, generation);
    while (done.isEmpty() && failed.isEmpty())
        QTest::qWait(10);
    return all;
}

} // namespace

class CoreTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void location();
    void listingEntries();
    void listingCancel();
    void naturalSort();
    void metadata();
    void dirSizeCount();
    void dirSizeWalk();
    void dirSizeCancel();
};

namespace {

// Asks the pool for one folder and returns the complete result.
core::DirSize sizeOf(core::DirSizePool& pool, const core::Location& dir, const QByteArray& name,
                     bool recursive, int* partials = nullptr)
{
    core::DirSizer sizer(&pool);
    core::DirSize got;
    bool done = false;
    QObject::connect(&sizer, &core::DirSizer::result,
                     [&](quint64 gen, const QByteArray& n, const core::DirSize& s) {
                         if (gen != 5 || n != name)
                             return;
                         if (s.complete) {
                             got = s;
                             done = true;
                         } else if (partials) {
                             ++*partials;
                         }
                     });
    sizer.request(dir, name, 5, recursive);
    while (!done)
        QTest::qWait(5);
    return got;
}

} // namespace

void CoreTest::location()
{
    const core::Location root = core::Location::root();
    const core::Location home = core::Location::home();
    QVERIFY(root.isValid());
    QVERIFY(!root.parent().isValid());
    QVERIFY(home.isNative());
    QCOMPARE(root.path(), QByteArray("/"));
    QCOMPARE(root.relativePathFrom(root), QByteArray(""));
    QVERIFY(!root.relativePathFrom(root).isNull());
    QVERIFY(root.relativePathFrom(home).isNull());

    const core::Location a = root.child("usr").child("lib");
    QCOMPARE(a.path(), QByteArray("/usr/lib"));
    QCOMPARE(a.basename(), QByteArray("lib"));
    QCOMPARE(a.relativePathFrom(root), QByteArray("usr/lib"));
    QCOMPARE(a.parent(), root.child("usr"));
    QVERIFY(a != root);

    const core::Location copy = a;
    QCOMPARE(copy, a);
    QCOMPARE(core::Location::fromUri(QStringLiteral("file:///usr/lib")), a);
}

void CoreTest::listingEntries()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString d = tmp.path();
    touch(d + "/file10.txt");
    touch(d + "/file2.txt");
    touch(d + "/.hidden");
    QVERIFY(QDir(d).mkdir("sub"));
    QVERIFY(QFile::link(d + "/sub", d + "/link-to-dir"));
    QVERIFY(QFile::link(d + "/nowhere", d + "/broken"));

    auto entries = list(core::Location::fromPath(QFile::encodeName(d)));
    QCOMPARE(entries.size(), 6u);
    core::sortByName(entries);

    // Directories first, including a symlink to one; then natural order.
    QCOMPARE(entries[0].name, QByteArray("link-to-dir"));
    QVERIFY(entries[0].isDir());
    QVERIFY(entries[0].symlink);
    QCOMPARE(entries[1].name, QByteArray("sub"));
    QVERIFY(entries[1].isDir());
    QVERIFY(!entries[1].symlink);
    QCOMPARE(entries[2].name, QByteArray(".hidden"));
    QVERIFY(entries[2].hidden);
    QCOMPARE(entries[3].name, QByteArray("broken"));
    QCOMPARE(entries[3].type, core::EntryType::Symlink);
    QCOMPARE(entries[4].name, QByteArray("file2.txt"));
    QCOMPARE(entries[5].name, QByteArray("file10.txt"));
    QVERIFY(!entries[5].hidden);
}

void CoreTest::listingCancel()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    for (int i = 0; i < 2000; ++i)
        touch(tmp.path() + QString("/f%1").arg(i));
    const core::Location dir = core::Location::fromPath(QFile::encodeName(tmp.path()));

    core::Listing listing;
    int batches = 0;
    connect(&listing, &core::Listing::batch, [&](quint64, std::vector<core::Entry>) { ++batches; });
    QSignalSpy done(&listing, &core::Listing::finished);
    listing.start(dir, 1);
    listing.cancel();
    QVERIFY(!listing.running());
    QTest::qWait(100);
    QCOMPARE(batches, 0);
    QVERIFY(done.isEmpty());

    // A restart with a new generation works after cancel.
    listing.start(dir, 2);
    while (done.isEmpty())
        QTest::qWait(10);
    QCOMPARE(done.first().first().toULongLong(), 2u);
    QVERIFY(batches > 0);
}

void CoreTest::naturalSort()
{
    auto make = [](const char* name, bool dir = false) {
        core::Entry e;
        e.name = name;
        e.display = QString::fromUtf8(name);
        e.type = dir ? core::EntryType::Directory : core::EntryType::File;
        core::computeSortKey(e);
        return e;
    };
    std::vector<core::Entry> v = {make("b"), make("a10"), make("a2"), make("Z", true), make("a1"), make("c", true)};
    core::sortByName(v);
    QStringList names;
    for (const auto& e : v)
        names << e.display;
    QCOMPARE(names, QStringList({"c", "Z", "a1", "a2", "a10", "b"}));

    core::sortByName(v, false);
    names.clear();
    for (const auto& e : v)
        names << e.display;
    QCOMPARE(names, QStringList({"Z", "c", "b", "a10", "a2", "a1"}));
}

void CoreTest::metadata()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    touch(tmp.path() + "/notes.txt", QByteArray(1234, 'x'));
    const core::Location dir = core::Location::fromPath(QFile::encodeName(tmp.path()));

    core::MetadataQuery query;
    core::Metadata got;
    QByteArray gotName;
    quint64 gotGen = 0;
    connect(&query, &core::MetadataQuery::result,
            [&](quint64 gen, const QByteArray& name, const core::Metadata& m) {
                gotGen = gen;
                gotName = name;
                got = m;
            });
    query.request(dir, "notes.txt", 7);
    while (gotGen == 0)
        QTest::qWait(10);
    QCOMPARE(gotName, QByteArray("notes.txt"));
    QVERIFY(got.ok);
    QCOMPARE(got.size, 1234u);
    QVERIFY(got.modified > 0);
    QCOMPARE(got.contentType, QStringLiteral("text/plain"));
    QVERIFY(!got.iconNames.isEmpty());

    // A missing file reports failure rather than hanging.
    gotGen = 0;
    query.request(dir, "missing", 8);
    while (gotGen == 0)
        QTest::qWait(10);
    QVERIFY(!got.ok);
}

void CoreTest::dirSizeCount()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString d = tmp.path();
    QVERIFY(QDir(d).mkpath("top/sub/deep"));
    touch(d + "/top/a", QByteArray(100, 'a'));
    touch(d + "/top/.hidden", QByteArray(7, 'h'));
    touch(d + "/top/sub/b", QByteArray(200, 'b'));
    touch(d + "/top/sub/deep/c", QByteArray(50, 'c'));
    const core::Location dir = core::Location::fromPath(QFile::encodeName(d));

    core::DirSizePool pool;
    const core::DirSize count = sizeOf(pool, dir, "top", false);
    QVERIFY(count.ok);
    QVERIFY(!count.recursive);
    QCOMPARE(count.direct, 3u);  // a, .hidden, sub
    QCOMPARE(count.dirs, 1u);
    QCOMPARE(count.files, 2u);
    QCOMPARE(count.bytes, 0u);

    const core::DirSize missing = sizeOf(pool, dir, "nowhere", false);
    QVERIFY(!missing.ok);
    QVERIFY(missing.complete);
}

void CoreTest::dirSizeWalk()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString d = tmp.path();
    QVERIFY(QDir(d).mkpath("top/sub/deep"));
    touch(d + "/top/a", QByteArray(100, 'a'));
    touch(d + "/top/sub/b", QByteArray(200, 'b'));
    touch(d + "/top/sub/deep/c", QByteArray(50, 'c'));
    // A symlink counts as itself (its path length), never its target.
    QVERIFY(QFile::link("a", d + "/top/link"));
    // A hard link to a file inside the tree is counted once.
    QVERIFY(::link(QFile::encodeName(d + "/top/a").constData(),
                   QFile::encodeName(d + "/top/sub/a-again").constData()) == 0);
    // A linked folder is not descended into.
    QVERIFY(QDir(d).mkpath("outside"));
    touch(d + "/outside/big", QByteArray(5000, 'x'));
    QVERIFY(QFile::link("../outside", d + "/top/to-outside"));
    const core::Location dir = core::Location::fromPath(QFile::encodeName(d));

    core::DirSizePool pool;
    const core::DirSize walk = sizeOf(pool, dir, "top", true);
    QVERIFY(walk.ok);
    QVERIFY(walk.recursive);
    QCOMPARE(walk.direct, 4u);  // a, sub, link, to-outside
    QCOMPARE(walk.dirs, 2u);    // sub, sub/deep
    QCOMPARE(walk.files, 6u);   // a, link, to-outside, b, a-again, c
    QCOMPARE(walk.bytes, 100u + 200u + 50u + 1u + 10u);  // "a" and "../outside" are the link sizes
    QCOMPARE(walk.unreadable, 0u);

    // A second ask is answered from the cache with the same numbers.
    const core::DirSize again = sizeOf(pool, dir, "top", true);
    QCOMPARE(again.bytes, walk.bytes);
    QCOMPARE(again.total(), walk.total());

    // Touching the folder changes its mtime and so misses the cache.
    touch(d + "/top/new", QByteArray(9, 'n'));
    const core::DirSize after = sizeOf(pool, dir, "top", true);
    QCOMPARE(after.bytes, walk.bytes + 9);
    QCOMPARE(after.direct, 5u);
}

void CoreTest::dirSizeCancel()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString d = tmp.path();
    for (int i = 0; i < 40; ++i) {
        QVERIFY(QDir(d).mkpath(QStringLiteral("top/d%1").arg(i)));
        for (int j = 0; j < 50; ++j)
            touch(d + QStringLiteral("/top/d%1/f%2").arg(i).arg(j));
    }
    const core::Location dir = core::Location::fromPath(QFile::encodeName(d));

    core::DirSizePool pool;
    core::DirSizer sizer(&pool);
    QSignalSpy results(&sizer, &core::DirSizer::result);
    sizer.request(dir, "top", 1, true);
    sizer.cancelAll();
    QTest::qWait(200);
    QCOMPARE(results.count(), 0);

    // The client is usable again afterwards.
    sizer.request(dir, "top", 2, true);
    while (results.isEmpty() || !results.last().at(2).value<core::DirSize>().complete)
        QTest::qWait(5);
    QCOMPARE(results.last().at(0).toULongLong(), 2u);
    QCOMPARE(results.last().at(2).value<core::DirSize>().files, 2000u);
}

QTEST_GUILESS_MAIN(CoreTest)
#include "test_core.moc"
