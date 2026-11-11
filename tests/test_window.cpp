// The whole window over temp folders, offscreen. These are the things
// that only work when the parts are wired together, and that broke
// unnoticed before: the tree's keys and drops reaching the window, the
// tab strip's and the tree's right-click menus, the column list driven
// from the keyboard, the function bar's layers.
#include "attributesedit.h"
#include "config.h"
#include "funcbar.h"
#include "highlight.h"
#include "jobs/bars.h"
#include "jobs/strip.h"
#include "mainwindow.h"
#include "menu.h"
#include "openwith.h"
#include "pane/columns.h"
#include "pane/listview.h"
#include "pane/pane.h"
#include "pane/tabbar.h"
#include "preview.h"
#include "tree.h"
#include "uris.h"
#include "viewer.h"

#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>
#include <QToolButton>
#include <memory>

#include <sys/stat.h>
#include <unistd.h>

using keymap::Command;

namespace {

// Apps, defaults and PATH of the test's own (set in main, before GLib
// reads them): a stand-in viewer registered for text, which writes down
// its arguments.
QString g_env;

QStringList viewerCall()
{
    QFile f(g_env + "/viewer-call");
    return f.open(QIODevice::ReadOnly)
        ? QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts) : QStringList();
}

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
    void cursorStaysWhereItsItemWas();
    void openWith();
    void undoAndRedo();
    void warningsSayWhatCannotBeUndone();
    void queueBehindARunningJob();
    void syntaxColoursFromBat();
    void permissionDeniedOffersAdministrator();

