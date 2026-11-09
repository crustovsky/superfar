// App code with no widget behind it: the key table and the help must
// agree with each other, and the mark store keeps what a tab leaves in
// a folder.
#include "counted.h"
#include "archivetext.h"
#include "help.h"
#include "keymap.h"
#include "pane/columns.h"
#include "pane/markstore.h"

#include <QTest>

class AppTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void everyCommandIsComplete();
    void propertiesKeys();
    void functionKeyLayers();
    void countsOfOne();
    void keyShownInMenus();
    void oneCommandPerKey();
    void markStore();
    void archiveTree();
    void columnLayout();
};

void AppTest::everyCommandIsComplete()
{
    using keymap::Command;
    // label() is a switch without a default, so the compiler keeps it
    // complete; the name table and the key table are what can drift.
    int n = 0;
    for (int i = 1; !keymap::label(Command(i)).isEmpty(); ++i, ++n) {
        const Command c = Command(i);
        const QString name = keymap::name(c);
        QVERIFY2(!name.isEmpty(), qPrintable(QStringLiteral("command %1 (%2) has no config name").arg(i).arg(keymap::label(c))));
        QCOMPARE(keymap::fromName(name), c);
        QVERIFY2(!keymap::keysOf(c).isEmpty(), qPrintable(name + QStringLiteral(" has no default key")));
        QVERIFY2(help::text().contains(name.isEmpty() ? QString() : keymap::keysOf(c).isEmpty()
                                                       ? QStringLiteral("(unbound)")
                                                       : keymap::keysOf(c).first().toString(QKeySequence::NativeText)),
                 qPrintable(name + QStringLiteral(" is missing from the help")));
    }
    QCOMPARE(keymap::allCommands().size(), n);
    QCOMPARE(keymap::defaults().size(), n);
}

void AppTest::propertiesKeys()
{
    // On the function bar under Alt, and still where other managers have it.
    using keymap::Command;
    QCOMPARE(keymap::lookup(QKeySequence(Qt::ALT | Qt::Key_F2)), Command::Properties);
    QCOMPARE(keymap::lookup(QKeySequence(Qt::ALT | Qt::Key_Return)), Command::Properties);
    QCOMPARE(keymap::keysOf(Command::Properties).first(), QKeySequence(Qt::ALT | Qt::Key_F2));
    // The line a 0.7 first run wrote is retired, so such a config gets
    // the new key without anyone editing it.
    bool retired = false;
    for (const auto& [name, old] : keymap::retiredDefaults())
        retired = retired || (name == QLatin1String("Properties") && old == QLatin1String("Alt+Return;Alt+Enter"));
    QVERIFY(retired);
    for (const auto& [name, keys] : keymap::defaults())
        if (name == QLatin1String("Properties"))
            QCOMPARE(keys, QStringList({"Alt+F2", "Alt+Return", "Alt+Enter"}));
}

void AppTest::countsOfOne()
{
    // "1 items" was everywhere (#64).
    QCOMPARE(counted(0, "item", "items"), QStringLiteral("0 items"));
    QCOMPARE(counted(1, "item", "items"), QStringLiteral("1 item"));
    QCOMPARE(counted(2, "entry", "entries"), QStringLiteral("2 entries"));
}

