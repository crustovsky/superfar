// A label that elides (from the left by default, so the end of a path
// stays visible) and never forces its parent wider.
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
        p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text(), mode_, r.width()));
    }

private:
    Qt::TextElideMode mode_;
};
