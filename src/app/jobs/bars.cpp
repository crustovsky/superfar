#include "jobs/bars.h"

#include "jobs/format.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QPushButton* button(const QString& text, QWidget* parent)
{
    auto* b = new QPushButton(text, parent);
    b->setAutoDefault(true);  // Enter clicks the focused button outside a dialog too
    return b;
}

void frame(QFrame* f, bool grows = false)
{
    f->setFrameShape(QFrame::StyledPanel);
    f->setFocusPolicy(Qt::StrongFocus);
    f->setSizePolicy(QSizePolicy::Preferred, grows ? QSizePolicy::Expanding : QSizePolicy::Fixed);
}

QString when(qint64 secs)
{
    return QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

} // namespace

PromptBar::PromptBar(const QString& message, const QString& okLabel, std::optional<QString> input,
                     QWidget* parent)
    : QFrame(parent)
{
    frame(this);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->addWidget(new QLabel(message, this));
    if (input) {
        edit_ = new QLineEdit(*input, this);
        layout->addWidget(edit_, 1);
        connect(edit_, &QLineEdit::returnPressed, this, [this] { Q_EMIT accepted(edit_->text()); });
    } else {
        layout->addStretch(1);
    }
    auto* ok = ok_ = button(okLabel, this);
    auto* cancel = button(tr("&Cancel"), this);
    layout->addWidget(ok);
    layout->addWidget(cancel);
    connect(ok, &QPushButton::clicked, this, [this] { Q_EMIT accepted(edit_ ? edit_->text() : QString()); });
    connect(cancel, &QPushButton::clicked, this, &PromptBar::rejected);
    if (edit_) {
        setFocusProxy(edit_);
        edit_->selectAll();
    } else {
        setFocusProxy(ok);
    }
}

void PromptBar::offerQueue()
{
    auto* queue = button(tr("&Queue"), this);
    queue->setToolTip(tr("Start when the jobs before it have ended (Shift+Enter)"));
    auto* row = static_cast<QHBoxLayout*>(layout());
    row->insertWidget(row->indexOf(ok_) + 1, queue);
    connect(queue, &QPushButton::clicked, this, [this] {
        queued_ = true;
        Q_EMIT accepted(edit_ ? edit_->text() : QString());
    });
    // Shift+Enter where Enter accepts: the field, or the button.
    (edit_ ? static_cast<QWidget*>(edit_) : static_cast<QWidget*>(ok_))->installEventFilter(this);
}

bool PromptBar::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && (key->modifiers() & Qt::ShiftModifier))
            queued_ = true;  // the Enter then accepts as it always does
    }
    return QFrame::eventFilter(watched, event);
}

void PromptBar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT rejected();
        return;
    }
    QFrame::keyPressEvent(event);
}

ChoiceBar::ChoiceBar(const QString& message, const QStringList& items, QWidget* parent)
    : QFrame(parent)
    , list_(new QListWidget(this))
{
    frame(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);
    layout->addWidget(new QLabel(message, this));
    list_->addItems(items);
    list_->setCurrentRow(0);
    const int rows = std::min(static_cast<int>(items.size()), 8);
    list_->setFixedHeight(list_->sizeHintForRow(0) * rows + 2 * list_->frameWidth());
    layout->addWidget(list_);
    connect(list_, &QListWidget::itemActivated, this, [this] { Q_EMIT chosen(list_->currentRow()); });
    setFocusProxy(list_);
}

void ChoiceBar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT rejected();
        return;
    }
    QFrame::keyPressEvent(event);
}

CheckBar::CheckBar(const QString& message, QWidget* parent)
    : QFrame(parent)
    , list_(new QListWidget(this))
{
    frame(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);
    layout->addWidget(new QLabel(message, this));
    layout->addWidget(list_);
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        Q_EMIT toggled(list_->row(item), item->checkState() == Qt::Checked);
    });
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!(item->flags() & Qt::ItemIsUserCheckable))
            Q_EMIT actionChosen();
    });
    list_->installEventFilter(this);  // the list would take Shift+arrows for itself
    setFocusProxy(list_);
}

void CheckBar::setCurrent(int index)
{
    list_->setCurrentRow(std::clamp(index, 0, std::max(0, list_->count() - 1)));
}