private:
    // Shift+F8 on the left tab's selection, confirmed.
    void deleteSelection();
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

    // The button of that text among w's, a shown one first; null when none.
    template <typename Button>
    static Button* buttonOf(const QWidget* w, const QString& text)
    {
        Button* hidden = nullptr;
        for (Button* b : w->findChildren<Button*>()) {
            if (b->text() == text && b->isVisible())
                return b;
            if (b->text() == text)
                hidden = b;
        }
        return hidden;
    }
    // The prompt that is up now: closed ones linger until they are deleted.
    PromptBar* openPrompt() const
    {
        for (PromptBar* bar : window_->findChildren<PromptBar*>())
            if (bar->isVisible())
                return bar;
        return nullptr;
    }
    // The row of the one job that waits in the queue.
    JobRow* queuedRow() const
    {
        for (JobRow* row : window_->findChildren<JobRow*>())
            if (row->isVisible() && !row->job()->hasStarted() && !row->job()->done())
                return row;
        return nullptr;
    }

    // The colour a highlighter gave the first character of a text
    // view; invalid while it is plain.
    static QColor firstColour(const QPlainTextEdit* edit)
    {
        const auto formats = edit->document()->firstBlock().layout()->formats();
        return formats.isEmpty() ? QColor() : formats.first().format.foreground().color();
    }

    // Every job has ended and been taken note of, and the keys are the list's again.
    void settle()
    {
        QTRY_VERIFY(window_->findChildren<core::Job*>().isEmpty());
        QTRY_VERIFY(left()->activeTab()->view()->hasFocus());
    }
    // Whether some label under w reads text (the status line, a bar).
    static bool shows(const QWidget* w, const QString& text)
    {
        for (const QLabel* label : w->findChildren<QLabel*>())
            if (label->isVisible() && label->text().contains(text))
                return true;
        return false;
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

void WindowTest::deleteSelection()
{
    window_->execute(Command::DeletePermanent);
    QTRY_VERIFY(window_->findChild<PromptBar*>());
    Q_EMIT window_->findChild<PromptBar*>()->accepted(QString());
}

void WindowTest::cursorStaysWhereItsItemWas()
{
    // Deleting the item under the cursor leaves the cursor on the next
    // one, not on the first row (#54).
    for (int i = 0; i < 8; ++i)
        write(a_ + QStringLiteral("/f%1.txt").arg(i), "x");
    Tab* tab = left()->activeTab();
    QTRY_COMPARE(tab->entryNames().size(), 11);  // sub, f0..f7, one, two
    ListView* view = tab->view();
    auto putCursorOn = [&](const QByteArray& name) {
        for (int row = 0; row < view->model()->rowCount(); ++row) {
            view->setCurrentIndex(view->model()->index(row, 0));
            if (tab->currentName() == name)
                return;
        }
        QFAIL("no such row");
    };
    putCursorOn("f3.txt");
    deleteSelection();
    QTRY_VERIFY(!QFile::exists(a_ + "/f3.txt"));
    QTRY_COMPARE(tab->entryNames().size(), 10);
    QCOMPARE(tab->currentName(), QByteArray("f4.txt"));

    // A marked block with the cursor inside it: the first item after
    // the block.
    putCursorOn("f4.txt");
    QTest::keyClick(view, Qt::Key_Insert);
    QTest::keyClick(view, Qt::Key_Insert);
    QTest::keyClick(view, Qt::Key_Insert);  // f4, f5, f6 marked, cursor on f7
    putCursorOn("f5.txt");
    QCOMPARE(tab->markedCount(), 3);
    deleteSelection();
    QTRY_COMPARE(tab->entryNames().size(), 7);
    QCOMPARE(tab->currentName(), QByteArray("f7.txt"));

    // The last row: the one before it.
    putCursorOn("two.txt");
    deleteSelection();
    QTRY_COMPARE(tab->entryNames().size(), 6);
    QCOMPARE(tab->currentName(), QByteArray("one.txt"));

    // An item removed by something else: the same row, not the top.
    putCursorOn("f1.txt");
    QVERIFY(QFile::remove(a_ + "/f1.txt"));
    QTRY_COMPARE(tab->entryNames().size(), 5);
    QCOMPARE(tab->currentName(), QByteArray("f2.txt"));
}

void WindowTest::openWith()
{
    Tab* tab = left()->activeTab();
    ListView* view = tab->view();
    view->setCurrentIndex(view->model()->index(1, 0));
    QCOMPARE(tab->currentName(), QByteArray("one.txt"));
    auto bar = [&]() -> OpenWithBar* {
        window_->execute(Command::OpenWith);
        OpenWithBar* found = nullptr;
        [&] {
            QTRY_VERIFY(window_->findChild<OpenWithBar*>());
            found = window_->findChild<OpenWithBar*>();
            QTRY_VERIFY(found->findChild<QLineEdit*>()->hasFocus());
        }();
        return found;
    };
    // Typing narrows the list to the one app, Enter opens with it (#57).
    OpenWithBar* b = bar();
    QVERIFY(b);
    auto* list = b->findChild<QListWidget*>();
    QVERIFY(list->count() >= 1);
    QTest::keyClicks(b->findChild<QLineEdit*>(), QStringLiteral("zzq"));
    QCOMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().startsWith(QStringLiteral("Zzq Stub View")));
    QTest::keyClick(b->findChild<QLineEdit*>(), Qt::Key_Return);
    QTRY_VERIFY(viewerCall().contains(QStringLiteral("END")));
    QCOMPARE(viewerCall(), QStringList({a_ + "/one.txt", "END"}));
    QFile::remove(g_env + "/viewer-call");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_VERIFY(!window_->findChild<OpenWithBar*>());
    QTRY_VERIFY(view->hasFocus());

    // Marked items all open, and text that names no app is a command
    // run on them.
    QTest::keyClick(view, Qt::Key_Insert);
    QTest::keyClick(view, Qt::Key_Insert);
    QCOMPARE(tab->markedCount(), 2);
    b = bar();
    QVERIFY(b);
    bool titled = false;
    for (const QLabel* label : b->findChildren<QLabel*>())
        titled = titled || label->text() == QLatin1String("Open 2 items with");
    QVERIFY(titled);
    QTest::keyClicks(b->findChild<QLineEdit*>(), QStringLiteral("zzqview --typed"));
    QCOMPARE(b->findChild<QListWidget*>()->count(), 0);
    QTest::keyClick(b->findChild<QLineEdit*>(), Qt::Key_Return);
    QTRY_VERIFY(viewerCall().contains(QStringLiteral("END")));
    QCOMPARE(viewerCall(), QStringList({"--typed", a_ + "/one.txt", a_ + "/two.txt", "END"}));
    QFile::remove(g_env + "/viewer-call");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(tab->markedCount(), 2);  // opening spends nothing

    // A folder gets the bar too; Esc closes it.
    tab->clearMarks();
    view->setCurrentIndex(view->model()->index(0, 0));
    QVERIFY(tab->cursorIsDir());
    b = bar();
    QVERIFY(b);
    QTest::keyClick(b->findChild<QLineEdit*>(), Qt::Key_Escape);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_VERIFY(!window_->findChild<OpenWithBar*>());
}

