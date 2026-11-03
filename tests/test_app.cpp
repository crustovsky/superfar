// App code with no widget behind it: the key table and the help must
// agree with each other, and the mark store keeps what a tab leaves in
// a folder.
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
        // About is reached from the menu and the first start of a version.
        if (c != Command::About)
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

QTEST_GUILESS_MAIN(AppTest)
#include "test_app.moc"
