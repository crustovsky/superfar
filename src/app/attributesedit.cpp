#include "attributesedit.h"

#include "ownercompletion.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <unistd.h>

AttributesEditor::AttributesEditor(QWidget* parent)
    : QWidget(parent)
    , modeLabel_(new QLabel(this))
    , owner_(new QLineEdit(this))
    , recursive_(new QCheckBox(tr("Apply to the &contents of folders too"), this))
    , recursiveNote_(new QLabel(tr("Folders below take the bits as set; a file takes execute only where it already has one. "
                                   "Links are not followed."), this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(4);

    auto* grid = new QGridLayout;
    const char* who[] = {QT_TR_NOOP("Owner"), QT_TR_NOOP("Group"), QT_TR_NOOP("Others")};
    const char* what[] = {QT_TR_NOOP("read"), QT_TR_NOOP("write"), QT_TR_NOOP("execute")};
    for (int c = 0; c < 3; ++c)
        grid->addWidget(new QLabel(tr(what[c]), this), 0, c + 1);
    for (int r = 0; r < 3; ++r) {
        grid->addWidget(new QLabel(tr(who[r]), this), r + 1, 0);
        for (int c = 0; c < 3; ++c) {
            auto* box = new QCheckBox(this);
            bits_[static_cast<size_t>(r * 3 + c)] = box;
            grid->addWidget(box, r + 1, c + 1);
            connect(box, &QCheckBox::clicked, this, [this] {
                modeTouched_ = true;
                showMode();
            });
        }
    }
    grid->setColumnStretch(4, 1);
    layout->addLayout(grid);
    // Wrapped lines below, never a long one beside: the panel must not
    // push its pane wider than the list was.
    modeLabel_->setWordWrap(true);
    layout->addWidget(modeLabel_);

    auto* ownerRow = new QHBoxLayout;
    auto* ownerLabel = new QLabel(tr("O&wner and group"), this);
    ownerLabel->setBuddy(owner_);
    owner_->setPlaceholderText(tr("user:group"));
    completeOwners(owner_);
    ownerRow->addWidget(ownerLabel);
    ownerRow->addWidget(owner_, 1);
    layout->addLayout(ownerRow);
    if (::geteuid() != 0) {
        // Said up front, so the refusal is no surprise.
        auto* note = new QLabel(tr("Only root can give a file to another user. You can change the group to one of your own."), this);
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    layout->addWidget(recursive_);
    recursiveNote_->setWordWrap(true);
    layout->addWidget(recursiveNote_);

    auto* buttons = new QHBoxLayout;
    auto* applyBtn = new QPushButton(tr("&Apply"), this);
    auto* closeBtn = new QPushButton(tr("C&lose"), this);
    buttons->addWidget(applyBtn);
    buttons->addWidget(closeBtn);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    // Tab goes the way the panel reads, not the way it was built.
    for (size_t i = 0; i + 1 < bits_.size(); ++i)
        setTabOrder(bits_[i], bits_[i + 1]);
    setTabOrder(bits_.back(), owner_);
    setTabOrder(owner_, recursive_);
    setTabOrder(recursive_, applyBtn);
    setTabOrder(applyBtn, closeBtn);
    connect(applyBtn, &QPushButton::clicked, this, &AttributesEditor::apply);
    connect(owner_, &QLineEdit::returnPressed, this, &AttributesEditor::apply);
    connect(closeBtn, &QPushButton::clicked, this, &AttributesEditor::closeRequested);
}

void AttributesEditor::clear()
{
    allSet_ = 0777;
    anySet_ = 0;
    user_.clear();
    group_.clear();
    count_ = 0;
    anyDir_ = false;
    modeTouched_ = false;
    // Not disabled while the items answer: a disabled widget drops the
    // focus down the chain and out of the panel. Apply waits instead.
    ready_ = false;
}

void AttributesEditor::add(const core::Details& d)
{
    if (!d.ok)
        return;
    allSet_ &= d.mode;
    anySet_ |= d.mode & 0777;
    if (count_ == 0) {
        user_ = d.owner;
        group_ = d.group;
    } else {
        if (user_ != d.owner)
            user_.clear();
        if (group_ != d.group)
            group_.clear();
    }
    anyDir_ = anyDir_ || d.isDir;
    ++count_;
}

void AttributesEditor::finish()
{
    for (int i = 0; i < 9; ++i) {
        QCheckBox* box = bits_[static_cast<size_t>(i)];
        const quint32 bit = 0400u >> i;
        const bool mixed = (anySet_ & bit) && !(allSet_ & bit);
        box->setTristate(mixed);
        box->setCheckState(mixed ? Qt::PartiallyChecked : (allSet_ & bit) ? Qt::Checked : Qt::Unchecked);
    }
    owner_->setText(user_.isEmpty() && group_.isEmpty() ? QString() : user_ + QLatin1Char(':') + group_);
    recursive_->setChecked(false);
    recursive_->setVisible(anyDir_);
    recursiveNote_->setVisible(anyDir_);
    showMode();
    ready_ = count_ > 0;
}

void AttributesEditor::showMode()
{
    quint32 mode = 0;
    bool mixed = false;
    for (int i = 0; i < 9; ++i) {
        const Qt::CheckState state = bits_[static_cast<size_t>(i)]->checkState();
        mixed = mixed || state == Qt::PartiallyChecked;
        if (state == Qt::Checked)
            mode |= 0400u >> i;
    }
    modeLabel_->setText(mixed ? tr("The items differ: a half-checked bit stays as each item has it.")
                              : tr("Mode %1").arg(mode, 4, 8, QLatin1Char('0')));
}

void AttributesEditor::apply()
{
    if (!ready_)
        return;
    core::AttributeChange change;
    if (modeTouched_) {
        quint32 mode = 0, mask = 0;
        for (int i = 0; i < 9; ++i) {
            const Qt::CheckState state = bits_[static_cast<size_t>(i)]->checkState();
            if (state == Qt::PartiallyChecked)
                continue;
            mask |= 0400u >> i;
            if (state == Qt::Checked)
                mode |= 0400u >> i;
        }
        if (mask) {
            change.mode = mode;
            change.modeMask = mask;  // setuid, setgid and sticky are never in it
        }
    }
    // Only the half that was edited is asked for: naming the owner as it
    // is must not turn a group change into a chown a user is refused.
    const QString text = owner_->text().trimmed();
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    const QString user = (colon < 0 ? text : text.left(colon)).trimmed();
    const QString group = colon < 0 ? QString() : text.mid(colon + 1).trimmed();
    const QString wanted = (user == user_ ? QString() : user) + QLatin1Char(':') + (group == group_ ? QString() : group);
    if (wanted != QLatin1String(":")) {
        QString error;
        const auto owner = core::parseOwner(wanted, &error);
        if (!owner) {
            Q_EMIT message(error);
            return;
        }
        change.owner = *owner;
    }
    if (change.isEmpty()) {
        Q_EMIT message(tr("Nothing was changed here: set a box or edit the owner first"));
        return;
    }
    change.recursive = anyDir_ && recursive_->isChecked();
    Q_EMIT applyRequested(change);
}