bool CheckBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == list_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const bool up = key->key() == Qt::Key_Up, down = key->key() == Qt::Key_Down;
        if ((up || down) && (key->modifiers() & Qt::ShiftModifier)) {
            const QListWidgetItem* item = list_->currentItem();
            if (item && (item->flags() & Qt::ItemIsUserCheckable))
                Q_EMIT moveRequested(list_->currentRow(), up ? -1 : 1);
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void CheckBar::setItems(const QStringList& labels, const QList<bool>& checked, const QString& action)
{
    const int row = std::max(0, list_->currentRow());
    const QSignalBlocker block(list_);
    list_->clear();
    for (int i = 0; i < labels.size(); ++i) {
        auto* item = new QListWidgetItem(labels[i], list_);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(checked.value(i) ? Qt::Checked : Qt::Unchecked);
    }
    if (!action.isEmpty())
        (new QListWidgetItem(action, list_))->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    list_->setCurrentRow(std::min(row, list_->count() - 1));
    list_->setFixedHeight(list_->sizeHintForRow(0) * std::min(list_->count(), 8) + 2 * list_->frameWidth());
}

void CheckBar::keyPressEvent(QKeyEvent* event)
{
    // The list leaves Enter to its parent; Space it handles itself.
    QListWidgetItem* item = list_->currentItem();
    switch (event->key()) {
    case Qt::Key_Escape:
        Q_EMIT closed();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!item)
            return;
        if (item->flags() & Qt::ItemIsUserCheckable)
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        else
            Q_EMIT actionChosen();
        return;
    default:
        QFrame::keyPressEvent(event);
    }
}

AuthBar::AuthBar(const QString& message, const QString& user, const QString& domain, bool needUser,
                 bool needDomain, bool needPassword, bool anonymousOk, QWidget* parent)
    : QFrame(parent)
{
    frame(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);
    layout->addWidget(new QLabel(message, this));
    auto* row = new QHBoxLayout;
    auto field = [&](const QString& label, const QString& value, bool password) {
        row->addWidget(new QLabel(label, this));
        auto* e = new QLineEdit(value, this);
        if (password)
            e->setEchoMode(QLineEdit::Password);
        connect(e, &QLineEdit::returnPressed, this, &AuthBar::submit);
        row->addWidget(e, 1);
        return e;
    };
    if (needUser)
        user_ = field(tr("User"), user, false);
    if (needDomain)
        domain_ = field(tr("Domain"), domain, false);
    if (needPassword)
        password_ = field(tr("Password"), QString(), true);
    if (anonymousOk) {
        anonymous_ = new QCheckBox(tr("&Anonymous"), this);
        row->addWidget(anonymous_);
    }
    auto* ok = button(tr("&Connect"), this);
    auto* cancel = button(tr("&Cancel"), this);
    row->addWidget(ok);
    row->addWidget(cancel);
    layout->addLayout(row);
    connect(ok, &QPushButton::clicked, this, &AuthBar::submit);
    connect(cancel, &QPushButton::clicked, this, &AuthBar::rejected);
    QWidget* first = password_;
    if (needUser && user.isEmpty())
        first = user_;
    setFocusProxy(first ? first : static_cast<QWidget*>(ok));
}

void AuthBar::submit()
{
    Q_EMIT answered(user_ ? user_->text() : QString(), domain_ ? domain_->text() : QString(),
                    password_ ? password_->text() : QString(), anonymous_ && anonymous_->isChecked());
}

void AuthBar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT rejected();
        return;
    }
    QFrame::keyPressEvent(event);
}

GoToBar::GoToBar(QWidget* parent)
    : QFrame(parent)
    , edit_(new QLineEdit(this))
    , list_(new QListWidget(this))
{
    frame(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Go to"), this));
    row->addWidget(edit_, 1);
    layout->addLayout(row);
    layout->addWidget(list_);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setFixedHeight(list_->fontMetrics().height() * 8 + 2 * list_->frameWidth() + 8);
    edit_->installEventFilter(this);
    connect(edit_, &QLineEdit::textChanged, this, &GoToBar::textChanged);
    connect(edit_, &QLineEdit::returnPressed, this, &GoToBar::accept);
    connect(list_, &QListWidget::itemActivated, this, &GoToBar::accept);
    setFocusProxy(edit_);
}

void GoToBar::setEntries(const QStringList& entries)
{
    list_->clear();
    list_->addItems(entries);
    if (!entries.isEmpty())
        list_->setCurrentRow(0);
}

QString GoToBar::text() const
{
    return edit_->text();
}

void GoToBar::accept()
{
    const QString typed = edit_->text().trimmed();
    QListWidgetItem* item = list_->currentItem();
    // A typed absolute path or URI wins over the list.
    if (item && !typed.startsWith(QLatin1Char('/')) && !typed.contains(QLatin1String("://")))
        Q_EMIT chosen(item->text());
    else if (!typed.isEmpty())
        Q_EMIT chosen(typed);
    else if (item)
        Q_EMIT chosen(item->text());
}

bool GoToBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == edit_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
            QApplication::sendEvent(list_, event);
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void GoToBar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT rejected();
        return;
    }
    QFrame::keyPressEvent(event);
}

