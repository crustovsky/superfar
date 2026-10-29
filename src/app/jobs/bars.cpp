#include "jobs/bars.h"

#include "jobs/format.h"

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
    auto* ok = button(okLabel, this);
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

void PromptBar::keyPressEvent(QKeyEvent* event)
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
        first = add(tr("Delete &permanently"), core::ErrorAction::DeletePermanently);
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
        const QString kind = e.kind == core::LogEntry::Kind::Skipped ? tr("skipped") : tr("failed");
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
