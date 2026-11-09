// A tab over a real temp folder, offscreen: the marks of a folder
// survive a round trip into a subfolder or an archive and act only
// where they live.
#include "attributesedit.h"
#include "core/dirsize.h"
#include "core/thumbnails.h"
#include "core/unixattrs.h"
#include "menu.h"
#include "ownercompletion.h"
#include "pane/listmodel.h"
#include "pane/listview.h"
#include "pane/tab.h"
#include "services.h"

#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QCheckBox>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QProcess>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>
#include <unistd.h>

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

quint32 modeOf(const QString& path)
{
    struct stat st {};
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? quint32(st.st_mode & 07777) : ~0u;
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
    void marksUnderAFilter();
    void marksOnHiddenFilesStayCounted();
    void cursorRowSpansTheList();
    void columnsHideAndMove();
    void nameKeepsAFloor();
    void attributeCells();
    void tabWalksTheRow();
    void ownerCompletion();
    void attributesEditor();
    void contextMenus();

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

void TabTest::marksUnderAFilter()
{
    // Invert and mark all act on the rows the filter shows; the marks it
    // hides stay, stay counted, and the prompt of a job names them (#55).
    Tab tab(services_);
    load(tab, [&] { tab.navigate(loc(root_)); });
    markFirstRows(tab, 2);  // sub, a.txt
    QTest::keyClick(tab.view(), Qt::Key_B);
    QCOMPARE(tab.entryNames().size(), 3);
    QCOMPARE(tab.view()->model()->rowCount(), 2);  // sub, b.txt
    QCOMPARE(tab.markedCount(), 2);
    tab.execute(keymap::Command::InvertMarks);  // sub off, b.txt on; a.txt untouched
    QCOMPARE(tab.markedCount(), 2);
    QCOMPARE(tab.selectionText(), QStringLiteral("2 items (1 not shown)"));
    QCOMPARE(tab.selectedLocations().size(), size_t(2));
    tab.execute(keymap::Command::MarkAll);
    QCOMPARE(tab.markedCount(), 3);
    tab.execute(keymap::Command::Cancel);  // the filter goes, the marks stay
    QCOMPARE(tab.view()->model()->rowCount(), 3);
    QCOMPARE(markedNames(tab), QStringList({"a.txt", "b.txt", "sub"}));
    QCOMPARE(tab.selectionText(), QStringLiteral("3 items"));
    // Clearing takes the hidden ones too: nothing is left to surprise F8.
    QTest::keyClick(tab.view(), Qt::Key_B);
    tab.execute(keymap::Command::ClearMarks);
    tab.execute(keymap::Command::Cancel);
    QCOMPARE(tab.markedCount(), 0);
    QVERIFY(tab.selectedLocations().size() <= 1);
}

void TabTest::marksOnHiddenFilesStayCounted()
{
    // A marked dotfile, hidden afterwards: the count, the prompt and
    // what a job would take agree, before and after a reload (#62).
    write(root_ + "/.dot", "d");
    Tab tab(services_);
    tab.setShowHidden(true);
    load(tab, [&] { tab.navigate(loc(root_)); });
    QCOMPARE(tab.entryNames(), QStringList({"sub", ".dot", "a.txt", "b.txt"}));
    tab.view()->setCurrentIndex(tab.view()->model()->index(1, 0));
    markFirstRows(tab, 2);  // .dot, a.txt
    tab.setShowHidden(false);
    QCOMPARE(tab.markedCount(), 2);
    load(tab, [&] { tab.reload(); });
    QCOMPARE(tab.markedCount(), 2);
    QCOMPARE(tab.selectionText(), QStringLiteral("2 items (1 not shown)"));
    QCOMPARE(tab.selectedLocations().size(), size_t(2));
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
    QCOMPARE(view->columnLayout().hiddenNames(), QStringList({"size", "items", "permissions", "owner"}));
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

    // The same one step at a time, as the column list's keys ask for it.
    view->moveColumn(SizeColumn, 1);
    QCOMPARE(changed.size(), 4);
    QCOMPARE(view->columnLayout().orderNames().mid(0, 3), QStringList({"name", "modified", "size"}));
    view->moveColumn(ModifiedColumn, -1);  // nothing goes before Name
    view->moveColumn(NameColumn, 1);       // and Name does not move
    QCOMPARE(changed.size(), 4);
    QCOMPARE(h->visualIndex(NameColumn), 0);

    // And with the mouse: a header section dragged past its neighbour.
    const auto centre = [&](int column) {
        return QPoint(h->sectionViewportPosition(column) + h->sectionSize(column) / 2, h->height() / 2);
    };
    const QPoint from = centre(ModifiedColumn), to = centre(SizeColumn) + QPoint(h->sectionSize(SizeColumn) / 3, 0);
    QTest::mousePress(h->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(h->viewport(), (from + to) / 2);
    QTest::mouseMove(h->viewport(), to);
    QTest::mouseRelease(h->viewport(), Qt::LeftButton, Qt::NoModifier, to);
    QCOMPARE(changed.size(), 5);
    QCOMPARE(view->columnLayout().orderNames().mid(0, 3), QStringList({"name", "size", "modified"}));

    // The sort follows its column wherever the column sits.
    tab.setSort(core::SortBy::Modified, false);
    QCOMPARE(h->sortIndicatorSection(), int(ModifiedColumn));
    QCOMPARE(tab.sortBy(), core::SortBy::Modified);
}

void TabTest::attributeCells()
{
    using namespace columns;
    QVERIFY(QFile::setPermissions(root_ + "/a.txt", QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                        | QFileDevice::ReadGroup | QFileDevice::ReadOther));
    Tab tab(services_);
    tab.resize(900, 400);
    tab.show();
    load(tab, [&] { tab.navigate(loc(root_)); });
    ListView* view = tab.view();
    view->applyColumns(Layout::from({"name", "permissions", "owner"}, {}), {});
    QAbstractItemModel* m = view->model();
    auto cell = [&](int column) { return m->index(tab.view()->model()->match(m->index(0, 0), Qt::DisplayRole, QStringLiteral("a.txt")).value(0).row(), column); };
    QSignalSpy messages(&tab, &Tab::message);

    // What ls -l says, from the listing's own stat.
    QCOMPARE(cell(PermissionsColumn).data().toString(), QStringLiteral("-rw-r--r--"));
    struct stat st {};
    QCOMPARE(::stat(QFile::encodeName(root_ + "/a.txt").constData(), &st), 0);
    core::IdNames ids;
    QCOMPARE(cell(OwnerColumn).data().toString(), ids.owner(st.st_uid, st.st_gid));
    QVERIFY(m->index(0, PermissionsColumn).data().toString().startsWith(QLatin1Char('d')));  // sub/

    // The cell edits the nine letters; either spelling changes the file
    // and the row follows.
    QVERIFY(cell(PermissionsColumn).flags() & Qt::ItemIsEditable);
    QVERIFY(cell(OwnerColumn).flags() & Qt::ItemIsEditable);
    QVERIFY(!(cell(SizeColumn).flags() & Qt::ItemIsEditable));
    QCOMPARE(cell(PermissionsColumn).data(Qt::EditRole).toString(), QStringLiteral("rw-r--r--"));
    QVERIFY(m->setData(cell(PermissionsColumn), QStringLiteral("rwxr-x---"), Qt::EditRole));
    QTRY_COMPARE(modeOf(root_ + "/a.txt"), 0750u);
    QTRY_COMPARE(cell(PermissionsColumn).data().toString(), QStringLiteral("-rwxr-x---"));
    QVERIFY(m->setData(cell(PermissionsColumn), QStringLiteral("0604"), Qt::EditRole));
    QTRY_COMPARE(modeOf(root_ + "/a.txt"), 0604u);
    QVERIFY(messages.isEmpty());

    // Not a mode, nobody's name: a message, and the file as it was.
    QVERIFY(!m->setData(cell(PermissionsColumn), QStringLiteral("rwxrwxrwz"), Qt::EditRole));
    QCOMPARE(messages.size(), 1);
    QVERIFY(!m->setData(cell(OwnerColumn), QStringLiteral("no-such-user-zz"), Qt::EditRole));
    QCOMPARE(messages.size(), 2);
    QVERIFY(messages.last().first().toString().contains(QStringLiteral("no-such-user-zz")));
    QCOMPARE(modeOf(root_ + "/a.txt"), 0604u);
    // The text as it stands asks for nothing.
    QVERIFY(!m->setData(cell(OwnerColumn), cell(OwnerColumn).data(Qt::EditRole), Qt::EditRole));
    QVERIFY(!m->setData(cell(PermissionsColumn), QStringLiteral("rw----r--"), Qt::EditRole));
    QCOMPARE(messages.size(), 2);
    // The group alone, to another of this user's groups (any group for
    // root): typed as :group, the owner stays as it is.
    std::optional<quint32> other;
    if (::geteuid() == 0) {
        other = st.st_gid == 1 ? 2 : 1;
    } else {
        std::vector<gid_t> groups(static_cast<size_t>(std::max(0, ::getgroups(0, nullptr))));
        const int n = ::getgroups(static_cast<int>(groups.size()), groups.data());
        for (int i = 0; i < n; ++i)
            if (groups[static_cast<size_t>(i)] != st.st_gid)
                other = groups[static_cast<size_t>(i)];
    }
    if (other) {
        const int before = int(messages.size());
        QVERIFY(m->setData(cell(OwnerColumn), QStringLiteral(":%1").arg(*other), Qt::EditRole));
        struct stat now {};
        QTRY_VERIFY(::stat(QFile::encodeName(root_ + "/a.txt").constData(), &now) == 0 && now.st_gid == *other);
        QCOMPARE(now.st_uid, st.st_uid);
        QTRY_COMPARE(cell(OwnerColumn).data().toString(), ids.owner(st.st_uid, *other));
        QCOMPARE(int(messages.size()), before);
    }
    if (::geteuid() != 0) {
        // Giving the file to root is refused by the system: its words, in the pane.
        const int before = int(messages.size());
        QVERIFY(m->setData(cell(OwnerColumn), QStringLiteral("root"), Qt::EditRole));
        QTRY_COMPARE(int(messages.size()), before + 1);
    }

    // A header click on Permissions orders by the bits, folders first.
    QVERIFY(QFile::setPermissions(root_ + "/b.txt", QFileDevice::ReadOwner));
    load(tab, [&] { tab.reload(); });
    tab.setSort(core::SortBy::Permissions, true);
    QCOMPARE(tab.sortBy(), core::SortBy::Permissions);
    QCOMPARE(m->index(0, NameColumn).data().toString(), QStringLiteral("sub"));
    QCOMPARE(m->index(1, NameColumn).data().toString(), QStringLiteral("b.txt"));  // 0400 before 0604
}

void TabTest::tabWalksTheRow()
{
    using namespace columns;
    QVERIFY(QFile::setPermissions(root_ + "/a.txt", QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    Tab tab(services_);
    tab.resize(900, 400);
    tab.show();
    load(tab, [&] { tab.navigate(loc(root_)); });
    ListView* view = tab.view();
    view->applyColumns(Layout::from({"name", "owner", "permissions"}, {}), {});
    view->setCurrentIndex(view->model()->index(1, 0));  // a.txt
    auto editor = [&] { return view->viewport()->findChild<QLineEdit*>(); };

    // F2 opens the name; Tab goes along the row in the order shown.
    tab.execute(keymap::Command::Rename);
    QVERIFY(view->isEditing());
    QCOMPARE(view->currentIndex().column(), int(NameColumn));
    QTest::keyClick(editor(), Qt::Key_Tab);
    QTRY_COMPARE(view->currentIndex().column(), int(OwnerColumn));
    QTRY_VERIFY(view->isEditing() && editor());
    QTest::keyClick(editor(), Qt::Key_Tab);
    QTRY_COMPARE(view->currentIndex().column(), int(PermissionsColumn));
    QTRY_VERIFY(view->isEditing() && editor() && editor()->text() == QLatin1String("rw-------"));
    QCOMPARE(tab.currentName(), QByteArray("a.txt"));  // still the same row, not renamed

    // Typed and entered: the file has the mode.
    editor()->setText(QStringLiteral("rwxr-xr-x"));
    QTest::keyClick(editor(), Qt::Key_Return);
    QTRY_COMPARE(modeOf(root_ + "/a.txt"), 0755u);
    QTRY_VERIFY(!view->isEditing());

    // Shift+Tab walks back, around the row's end.
    tab.execute(keymap::Command::Rename);
    QTest::keyClick(editor(), Qt::Key_Backtab);
    QTRY_COMPARE(view->currentIndex().column(), int(PermissionsColumn));
    QTRY_VERIFY(view->isEditing() && editor());
    QTest::keyClick(editor(), Qt::Key_Escape);
    QTRY_VERIFY(!view->isEditing());
    QVERIFY(QFile::exists(root_ + "/a.txt"));
}

void TabTest::ownerCompletion()
{
    // Users before the colon, groups after it, completed in the line.
    QLineEdit edit;
    completeOwners(&edit);
    edit.show();
    edit.activateWindow();
    edit.setFocus();
    if (!QTest::qWaitForWindowActive(&edit) || !edit.hasFocus())
        QSKIP("no window activation on this platform");
    QTest::keyClicks(&edit, QStringLiteral("roo"));
    QCOMPARE(edit.text(), QStringLiteral("root"));
    QCOMPARE(edit.selectedText(), QStringLiteral("t"));
    QTest::keyClick(&edit, Qt::Key_End);
    QTest::keyClicks(&edit, QStringLiteral(":roo"));
    QCOMPARE(edit.text(), QStringLiteral("root:root"));
}

void TabTest::attributesEditor()
{
    // The properties panel's editor over a file (rw-r--r--) and a folder
    // (rwxr-xr-x), both root's: it asks only for what was changed in it.
    qRegisterMetaType<core::AttributeChange>();
    AttributesEditor editor;
    QSignalSpy applied(&editor, &AttributesEditor::applyRequested);
    QSignalSpy said(&editor, &AttributesEditor::message);
    auto details = [](quint32 mode, bool dir) {
        core::Details d;
        d.ok = true;
        d.mode = mode;
        d.isDir = dir;
        d.owner = d.group = QStringLiteral("root");
        return d;
    };
    editor.clear();
    editor.add(details(0100644, false));
    editor.add(details(040755, true));
    editor.finish();
    // Nine bits in reading order, then "the contents too" (the one with a text).
    QList<QCheckBox*> boxes;
    QCheckBox* contents = nullptr;
    for (QCheckBox* box : editor.findChildren<QCheckBox*>()) {
        if (box->text().isEmpty())
            boxes.append(box);
        else
            contents = box;
    }
    QCOMPARE(boxes.size(), 9);
    QVERIFY(contents);
    boxes.append(contents);
    auto* apply = editor.findChildren<QPushButton*>().value(0);
    auto* owner = editor.findChild<QLineEdit*>();
    QVERIFY(apply && owner);
    QCOMPARE(owner->text(), QStringLiteral("root:root"));
    // Bits both have are checked, the execute bits only the folder has
    // are half checked.
    QCOMPARE(boxes[0]->checkState(), Qt::Checked);
    QCOMPARE(boxes[2]->checkState(), Qt::PartiallyChecked);
    QCOMPARE(boxes[6]->checkState(), Qt::Checked);
    QCOMPARE(boxes[8]->checkState(), Qt::PartiallyChecked);
    QCOMPARE(boxes[1]->isTristate(), false);

    // Nothing changed: nothing asked, and it says so.
    apply->click();
    QCOMPARE(applied.size(), 0);
    QCOMPARE(said.size(), 1);

    // Others lose read. The half-checked bits are not in the mask, so
    // the folder keeps its execute bits and the file gets none.
    boxes[6]->click();
    QCOMPARE(boxes[6]->checkState(), Qt::Unchecked);
    apply->click();
    QCOMPARE(applied.size(), 1);
    auto change = applied.last().first().value<core::AttributeChange>();
    QCOMPARE(change.mode, std::optional<quint32>(0640));
    QCOMPARE(change.modeMask, 0666u);
    QVERIFY(change.owner.isEmpty());  // the owner was not edited
    QVERIFY(!change.recursive);

    // The group alone, typed next to the owner as it is, down the tree:
    // no uid is asked for, and no mode once the items are read again.
    editor.clear();
    editor.add(details(0100644, false));
    editor.add(details(040755, true));
    editor.finish();
    owner->setText(QStringLiteral("root:54321"));
    boxes[9]->click();
    apply->click();
    QCOMPARE(applied.size(), 2);
    change = applied.last().first().value<core::AttributeChange>();
    QVERIFY(!change.mode);
    QVERIFY(!change.owner.uid);
    QCOMPARE(change.owner.gid, std::optional<quint32>(54321));
    QVERIFY(change.recursive);

    // A name nobody has: said, not asked for.
    owner->setText(QStringLiteral("no-such-user-zz:root"));
    apply->click();
    QCOMPARE(applied.size(), 2);
    QCOMPARE(said.size(), 2);
    QVERIFY(said.last().first().toString().contains(QStringLiteral("no-such-user-zz")));

    // Files only: there is no "contents" to offer.
    editor.clear();
    editor.add(details(0100600, false));
    editor.finish();
    QVERIFY(boxes[9]->isHidden());
    QCOMPARE(boxes[2]->isTristate(), false);
}

void TabTest::nameKeepsAFloor()
{
    using namespace columns;
    Tab tab(services_);
    tab.show();
    load(tab, [&] { tab.navigate(loc(root_)); });
    ListView* view = tab.view();
    QHeaderView* h = view->header();
    view->applyColumns(Layout::from({}, {}), {});  // the default columns, as a pane applies them
    // Widths come from the font, which differs from machine to machine:
    // the test sizes the tab from what the columns ask for.
    int others = 0;
    for (int c = NameColumn + 1; c < ColumnCount; ++c)
        if (!view->isColumnHidden(c))
            others += h->sectionSize(c);
    const int roomy = others + view->nameFloor() + 300;

    // Room for everything: Name takes the rest, nothing to scroll.
    tab.resize(roomy, 400);
    QTRY_VERIFY(h->sectionSize(NameColumn) > view->nameFloor());
    QTRY_COMPARE(h->length(), view->viewport()->width());
    QTRY_COMPARE(view->horizontalScrollBar()->maximum(), 0);

    // A pane too narrow for its columns: Name keeps its floor and the
    // list scrolls sideways to the rest instead of cutting it off.
    tab.resize(others / 2 + view->nameFloor(), 400);
    QTRY_COMPARE(h->sectionSize(NameColumn), view->nameFloor());
    QVERIFY(h->length() > view->viewport()->width());
    QTRY_VERIFY(view->horizontalScrollBar()->maximum() > 0);
    // The cursor row is still outlined across what is in view.
    QCOMPARE(view->rowRect(view->model()->index(0, 0)).width(), view->viewport()->width());

    // Wide again: back to filling the pane exactly.
    tab.resize(roomy, 400);
    QTRY_COMPARE(h->length(), view->viewport()->width());
    QTRY_COMPARE(view->horizontalScrollBar()->maximum(), 0);
}

void TabTest::contextMenus()
{
    using keymap::Command;
    // Every entry of every menu is a command with words to show, once.
    for (menu::Context context : {menu::Context::Item, menu::Context::Folder, menu::Context::Tabs,
                                  menu::Context::Tree, menu::Context::TreeMount}) {
        QSet<int> seen;
        const auto& entries = menu::contextEntries(context);
        QVERIFY(!entries.empty());
        QVERIFY(entries.front().command != Command::None && entries.back().command != Command::None);
        for (const menu::Entry& e : entries) {
            if (e.command == Command::None)
                continue;
            QVERIFY2(!menu::entryText(e).isEmpty(), qPrintable(keymap::name(e.command)));
            QVERIFY2(!seen.contains(int(e.command)), qPrintable(keymap::name(e.command)));
            seen.insert(int(e.command));
        }
    }

    Tab tab(services_);
    tab.resize(600, 400);
    tab.show();
    load(tab, [&] { tab.navigate(loc(root_)); });
    ListView* view = tab.view();
    QSignalSpy commands(&tab, &Tab::commandRequested);
    auto rightClick = [&](const QPoint& at) {
        QContextMenuEvent event(QContextMenuEvent::Mouse, at, view->viewport()->mapToGlobal(at));
        QCoreApplication::sendEvent(view->viewport(), &event);
        QMenu* menu = nullptr;
        for (QMenu* m : view->findChildren<QMenu*>())
            if (m->isVisible())
                menu = m;
        return menu;
    };
    auto commandsOf = [](QMenu* menu) {
        QList<int> out;
        for (QAction* a : menu->actions())
            if (a->data().isValid())
                out << a->data().toInt();
        return out;
    };

    // On a row: the cursor goes there, the first line names it, and a
    // choice comes out as the command the key would give.
    const QModelIndex b = view->model()->index(2, 0);  // b.txt
    QMenu* menu = rightClick(view->visualRect(b).center());
    QVERIFY(menu);
    QCOMPARE(tab.currentName(), QByteArray("b.txt"));
    QCOMPARE(menu->actions().first()->text(), QStringLiteral("b.txt"));
    QVERIFY(!menu->actions().first()->isEnabled());
    QVERIFY(commandsOf(menu).contains(int(Command::Properties)));
    QVERIFY(!commandsOf(menu).contains(int(Command::Mkdir)));
    // One key beside each entry, so the keys stay close to the words.
    for (QAction* a : menu->actions())
        if (a->data().isValid())
            QVERIFY2(!a->text().section(QLatin1Char('\t'), 1).contains(QLatin1Char(',')), qPrintable(a->text()));
    for (QAction* a : menu->actions())
        if (a->data().toInt() == int(Command::Copy))
            a->trigger();
    QCOMPARE(commands.size(), 1);
    QCOMPARE(commands.last().first().value<keymap::Command>(), Command::Copy);
    menu->close();

    // With marks, the commands are for them, and the menu says so even
    // when the click is on an unmarked row.
    view->setCurrentIndex(view->model()->index(0, 0));
    markFirstRows(tab, 2);
    menu = rightClick(view->visualRect(b).center());
    QVERIFY(menu);
    QVERIFY(menu->actions().first()->text().contains(QLatin1Char('2')));
    menu->close();

    // On empty space: the folder's own menu.
    menu = rightClick(QPoint(20, view->viewport()->height() - 5));
    QVERIFY(menu);
    QVERIFY(commandsOf(menu).contains(int(Command::Mkdir)));
    QVERIFY(!commandsOf(menu).contains(int(Command::Trash)));
    menu->close();
}

QTEST_MAIN(TabTest)
#include "test_tab.moc"
