#include "devaids.h"

#include "bench.h"
#include "core/location.h"
#include "mainwindow.h"
#include "pane/pane.h"
#include "pane/tab.h"

#include <QApplication>
#include <QKeyEvent>
#include <QTimer>

void installDevAids(MainWindow* window)
{
    Pane* left = window->leftPane();
    // Bench aid: SUPERFAR_BENCH_THEN=<folder> navigates there once the
    // first folder has loaded, so memory after leaving a big folder can
    // be measured.
    if (const QByteArray then = qgetenv("SUPERFAR_BENCH_THEN"); !then.isEmpty()) {
        Tab* tab = left->activeTab();
        QObject::connect(tab, &Tab::loaded, window, [window, tab, then] {
            QObject::disconnect(tab, &Tab::loaded, window, nullptr);
            QTimer::singleShot(1000, window, [tab, then] {
                tab->navigate(core::Location::fromCommandLine(then));
            });
        });
    }

    // Dev aid: SUPERFAR_DEV_KEYS="Ctrl+A;F5;Return" posts those keys to the
    // focused widget, one every 400 ms, once the first folder has loaded.
    if (const QByteArray keys = qgetenv("SUPERFAR_DEV_KEYS"); !keys.isEmpty()) {
        Tab* tab = left->activeTab();
        QObject::connect(tab, &Tab::loaded, window, [window, keys] {
            static bool fired = false;
            if (fired)
                return;
            fired = true;
            const QStringList parts = QString::fromLatin1(keys).split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (int i = 0; i < parts.size(); ++i) {
                QTimer::singleShot(400 * (i + 1), window, [window, part = parts[i]] {
                    // The window may not be active under a compositor
                    // that keeps focus elsewhere; its own focus child is
                    // still what a key would reach.
                    QWidget* target = QApplication::focusWidget();
                    if (!target)
                        target = window->focusWidget();
                    if (!target)
                        return;
                    const QKeySequence seq(part);
                    const QKeyCombination combo = seq[0];
                    const QString text = combo.keyboardModifiers() == Qt::NoModifier && combo.key() < 0x100
                        ? QString(QChar(static_cast<int>(combo.key()))) : QString();
                    QApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, combo.key(), combo.keyboardModifiers(), text));
                    QApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, combo.key(), combo.keyboardModifiers(), text));
                });
            }
        });
    }

    // Dev aid: SUPERFAR_GRAB=<file.png> renders the window to a file and
    // quits, so the look can be checked without a screenshot tool.
    if (const QByteArray grabPath = qgetenv("SUPERFAR_GRAB"); !grabPath.isEmpty()) {
        const int delay = qEnvironmentVariableIntValue("SUPERFAR_GRAB_MS") > 0
            ? qEnvironmentVariableIntValue("SUPERFAR_GRAB_MS") : 1500;
        QTimer::singleShot(delay, window, [window, grabPath] {
            window->grab().save(QString::fromLocal8Bit(grabPath));
            qApp->quit();
        });
    }
}