void AppTest::functionKeyLayers()
{
    // What the bar shows with a modifier held, row by row. A dash is a
    // free key.
    using keymap::Command;
    const struct { Qt::KeyboardModifier mod; QStringList row; } layers[] = {
        {Qt::NoModifier, {"Help", "Rename", "View", "Edit", "Copy", "Move", "Mkdir", "Trash", "Menu", "Quit"}},
        {Qt::ShiftModifier, {"About", "MultiRename", "OpenWith", "NewFile", "Sync", "-", "-", "DeletePermanent", "-", "-"}},
        {Qt::ControlModifier, {"HideLeft", "HideRight", "ToggleSizes", "ToggleThumbnails", "ToggleTree", "Columns",
                               "ToggleHidden", "Preview", "-", "-"}},
        {Qt::AltModifier, {"GoTo", "Properties", "-", "-", "Pack", "Bookmark", "Find", "InvertMarks", "MarkGlob", "UnmarkGlob"}},
    };
    for (const auto& layer : layers) {
        QStringList row;
        for (int i = 0; i < 10; ++i) {
            const Command c = keymap::lookup(QKeySequence(QKeyCombination(layer.mod, Qt::Key(Qt::Key_F1 + i)).toCombined()));
            row << (c == Command::None ? QStringLiteral("-") : keymap::name(c));
        }
        QCOMPARE(row, layer.row);
    }
    // The function key comes first (the help names it), the old chord
    // stays, and a config still holding the old line gets both.
    const QList<QPair<Command, QString>> moved = {
        {Command::ToggleHidden, "Ctrl+H"}, {Command::Preview, "Ctrl+Q"}, {Command::OpenWith, "Shift+Return;Shift+Enter"},
        {Command::GoTo, "Ctrl+G"}, {Command::Bookmark, "Ctrl+D"}, {Command::InvertMarks, "*"},
        {Command::MarkGlob, "Ctrl+=;Ctrl++;Num++"}, {Command::UnmarkGlob, "Ctrl+-;Num+-"}};
    for (const auto& [command, old] : moved) {
        const auto keys = keymap::keysOf(command);
        QVERIFY2(keys.first().toString().contains(QLatin1String("+F")), qPrintable(keymap::name(command)));
        QCOMPARE(keys.mid(1).first().toString(QKeySequence::PortableText), old.section(QLatin1Char(';'), 0, 0));
        bool retired = false;
        for (const auto& [name, was] : keymap::retiredDefaults())
            retired = retired || (name == keymap::name(command) && was == old);
        QVERIFY2(retired, qPrintable(keymap::name(command)));
    }
}

void AppTest::keyShownInMenus()
{
    using keymap::Command;
    // A function key where there is one, Return for what Enter does,
    // the first key otherwise; always exactly one.
    QCOMPARE(keymap::shownKey(Command::Properties), QKeySequence(Qt::ALT | Qt::Key_F2));
    QCOMPARE(keymap::shownKey(Command::OpenWith), QKeySequence(Qt::SHIFT | Qt::Key_F3));
    QCOMPARE(keymap::shownKey(Command::Activate), QKeySequence(Qt::Key_Return));
    QCOMPARE(keymap::shownKey(Command::CopyClip), QKeySequence(Qt::CTRL | Qt::Key_C));
    QCOMPARE(keymap::shownKey(Command::Mark), QKeySequence(Qt::Key_Insert));
    for (Command c : keymap::allCommands()) {
        QVERIFY(!keymap::shownKey(c).isEmpty());
        QVERIFY(keymap::keysOf(c).contains(keymap::shownKey(c)));
    }
    QVERIFY(keymap::shownKey(Command::None).isEmpty());
}

void AppTest::oneCommandPerKey()
{
    using keymap::Command;
    for (Command c : keymap::allCommands())
        for (const QKeySequence& k : keymap::keysOf(c))
            QVERIFY2(keymap::lookup(k) == c,
                     qPrintable(k.toString() + QStringLiteral(" is bound to ") + keymap::name(keymap::lookup(k))
                                + QStringLiteral(" and ") + keymap::name(c)));
}

void AppTest::markStore()
{
    MarkStore store;
    QVERIFY(store.take("none").isEmpty());
    store.remember("a", {"x", "y"});
    store.remember("b", {"z"});
    QCOMPARE(store.size(), 2);
    QCOMPARE(store.take("a"), QSet<QByteArray>({"x", "y"}));
    QVERIFY(store.take("a").isEmpty());  // taken once
    store.remember("b", {});  // an empty set forgets
    QCOMPARE(store.size(), 0);
    for (int i = 0; i < MarkStore::kLimit + 3; ++i)
        store.remember(QByteArray::number(i), {"m"});
    QCOMPARE(store.size(), MarkStore::kLimit);
    QVERIFY(store.take("0").isEmpty());  // the oldest went first
    QVERIFY(!store.take(QByteArray::number(MarkStore::kLimit + 2)).isEmpty());
}

