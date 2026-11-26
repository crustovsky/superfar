#include "funcbar.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolButton>

#include <algorithm>

namespace {

// A button of the bar. Where its label does not fit it is cut at the
// end, so the key and the start of the word stay ("F1 Abo…"); left to
// itself the style cuts the middle ("F1...ut"). The size hint is the
// whole label's.
class BarButton : public QToolButton {
public:
    using QToolButton::QToolButton;

    QString shownText() const
    {
        // A cramped button gives its label all it has but the frame.
        QStyleOptionToolButton opt;
        initStyleOption(&opt);
        const int frame = style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &opt, this);
        const int room = width() - 2 * frame - 2;
        const QString shown = fontMetrics().elidedText(text(), Qt::ElideRight, room);
        // Never dots alone: the key at least, or nothing (a free key).
        const QString key = text().section(QLatin1Char(' '), 0, 0);
        if (shown.startsWith(key))
            return shown;
        return fontMetrics().horizontalAdvance(key) <= room ? key : QString();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStylePainter p(this);
        QStyleOptionToolButton opt;
        initStyleOption(&opt);
        opt.text = shownText();
        p.drawComplexControl(QStyle::CC_ToolButton, opt);
    }
};

} // namespace

QString FuncBar::shownLabel(int index) const
{
    return static_cast<const BarButton*>(buttons_.value(index))->shownText();
}

FuncBar::FuncBar(QWidget* parent)
    : QWidget(parent)
{
    held_ = new QLabel(this);
    held_->setContentsMargins(6, 0, 6, 0);
    QFont bold = held_->font();
    bold.setBold(true);
    held_->setFont(bold);
    for (int i = 1; i <= 10; ++i) {
        auto* b = new BarButton(this);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        connect(b, &QToolButton::clicked, this, [this, i] {
            const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
            const auto key = Qt::Key(Qt::Key_F1 + i - 1);
            const keymap::Command cmd = keymap::lookup(QKeySequence(QKeyCombination(mods, key).toCombined()));
            if (cmd != keymap::Command::None)
                Q_EMIT commandRequested(cmd);
        });
        buttons_.append(b);
    }
    // The height follows the width (one row or two); the width is
    // whatever the window has, and the bar never asks for more (#91).
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    relabel();
    measure();
    qApp->installEventFilter(this);
}

QString FuncBar::labelOf(Qt::KeyboardModifiers mods, int index)
{
    const auto key = Qt::Key(Qt::Key_F1 + index);
    const keymap::Command cmd = keymap::lookup(QKeySequence(QKeyCombination(mods, key).toCombined()));
    return QStringLiteral("F%1 %2").arg(index + 1).arg(keymap::label(cmd));
}

void FuncBar::measure()
{
    // Reserve the widest name so the buttons do not shift.
    held_->setFixedWidth(held_->fontMetrics().horizontalAdvance(QStringLiteral("Ctrl+Shift")) + 12);
    // What a button adds around its text is the same for every label,
    // so a layer that is not shown can be measured from its texts.
    const QToolButton* b = buttons_.first();
    const QFontMetrics fm = b->fontMetrics();
    auto textWidth = [&](const QString& text) { return fm.size(Qt::TextShowMnemonic, text).width(); };
    const int chrome = b->sizeHint().width() - textWidth(b->text());
    oneRow_ = 0;
    for (const Qt::KeyboardModifiers mods : {Qt::KeyboardModifiers(Qt::NoModifier), Qt::KeyboardModifiers(Qt::ShiftModifier),
                                             Qt::KeyboardModifiers(Qt::ControlModifier), Qt::KeyboardModifiers(Qt::AltModifier)}) {
        int width = held_->width();
        for (int i = 0; i < buttons_.size(); ++i)
            width += chrome + textWidth(labelOf(mods, i));
        oneRow_ = std::max(oneRow_, width);
    }
    updateGeometry();
    arrange();
}

