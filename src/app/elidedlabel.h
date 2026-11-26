// A label that elides (from the left by default, so the end of a path
// stays visible) and never forces its parent wider. ElidedLabel takes
// the room it is given (put it where a layout stretches it);
// FittedLabel below asks for the width of its text and gives way when
// there is less.
#pragma once

#include <QLabel>
#include <QPainter>

class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget* parent = nullptr, Qt::TextElideMode mode = Qt::ElideLeft)
        : QLabel(parent)
        , mode_(mode)
    {
    }
    QSize minimumSizeHint() const override { return {0, QLabel::minimumSizeHint().height()}; }
    QSize sizeHint() const override { return {0, QLabel::sizeHint().height()}; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const QRect r = contentsRect();
        p.setPen(palette().color(foregroundRole()));
        // Line by line: each is cut on its own, none wraps.
        QString shown;
        for (const QString& line : text().split(QLatin1Char('\n')))
            shown += (shown.isEmpty() ? QString() : QStringLiteral("\n")) + fontMetrics().elidedText(line, mode_, r.width());
        p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, shown);
    }

private:
    Qt::TextElideMode mode_;
};

// For a message that names an item or a folder: as wide as its text
// when there is room, cut in the middle when there is not, so both the
// start of the sentence and the end of the path stay.
class FittedLabel : public ElidedLabel {
public:
    explicit FittedLabel(const QString& text, QWidget* parent = nullptr, Qt::TextElideMode mode = Qt::ElideMiddle)
        : ElidedLabel(parent, mode)
    {
        setText(text);
    }
    QSize sizeHint() const override { return QLabel::sizeHint(); }
};
