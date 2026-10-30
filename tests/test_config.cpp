// The config file: key bindings, session round trip, frecency ranking.
#include "config.h"

#include <QTemporaryDir>
#include <QTest>

class ConfigTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void sessionRoundTrip();
    void keysWrittenOnce();
    void historyRanking();
    void columns();
};

void ConfigTest::sessionRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + "/superfar.ini";
    {
        Config c(path);
        QVERIFY(!c.session().valid);
        Config::Session s;
        s.left = {{QStringLiteral("file:///usr"), false, true, false}, {QStringLiteral("file:///etc"), true, false, true}};
        s.right = {{QStringLiteral("file:///tmp"), false, false, false}};
        s.leftActive = 1;
        s.activePane = QStringLiteral("right");
        c.saveSession(s);
        c.setWindowSize(QSize(1200, 800));
        c.sync();
    }
    Config c(path);
    const Config::Session s = c.session();
    QVERIFY(s.valid);
    QCOMPARE(s.left.size(), 2);
    QCOMPARE(s.left[0].uri, QStringLiteral("file:///usr"));
    QVERIFY(s.left[0].locked);
    QVERIFY(s.left[1].hidden);
    QVERIFY(s.left[1].sizes);
    QCOMPARE(s.right.size(), 1);
    QCOMPARE(s.leftActive, 1);
    QCOMPARE(s.activePane, QStringLiteral("right"));
    QCOMPARE(c.windowSize(), QSize(1200, 800));
}

void ConfigTest::keysWrittenOnce()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + "/superfar.ini";
    Config c(path);
    c.writeDefaultKeys({{QStringLiteral("Copy"), {QStringLiteral("F5")}},
                        {QStringLiteral("GoUp"), {QStringLiteral("Backspace"), QStringLiteral("Left")}}});
    c.sync();
    auto keys = c.keyBindings();
    QCOMPARE(keys.value(QStringLiteral("GoUp")), QStringList({QStringLiteral("Backspace"), QStringLiteral("Left")}));

    // The user edits one; defaults are not rewritten over it.
    QSettings edit(path, QSettings::IniFormat);
    edit.setValue(QStringLiteral("keys/Copy"), QStringLiteral("Ctrl+F5"));
    edit.setValue(QStringLiteral("keys/Move"), QString());
    edit.sync();
    Config again(path);
    again.writeDefaultKeys({{QStringLiteral("Copy"), {QStringLiteral("F5")}}});
    keys = again.keyBindings();
    QCOMPARE(keys.value(QStringLiteral("Copy")), QStringList({QStringLiteral("Ctrl+F5")}));
    QVERIFY(keys.contains(QStringLiteral("Move")));
    QVERIFY(keys.value(QStringLiteral("Move")).isEmpty());  // unbound
}

void ConfigTest::historyRanking()
{
    const qint64 now = 1'000'000'000;
    QList<Config::Visit> visits = {
        {QStringLiteral("file:///home/me/projects"), 3, now - 3600},         // recent, 3 visits
        {QStringLiteral("file:///home/me/Downloads"), 20, now - 30 * 86400},  // old, many visits
        {QStringLiteral("file:///usr/share/doc"), 1, now - 60},
        {QStringLiteral("file:///var/log"), 50, now - 2 * 86400},
    };
    QVERIFY(Config::frecency(visits[0], now) > Config::frecency(visits[1], now));

    auto all = Config::rank(visits, QString(), now);
    QCOMPARE(all.first().uri, QStringLiteral("file:///var/log"));  // 50 * 50

    // Prefix match beats a better frecency that only matches as a subsequence.
    auto pro = Config::rank(visits, QStringLiteral("file:///home/me/pro"), now);
    QCOMPARE(pro.first().uri, QStringLiteral("file:///home/me/projects"));
    auto dl = Config::rank(visits, QStringLiteral("downl"), now);
    QCOMPARE(dl.size(), 1);
    auto none = Config::rank(visits, QStringLiteral("zzz"), now);
    QVERIFY(none.isEmpty());

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Config c(tmp.path() + "/superfar.ini");
    c.recordVisit(QStringLiteral("file:///home/me/a b"), now);
    c.recordVisit(QStringLiteral("file:///home/me/a b"), now + 1);
    c.recordVisit(QStringLiteral("file:///etc"), now);
    const auto h = c.history();
    QCOMPARE(h.size(), 2);
    for (const auto& v : h)
        if (v.uri == QStringLiteral("file:///home/me/a b"))
            QCOMPARE(v.count, 2);
}

void ConfigTest::columns()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Config c(tmp.path() + "/superfar.ini");
    c.setColumnWidth(QStringLiteral("left"), QStringLiteral("size"), 120);
    QSettings edit(tmp.path() + "/superfar.ini", QSettings::IniFormat);
    c.sync();
    edit.setValue(QStringLiteral("columns/right/hidden"), QStringLiteral("items,modified"));
    edit.sync();
    Config again(tmp.path() + "/superfar.ini");
    QCOMPARE(again.columns(QStringLiteral("left")).widths.value(QStringLiteral("size")), 120);
    QCOMPARE(again.columns(QStringLiteral("right")).hidden, QStringList({QStringLiteral("items"), QStringLiteral("modified")}));
}

QTEST_GUILESS_MAIN(ConfigTest)
#include "test_config.moc"
