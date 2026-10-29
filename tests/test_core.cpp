// Headless tests for core against a real temp directory (never a mock).
#include "core/listing.h"
#include "core/location.h"
#include "core/metadata.h"
#include "core/sort.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
};

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

QTEST_GUILESS_MAIN(CoreTest)
#include "test_core.moc"
