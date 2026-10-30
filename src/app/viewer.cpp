#include "viewer.h"

#include "elidedlabel.h"
#include "jobs/format.h"

#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {
constexpr quint32 kWindow = 1024 * 1024;
}

Viewer::Viewer(QWidget* parent)
    : QWidget(parent)
    , reader_(new core::Reader(this))
    , title_(new ElidedLabel(this))
    , text_(new QPlainTextEdit(this))
    , status_(new QLabel(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(title_);
    layout->addWidget(text_, 1);
    layout->addWidget(status_);
    title_->setContentsMargins(4, 2, 4, 2);
    status_->setContentsMargins(4, 2, 4, 2);
    text_->setReadOnly(true);
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    text_->setUndoRedoEnabled(false);
    text_->installEventFilter(this);
    setFocusProxy(text_);

    connect(reader_, &core::Reader::opened, this, [this](quint64 size) {
        size_ = size;
        loadMore();
    });
    connect(reader_, &core::Reader::data, this, &Viewer::onData);
    connect(reader_, &core::Reader::failed, this, [this](const QString& m) { status_->setText(m); });
    connect(text_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value >= text_->verticalScrollBar()->maximum() - 2)
            loadMore();
    });
}

void Viewer::open(const core::Location& file)
{
    clear();
    title_->setText(file.displayPath());
    reader_->open(file);
}

void Viewer::showText(const QString& title, const QString& text)
{
    clear();
    title_->setText(title);
    text_->setPlainText(text);
    status_->setText(tr("Esc closes"));
}

void Viewer::clear()
{
    reader_->close();
    text_->clear();
    size_ = loaded_ = 0;
    hex_ = loading_ = false;
    status_->clear();
}

void Viewer::loadMore()
{
    if (loading_ || !reader_->isOpen() || loaded_ >= size_)
        return;
    loading_ = true;
    reader_->read(loaded_, kWindow);
}

void Viewer::onData(quint64 offset, const QByteArray& bytes)
{
    loading_ = false;
    if (offset != loaded_)
        return;
    if (loaded_ == 0)
        hex_ = bytes.contains('\0');
    loaded_ += static_cast<quint64>(bytes.size());
    const QString chunk = hex_ ? hexDump(offset, bytes) : QString::fromUtf8(bytes);
    QTextCursor cursor(text_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(chunk);
    updateStatus();
    if (bytes.isEmpty())
        loaded_ = size_;  // short read: treat as the end
}

void Viewer::updateStatus()
{
    status_->setText(loaded_ >= size_ ? tr("%1, %2").arg(jobfmt::bytes(size_), hex_ ? tr("hex") : tr("text"))
                                      : tr("%1 of %2 loaded, scroll for more")
                                            .arg(jobfmt::bytes(loaded_), jobfmt::bytes(size_)));
}

QString Viewer::hexDump(quint64 offset, const QByteArray& bytes)
{
    QString out;
    out.reserve(bytes.size() * 4);
    for (qsizetype at = 0; at < bytes.size(); at += 16) {
        out += QStringLiteral("%1  ").arg(offset + static_cast<quint64>(at), 8, 16, QLatin1Char('0'));
        QString ascii;
        for (qsizetype i = 0; i < 16; ++i) {
            if (at + i < bytes.size()) {
                const auto b = static_cast<unsigned char>(bytes.at(at + i));
                out += QStringLiteral("%1 ").arg(b, 2, 16, QLatin1Char('0'));
                ascii += b >= 32 && b < 127 ? QChar(b) : QLatin1Char('.');
            } else {
                out += QStringLiteral("   ");
            }
            if (i == 7)
                out += QLatin1Char(' ');
        }
        out += QStringLiteral(" |%1|\n").arg(ascii);
    }
    return out;
}

void Viewer::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_F3 || event->key() == Qt::Key_F1) {
        Q_EMIT closed();
        return;
    }
    QWidget::keyPressEvent(event);
}
