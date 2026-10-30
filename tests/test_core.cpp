// Headless tests for core against a real temp directory (never a mock).
#include "core/apps.h"
#include "core/bookmarks.h"
#include "core/dirsize.h"
#include "core/mounts.h"
#include "core/image.h"
#include "core/listing.h"
#include "core/reader.h"
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
    void readerWindows();
    void details();
    void shellQuoting();
    void imageDecode();
    void bookmarks();
    void mountsAndTrash();
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

void CoreTest::readerWindows()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QByteArray content;
    for (int i = 0; i < 5000; ++i)
        content += QByteArray::number(i) + '\n';
    touch(tmp.path() + "/big.txt", content);
    const core::Location file = core::Location::fromPath(QFile::encodeName(tmp.path() + "/big.txt"));

    core::Reader reader;
    quint64 size = 0;
    std::vector<std::pair<quint64, QByteArray>> got;
    connect(&reader, &core::Reader::opened, [&](quint64 s) { size = s; });
    connect(&reader, &core::Reader::data, [&](quint64 off, const QByteArray& b) { got.emplace_back(off, b); });
    QSignalSpy failed(&reader, &core::Reader::failed);
    reader.open(file);
    reader.read(0, 1000);
    while (got.empty())
        QTest::qWait(5);
    QCOMPARE(size, static_cast<quint64>(content.size()));
    QCOMPARE(got[0].first, 0u);
    QCOMPARE(got[0].second, content.left(1000));

    // A window in the middle, and one that runs past the end.
    reader.read(20000, 500);
    while (got.size() < 2)
        QTest::qWait(5);
    QCOMPARE(got[1].second, content.mid(20000, 500));
    reader.read(size - 10, 1000);
    while (got.size() < 3)
        QTest::qWait(5);
    QCOMPARE(got[2].second, content.right(10));
    QVERIFY(failed.isEmpty());

    reader.open(core::Location::fromPath(QFile::encodeName(tmp.path() + "/missing")));
    while (failed.isEmpty())
        QTest::qWait(5);
}

void CoreTest::details()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    touch(tmp.path() + "/notes.txt", "hello\n");
    QVERIFY(QFile::setPermissions(tmp.path() + "/notes.txt",
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup));
    QVERIFY(QFile::link("notes.txt", tmp.path() + "/link"));
    const core::Location dir = core::Location::fromPath(QFile::encodeName(tmp.path()));

    core::Details file, link, folder;
    int done = 0;
    core::queryDetails(dir.child("notes.txt"), [&](const core::Details& d) { file = d; ++done; });
    core::queryDetails(dir.child("link"), [&](const core::Details& d) { link = d; ++done; });
    core::queryDetails(dir, [&](const core::Details& d) { folder = d; ++done; });
    while (done < 3)
        QTest::qWait(5);
    QVERIFY(file.ok);
    QCOMPARE(file.size, 6u);
    QCOMPARE(file.contentType, QStringLiteral("text/plain"));
    QVERIFY(!file.typeDescription.isEmpty());
    QCOMPARE(file.mode & 0777, 0640u);
    QVERIFY(file.modified > 0);
    QVERIFY(!file.owner.isEmpty());
    QVERIFY(link.symlink);
    QCOMPARE(link.symlinkTarget, QStringLiteral("notes.txt"));
    QVERIFY(folder.isDir);
}

void CoreTest::shellQuoting()
{
    QCOMPARE(core::shellQuote("plain"), QByteArray("'plain'"));
    QCOMPARE(core::shellQuote("it's a file"), QByteArray("'it'\\''s a file'"));
    QVERIFY(!core::editorCommand().isEmpty());
}

void CoreTest::imageDecode()
{
    // A 2x2 PNG written byte by byte: red, green / blue, white.
    static const unsigned char png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a,
        0x73, 0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xc0,
        0x00, 0xc2, 0x0c, 0xff, 0x81, 0x00, 0x00, 0x1f, 0xee, 0x05, 0xfb, 0x0b, 0xd9, 0x68, 0x8b, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    touch(tmp.path() + "/tiny.png", QByteArray(reinterpret_cast<const char*>(png), sizeof png));

    core::ImageLoader loader;
    core::ImageFrame frame;
    QString error;
    bool done = false;
    connect(&loader, &core::ImageLoader::decoded, [&](const core::ImageFrame& f) { frame = f; done = true; });
    connect(&loader, &core::ImageLoader::failed, [&](const QString& e) { error = e; done = true; });
    loader.load(core::Location::fromPath(QFile::encodeName(tmp.path() + "/tiny.png")), 64, 64);
    while (!done)
        QTest::qWait(10);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(frame.width, 2u);
    QCOMPARE(frame.height, 2u);
    QCOMPARE(frame.sourceWidth, 2u);
    QVERIFY(frame.stride >= 8);
    QCOMPARE(static_cast<quint64>(frame.bgra.size()), static_cast<quint64>(frame.stride) * frame.height);
    const auto* px = reinterpret_cast<const unsigned char*>(frame.bgra.constData());
    QCOMPARE(int(px[2]), 255);  // first pixel red, in BGRA
    QCOMPARE(int(px[1]), 0);
    QCOMPARE(int(px[3]), 255);
}

void CoreTest::bookmarks()
{
    const auto list = core::Bookmarks::parse("file:///usr/share Shared stuff\n\nfile:///tmp\n# not a uri\n");
    QCOMPARE(list.size(), 2u);
    QCOMPARE(list[0].name, QStringLiteral("Shared stuff"));
    QCOMPARE(list[0].location.path(), QByteArray("/usr/share"));
    QCOMPARE(list[1].name, QStringLiteral("tmp"));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + "/bookmarks";
    touch(path, "file:///usr\n");
    core::Bookmarks marks(QFile::encodeName(path));
    QCOMPARE(marks.list().size(), 1u);
    QSignalSpy changed(&marks, &core::Bookmarks::changed);
    touch(path, "file:///usr\nfile:///etc Config\n");
    while (changed.isEmpty())
        QTest::qWait(10);
    QCOMPARE(marks.list().size(), 2u);
    QCOMPARE(marks.list()[1].name, QStringLiteral("Config"));
}

void CoreTest::mountsAndTrash()
{
    core::Mounts mounts;
    for (const core::Place& p : mounts.places()) {
        QVERIFY(!p.id.isEmpty());
        QVERIFY(!p.name.isEmpty());
        QCOMPARE(p.location.isValid(), p.mounted);
    }
    // The trash lists like any folder (it may be empty).
    core::Listing listing;
    QSignalSpy done(&listing, &core::Listing::finished);
    QSignalSpy failed(&listing, &core::Listing::failed);
    listing.start(core::Location::fromUri(QStringLiteral("trash:///")), 1);
    while (done.isEmpty() && failed.isEmpty())
        QTest::qWait(10);
    QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(1).toString()));
}

QTEST_GUILESS_MAIN(CoreTest)
#include "test_core.moc"