int FuncBar::rowHeight() const
{
    return buttons_.first()->sizeHint().height();
}

int FuncBar::rows(int width) const
{
    return width >= oneRow_ ? 1 : 2;
}

QSize FuncBar::sizeHint() const
{
    return {oneRow_, rowHeight()};
}

QSize FuncBar::minimumSizeHint() const
{
    return {held_->width() + static_cast<int>(buttons_.size()), rowHeight()};
}

int FuncBar::heightForWidth(int width) const
{
    return rows(width) * rowHeight();
}

void FuncBar::resizeEvent(QResizeEvent*)
{
    arrange();
}

void FuncBar::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        measure();
    QWidget::changeEvent(event);
}

void FuncBar::arrange()
{
    // One row, or F1 to F5 over F6 to F10 with the keys in columns. A
    // column is as wide as the wider of its labels asks; room left over
    // is shared out, and room missing (a window too narrow even for two
    // rows) is taken from each in proportion, the labels cut at the end.
    const bool two = rows(width()) == 2;
    const int columns = static_cast<int>(buttons_.size()) / (two ? 2 : 1);
    const int height = rowHeight();
    held_->setGeometry(0, 0, held_->width(), height);
    QList<int> want;
    int total = 0;
    for (int c = 0; c < columns; ++c) {
        int w = buttons_[c]->sizeHint().width();
        if (two)
            w = std::max(w, buttons_[c + columns]->sizeHint().width());
        want.append(w);
        total += w;
    }
    const int room = std::max(columns, width() - held_->width());
    int x = held_->width();
    for (int c = 0; c < columns; ++c) {
        const int w = total <= room ? want[c] + (room - total) / columns
                                    : static_cast<int>(static_cast<qint64>(want[c]) * room / total);
        buttons_[c]->setGeometry(x, 0, w, height);
        if (two)
            buttons_[c + columns]->setGeometry(x, height, w, height);
        x += w;
    }
}

Qt::KeyboardModifier FuncBar::modifierOf(int key)
{
    switch (key) {
    case Qt::Key_Shift: return Qt::ShiftModifier;
    case Qt::Key_Control: return Qt::ControlModifier;
    case Qt::Key_Alt: return Qt::AltModifier;
    default: return Qt::NoModifier;
    }
}

bool FuncBar::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        const auto* k = static_cast<QKeyEvent*>(event);
        const Qt::KeyboardModifier m = modifierOf(k->key());
        // A modifier's own press and release are the primary source; any
        // other key brings the state it was pressed with.
        Qt::KeyboardModifiers next = m != Qt::NoModifier
            ? mods_ : k->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
        if (m != Qt::NoModifier)
            next.setFlag(m, event->type() == QEvent::KeyPress);
        if (next != mods_) {
            mods_ = next;
            relabel();
        }
        break;
    }
    case QEvent::ApplicationDeactivate:
    case QEvent::WindowDeactivate:
        if (mods_ != Qt::NoModifier) {
            mods_ = Qt::NoModifier;
            relabel();
        }
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void FuncBar::relabel()
{
    QStringList held;
    if (mods_ & Qt::ControlModifier)
        held << QStringLiteral("Ctrl");
    if (mods_ & Qt::AltModifier)
        held << QStringLiteral("Alt");
    if (mods_ & Qt::ShiftModifier)
        held << QStringLiteral("Shift");
    held_->setText(held.join(QLatin1Char('+')));
    for (int i = 0; i < buttons_.size(); ++i) {
        const auto key = Qt::Key(Qt::Key_F1 + i);
        const keymap::Command cmd = keymap::lookup(QKeySequence(QKeyCombination(mods_, key).toCombined()));
        buttons_[i]->setText(labelOf(mods_, i));
        buttons_[i]->setEnabled(cmd != keymap::Command::None);
    }
    arrange();  // the columns follow the labels; the height does not change
}
