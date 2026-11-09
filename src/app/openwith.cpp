#include "openwith.h"

#include "core/match.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <algorithm>

OpenWithBar::OpenWithBar(const QString& title, std::vector<core::AppChoice> apps, QWidget* parent)
    : QFrame(parent)
    , apps_(std::move(apps))
    , edit_(new QLineEdit(this))
    , list_(new QListWidget(this))
    , hint_(new QLabel(this))
{
    setFrameShape(QFrame::StyledPanel);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(title, this));
    row->addWidget(edit_, 1);
    layout->addLayout(row);
    layout->addWidget(list_);
    hint_->setEnabled(false);
    layout->addWidget(hint_);
    list_->setFocusPolicy(Qt::NoFocus);
    edit_->installEventFilter(this);
    connect(edit_, &QLineEdit::textChanged, this, &OpenWithBar::refill);
    connect(list_, &QListWidget::itemActivated, this, [this] { accept(false); });
    setFocusProxy(edit_);
    refill(QString());
    // Eight whole rows, as tall as the icons make them.
    const int rowHeight = std::max(list_->sizeHintForRow(0), list_->fontMetrics().height() + 4);
    list_->setFixedHeight(rowHeight * 8 + 2 * list_->frameWidth());
}

void OpenWithBar::refill(const QString& text)
{
    // Ranked as the quick filter ranks names: prefix, substring, then
    // abbreviation; the order within a rank is the list's own (default,
    // recommended, the rest).
    const QString typed = text.trimmed().toCaseFolded();
    std::vector<int> ranked[3];
    for (int i = 0; i < static_cast<int>(apps_.size()); ++i) {
        const int rank = typed.isEmpty() ? 0 : core::matchRank(apps_[static_cast<size_t>(i)].name.toCaseFolded(), typed);
        if (rank >= 0 && rank < 3)
            ranked[rank].push_back(i);
    }
    list_->clear();
    for (const auto& group : ranked) {
        for (int i : group) {
            const core::AppChoice& app = apps_[static_cast<size_t>(i)];
            QIcon icon;
            for (const QString& name : app.iconNames) {
                icon = QIcon::fromTheme(name);
                if (!icon.isNull())
                    break;
            }
            auto* item = new QListWidgetItem(icon, app.isDefault ? tr("%1 (default)").arg(app.name) : app.name, list_);
            item->setData(Qt::UserRole, i);
        }
    }
    if (list_->count() > 0) {
        list_->setCurrentRow(0);
        hint_->setText(tr("Enter opens  ·  Ctrl+Enter opens and makes it the default  ·  Esc"));
    } else if (typed.isEmpty()) {
        hint_->setText(tr("No application is registered for this type: type a command  ·  Esc"));
    } else {
        hint_->setText(tr("No such app: Enter runs this as a command on the items  ·  Ctrl+Enter in a terminal  ·  Esc"));
    }
}

void OpenWithBar::accept(bool ctrl)
{
    if (const QListWidgetItem* item = list_->currentItem()) {
        Q_EMIT appChosen(apps_[static_cast<size_t>(item->data(Qt::UserRole).toInt())], ctrl);
        return;
    }
    const QString typed = edit_->text().trimmed();
    if (!typed.isEmpty())
        Q_EMIT commandTyped(typed, ctrl);
}

bool OpenWithBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == edit_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            QApplication::sendEvent(list_, event);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            accept(key->modifiers() & Qt::ControlModifier);
            return true;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void OpenWithBar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT rejected();
        return;
    }
    QFrame::keyPressEvent(event);
}
