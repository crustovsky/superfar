// The whole window over temp folders, offscreen. These are the things
// that only work when the parts are wired together, and that broke
// unnoticed before: the tree's keys and drops reaching the window, the
// tab strip's and the tree's right-click menus, the column list driven
// from the keyboard, the function bar's layers.
#include "attributesedit.h"
#include "config.h"
#include "funcbar.h"
#include "jobs/bars.h"
#include "mainwindow.h"
#include "menu.h"
#include "pane/columns.h"
#include "pane/listview.h"
#include "pane/pane.h"
#include "pane/tabbar.h"
#include "tree.h"
#include "uris.h"

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <memory>

#include <sys/stat.h>

using keymap::Command;

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

// A right click as the platform delivers it: the menu the widget puts up.
QMenu* rightClick(QWidget* owner, QWidget* target, const QPoint& at)
{
    QContextMenuEvent event(QContextMenuEvent::Mouse, at, target->mapToGlobal(at));
    QCoreApplication::sendEvent(target, &event);
    for (QMenu* menu : owner->findChildren<QMenu*>())
        if (menu->isVisible())
            return menu;
    return nullptr;
}

QList<int> commandsOf(const QMenu* menu)
{
    QList<int> out;
    for (const QAction* a : menu->actions())
        if (a->data().isValid())
            out << a->data().toInt();
    return out;
}

bool choose(QMenu* menu, Command command)
{
    for (QAction* a : menu->actions()) {
        if (a->data().isValid() && a->data().toInt() == int(command)) {
            a->trigger();
            menu->close();
            return true;
        }
    }
    return false;
}

} // namespace

class WindowTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void init();
    void cleanup();
    void treeKeysReachTheWindow();
    void dropOnATreeFolderCopies();
    void tabStripMenu();
    void treeMenu();
    void rightClickInTheOtherPane();
    void columnListFromTheKeyboard();
    void functionBarLayers();
    void propertiesGoDownAFolder();
    void promptOutlivesItsTab();

private:
    Pane* left() const { return window_->leftPane(); }
    Pane* right() const
    {
        for (Pane* p : window_->findChildren<Pane*>())
            if (p != left())
                return p;
        return nullptr;
    }
    Tree* tree() const { return window_->findChild<Tree*>(); }
    // The index of a folder in the tree, loading the way down to it.
    QModelIndex treeIndex(const QString& path);
    QLineEdit* promptEdit() const
    {
        const auto* bar = window_->findChild<PromptBar*>();
        return bar ? bar->findChild<QLineEdit*>() : nullptr;
    }

    QTemporaryDir tmp_;
    QString a_, b_;
    std::unique_ptr<Config> config_;
    std::unique_ptr<MainWindow> window_;
};

void WindowTest::init()
{
    // Per case: a/ (sub/, one.txt, two.txt) on the left, b/ (from-b.txt)
    // on the right, a config and a state of their own.
    QVERIFY(tmp_.isValid());
    const QString root = tmp_.path() + QLatin1Char('/') + QString::fromLatin1(QTest::currentTestFunction());
    a_ = root + "/a";
    b_ = root + "/b";
    QVERIFY(QDir().mkpath(a_ + "/sub"));
    QVERIFY(QDir().mkpath(b_));
    write(a_ + "/one.txt", "1");
    write(a_ + "/two.txt", "2");
    write(b_ + "/from-b.txt", "b");
    config_ = std::make_unique<Config>(root + "/superfar.ini");
    config_->setLastVersion(QStringLiteral(SUPERFAR_VERSION));  // no about note over the pane
    window_ = std::make_unique<MainWindow>(config_.get(), loc(a_), loc(b_));
    window_->resize(1200, 800);
    window_->show();
    QVERIFY(QTest::qWaitForWindowActive(window_.get()));
    QTRY_COMPARE(left()->activeTab()->entryNames().size(), 3);
    QTRY_COMPARE(right()->activeTab()->entryNames().size(), 1);
    QTRY_VERIFY(left()->activeTab()->view()->hasFocus());
}

void WindowTest::cleanup()
{
    window_.reset();
    config_.reset();
}

QModelIndex WindowTest::treeIndex(const QString& path)
{
    auto* model = qobject_cast<TreeModel*>(tree()->model());
    const core::Location target = loc(path);
    QModelIndex found;
    // The tree reveals the active tab's folder on its own; anything
    // below it loads when asked.
    [&] {
        QTRY_VERIFY(model->indexFor(loc(a_)).isValid());
        const QModelIndex a = model->indexFor(loc(a_));
        if (target == loc(a_)) {
            found = a;
            return;
        }
        model->load(a);
        QTRY_VERIFY(model->indexFor(target).isValid());
        found = model->indexFor(target);
    }();
    return found;
}

