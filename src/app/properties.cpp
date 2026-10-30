#include "properties.h"

#include "core/fileops.h"
#include "elidedlabel.h"
#include "jobs/format.h"

#include <QCheckBox>
#include <QDateTime>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QString when(qint64 secs)
{
    return secs > 0 ? QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString();
}
} // namespace

PropertiesPanel::PropertiesPanel(core::DirSizePool* sizes, QWidget* parent)
    : QWidget(parent)
    , sizer_(new core::DirSizer(sizes, this))
    , title_(new ElidedLabel(this))
    , info_(new QLabel(this))
    , modeLabel_(new QLabel(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    title_->setContentsMargins(4, 2, 4, 2);
    layout->addWidget(title_);
    info_->setContentsMargins(6, 4, 6, 4);
    info_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    info_->setWordWrap(true);
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(info_);

    auto* grid = new QGridLayout;
    grid->setContentsMargins(6, 0, 6, 0);
    const char* who[] = {"Owner", "Group", "Others"};
    const char* what[] = {"read", "write", "execute"};
    for (int c = 0; c < 3; ++c)
        grid->addWidget(new QLabel(tr(what[c]), this), 0, c + 1);
    for (int r = 0; r < 3; ++r) {
        grid->addWidget(new QLabel(tr(who[r]), this), r + 1, 0);
        for (int c = 0; c < 3; ++c) {
            auto* box = new QCheckBox(this);
            bits_[static_cast<size_t>(r * 3 + c)] = box;
            grid->addWidget(box, r + 1, c + 1);
            connect(box, &QCheckBox::toggled, this, [this] {
                modeLabel_->setText(QStringLiteral("%1").arg(modeFromBoxes(), 4, 8, QLatin1Char('0')));
            });
        }
    }
    grid->addWidget(modeLabel_, 1, 4);
    auto* applyBtn = new QPushButton(tr("&Apply permissions"), this);
    auto* closeBtn = new QPushButton(tr("&Close"), this);
    grid->addWidget(applyBtn, 2, 4);
    grid->addWidget(closeBtn, 3, 4);
    grid->setColumnStretch(5, 1);
    layout->addLayout(grid);
    layout->addStretch(1);
    connect(applyBtn, &QPushButton::clicked, this, &PropertiesPanel::apply);
    connect(closeBtn, &QPushButton::clicked, this, &PropertiesPanel::closed);
    setFocusPolicy(Qt::StrongFocus);

    connect(sizer_, &core::DirSizer::result, this, [this](quint64 gen, const QByteArray&, const core::DirSize& s) {
        if (gen != generation_)
            return;
        // Partial totals replace earlier partials of the same walk; a
        // simple approach: refetch the sum from scratch is not possible,
        // so completed walks accumulate and running ones show as such.
        if (s.complete) {
            folderTotals_.bytes += s.bytes;
            folderTotals_.files += s.files;
            folderTotals_.dirs += s.dirs;
            ++foldersCounted_;
        }
        refresh();
    });
}

void PropertiesPanel::show(const std::vector<core::Location>& items)
{
    ++generation_;
    sizer_->cancelAll();
    items_ = items;
    pendingDetails_ = static_cast<int>(items.size());
    files_ = folders_ = foldersCounted_ = 0;
    fileBytes_ = 0;
    folderTotals_ = core::DirSize();
    firstSeen_ = false;
    title_->setText(items.size() == 1 ? items.front().displayPath()
                                      : tr("%n items", nullptr, static_cast<int>(items.size())));
    info_->setText(tr("Reading"));
    for (const core::Location& item : items) {
        core::queryDetails(item, [this, gen = generation_](const core::Details& d) {
            if (gen == generation_)
                onDetails(d);
        });
    }
}

void PropertiesPanel::onDetails(const core::Details& d)
{
    --pendingDetails_;
    if (!firstSeen_ && d.ok) {
        firstSeen_ = true;
        first_ = d;
        for (int i = 0; i < 9; ++i) {
            QSignalBlocker block(bits_[static_cast<size_t>(i)]);
            bits_[static_cast<size_t>(i)]->setChecked(d.mode & (0400 >> i));
        }
        modeLabel_->setText(QStringLiteral("%1").arg(d.mode & 07777, 4, 8, QLatin1Char('0')));
    }
    if (d.isDir) {
        ++folders_;
    } else {
        ++files_;
        fileBytes_ += d.size;
    }
    if (pendingDetails_ == 0) {
        // Folders get a walk each once every item is known.
        for (const core::Location& item : items_) {
            core::Location parent = item.parent();
            if (parent.isValid() && folders_ > 0)
                sizer_->request(parent, item.basename(), generation_, true);
        }
    }
    refresh();
}

void PropertiesPanel::refresh()
{
    QStringList lines;
    if (items_.size() == 1 && firstSeen_) {
        const core::Details& d = first_;
        lines << (d.typeDescription.isEmpty() ? d.contentType : QStringLiteral("%1 (%2)").arg(d.typeDescription, d.contentType));
        if (d.symlink)
            lines << tr("Link to %1").arg(d.symlinkTarget);
        if (!d.isDir)
            lines << tr("Size: %1 (%2 bytes)").arg(jobfmt::bytes(d.size)).arg(d.size);
        lines << tr("Modified: %1").arg(when(d.modified));
        if (d.accessed > 0)
            lines << tr("Accessed: %1").arg(when(d.accessed));
        if (!d.owner.isEmpty())
            lines << tr("Owner: %1:%2").arg(d.owner, d.group);
    } else {
        lines << tr("%n files", nullptr, files_) + tr(", %n folders", nullptr, folders_);
        if (files_ > 0)
            lines << tr("Files: %1").arg(jobfmt::bytes(fileBytes_));
    }
    if (folders_ > 0) {
        const bool running = foldersCounted_ < folders_;
        lines << tr("Folder contents: %1 in %2 files and %3 folders%4")
                     .arg(jobfmt::bytes(folderTotals_.bytes))
                     .arg(folderTotals_.files)
                     .arg(folderTotals_.dirs)
                     .arg(running ? tr(" (counting)") : QString());
    }
    info_->setText(lines.join(QLatin1Char('\n')));
}

quint32 PropertiesPanel::modeFromBoxes() const
{
    quint32 mode = 0;
    for (int i = 0; i < 9; ++i)
        if (bits_[static_cast<size_t>(i)]->isChecked())
            mode |= 0400 >> i;
    return mode;
}

void PropertiesPanel::apply()
{
    const quint32 mode = modeFromBoxes();
    QPointer<PropertiesPanel> guard(this);
    auto remaining = std::make_shared<int>(static_cast<int>(items_.size()));
    for (const core::Location& item : items_) {
        core::setMode(item, mode, [guard, remaining](const QString& error) {
            if (!guard)
                return;
            if (!error.isEmpty())
                Q_EMIT guard->message(error);
            if (--*remaining == 0) {
                Q_EMIT guard->message(tr("Permissions applied"));
                Q_EMIT guard->changed();
            }
        });
    }
}

void PropertiesPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT closed();
        return;
    }
    QWidget::keyPressEvent(event);
}
