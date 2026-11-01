// The config file: key bindings, session round trip, frecency ranking.
#include "config.h"
#include "core/match.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {
QByteArray slurp(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

class ConfigTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void sessionRoundTrip();
    void keysWrittenOnce();
    void historyRanking();
    void columns();
    void stateSeparate();
    void abbreviationMatch();
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
        s.left = {{QStringLiteral("file:///usr"), false, true, false, false, {}},
                  {QStringLiteral("file:///etc"), true, false, true, false, {}}};
        s.right = {{QStringLiteral("file:///tmp"), false, false, false, false, {}}};
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
    again.writeDefaultKeys({{QStringLiteral("Copy"), {QStringLiteral("F5")}}, {QStringLiteral("MarkUp"), {QStringLiteral("Shift+Up")}}});
    keys = again.keyBindings();
    QCOMPARE(keys.value(QStringLiteral("Copy")), QStringList({QStringLiteral("Ctrl+F5")}));
    QCOMPARE(keys.value(QStringLiteral("MarkUp")), QStringList({QStringLiteral("Shift+Up")}));  // new command, new line
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
    auto abbr = Config::rank(visits, QStringLiteral("h/me/d"), now);  // word starts
    QCOMPARE(abbr.size(), 1);
    QCOMPARE(abbr.first().uri, QStringLiteral("file:///home/me/Downloads"));
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

void ConfigTest::stateSeparate()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString ini = tmp.path() + "/superfar.ini";
    const QString state = tmp.path() + "/state.ini";
    {
        // A pre-0.6 config with the session and history inside.
        QSettings old(ini, QSettings::IniFormat);
        old.setValue(QStringLiteral("keys/Copy"), QStringLiteral("F5"));
        old.setValue(QStringLiteral("session/leftActive"), 1);
        old.setValue(QStringLiteral("session/left/size"), 1);
        old.setValue(QStringLiteral("session/left/1/uri"), QStringLiteral("file:///usr"));
        old.setValue(QStringLiteral("session/right/size"), 1);
        old.setValue(QStringLiteral("session/right/1/uri"), QStringLiteral("file:///etc"));
        old.setValue(QStringLiteral("history/file%3A%2F%2F%2Fetc"), QStringLiteral("3,100"));
        old.setValue(QStringLiteral("window/size"), QSize(800, 600));
        old.setValue(QStringLiteral("layout/split"), QStringLiteral("stacked"));
        old.setValue(QStringLiteral("layout/tree"), QStringLiteral("200,800"));
        old.setValue(QStringLiteral("columns/left/size"), 90);
        old.setValue(QStringLiteral("columns/left/hidden"), QStringLiteral("items"));
    }
    {
        Config c(ini);
        QCOMPARE(c.statePath(), state);
        const Config::Session s = c.session();
        QVERIFY(s.valid);
        QCOMPARE(s.leftActive, 1);
        QCOMPARE(s.left[0].uri, QStringLiteral("file:///usr"));
        QCOMPARE(c.history().size(), 1);
        QCOMPARE(c.windowSize(), QSize(800, 600));
        QVERIFY(c.stackedSplit());
        QCOMPARE(c.splitterSizes(QStringLiteral("tree")), QList<int>({200, 800}));
        QCOMPARE(c.columns(QStringLiteral("left")).widths.value(QStringLiteral("size")), 90);
        QCOMPARE(c.columns(QStringLiteral("left")).hidden, QStringList{QStringLiteral("items")});
    }
    // The config keeps what the user edits and nothing else.
    QSettings after(ini, QSettings::IniFormat);
    QCOMPARE(after.allKeys(), QStringList({QStringLiteral("columns/left/hidden"), QStringLiteral("keys/Copy"),
                                           QStringLiteral("layout/split")}));
    QSettings st(state, QSettings::IniFormat);
    QVERIFY(st.contains(QStringLiteral("session/leftActive")));
    QVERIFY(st.contains(QStringLiteral("columns/left/size")));

    // Later sessions, visits and drags leave the config alone.
    const QByteArray before = slurp(ini);
    {
        Config c(ini);
        Config::Session s = c.session();
        s.left[0].cursor = QStringLiteral("bin");
        c.saveSession(s);
        c.recordVisit(QStringLiteral("file:///tmp"), 200);
        c.setColumnWidth(QStringLiteral("left"), QStringLiteral("size"), 100);
        c.setWindowSize(QSize(1000, 700));
        c.setLastVersion(QStringLiteral("0.6.0"));
        c.sync();
        QCOMPARE(c.lastVersion(), QStringLiteral("0.6.0"));
        QCOMPARE(c.session().left[0].cursor, QStringLiteral("bin"));
    }
    QCOMPARE(slurp(ini), before);
}

void ConfigTest::abbreviationMatch()
{
    QCOMPARE(core::matchRank(QStringLiteral("bandcamp-download"), QStringLiteral("band")), 0);
    QCOMPARE(core::matchRank(QStringLiteral("bandcamp-download"), QStringLiteral("down")), 1);
    QCOMPARE(core::matchRank(QStringLiteral("bandcamp-download"), QStringLiteral("bd")), 2);
    QCOMPARE(core::matchRank(QStringLiteral("bandcamp-download"), QStringLiteral("bdow")), 2);
    QCOMPARE(core::matchRank(QStringLiteral("bandcamp-download"), QStringLiteral("pl")), -1);
    QCOMPARE(core::matchRank(QStringLiteral("bandcamp-download"), QStringLiteral("dl")), -1);
    QCOMPARE(core::matchRank(QStringLiteral("img_2024.jpg"), QStringLiteral("i2j")), 2);
    QCOMPARE(core::matchRank(QStringLiteral("notes.txt"), QStringLiteral("nt")), 2);
    QCOMPARE(core::matchRank(QStringLiteral("notes.txt"), QStringLiteral("nx")), -1);
    QCOMPARE(core::matchRank(QStringLiteral("bandcamp-download"), QStringLiteral("b-d")), 2);
    QCOMPARE(core::matchRank(QStringLiteral("notes.txt"), QStringLiteral("s.t")), 1);
}

QTEST_GUILESS_MAIN(ConfigTest)
#include "test_config.moc"
