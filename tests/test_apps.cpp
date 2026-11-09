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

private:
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
    write(files_ + "/it's.txt", "x");
    write(files_ + "/b.txt", "x");
    qputenv("XDG_DATA_HOME", QFile::encodeName(root + "/data"));
    qputenv("XDG_DATA_DIRS", QFile::encodeName(root + "/none"));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(root + "/config"));
    qputenv("XDG_CONFIG_DIRS", QFile::encodeName(root + "/none"));
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

QTEST_GUILESS_MAIN(AppsTest)
#include "test_apps.moc"
