#include "bench.h"
#include "config.h"
#include "core/location.h"
#include "fm1.h"
#include "keymap.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QLoggingCategory>
#include <cstdio>

int main(int argc, char** argv)
{
    bench::init();
    // Qt's text-input-v3 client logs "Trying to disable ... but 0x0 is
    // focused" on every focus change under wlroots compositors; it is
    // Qt's own bookkeeping, nothing the app can act on.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.wayland.textinput.warning=false"));
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("superfar"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QGuiApplication::setDesktopFileName(QStringLiteral("superfar"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Dual-pane file manager"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("left"), QStringLiteral("Folder for the left pane"));
    parser.addPositionalArgument(QStringLiteral("right"), QStringLiteral("Folder for the right pane"), QStringLiteral("[right]"));
    QCommandLineOption baseline(QStringLiteral("baseline"),
                                QStringLiteral("Open an empty window, for tools/bench.sh"));
    parser.addOption(baseline);
    parser.process(app);

    if (parser.isSet(baseline)) {
        QMainWindow empty;
        empty.setWindowTitle(QStringLiteral("superfar"));
        empty.resize(1100, 700);
        empty.show();
        return app.exec();
    }

    Config config;
    config.writeDefaultKeys(keymap::defaults());
    for (const QString& problem : keymap::applyBindings(config.keyBindings()))
        fprintf(stderr, "superfar: config [keys]: %s\n", qPrintable(problem));

    const QStringList args = parser.positionalArguments();
    auto at = [&](int i) {
        return args.size() > i ? core::Location::fromCommandLine(QFile::encodeName(args[i])) : core::Location();
    };
    MainWindow window(&config, at(0), at(1));
    FileManager1::publish(&window);  // another instance may own the name; that is fine
    window.show();
    return app.exec();
}
