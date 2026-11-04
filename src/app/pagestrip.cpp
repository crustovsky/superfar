#include "pagestrip.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QToolButton>

PageStrip::PageStrip(QWidget* parent)
    : QWidget(parent)
    , label_(new QLabel(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 2);
    layout->setSpacing(2);
    first_ = button("go-first", QStyle::SP_MediaSkipBackward, tr("First page"));
    prev_ = button("go-previous", QStyle::SP_MediaSeekBackward, tr("Previous page"));
    next_ = button("go-next", QStyle::SP_MediaSeekForward, tr("Next page"));
    last_ = button("go-last", QStyle::SP_MediaSkipForward, tr("Last page"));
    layout->addStretch(1);
    layout->addWidget(first_);
    layout->addWidget(prev_);
    layout->addWidget(label_);
    layout->addWidget(next_);
    layout->addWidget(last_);
    layout->addStretch(1);
    label_->setContentsMargins(6, 0, 6, 0);
    setFocusPolicy(Qt::NoFocus);
    connect(first_, &QToolButton::clicked, this, [this] { Q_EMIT pageRequested(1); });
    connect(prev_, &QToolButton::clicked, this, [this] { Q_EMIT pageRequested(page_ - 1); });
    connect(next_, &QToolButton::clicked, this, [this] { Q_EMIT pageRequested(page_ + 1); });
    connect(last_, &QToolButton::clicked, this, [this] { Q_EMIT pageRequested(count_); });
    hide();
}

QToolButton* PageStrip::button(const char* themeName, int fallback, const QString& tip)
{
    auto* b = new QToolButton(this);
    b->setIcon(QIcon::fromTheme(QLatin1String(themeName), style()->standardIcon(static_cast<QStyle::StandardPixmap>(fallback))));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

void PageStrip::setPages(int page, int count)
{
    page_ = page;
    count_ = count;
    if (count < 2) {
        hide();
        return;
    }
    label_->setText(tr("Page %1 / %2").arg(page).arg(count));
    first_->setEnabled(page > 1);
    prev_->setEnabled(page > 1);
    next_->setEnabled(page < count);
    last_->setEnabled(page < count);
    show();
}
