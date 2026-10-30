// The F3 viewer: a read-only text view that loads the file one window
// at a time through core::Reader, so size is no concern (rule 8). A
// window that holds NUL bytes is shown as a hex dump. Esc closes.
#pragma once

#include "core/location.h"
#include "core/reader.h"

#include <QWidget>

class QLabel;
class QPlainTextEdit;

class Viewer : public QWidget {
    Q_OBJECT
public:
    explicit Viewer(QWidget* parent = nullptr);

    void open(const core::Location& file);
    void clear();

Q_SIGNALS:
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void loadMore();
    void onData(quint64 offset, const QByteArray& bytes);
    void updateStatus();
    static QString hexDump(quint64 offset, const QByteArray& bytes);

    core::Reader* reader_;
    QLabel* title_;
    QPlainTextEdit* text_;
    QLabel* status_;
    quint64 size_ = 0;
    quint64 loaded_ = 0;
    bool hex_ = false;
    bool loading_ = false;
};