void AppTest::archiveTree()
{
    // Folders first at each level, implied folders listed once, the
    // tail cut with a count.
    const auto entries = core::parseArchiveListing(
        "-rw-r--r--  0 m m   3 Oct 30 12:14 zeta.txt\n"
        "-rw-r--r--  0 m m   4 Oct 30 12:14 src/sub/b.txt\n"
        "-rw-r--r--  0 m m   5 Oct 30 12:14 src/a.txt\n"
        "drwxr-xr-x  0 m m   0 Oct 30 12:14 src/\n"
        "-rw-r--r--  0 m m   6 Oct 30 12:14 alpha.txt\n", 2026);
    const QStringList lines = archivetext::tree(entries, 100).split(QLatin1Char('\n'));
    QStringList names;
    for (const QString& l : lines)
        names << l.mid(12);  // past the size column
    QCOMPARE(names, QStringList({"src/", "  sub/", "    b.txt", "  a.txt", "alpha.txt", "zeta.txt"}));
    QVERIFY(lines[2].trimmed().startsWith(QStringLiteral("4 ")));  // the size sits left of the name
    const QStringList cut = archivetext::tree(entries, 2).split(QLatin1Char('\n'));
    QCOMPARE(cut.size(), 3);
    QCOMPARE(cut.last(), QStringLiteral("and 4 more"));
}

void AppTest::columnLayout()
{
    using namespace columns;
    // Nothing saved: the table's order, everything shown by default.
    const Layout plain = Layout::from({}, {});
    QCOMPARE(plain.order.size(), int(ColumnCount));
    QCOMPARE(plain.order.first(), int(NameColumn));
    QCOMPARE(plain.orderNames().mid(0, 4), QStringList({"name", "size", "items", "modified"}));
    // Permissions and Owner exist, last and off until asked for.
    QCOMPARE(plain.orderNames().mid(4), QStringList({"permissions", "owner"}));
    QCOMPARE(plain.hiddenNames(), QStringList({"permissions", "owner"}));
    // Listing one in the order asks for it; hidden= still has the last word.
    QCOMPARE(Layout::from({"name", "owner", "size"}, {}).hiddenNames(), QStringList{"permissions"});
    QCOMPARE(Layout::from({"name", "owner", "size"}, {"owner"}).hiddenNames(), QStringList({"owner", "permissions"}));

    // A saved order is kept; Name comes first wherever it was written,
    // unknown names and repeats are dropped, the unlisted follow.
    const Layout moved = Layout::from({"modified", "bogus", "name", "modified", "size"}, {"items", "name", "nope"});
    QCOMPARE(moved.orderNames().mid(0, 4), QStringList({"name", "modified", "size", "items"}));
    QCOMPARE(moved.hiddenNames(), QStringList({"items", "permissions", "owner"}));  // Name cannot be hidden
    QVERIFY(!moved.hidden[NameColumn]);

    // Names and back: what the state stores reads as the same layout.
    const Layout again = Layout::from(moved.orderNames(), moved.hiddenNames());
    QCOMPARE(again.order, moved.order);
    QCOMPARE(again.hidden, moved.hidden);

    for (int c = 0; c < ColumnCount; ++c) {
        QCOMPARE(fromName(name(c)), c);
        QVERIFY(!label(c).isEmpty());
        QCOMPARE(widest(c).isEmpty(), c == NameColumn);
    }
    QCOMPARE(fromName(QStringLiteral("Name")), -1);
}

QTEST_GUILESS_MAIN(AppTest)
#include "test_app.moc"
