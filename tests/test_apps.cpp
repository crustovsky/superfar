// Launching apps, against a data dir, a config dir and a PATH of the
// test's own: the desktop entries, the terminal and the defaults it
// sees are the ones written here, and nothing of the user's is touched.
// A program of its own because GLib reads those directories once.
#include "core/apps.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

void write(const QString& path, const QByteArray& content, bool executable = false)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
    f.close();
    if (executable)
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
}

core::Location loc(const QString& path)
{
    return core::Location::fromPath(QFile::encodeName(path));
}

} // namespace

class AppsTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void execLine();
    void terminalAppRunsInOurTerminal();
    void oneTerminalPerFile();
    void noTerminal();
    void severalFilesAndTheDefault();
    void typedCommand();

private:
    // The apps for a file, once the answer is in.
    std::vector<core::AppChoice> appsFor(const QString& path, QString* type)
    {
        bool done = false;
        std::vector<core::AppChoice> out;
        core::appsFor(loc(path), [&](const QString& t, std::vector<core::AppChoice> apps) {
            *type = t;
            out = std::move(apps);
            done = true;
        });
        [&] { QTRY_VERIFY(done); }();
        return out;
    }
    QStringList viewerCall()
    {
        QFile f(viewOut_);
        return f.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts) : QStringList();
    }
    QString viewOut_;
    // What the stand-in terminal was started with: its folder, then one
    // argument per line. Empty until it has run.
    QStringList terminalCall()
    {
        QFile f(out_);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        return lines.value(lines.size() - 1) == QLatin1String("END") ? lines.mid(0, lines.size() - 1) : QStringList();
    }

    QTemporaryDir tmp_;
    QString out_;
    QString files_;
};

void AppsTest::initTestCase()
{
    QVERIFY(tmp_.isValid());
    const QString root = tmp_.path();
    out_ = root + "/terminal-call";
    files_ = root + "/some files";
    // A terminal that writes down how it was called, and nothing else on
    // PATH: no xdg-terminal-exec, no real terminal.
    write(root + "/bin/stubterm",
          "#!/bin/sh\n{ pwd; printf '%s\\n' \"$@\"; echo END; } >> '" + QFile::encodeName(out_) + "'\n", true);
    write(root + "/bin/stubedit", "#!/bin/sh\n", true);
    write(root + "/data/applications/stubedit.desktop",
          "[Desktop Entry]\nType=Application\nName=Stub Edit\nExec=stubedit --flag %f\nTerminal=true\n"
          "MimeType=text/x-superfar-test;\n");
    write(root + "/data/applications/stubmany.desktop",
          "[Desktop Entry]\nType=Application\nName=Stub Many\nExec=stubedit %F\nTerminal=true\n");
    // A windowed app, and both registered for whatever type a file gets
    // here (there is no MIME database in this data dir).
    viewOut_ = root + "/viewer-call";
    write(root + "/bin/stubview",
          "#!/bin/sh\n{ pwd; printf '%s\\n' \"$@\"; echo END; } >> '" + QFile::encodeName(viewOut_) + "'\n", true);
    write(root + "/data/applications/stubview.desktop",
          "[Desktop Entry]\nType=Application\nName=Stub View\nExec=stubview %F\n"
          "MimeType=text/plain;application/octet-stream;\n");
    write(root + "/data/applications/stubother.desktop",
          "[Desktop Entry]\nType=Application\nName=Stub Other\nExec=stubview --other %F\n"
          "MimeType=text/plain;application/octet-stream;\n");
    // GLib finds apps by type through this cache, never by reading the
    // entries; update-desktop-database writes it on a real system.
    write(root + "/data/applications/mimeinfo.cache",
          "[MIME Cache]\ntext/plain=stubview.desktop;stubother.desktop;\n"
          "application/octet-stream=stubview.desktop;stubother.desktop;\n");
    write(files_ + "/it's.txt", "x");
    write(files_ + "/b.txt", "x");
    qputenv("XDG_DATA_HOME", QFile::encodeName(root + "/data"));
    qputenv("XDG_DATA_DIRS", QFile::encodeName(root + "/none"));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(root + "/config"));
    qputenv("XDG_CONFIG_DIRS", QFile::encodeName(root + "/none"));
    // sh itself has to be found: the stand-ins are scripts.
    QVERIFY(QFile::link(QStringLiteral("/bin/sh"), root + "/bin/sh"));
    qputenv("PATH", QFile::encodeName(root + "/bin"));
    qputenv("TERMINAL", "stubterm");
}

void AppsTest::execLine()
{
    const std::vector<core::Location> two = {loc("/tmp/a b"), loc("/tmp/c")};
    QCOMPARE(core::expandExec("vi %f", two), QByteArray("vi '/tmp/a b'"));
    QCOMPARE(core::expandExec("vi %F", two), QByteArray("vi '/tmp/a b' '/tmp/c'"));
    QCOMPARE(core::expandExec("get %u", two), QByteArray("get 'file:///tmp/a%20b'"));
    QCOMPARE(core::expandExec("get %U", two), QByteArray("get 'file:///tmp/a%20b' 'file:///tmp/c'"));
    // No place for files: they go at the end. Icon, name and the entry's
    // own path are for launchers; a literal percent stays one.
    QCOMPARE(core::expandExec("top", two), QByteArray("top '/tmp/a b' '/tmp/c'"));
    QCOMPARE(core::expandExec("top", {}), QByteArray("top"));
    QCOMPARE(core::expandExec("app %i %c --pct=5%% %f", two), QByteArray("app   --pct=5% '/tmp/a b'"));
    // What has no path goes as its URI.
    QCOMPARE(core::expandExec("vi %f", {core::Location::fromUri(QStringLiteral("sftp://host/x"))}),
             QByteArray("vi 'sftp://host/x'"));
}

