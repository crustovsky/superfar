#include "properties.h"

#include "counted.h"
#include "attributesedit.h"
#include "elidedlabel.h"
#include "jobs/format.h"

#include <QDateTime>
#include <QKeyEvent>
#include <QLabel>
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
    , editor_(new AttributesEditor(this))
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
    layout->addWidget(editor_);
    layout->addStretch(1);
    connect(editor_, &AttributesEditor::applyRequested, this,
            [this](const core::AttributeChange& change) { Q_EMIT applyRequested(items_, change); });
    connect(editor_, &AttributesEditor::closeRequested, this, &PropertiesPanel::closed);
    connect(editor_, &AttributesEditor::message, this, &PropertiesPanel::message);
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
    editor_->clear();
    title_->setText(items.size() == 1 ? items.front().displayPath()
                                      : counted(static_cast<qint64>(items.size()), tr("item"), tr("items")));
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
    }
    editor_->add(d);
    if (d.isDir) {
        ++folders_;
    } else {
        ++files_;
        fileBytes_ += d.size;
    }
    if (pendingDetails_ == 0) {
        editor_->finish();
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
        lines << counted(files_, tr("file"), tr("files")) + QStringLiteral(", ") + counted(folders_, tr("folder"), tr("folders"));
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

void PropertiesPanel::reread()
{
    if (!items_.empty())
        show(std::vector<core::Location>(items_));
}

void PropertiesPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT closed();
        return;
    }
    QWidget::keyPressEvent(event);
}
