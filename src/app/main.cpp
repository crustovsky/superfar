#include "bench.h"
#include "core/location.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>

int main(int argc, char** argv)
{
    bench::init();
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("superfar"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QGuiApplication::setDesktopFileName(QStringLiteral("superfar"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Dual-pane file manager"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("folder"), QStringLiteral("Folder to open"));
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

    const QStringList args = parser.positionalArguments();
    core::Location start = args.isEmpty() ? core::Location::home()
                                          : core::Location::fromCommandLine(QFile::encodeName(args.first()));
    MainWindow window(start, core::Location::home());
    window.show();
    return app.exec();
}