void AppsTest::terminalAppRunsInOurTerminal()
{
    // Terminal=true: GLib would look for a terminal on a list of its own
    // and fail here ("Unable to find terminal required for application").
    // It runs in $TERMINAL, in the file's folder (#56).
    QString error;
    QVERIFY2(core::launchWith(QStringLiteral("stubedit.desktop"), {loc(files_ + "/it's.txt")}, &error), qPrintable(error));
    QTRY_VERIFY(!terminalCall().isEmpty());
    QCOMPARE(terminalCall(), QStringList({files_, "-e", "sh", "-c",
                                          "stubedit --flag '" + files_ + "/it'\\''s.txt'"}));
    QFile::remove(out_);
}

void AppsTest::oneTerminalPerFile()
{
    const std::vector<core::Location> two = {loc(files_ + "/b.txt"), loc(files_ + "/it's.txt")};
    QString error;
    QVERIFY2(core::launchWith(QStringLiteral("stubmany.desktop"), two, &error), qPrintable(error));
    QTRY_VERIFY(!terminalCall().isEmpty());
    QCOMPARE(terminalCall().last(), "stubedit '" + files_ + "/b.txt' '" + files_ + "/it'\\''s.txt'");
    QFile::remove(out_);
    // %f takes one file: a terminal each.
    QVERIFY2(core::launchWith(QStringLiteral("stubedit.desktop"), two, &error), qPrintable(error));
    QTRY_COMPARE(terminalCall().count(QStringLiteral("-e")), 2);
    QFile::remove(out_);
}

void AppsTest::noTerminal()
{
    qputenv("TERMINAL", "");
    QString error;
    QVERIFY(!core::launchWith(QStringLiteral("stubedit.desktop"), {loc(files_ + "/b.txt")}, &error));
    QCOMPARE(error, QStringLiteral("No terminal found; set $TERMINAL"));
    qputenv("TERMINAL", "stubterm");
}

void AppsTest::severalFilesAndTheDefault()
{
    const QString a = files_ + "/b.txt", b = files_ + "/it's.txt";
    QString type;
    std::vector<core::AppChoice> apps = appsFor(a, &type);
    QVERIFY(!type.isEmpty());
    QVERIFY2(apps.size() == 2, qPrintable(type));
    // Both files go to one instance of an app that takes many, and the
    // app becomes the default when asked (#57).
    QString error;
    QVERIFY2(core::launchWith(QStringLiteral("stubother.desktop"), {loc(a), loc(b)}, &error, {type, true}),
             qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QTRY_VERIFY(viewerCall().contains(QStringLiteral("END")));
    QCOMPARE(viewerCall().mid(1), QStringList({"--other", a, b, "END"}));
    QFile::remove(viewOut_);
    apps = appsFor(a, &type);
    QCOMPARE(apps.front().id, QStringLiteral("stubother.desktop"));
    QVERIFY(apps.front().isDefault);
    QVERIFY(!apps.back().isDefault);
    QFile list(tmp_.path() + "/config/mimeapps.list");
    QVERIFY(list.open(QIODevice::ReadOnly));
    QVERIFY(list.readAll().contains("stubother.desktop"));
    // The other one, plainly: it is the default no more than before, but
    // it was used last, so it leads the rest.
    QVERIFY(core::launchWith(QStringLiteral("stubview.desktop"), {loc(a)}, &error, {type, false}));
    QTRY_VERIFY(viewerCall().contains(QStringLiteral("END")));
    QFile::remove(viewOut_);
    apps = appsFor(a, &type);
    QCOMPARE(apps.size(), size_t(2));
    QCOMPARE(apps[0].id, QStringLiteral("stubother.desktop"));
    QVERIFY(apps[0].isDefault);
    QCOMPARE(apps[1].id, QStringLiteral("stubview.desktop"));
}

void AppsTest::typedCommand()
{
    const QString a = files_ + "/b.txt", b = files_ + "/it's.txt";
    QString error;
    QVERIFY2(core::launchCommand(QStringLiteral("stubview --typed 50%"), {loc(a), loc(b)}, false, &error),
             qPrintable(error));
    QTRY_VERIFY(viewerCall().contains(QStringLiteral("END")));
    QCOMPARE(viewerCall(), QStringList({files_, "--typed", "50%", a, b, "END"}));
    QFile::remove(viewOut_);
    // In a terminal when asked.
    QVERIFY2(core::launchCommand(QStringLiteral("stubview"), {loc(a)}, true, &error), qPrintable(error));
    QTRY_VERIFY(!terminalCall().isEmpty());
    QCOMPARE(terminalCall().last(), "stubview '" + a + "'");
    QFile::remove(out_);
    // What is not there is said, not started.
    QVERIFY(!core::launchCommand(QStringLiteral("no-such-program-zzqx -v"), {loc(a)}, false, &error));
    QCOMPARE(error, QStringLiteral("no-such-program-zzqx is not on PATH"));
}

QTEST_GUILESS_MAIN(AppsTest)
#include "test_apps.moc"