void WindowTest::undoAndRedo()
{
    ListView* view = left()->activeTab()->view();
    view->setCurrentIndex(view->model()->index(1, 0));
    QCOMPARE(left()->activeTab()->currentName(), QByteArray("one.txt"));
    QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
    QTRY_VERIFY(shows(left(), QStringLiteral("Nothing to undo")));

    // F5 and Enter copy it across; Ctrl+Z takes the copy back (to the
    // trash), Ctrl+Shift+Z puts it there again.
    QTest::keyClick(view, Qt::Key_F5);
    QTRY_VERIFY(promptEdit());
    QTest::keyClick(promptEdit(), Qt::Key_Return);
    QTRY_VERIFY(QFile::exists(b_ + "/one.txt"));
    settle();
    QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
    QTRY_VERIFY(!QFile::exists(b_ + "/one.txt"));
    QTRY_VERIFY(shows(left(), QStringLiteral("Undid: copy of one.txt")));
    QVERIFY(QFile::exists(a_ + "/one.txt"));
    settle();
    QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(QFile::exists(b_ + "/one.txt"));
    QTRY_VERIFY(shows(left(), QStringLiteral("Redid: copy of one.txt")));

    // F8 and Enter trash it; Ctrl+Z brings it back from the trash.
    settle();
    QTRY_COMPARE(left()->activeTab()->currentName(), QByteArray("one.txt"));
    QTest::keyClick(view, Qt::Key_F8);
    QTRY_VERIFY(window_->findChild<PromptBar*>());
    QVERIFY(shows(window_.get(), QStringLiteral("Move one.txt to the trash?")));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QTRY_VERIFY(!QFile::exists(a_ + "/one.txt"));
    settle();
    QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
    QTRY_VERIFY(QFile::exists(a_ + "/one.txt"));
    QTRY_VERIFY(shows(left(), QStringLiteral("Undid: trash of one.txt")));
    QTRY_VERIFY(left()->activeTab()->entryNames().contains(QStringLiteral("one.txt")));

    // F7 makes a folder, F2 would rename: both are on the stack too.
    // The last operation goes first.
    window_->execute(Command::Mkdir);
    QTRY_VERIFY(promptEdit());
    promptEdit()->setText(QStringLiteral("made"));
    QTest::keyClick(promptEdit(), Qt::Key_Return);
    QTRY_VERIFY(left()->activeTab()->entryNames().contains(QStringLiteral("made")));  // made, and noted
    window_->execute(Command::Undo);
    QTRY_VERIFY(!QDir(a_ + "/made").exists());
    QTRY_VERIFY(shows(left(), QStringLiteral("Undid: new folder made")));
    // The menu has both, with their keys.
    bool undo = false, redo = false;
    for (const QAction* a : window_->findChildren<QAction*>()) {
        undo = undo || a->text().startsWith(QStringLiteral("&Undo"));
        redo = redo || a->text().startsWith(QStringLiteral("Redo"));
    }
    QVERIFY(undo && redo);
}

