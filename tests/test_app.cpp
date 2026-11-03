// App code with no widget behind it: the key table and the help must
// agree with each other, and the mark store keeps what a tab leaves in
// a folder.
#include "archivetext.h"
#include "help.h"
#include "keymap.h"
#include "pane/markstore.h"

#include <QTest>

class AppTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void everyCommandIsComplete();
    void oneCommandPerKey();
    void markStore();
    void archiveTree();
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

QTEST_GUILESS_MAIN(AppTest)
#include "test_app.moc"
