// A tab over a real temp folder, offscreen: the marks of a folder
// survive a round trip into a subfolder or an archive and act only
// where they live.
#include "core/dirsize.h"
#include "core/thumbnails.h"
#include "pane/listview.h"
#include "pane/tab.h"
#include "services.h"

#include <QDir>
#include <QFile>
#include <QProcess>
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

core::Location loc(const QString& path)
{
    return core::Location::fromPath(QFile::encodeName(path));
}

QStringList markedNames(const Tab& tab)
{
    QStringList out;
    if (tab.markedCount() == 0)
        return out;
    for (const core::Location& l : tab.selectedLocations())
        out << l.displayName();
    out.sort();
    return out;
}

} // namespace

class TabTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void marksComeBack();
    void marksActOnlyWhereTheyLive();
    void vanishedNamesAreNotRestored();
    void finishedJobClearsWhileAway();
    void marksAcrossAnArchive();

private:
    // Runs go() and waits for the load it starts.
    template <typename F>
    void load(Tab& tab, F go)
    {
        QSignalSpy loaded(&tab, &Tab::loaded);
        go();
        QTRY_VERIFY(!loaded.isEmpty());
    }
    void markFirstRows(Tab& tab, int n)
    {
        for (int i = 0; i < n; ++i)
            tab.execute(keymap::Command::Mark);
    }

    QTemporaryDir tmp_;
    QString root_;
    core::DirSizePool* pool_ = nullptr;
    core::Thumbnailer* thumbs_ = nullptr;
    Services services_;
};

void TabTest::initTestCase()
{
    QVERIFY(tmp_.isValid());
    pool_ = new core::DirSizePool(this);
    thumbs_ = new core::Thumbnailer(this);
    services_ = {pool_, thumbs_, nullptr};
}

void TabTest::init()
{
    // A fresh tree per case: sub/ first in the list, then a.txt, b.txt.
    root_ = tmp_.path() + QStringLiteral("/") + QString::fromLatin1(QTest::currentTestFunction());
    QVERIFY(QDir().mkpath(root_ + "/sub"));
    write(root_ + "/sub/inner.txt", "in");
    write(root_ + "/a.txt", "a");
    write(root_ + "/b.txt", "b");
}

void TabTest::marksComeBack()
{
    Tab tab(services_);
    load(tab, [&] { tab.navigate(loc(root_)); });
    markFirstRows(tab, 2);
    QCOMPARE(markedNames(tab), QStringList({"a.txt", "sub"}));

    load(tab, [&] { tab.navigate(loc(root_ + "/sub")); });
    QCOMPARE(tab.markedCount(), 0);
    load(tab, [&] { tab.goUp(); });
    QCOMPARE(markedNames(tab), QStringList({"a.txt", "sub"}));
    QCOMPARE(tab.currentName(), QByteArray("sub"));  // rule 3 for the cursor too
}

void TabTest::marksActOnlyWhereTheyLive()
{
    Tab tab(services_);
    load(tab, [&] { tab.navigate(loc(root_)); });
    markFirstRows(tab, 2);
    load(tab, [&] { tab.navigate(loc(root_ + "/sub")); });
    // F5 here copies the cursor item, never the parent's marks.
    const auto items = tab.selectedLocations();
    QCOMPARE(items.size(), 1u);
    QCOMPARE(items.front().displayName(), QStringLiteral("inner.txt"));
}

void TabTest::vanishedNamesAreNotRestored()
{
    Tab tab(services_);
    load(tab, [&] { tab.navigate(loc(root_)); });
    tab.view()->setCurrentIndex(tab.view()->model()->index(1, 0));  // a.txt
    markFirstRows(tab, 2);
    QCOMPARE(markedNames(tab), QStringList({"a.txt", "b.txt"}));
    load(tab, [&] { tab.navigate(loc(root_ + "/sub")); });
    QVERIFY(QFile::remove(root_ + "/b.txt"));
    load(tab, [&] { tab.goUp(); });
    QCOMPARE(markedNames(tab), QStringList({"a.txt"}));
}

void TabTest::finishedJobClearsWhileAway()
{
    Tab tab(services_);
    load(tab, [&] { tab.navigate(loc(root_)); });
    markFirstRows(tab, 2);
    load(tab, [&] { tab.navigate(loc(root_ + "/sub")); });
    tab.clearMarks(loc(root_));  // the copy of those two items ended
    load(tab, [&] { tab.goUp(); });
    QCOMPARE(tab.markedCount(), 0);
    // While the tab shows the folder, the same call clears the live marks.
    markFirstRows(tab, 1);
    tab.clearMarks(loc(root_));
    QCOMPARE(tab.markedCount(), 0);
}

void TabTest::marksAcrossAnArchive()
{
    QProcess tar;
    tar.setWorkingDirectory(root_);
    tar.start(QStringLiteral("bsdtar"), {QStringLiteral("-cf"), QStringLiteral("demo.zip"), QStringLiteral("sub")});
    if (!tar.waitForFinished(10000) || tar.exitCode() != 0)
        QSKIP("bsdtar not available");

    Tab tab(services_);
    load(tab, [&] { tab.navigate(loc(root_)); });
    tab.view()->setCurrentIndex(tab.view()->model()->index(1, 0));  // a.txt
    markFirstRows(tab, 1);
    QCOMPARE(markedNames(tab), QStringList({"a.txt"}));

    // Into the archive (the failed listing turns it into a folder), mark
    // its one folder, step in and back: the marks inside come back too.
    load(tab, [&] { tab.navigate(loc(root_ + "/demo.zip")); });
    QVERIFY(tab.isArchive());
    QCOMPARE(tab.markedCount(), 0);
    markFirstRows(tab, 1);
    QCOMPARE(markedNames(tab), QStringList({"sub"}));
    load(tab, [&] { tab.execute(keymap::Command::Activate); });
    QCOMPARE(tab.archiveDepth(), 1);
    QCOMPARE(tab.markedCount(), 0);
    load(tab, [&] { tab.goUp(); });
    QCOMPARE(markedNames(tab), QStringList({"sub"}));

    // Out of the archive: the folder's own marks.
    load(tab, [&] { tab.goUp(); });
    QVERIFY(!tab.isArchive());
    QCOMPARE(markedNames(tab), QStringList({"a.txt"}));
    QCOMPARE(tab.currentName(), QByteArray("demo.zip"));
}

QTEST_MAIN(TabTest)
#include "test_tab.moc"