void WindowTest::warningsSayWhatCannotBeUndone()
{
    // Shift+F8 says so before it deletes.
    ListView* view = left()->activeTab()->view();
    view->setCurrentIndex(view->model()->index(1, 0));
    window_->execute(Command::DeletePermanent);
    QTRY_VERIFY(window_->findChild<PromptBar*>());
    QVERIFY(shows(window_.get(), QStringLiteral("Delete one.txt permanently? This cannot be undone.")));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QVERIFY(QFile::exists(a_ + "/one.txt"));

    // So does the choice offered when the trash refuses an item, and
    // the conflict question where what is replaced cannot be kept.
    core::Failure failure;
    failure.offerDelete = true;
    ErrorBar error(QStringLiteral("trash"), failure);
    bool said = false;
    for (const QPushButton* b : error.findChildren<QPushButton*>())
        said = said || b->text() == QLatin1String("Delete &permanently (cannot be undone)");
    QVERIFY(said);
    core::Conflict conflict;
    ConflictBar plain(conflict);
    conflict.cannotUndo = true;
    ConflictBar warned(conflict);
    plain.show();
    warned.show();
    QVERIFY(!shows(&plain, QStringLiteral("cannot be undone")));
    QVERIFY(shows(&warned, QStringLiteral("overwriting cannot be undone")));
}

void WindowTest::queueBehindARunningJob()
{
    Tab* tab = left()->activeTab();
    ListView* view = tab->view();
    auto startCopy = [&](int row) {
        view->setCurrentIndex(view->model()->index(row, 0));
        window_->execute(Command::Copy);
        QTRY_VERIFY(openPrompt());
    };
    // Nothing runs: the copy prompt has its one button.
    write(b_ + "/two.txt", "there already");
    startCopy(2);
    QCOMPARE(tab->currentName(), QByteArray("two.txt"));
    QVERIFY(!buttonOf<QPushButton>(openPrompt(), QStringLiteral("&Queue")));
    // The copy asks about two.txt and waits for an answer: a running job.
    QTest::keyClick(openPrompt()->findChild<QLineEdit*>(), Qt::Key_Return);
    QTRY_VERIFY(window_->findChild<ConflictBar*>());

    // Now the prompt offers Queue. The job gets a row that says what
    // it will do, with Start now, Unattended and Abort, and waits.
    startCopy(1);
    QVERIFY(buttonOf<QPushButton>(openPrompt(), QStringLiteral("&Queue")));
    buttonOf<QPushButton>(openPrompt(), QStringLiteral("&Queue"))->click();
    QTRY_VERIFY(queuedRow());
    JobRow* row = queuedRow();
    QVERIFY(shows(row, QStringLiteral("Queued: one.txt from %1 to %2").arg(a_, b_)));
    for (const char* text : {"Start now", "Unattended", "Abort"})
        QVERIFY2(buttonOf<QToolButton>(row, QString::fromLatin1(text))->isVisible(), text);
    QVERIFY(!buttonOf<QToolButton>(row, QStringLiteral("Pause"))->isVisible());
    QTest::qWait(100);
    QVERIFY(!QFile::exists(b_ + "/one.txt"));

    // Abort takes it out of the queue: it never runs.
    buttonOf<QToolButton>(row, QStringLiteral("Abort"))->click();
    QTRY_VERIFY(!queuedRow());
    QVERIFY(!QFile::exists(b_ + "/one.txt"));

    // Shift+Enter queues from the keyboard; Unattended can be set
    // ahead. When the job before it ends, it runs.
    startCopy(1);
    QTest::keyClick(openPrompt()->findChild<QLineEdit*>(), Qt::Key_Return, Qt::ShiftModifier);
    QTRY_VERIFY(queuedRow());
    row = queuedRow();
    buttonOf<QToolButton>(row, QStringLiteral("Unattended"))->click();
    QVERIFY(row->job()->unattended());
    // Quitting now would drop both: the window asks first.
    window_->close();
    QVERIFY(window_->isVisible());
    QTRY_VERIFY(openPrompt());
    QVERIFY(shows(openPrompt(), QStringLiteral("2 jobs are running or queued. Abort and quit?")));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QVERIFY(window_->isVisible());
    QVERIFY(!QFile::exists(b_ + "/one.txt"));
    buttonOf<QPushButton>(window_->findChild<ConflictBar*>(), QStringLiteral("&Skip"))->click();
    QTRY_VERIFY(QFile::exists(b_ + "/one.txt"));
    // The first job's log (two.txt was skipped) is up; Esc closes it.
    QTRY_VERIFY(window_->findChild<LogBar*>());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    settle();

    // Start now: the queued job runs beside the one it waited for.
    QFile::remove(b_ + "/one.txt");
    startCopy(2);
    QVERIFY(!buttonOf<QPushButton>(openPrompt(), QStringLiteral("&Queue")));  // the queue is empty again
    QTest::keyClick(openPrompt()->findChild<QLineEdit*>(), Qt::Key_Return);
    QTRY_VERIFY(window_->findChild<ConflictBar*>() && window_->findChild<ConflictBar*>()->isVisible());
    startCopy(1);
    buttonOf<QPushButton>(openPrompt(), QStringLiteral("&Queue"))->click();
    QTRY_VERIFY(queuedRow());
    buttonOf<QToolButton>(queuedRow(), QStringLiteral("Start now"))->click();
    QTRY_VERIFY(QFile::exists(b_ + "/one.txt"));
    QVERIFY(window_->findChild<ConflictBar*>()->isVisible());  // the first still waits

    // The trash prompt offers it too; confirmed, the window closes
    // with jobs still there.
    window_->execute(Command::Trash);
    QTRY_VERIFY(openPrompt());
    QVERIFY(buttonOf<QPushButton>(openPrompt(), QStringLiteral("&Queue")));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    window_->close();
    QTRY_VERIFY(openPrompt());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QTRY_VERIFY(!window_->isVisible());
}