ConflictBar::ConflictBar(const core::Conflict& c, QWidget* parent)
    : QFrame(parent)
{
    frame(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);

    const QString what = c.targetIsDir ? tr("Folder %1 already exists in %2")
                                       : tr("%1 already exists in %2");
    layout->addWidget(new QLabel(what.arg(c.name, c.targetPath.left(c.targetPath.size() - c.name.size())), this));
    auto* details = new QLabel(tr("Source: %1, %2    Target: %3, %4")
                                   .arg(jobfmt::bytes(c.sourceSize), when(c.sourceModified),
                                        jobfmt::bytes(c.targetSize), when(c.targetModified)),
                               this);
    details->setEnabled(false);
    layout->addWidget(details);
    if (c.cannotUndo)
        layout->addWidget(new QLabel(tr("There is no trash here to keep what is replaced: overwriting cannot be undone."), this));

    auto* row = new QHBoxLayout;
    auto* all = new QCheckBox(tr("Apply to &all"), this);
    auto add = [&](const QString& text, core::ConflictAction action) {
        auto* b = button(text, this);
        row->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, all, action] { Q_EMIT answered(action, all->isChecked()); });
        return b;
    };
    auto* first = add(tr("&Overwrite"), core::ConflictAction::Overwrite);
    add(tr("Overwrite if &newer"), core::ConflictAction::OverwriteIfNewer);
    add(tr("&Rename"), core::ConflictAction::Rename);
    add(tr("&Skip"), core::ConflictAction::Skip);
    row->addStretch(1);
    row->addWidget(all);
    layout->addLayout(row);
    setFocusProxy(first);
}

void ConflictBar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT answered(core::ConflictAction::Skip, false);
        return;
    }
    QFrame::keyPressEvent(event);
}

ErrorBar::ErrorBar(const QString& verb, const core::Failure& f, QWidget* parent)
    : QFrame(parent)
{
    frame(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);
    auto* text = new QLabel(tr("Could not %1 %2: %3").arg(verb, f.path, f.message), this);
    text->setWordWrap(true);
    layout->addWidget(text);

    auto* row = new QHBoxLayout;
    auto* all = new QCheckBox(tr("Apply to &all"), this);
    auto add = [&](const QString& label, core::ErrorAction action) {
        auto* b = button(label, this);
        row->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, all, action] { Q_EMIT answered(action, all->isChecked()); });
        return b;
    };
    auto* first = add(tr("&Retry"), core::ErrorAction::Retry);
    add(tr("&Skip"), core::ErrorAction::Skip);
    if (f.offerDelete)
        first = add(tr("Delete &permanently (cannot be undone)"), core::ErrorAction::DeletePermanently);
    add(tr("A&bort"), core::ErrorAction::Abort);
    row->addStretch(1);
    row->addWidget(all);
    layout->addLayout(row);
    setFocusProxy(first);
}

void ErrorBar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT answered(core::ErrorAction::Skip, false);
        return;
    }
    QFrame::keyPressEvent(event);
}

LogBar::LogBar(const QString& title, const std::vector<core::LogEntry>& log, QWidget* parent)
    : QFrame(parent)
{
    frame(this, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);

    auto* head = new QHBoxLayout;
    head->addWidget(new QLabel(title, this), 1);
    auto* close = button(tr("&Close"), this);
    head->addWidget(close);
    layout->addLayout(head);

    auto* list = new QListWidget(this);
    list->setFocusPolicy(Qt::NoFocus);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const core::LogEntry& e : log) {
        const QString kind = e.kind == core::LogEntry::Kind::Skipped ? tr("skipped")
                             : e.kind == core::LogEntry::Kind::Failed ? tr("failed") : tr("overwritten");
        list->addItem(e.message.isEmpty() ? QStringLiteral("%1: %2").arg(kind, e.path)
                                          : QStringLiteral("%1: %2 (%3)").arg(kind, e.path, e.message));
    }
    list->setMinimumHeight(list->sizeHintForRow(0) * 3 + 8);
    layout->addWidget(list, 1);
    connect(close, &QPushButton::clicked, this, &LogBar::closed);
    setFocusProxy(close);
}

void LogBar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT closed();
        return;
    }
    QFrame::keyPressEvent(event);
}
