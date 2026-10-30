#include "funcbar.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QToolButton>

FuncBar::FuncBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    held_ = new QLabel(this);
    held_->setContentsMargins(6, 0, 6, 0);
    QFont bold = held_->font();
    bold.setBold(true);
    held_->setFont(bold);
    // Reserve the widest name so the buttons do not shift.
    held_->setFixedWidth(held_->fontMetrics().horizontalAdvance(QStringLiteral("Ctrl+Shift")) + 12);
    layout->addWidget(held_);
    for (int i = 1; i <= 10; ++i) {
        auto* b = new QToolButton(this);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(b, &QToolButton::clicked, this, [this, i] {
            const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
            const auto key = Qt::Key(Qt::Key_F1 + i - 1);
            const keymap::Command cmd = keymap::lookup(QKeySequence(QKeyCombination(mods, key).toCombined()));
            if (cmd != keymap::Command::None)
                Q_EMIT commandRequested(cmd);
        });
        buttons_.append(b);
        layout->addWidget(b);
    }
    relabel();
    qApp->installEventFilter(this);
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
        const QString text = keymap::label(cmd);
        buttons_[i]->setText(QStringLiteral("F%1 %2").arg(i + 1).arg(text));
        buttons_[i]->setEnabled(cmd != keymap::Command::None);
    }
}