void WindowTest::syntaxColoursFromBat()
{
    // The bat on PATH is the test's own: it writes down its arguments
    // and colours whatever it is fed in one colour.
    write(a_ + "/code.c", "int main() { return 0; }\r\n// end\n");
    Tab* tab = left()->activeTab();
    QTRY_VERIFY(tab->entryNames().contains(QStringLiteral("code.c")));
    ListView* view = tab->view();
    view->setCurrentIndex(view->model()->index(tab->entryNames().indexOf(QStringLiteral("code.c")), 0));
    QCOMPARE(tab->currentName(), QByteArray("code.c"));
    auto batCall = [] {
        QFile f(g_env + "/bat-call");
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)
                                           : QStringList();
    };

    // F3: the text is there at once, the colours when bat answers.
    QTest::keyClick(view, Qt::Key_F3);
    QTRY_VERIFY(left()->findChild<Viewer*>());
    auto* text = left()->findChild<Viewer*>()->findChild<QPlainTextEdit*>();
    QTRY_COMPARE(text->toPlainText(), QStringLiteral("int main() { return 0; }\n// end\n"));
    QTRY_COMPARE(firstColour(text), QColor(1, 2, 3));
    QVERIFY(batCall().contains(QStringLiteral("--file-name=code.c")));
    QVERIFY(batCall().contains(text->palette().color(QPalette::Base).lightness() < 128 ? QStringLiteral("--theme=dark")
                                                                                         : QStringLiteral("--theme=light")));
    QVERIFY(batCall().contains(QStringLiteral("--style=plain")));
    QCOMPARE(batCall().last(), QStringLiteral("-"));  // the text comes on stdin: bat opens nothing

    // Ctrl+F9 in the viewer switches them off, and on again; the
    // choice is kept in the state file.
    QTest::keyClick(text, Qt::Key_F9, Qt::ControlModifier);
    QTRY_VERIFY(!firstColour(text).isValid());
    QVERIFY(!Highlight::enabled(config_.get()));
    QCOMPARE(text->toPlainText(), QStringLiteral("int main() { return 0; }\n// end\n"));
    QTest::keyClick(text, Qt::Key_F9, Qt::ControlModifier);
    QTRY_COMPARE(firstColour(text), QColor(1, 2, 3));
    QVERIFY(Highlight::enabled(config_.get()));
    // The theme changes while the file is open: bat is asked again,
    // for the other kind of theme.
    const QPalette before = QApplication::palette();
    const bool wasDark = before.color(QPalette::Base).lightness() < 128;
    QPalette other = before;
    other.setColor(QPalette::Base, wasDark ? Qt::white : Qt::black);
    other.setColor(QPalette::Text, wasDark ? Qt::black : Qt::white);
    QFile::remove(g_env + "/bat-call");
    QApplication::setPalette(other);
    QTRY_VERIFY(batCall().contains(wasDark ? QStringLiteral("--theme=light") : QStringLiteral("--theme=dark")));
    QFile::remove(g_env + "/bat-call");
    QApplication::setPalette(before);
    QTRY_VERIFY(batCall().contains(wasDark ? QStringLiteral("--theme=dark") : QStringLiteral("--theme=light")));
    QTRY_COMPARE(firstColour(text), QColor(1, 2, 3));
    QTest::keyClick(text, Qt::Key_Escape);
    QTRY_VERIFY(view->hasFocus());

    // The preview's text gets them too.
    window_->execute(Command::Preview);
    QTRY_VERIFY(window_->findChild<Preview*>());
    auto* shown = window_->findChild<Preview*>()->findChild<QPlainTextEdit*>();
    QTRY_COMPARE(firstColour(shown), QColor(1, 2, 3));
    window_->execute(Command::Preview);

    // A bat that complains (one too old for --theme=dark does) is not
    // believed, and with none on PATH nothing happens either: the text
    // is shown plain, as it was before there were colours.
    write(g_env + "/bat-warns", "");
    QTest::keyClick(view, Qt::Key_F3);
    QTRY_COMPARE(text->toPlainText(), QStringLiteral("int main() { return 0; }\n// end\n"));
    QTest::qWait(300);
    QVERIFY(!firstColour(text).isValid());
    QTest::keyClick(text, Qt::Key_Escape);
    QFile::remove(g_env + "/bat-warns");
    const QByteArray path = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(g_env + "/data"));
    QTest::keyClick(view, Qt::Key_F3);
    QTRY_COMPARE(text->toPlainText(), QStringLiteral("int main() { return 0; }\n// end\n"));
    QTest::qWait(300);
    QVERIFY(!firstColour(text).isValid());
    qputenv("PATH", path);
}

