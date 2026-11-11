// The F3 viewer: a read-only text view that loads the file one window
// at a time through core::Reader, so size is no concern (rule 8). A
// window that holds NUL bytes is shown as a hex dump. Esc closes.
// Text that fits the first window whole gets syntax colours from bat
// (highlight.h); larger files and hex dumps stay plain.
#pragma once

#include "core/location.h"
#include "core/reader.h"
#include "keymap.h"

#include <QWidget>

class Config;
class Highlight;
class QLabel;
class QPlainTextEdit;

class Viewer : public QWidget {
    Q_OBJECT
public:
    explicit Viewer(Config* config, QWidget* parent = nullptr);

    void open(const core::Location& file);
    // Shows text that is not a file, such as the help.
    void showText(const QString& title, const QString& text);
    void clear();
    // The colours again: the switch was flipped, or the theme changed.
    void rehighlight();

Q_SIGNALS:
    void closed();
    void commandRequested(keymap::Command command);  // a key that is the window's

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void loadMore();
    void onData(quint64 offset, const QByteArray& bytes);
    void updateStatus();
    static QString hexDump(quint64 offset, const QByteArray& bytes);

    core::Reader* reader_;
    QLabel* title_;
    QPlainTextEdit* text_;
    QLabel* status_;
    Highlight* colours_;
    QByteArray whole_;  // the file, when it fits the first window as text
    QByteArray name_;
    quint64 size_ = 0;
    quint64 loaded_ = 0;
    bool hex_ = false;
    bool loading_ = false;
};
