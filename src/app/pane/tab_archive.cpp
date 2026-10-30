// Tab, archive mode: the part of the tab that turns a bsdtar listing
// into folder views. See tab.cpp for the rest of the class.
#include "pane/tab.h"

#include "core/sort.h"
#include "pane/listmodel.h"
#include "pane/listview.h"

#include <QHash>

void Tab::enterArchive(const core::Location& file)
{
    archive_ = file;
    archiveDir_.clear();
    archiveEntries_.clear();
    lister_->start(file, generation_);
    Q_EMIT statusChanged(tr("Reading archive"), QString());
}

void Tab::onArchiveListed(quint64 generation, std::vector<core::ArchiveEntry> entries)
{
    if (generation != generation_)
        return;
    archiveEntries_ = std::move(entries);
    showArchiveDir(archiveDir_);
}

void Tab::showArchiveDir(const QByteArray& dir)
{
    archiveDir_ = dir;
    const QByteArray prefix = dir.isEmpty() ? QByteArray() : dir + '/';
    // Direct children of dir; folders may exist only implicitly, through
    // the paths of what they hold.
    QHash<QByteArray, core::Entry> rows;
    QHash<QByteArray, quint64> counts;
    QHash<QByteArray, core::Metadata> metas;
    for (const core::ArchiveEntry& a : archiveEntries_) {
        if (!prefix.isEmpty() && !a.path.startsWith(prefix))
            continue;
        const QByteArray rest = a.path.mid(prefix.size());
        if (rest.isEmpty())
            continue;
        const int slash = rest.indexOf('/');
        const QByteArray leaf = slash < 0 ? rest : rest.left(slash);
        const bool isDir = slash >= 0 || a.isDir;
        if (!rows.contains(leaf)) {
            core::Entry e;
            e.name = leaf;
            e.display = QString::fromUtf8(leaf);
            e.type = isDir ? core::EntryType::Directory : core::EntryType::File;
            e.symlink = slash < 0 && a.symlink;
            core::computeSortKey(e);
            rows.insert(leaf, e);
        }
        if (slash >= 0) {
            if (rest.indexOf('/', slash + 1) < 0)
                counts[leaf] += 1;  // a direct child of the folder
        } else if (!a.isDir) {
            core::Metadata m;
            m.ok = true;
            m.size = a.size;
            m.modified = a.modified;
            metas.insert(leaf, m);
        }
    }
    model_->reset(location_);
    std::vector<core::Entry> batch;
    for (auto it = rows.cbegin(); it != rows.cend(); ++it)
        batch.push_back(it.value());
    model_->append(std::move(batch));
    for (auto it = metas.cbegin(); it != metas.cend(); ++it)
        model_->presetMetadata(it.key(), it.value());
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        model_->presetDirCount(it.key(), it.value());
    model_->finishLoad();
    placeCursor();
    updateStatus();
    Q_EMIT locationChanged(location_);
    Q_EMIT loaded();
}

int Tab::archiveDepth() const
{
    return archiveDir_.isEmpty() ? 0 : static_cast<int>(archiveDir_.count('/')) + 1;
}

std::vector<QByteArray> Tab::archiveMembers() const
{
    std::vector<QByteArray> out;
    const QByteArray prefix = archiveDir_.isEmpty() ? QByteArray() : archiveDir_ + '/';
    if (model_->markedCount() > 0) {
        for (const QByteArray& name : model_->markedNamesInOrder())
            out.push_back(prefix + name);
    } else if (const QByteArray name = currentName(); !name.isEmpty()) {
        out.push_back(prefix + name);
    }
    return out;
}

QString Tab::displayPath() const
{
    if (!isArchive())
        return location_.displayPath();
    return archive_.displayPath() + (archiveDir_.isEmpty() ? QString() : QLatin1Char('/') + QString::fromUtf8(archiveDir_));
}