void WindowTest::treeKeysReachTheWindow()
{
    // Tab comes back from the tree to the pane.
    window_->execute(Command::FocusTree);
    QTRY_VERIFY(tree()->hasFocus());
    QTest::keyClick(tree(), Qt::Key_Tab);
    QTRY_VERIFY(left()->activeTab()->view()->hasFocus());

    // A function key pressed in the tree acts on the active tab: F7
    // asks for a folder name and makes it there.
    window_->execute(Command::FocusTree);
    QTRY_VERIFY(tree()->hasFocus());
    QTest::keyClick(tree(), Qt::Key_F7);
    QTRY_VERIFY(promptEdit());
    promptEdit()->setText(QStringLiteral("made-from-the-tree"));
    QTest::keyClick(promptEdit(), Qt::Key_Return);
    QTRY_VERIFY(QDir(a_ + "/made-from-the-tree").exists());

    // The tree's own keys stay its own: Down moves in the tree.
    const QModelIndex before = tree()->currentIndex();
    tree()->setFocus();
    QTest::keyClick(tree(), Qt::Key_Down);
    QVERIFY(tree()->currentIndex() != before);
}

void WindowTest::dropOnATreeFolderCopies()
{
    auto* model = qobject_cast<TreeModel*>(tree()->model());
    const QModelIndex sub = treeIndex(a_ + "/sub");
    QVERIFY(sub.isValid());
    const std::unique_ptr<QMimeData> files(uris::make({loc(b_ + "/from-b.txt")}, false));
    QVERIFY(model->canDropMimeData(files.get(), Qt::CopyAction, -1, -1, sub));
    QVERIFY(model->dropMimeData(files.get(), Qt::CopyAction, -1, -1, sub));
    // The drop is a copy job in the window, not a signal into nowhere.
    QTRY_VERIFY(QFile::exists(a_ + "/sub/from-b.txt"));
    QVERIFY(QFile::exists(b_ + "/from-b.txt"));

    // And a move takes the file with it.
    write(b_ + "/moved.txt", "m");
    const std::unique_ptr<QMimeData> more(uris::make({loc(b_ + "/moved.txt")}, false));
    QVERIFY(model->dropMimeData(more.get(), Qt::MoveAction, -1, -1, sub));
    QTRY_VERIFY(QFile::exists(a_ + "/sub/moved.txt"));
    QTRY_VERIFY(!QFile::exists(b_ + "/moved.txt"));
}

void WindowTest::tabStripMenu()
{
    // The right pane's strip while the left pane is the active one: the
    // commands are for the pane whose strip was clicked.
    auto* strip = right()->findChild<TabBar*>();
    QVERIFY(strip);
    QCOMPARE(right()->tabCount(), 1);
    QMenu* menu = rightClick(strip, strip, strip->tabRect(0).center());
    QVERIFY(menu);
    QList<int> expected;
    for (const menu::Entry& e : menu::contextEntries(menu::Context::Tabs))
        expected << int(e.command);
    QCOMPARE(commandsOf(menu), expected);
    QVERIFY(choose(menu, Command::NewTab));
    QCOMPARE(right()->tabCount(), 2);
    QCOMPARE(left()->tabCount(), 1);
    QCOMPARE(right()->activeIndex(), 1);

    // A right click on a tab makes it the current one; Lock locks it,
    // Close closes it.
    menu = rightClick(strip, strip, strip->tabRect(0).center());
    QVERIFY(menu);
    QCOMPARE(right()->activeIndex(), 0);
    QVERIFY(choose(menu, Command::LockTab));
    QVERIFY(right()->activeTab()->isLocked());
    menu = rightClick(strip, strip, strip->tabRect(1).center());
    QVERIFY(menu);
    QCOMPARE(right()->activeIndex(), 1);
    QVERIFY(choose(menu, Command::CloseTab));
    QCOMPARE(right()->tabCount(), 1);
    QVERIFY(right()->activeTab()->isLocked());  // the one that was left
}

void WindowTest::treeMenu()
{
    const QModelIndex sub = treeIndex(a_ + "/sub");
    QVERIFY(sub.isValid());
    tree()->expand(sub.parent());
    tree()->scrollTo(sub);
    QMenu* menu = rightClick(tree(), tree()->viewport(), tree()->visualRect(sub).center());
    QVERIFY(menu);
    // A plain folder: the tree's menu, nothing about unmounting.
    QList<int> expected;
    for (const menu::Entry& e : menu::contextEntries(menu::Context::Tree))
        if (e.command != Command::None)
            expected << int(e.command);
    QCOMPARE(commandsOf(menu), expected);
    QVERIFY(!commandsOf(menu).contains(int(Command::Trash)));
    // The click chose the folder, as a left click does: the active tab
    // went there, and "open in a new tab" opens a second one on it.
    QTRY_COMPARE(left()->activeTab()->location(), loc(a_ + "/sub"));
    QVERIFY(choose(menu, Command::NewTab));
    QCOMPARE(left()->tabCount(), 2);
    QTRY_COMPARE(left()->activeTab()->location(), loc(a_ + "/sub"));
}