void WindowTest::permissionDeniedOffersAdministrator()
{
    if (::geteuid() == 0)
        QSKIP("root is never denied");
    // The offers are looked at and turned down, never taken: taking
    // one would ask the polkit agent of whoever runs the tests.
    QVERIFY(QDir().mkpath(a_ + "/locked"));
    QVERIFY(QDir().mkpath(b_ + "/ro"));
    QCOMPARE(::chmod(QFile::encodeName(a_ + "/locked").constData(), 0), 0);
    QCOMPARE(::chmod(QFile::encodeName(b_ + "/ro").constData(), 0555), 0);
    struct Unlock {
        QString a, b;
        ~Unlock()
        {
            ::chmod(QFile::encodeName(a).constData(), 0755);
            ::chmod(QFile::encodeName(b).constData(), 0755);
        }
    } unlock{a_ + "/locked", b_ + "/ro"};

    // A folder that cannot be read: a bar in the window, and the tab
    // stays where it is when the offer is turned down.
    Tab* tab = left()->activeTab();
    tab->navigate(loc(a_ + "/locked"));
    QTRY_VERIFY(openPrompt());
    QVERIFY(shows(openPrompt(), QStringLiteral("Permission denied: %1. Open as administrator?").arg(a_ + "/locked")));
    QVERIFY(buttonOf<QPushButton>(openPrompt(), QStringLiteral("As &administrator")));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QTRY_VERIFY(!openPrompt());
    QCOMPARE(tab->location(), loc(a_ + "/locked"));
    QVERIFY(shows(left(), QStringLiteral("denied")) || shows(left(), QStringLiteral("Permission")));
    tab->navigate(loc(a_));
    QTRY_COMPARE(tab->entryNames().size(), 4);

    // A job item that is denied has one more choice on its error bar.
    right()->activeTab()->navigate(loc(b_ + "/ro"));
    QTRY_COMPARE(right()->activeTab()->location(), loc(b_ + "/ro"));
    ListView* view = tab->view();
    view->setFocus();
    view->setCurrentIndex(view->model()->index(tab->entryNames().indexOf(QStringLiteral("one.txt")), 0));
    QCOMPARE(tab->currentName(), QByteArray("one.txt"));
    window_->execute(Command::Copy);
    QTRY_VERIFY(openPrompt());
    QTest::keyClick(openPrompt()->findChild<QLineEdit*>(), Qt::Key_Return);
    QTRY_VERIFY(window_->findChild<ErrorBar*>());
    auto* error = window_->findChild<ErrorBar*>();
    QVERIFY(buttonOf<QPushButton>(error, QStringLiteral("As a&dministrator")));
    buttonOf<QPushButton>(error, QStringLiteral("&Skip"))->click();
    QTRY_VERIFY(window_->findChild<LogBar*>());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    settle();
    QVERIFY(!QFile::exists(b_ + "/ro/one.txt"));

    // A new folder where it may not be made: the same question, and
    // nothing made when it is turned down.
    right()->activeTab()->view()->setFocus();
    QTRY_VERIFY(right()->isActive());
    window_->execute(Command::Mkdir);
    QTRY_VERIFY(promptEdit() && openPrompt());
    openPrompt()->findChild<QLineEdit*>()->setText(QStringLiteral("made"));
    QTest::keyClick(openPrompt()->findChild<QLineEdit*>(), Qt::Key_Return);
    QTRY_VERIFY(openPrompt() && shows(openPrompt(), QStringLiteral("Create it as administrator? It will belong to root.")));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QTRY_VERIFY(!openPrompt());
    QVERIFY(!QDir(b_ + "/ro/made").exists());
}

