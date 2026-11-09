// Open with (Shift+F3): a filter line over the apps for a content type.
// Typing narrows the list, Enter opens with the highlighted app and
// Ctrl+Enter makes it the default as well. Text that matches no app is
// a command: Enter runs it, Ctrl+Enter runs it in a terminal.
#pragma once

#include "core/apps.h"

#include <QFrame>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;

class OpenWithBar : public QFrame {
    Q_OBJECT
public:
    OpenWithBar(const QString& title, std::vector<core::AppChoice> apps, QWidget* parent = nullptr);

Q_SIGNALS:
    void appChosen(const core::AppChoice& app, bool makeDefault);
    void commandTyped(const QString& command, bool inTerminal);
    void rejected();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refill(const QString& text);
    void accept(bool ctrl);

    std::vector<core::AppChoice> apps_;
    QLineEdit* edit_;
    QListWidget* list_;
    QLabel* hint_;
};