void WindowTest::rightClickInTheOtherPane()
{
    // The press of a right click takes the focus, so the pane under the
    // pointer becomes the active one and its menu acts there: Copy
    // offers the other pane as the target.
    ListView* view = right()->activeTab()->view();
    const QPoint at = view->visualRect(view->model()->index(0, 0)).center();
    QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier, at);
    QTRY_VERIFY(right()->isActive());
    QMenu* menu = rightClick(view, view->viewport(), at);
    QVERIFY(menu);
    QCOMPARE(menu->actions().first()->text(), QStringLiteral("from-b.txt"));
    QVERIFY(choose(menu, Command::Copy));
    QTRY_VERIFY(promptEdit());
    QCOMPARE(promptEdit()->text(), loc(a_).displayPath());
    QTest::keyClick(promptEdit(), Qt::Key_Return);
    QTRY_VERIFY(QFile::exists(a_ + "/from-b.txt"));
}

void WindowTest::columnListFromTheKeyboard()
{
    using namespace columns;
    ListView* view = left()->activeTab()->view();
    window_->execute(Command::Columns);
    auto* bar = window_->findChild<CheckBar*>();
    QVERIFY(bar);
    auto* list = bar->findChild<QListWidget*>();
    QVERIFY(list);
    QCOMPARE(list->item(0)->text(), label(SizeColumn));
    QCOMPARE(list->item(list->count() - 1)->text(), QStringLiteral("Reset columns"));

    // Space hides the column under the cursor, in this pane only, and
    // the state file has it.
    QTest::keyClick(list, Qt::Key_Space);
    QVERIFY(view->isColumnHidden(SizeColumn));
    QVERIFY(!right()->activeTab()->view()->isColumnHidden(SizeColumn));
    QVERIFY(config_->columns(QStringLiteral("left")).hidden.contains(QStringLiteral("size")));
    QVERIFY(config_->columns(QStringLiteral("right")).order.isEmpty());

    // Shift+Down moves it one place right, and the cursor with it.
    QTest::keyClick(list, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(view->columnLayout().orderNames().mid(0, 3), QStringList({"name", "items", "size"}));
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(list->item(1)->text(), label(SizeColumn));
    QCOMPARE(config_->columns(QStringLiteral("left")).order.mid(0, 3), QStringList({"name", "items", "size"}));
    // Shift+Up at the top is a no-op, not a wrap.
    list->setCurrentRow(0);
    QTest::keyClick(list, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(view->columnLayout().orderNames().mid(0, 3), QStringList({"name", "items", "size"}));

    // A new tab in the pane takes the pane's columns.
    left()->execute(Command::NewTab);
    QVERIFY(left()->activeTab()->view()->isColumnHidden(SizeColumn));
    left()->execute(Command::CloseTab);

    // The last row puts everything back.
    list->setCurrentRow(list->count() - 1);
    QTest::keyClick(list, Qt::Key_Return);
    QVERIFY(!view->isColumnHidden(SizeColumn));
    QCOMPARE(view->columnLayout().orderNames().mid(0, 3), QStringList({"name", "size", "items"}));
    QVERIFY(config_->columns(QStringLiteral("left")).hidden.isEmpty());

    QTest::keyClick(list, Qt::Key_Escape);
    QTRY_VERIFY(!window_->findChild<CheckBar*>());
    QTRY_VERIFY(left()->activeTab()->view()->hasFocus());  // focus comes back to the list
}

void WindowTest::functionBarLayers()
{
    // What the bar reads with a modifier held is what the keys do.
    auto* bar = window_->findChild<FuncBar*>();
    QVERIFY(bar);
    const auto buttons = bar->findChildren<QToolButton*>();
    QCOMPARE(buttons.size(), 10);
    QWidget* focus = left()->activeTab()->view();
    auto row = [&] {
        QStringList out;
        for (const QToolButton* b : buttons)
            out << b->text();
        return out;
    };
    QCOMPARE(row().mid(0, 3), QStringList({"F1 Help", "F2 Rename", "F3 View"}));

    QTest::keyPress(focus, Qt::Key_Alt);
    QCOMPARE(row().value(0), QStringLiteral("F1 Go to"));
    QCOMPARE(row().value(1), QStringLiteral("F2 Properties"));
    QCOMPARE(row().value(5), QStringLiteral("F6 Bookmark"));
    QVERIFY(!buttons[2]->isEnabled());  // Alt+F3 is free
    QTest::keyRelease(focus, Qt::Key_Alt);

    QTest::keyPress(focus, Qt::Key_Control);
    QCOMPARE(row().mid(5, 3), QStringList({"F6 Columns", "F7 Hidden", "F8 Preview"}));
    QTest::keyRelease(focus, Qt::Key_Control);

    QTest::keyPress(focus, Qt::Key_Shift);
    QCOMPARE(row().value(2), QStringLiteral("F3 Open with"));
    QTest::keyRelease(focus, Qt::Key_Shift);
    QCOMPARE(row().value(1), QStringLiteral("F2 Rename"));

    // A click on the bar runs what it shows: F7 asks for a folder name.
    buttons[6]->click();
    QTRY_VERIFY(promptEdit());
    QTest::keyClick(promptEdit(), Qt::Key_Escape);
}

void WindowTest::propertiesGoDownAFolder()
{
    // Alt+F2 on a folder, one box changed, "the contents too" checked,
    // Apply: the job strip runs it and the tree below has the bit.
    auto mode = [&](const QString& path) {
        struct stat st {};
        return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? quint32(st.st_mode & 07777) : ~0u;
    };
    write(a_ + "/sub/inner.txt", "i");
    write(a_ + "/sub/run.sh", "#!/bin/sh\n");
    QCOMPARE(::chmod(QFile::encodeName(a_ + "/sub").constData(), 0755), 0);
    QCOMPARE(::chmod(QFile::encodeName(a_ + "/sub/inner.txt").constData(), 0644), 0);
    QCOMPARE(::chmod(QFile::encodeName(a_ + "/sub/run.sh").constData(), 0755), 0);
    QCOMPARE(left()->activeTab()->currentName(), QByteArray("sub"));  // folders first

    QTest::keyClick(left()->activeTab()->view(), Qt::Key_F2, Qt::AltModifier);
    auto* editor = window_->findChild<AttributesEditor*>();
    QVERIFY(editor);
    QList<QCheckBox*> bits;
    QCheckBox* contents = nullptr;
    for (QCheckBox* box : editor->findChildren<QCheckBox*>()) {
        if (box->text().isEmpty())
            bits.append(box);
        else
            contents = box;
    }
    QCOMPARE(bits.size(), 9);
    QVERIFY(contents);
    // The boxes show the folder once its details have come in.
    QTRY_COMPARE(bits[6]->checkState(), Qt::Checked);
    QVERIFY(contents->isVisible());

    bits[6]->click();  // others lose read
    contents->click();
    editor->findChildren<QPushButton*>().value(0)->click();  // Apply
    QTRY_COMPARE(mode(a_ + "/sub"), 0751u);
    QTRY_COMPARE(mode(a_ + "/sub/inner.txt"), 0640u);
    QTRY_COMPARE(mode(a_ + "/sub/run.sh"), 0751u);  // execute stays where it was
    QCOMPARE(mode(a_ + "/one.txt") & 0004u, 0004u);  // not below the folder

    // The panel reads the items again when the job ends, and Esc closes it.
    QTRY_COMPARE(bits[6]->checkState(), Qt::Unchecked);
    QTest::keyClick(editor, Qt::Key_Escape);
    QTRY_VERIFY(left()->activeTab()->view()->hasFocus());
}

void WindowTest::promptOutlivesItsTab()
{
    // F7 in a second tab, the tab closed while the prompt is open, then
    // Enter: nothing is made anywhere and nothing dangles (#50).
    left()->execute(Command::NewTab);
    QCOMPARE(left()->tabCount(), 2);
    QTRY_COMPARE(left()->activeTab()->entryNames().size(), 3);
    window_->execute(Command::Mkdir);
    QTRY_VERIFY(promptEdit());
    left()->execute(Command::CloseTab);
    QCOMPARE(left()->tabCount(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);  // the tab is really gone
    promptEdit()->setText(QStringLiteral("orphan"));
    QTest::keyClick(promptEdit(), Qt::Key_Return);
    QTest::qWait(200);
    QVERIFY(!QDir(a_ + "/orphan").exists());

    // A delete confirmed after its tab closed still deletes what was
    // asked about: the job has the items, it does not need the tab.
    left()->execute(Command::NewTab);
    QTRY_COMPARE(left()->activeTab()->entryNames().size(), 3);
    ListView* view = left()->activeTab()->view();
    view->setCurrentIndex(view->model()->index(1, 0));  // one.txt
    QCOMPARE(left()->activeTab()->currentName(), QByteArray("one.txt"));
    window_->execute(Command::DeletePermanent);
    QTRY_VERIFY(window_->findChild<PromptBar*>());
    PromptBar* bar = window_->findChild<PromptBar*>();
    left()->execute(Command::CloseTab);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    Q_EMIT bar->accepted(QString());
    QTRY_VERIFY(!QFile::exists(a_ + "/one.txt"));
    QVERIFY(QFile::exists(a_ + "/two.txt"));
}

QTEST_MAIN(WindowTest)
#include "test_window.moc"