int main(int argc, char** argv)
{
    QTemporaryDir env;
    g_env = env.path();
    QDir().mkpath(g_env + "/bin");
    QDir().mkpath(g_env + "/data/applications");
    {
        QFile script(g_env + "/bin/zzqview");
        if (!script.open(QIODevice::WriteOnly))
            return 1;
        script.write("#!/bin/sh\n{ printf '%s\\n' \"$@\"; echo END; } >> '" + QFile::encodeName(g_env) + "/viewer-call'\n");
        script.close();
        script.setPermissions(script.permissions() | QFile::ExeOwner);
        // A stand-in for bat: its arguments written down, its input
        // back in one colour; with bat-warns there, a warning as well.
        QFile bat(g_env + "/bin/bat");
        if (!bat.open(QIODevice::WriteOnly))
            return 1;
        bat.write("#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + QFile::encodeName(g_env) + "/bat-call'\n"
                  "[ -e '" + QFile::encodeName(g_env) + "/bat-warns' ] && echo '[bat warning]: Unknown theme' >&2\n"
                  "printf '\\033[38;2;1;2;3m'; cat; printf '\\033[0m'\n");
        bat.close();
        bat.setPermissions(bat.permissions() | QFile::ExeOwner);
        QFile entry(g_env + "/data/applications/zzqview.desktop");
        if (!entry.open(QIODevice::WriteOnly))
            return 1;
        entry.write("[Desktop Entry]\nType=Application\nName=Zzq Stub View\nExec=zzqview %F\nMimeType=text/plain;\n");
        QFile cache(g_env + "/data/applications/mimeinfo.cache");
        if (!cache.open(QIODevice::WriteOnly))
            return 1;
        cache.write("[MIME Cache]\ntext/plain=zzqview.desktop;\n");
    }
    // The home trash serves the home's filesystem: with the home here,
    // beside the test folders, F8 has a trash wherever the temp dir is.
    qputenv("HOME", QFile::encodeName(g_env));
    qputenv("XDG_DATA_HOME", QFile::encodeName(g_env + "/data"));
    // GVfs's admin backend counts as installed wherever the tests run.
    QDir().mkpath(g_env + "/sys/gvfs/mounts");
    if (QFile backend(g_env + "/sys/gvfs/mounts/admin.mount"); !backend.open(QIODevice::WriteOnly))
        return 1;
    qputenv("XDG_DATA_DIRS", QFile::encodeName(g_env + "/sys") + ":/usr/local/share:/usr/share");
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(g_env + "/config"));
    qputenv("PATH", QFile::encodeName(g_env + "/bin") + ':' + qgetenv("PATH"));
    QApplication app(argc, argv);
    QCoreApplication::setAttribute(Qt::AA_Use96Dpi, true);
    WindowTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "test_window.moc"
