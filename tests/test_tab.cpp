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
#include <QHeaderView>
#include <QProcess>
#include <QScrollBar>
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
    void cursorRowSpansTheList();
    void columnsHideAndMove();
    void nameKeepsAFloor();

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

void TabTest::cursorRowSpansTheList()
{
    // The inactive pane outlines this rectangle; a hidden last column
    // must not shrink it to the Name cell.
    Tab tab(services_);
    tab.resize(600, 400);
    tab.show();
    load(tab, [&] { tab.navigate(loc(root_)); });
    ListView* view = tab.view();
    const QModelIndex first = view->model()->index(0, 0);
    QCOMPARE(view->rowRect(first).width(), view->viewport()->width());
    view->applyColumns(columns::Layout::from({}, {QStringLiteral("modified")}), {});
    QCOMPARE(view->rowRect(first).width(), view->viewport()->width());
    QCOMPARE(view->rowRect(first).height(), view->visualRect(first).height());
}

void TabTest::columnsHideAndMove()
{
    using namespace columns;
    Tab tab(services_);
    tab.resize(600, 400);
    tab.show();
    load(tab, [&] { tab.navigate(loc(root_)); });
    ListView* view = tab.view();
    QHeaderView* h = view->header();
    QSignalSpy changed(view, &ListView::columnsChanged);

    // A saved arrangement: Modified before Size, Items hidden.
    view->applyColumns(Layout::from({"name", "modified", "size"}, {"items"}), {{QStringLiteral("size"), 91}});
    QVERIFY(changed.isEmpty());  // applying is not a change by the user
    QCOMPARE(h->visualIndex(NameColumn), 0);
    QCOMPARE(h->visualIndex(ModifiedColumn), 1);
    QCOMPARE(h->visualIndex(SizeColumn), 2);
    QVERIFY(view->isColumnHidden(ItemsColumn));
    QVERIFY(!view->isColumnHidden(SizeColumn));
    QCOMPARE(h->sectionSize(SizeColumn), 91);
    QCOMPARE(view->columnLayout().orderNames().mid(0, 3), QStringList({"name", "modified", "size"}));
    // The shown columns fill the list exactly, Name taking the rest.
    QCOMPARE(h->sectionSize(NameColumn) + h->sectionSize(ModifiedColumn) + h->sectionSize(SizeColumn),
             view->viewport()->width());

    // From the header: hiding takes the column out of the view and
    // reports the new arrangement; it comes back at its width.
    view->setColumnShown(SizeColumn, false);
    QCOMPARE(changed.size(), 1);
    QVERIFY(view->isColumnHidden(SizeColumn));
    QCOMPARE(view->columnLayout().hiddenNames(), QStringList({"size", "items"}));
    QCOMPARE(h->sectionSize(NameColumn) + h->sectionSize(ModifiedColumn), view->viewport()->width());
    view->setColumnShown(SizeColumn, true);
    QCOMPARE(changed.size(), 2);
    QCOMPARE(h->sectionSize(SizeColumn), 91);
    view->setColumnShown(SizeColumn, true);  // nothing to do, nothing said
    view->setColumnShown(NameColumn, false);  // Name stays
    QCOMPARE(changed.size(), 2);
    QVERIFY(!view->isColumnHidden(NameColumn));

    // A drag of a header section, as the header reports it.
    h->moveSection(h->visualIndex(SizeColumn), 1);
    QCOMPARE(changed.size(), 3);
    const auto layout = changed.last().first().value<columns::Layout>();
    QCOMPARE(layout.orderNames().mid(0, 3), QStringList({"name", "size", "modified"}));
    // Name cannot be dragged, and nothing can be dropped before it.
    QVERIFY(!h->isFirstSectionMovable());

    // The sort follows its column wherever the column sits.
    tab.setSort(core::SortBy::Modified, false);
    QCOMPARE(h->sortIndicatorSection(), int(ModifiedColumn));
    QCOMPARE(tab.sortBy(), core::SortBy::Modified);
}

void TabTest::nameKeepsAFloor()
{
    using namespace columns;
    Tab tab(services_);
    tab.resize(900, 400);
    tab.show();
    load(tab, [&] { tab.navigate(loc(root_)); });
    ListView* view = tab.view();
    QHeaderView* h = view->header();
    // Room for everything: Name takes the rest, nothing to scroll.
    QVERIFY(h->sectionSize(NameColumn) > view->nameFloor());
    QCOMPARE(h->length(), view->viewport()->width());
    QCOMPARE(view->horizontalScrollBar()->maximum(), 0);

    // A pane too narrow for its columns: Name keeps its floor and the
    // list scrolls sideways to the rest instead of cutting it off.
    tab.resize(180, 400);
    QTRY_COMPARE(h->sectionSize(NameColumn), view->nameFloor());
    QVERIFY(h->length() > view->viewport()->width());
    QTRY_VERIFY(view->horizontalScrollBar()->maximum() > 0);
    // The cursor row is still outlined across what is in view.
    QCOMPARE(view->rowRect(view->model()->index(0, 0)).width(), view->viewport()->width());

    // Wide again: back to filling the pane exactly.
    tab.resize(900, 400);
    QTRY_COMPARE(h->length(), view->viewport()->width());
    QTRY_COMPARE(view->horizontalScrollBar()->maximum(), 0);
}

QTEST_MAIN(TabTest)
#include "test_tab.moc"
