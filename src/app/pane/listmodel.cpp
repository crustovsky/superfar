#include "pane/listmodel.h"

#include "core/sort.h"
#include "uris.h"

#include <QDateTime>
#include <QLocale>
#include <QMimeData>
#include <QRegularExpression>
#include <algorithm>

#include <malloc.h>

ListModel::ListModel(core::DirSizePool* sizes, core::Thumbnailer* thumbs, QObject* parent)
    : QAbstractTableModel(parent)
    , meta_(new core::MetadataQuery(this))
    , sizer_(new core::DirSizer(sizes, this))
    , thumbs_(thumbs)
{
    connect(meta_, &core::MetadataQuery::result, this, &ListModel::onMetadata);
    connect(sizer_, &core::DirSizer::result, this, &ListModel::onDirSize);
    connect(thumbs_, &core::Thumbnailer::ready, this, &ListModel::onThumbnail);
    connect(thumbs_, &core::Thumbnailer::failed, this, [this](const QString& uri) {
        const QByteArray name = thumbPending_.take(uri);
        if (!name.isEmpty())
            thumbCache_.insert(name, QIcon());  // remembered as "none"
    });
}

void ListModel::setThumbnails(bool on)
{
    if (on == thumbnails_)
        return;
    thumbnails_ = on;
    thumbCache_.clear();
    thumbPending_.clear();
    rowsRepaint();
}

QIcon ListModel::thumbnail(const core::Entry& entry, const core::Metadata& meta) const
{
    const auto it = thumbCache_.constFind(entry.name);
    if (it != thumbCache_.cend())
        return it.value();
    if (meta.contentType.isEmpty() || !thumbs_->supports(meta.contentType))
        return {};
    const QString uri = dir_.child(entry.name).uri();
    if (thumbPending_.contains(uri))
        return {};
    const QString path = thumbs_->request(uri, meta.contentType, core::Thumbnailer::Flavor::Normal);
    if (path.isEmpty()) {
        thumbPending_.insert(uri, entry.name);
        return {};
    }
    const QIcon icon(path);
    thumbCache_.insert(entry.name, icon);
    return icon;
}

void ListModel::onThumbnail(const QString& uri, const QString& path)
{
    const QByteArray name = thumbPending_.take(uri);
    if (name.isEmpty())
        return;
    thumbCache_.insert(name, QIcon(path));
    const int row = rowOf(name);
    if (row >= 0)
        Q_EMIT dataChanged(index(row, NameColumn), index(row, NameColumn));
}

void ListModel::reset(const core::Location& dir)
{
    beginResetModel();
    dir_ = dir;
    std::vector<core::Entry>().swap(entries_);  // clear() would keep capacity
    std::vector<quint32>().swap(rows_);
    std::vector<quint8>().swap(marked_);
    markedCount_ = 0;
    rowOfName_.clear();
    metaCache_.clear();
    metaPending_.clear();
    dirCache_.clear();
    dirPending_.clear();
    thumbCache_.clear();
    thumbPending_.clear();
    ++metaGeneration_;
    meta_->cancelAll();
    sizer_->cancelAll();
    endResetModel();
    // Hand freed pages back so memory after leaving a big folder reflects
    // what is still held, not what glibc keeps around.
    malloc_trim(0);
}

void ListModel::append(std::vector<core::Entry> batch)
{
    if (batch.empty())
        return;
    core::sortByName(batch);

    std::vector<quint32> added;
    const auto base = static_cast<quint32>(entries_.size());
    for (quint32 i = 0; i < batch.size(); ++i)
        if (showHidden_ || !batch[i].hidden)
            added.push_back(base + i);
    entries_.insert(entries_.end(), std::make_move_iterator(batch.begin()),
                    std::make_move_iterator(batch.end()));
    marked_.resize(entries_.size(), 0);
    if (added.empty())
        return;

    const int first = static_cast<int>(rows_.size());
    beginInsertRows({}, first, first + static_cast<int>(added.size()) - 1);
    for (quint32 idx : added) {
        rowOfName_.insert(entries_[idx].name, static_cast<int>(rows_.size()));
        rows_.push_back(idx);
    }
    endInsertRows();
}

void ListModel::finishLoad()
{
    beginResetModel();
    core::sortByName(entries_);
    marked_.assign(entries_.size(), 0);  // sorted entries: marks are restored by name
    markedCount_ = 0;
    rebuildRows();
    endResetModel();
    Q_EMIT marksChanged();
}

void ListModel::setMarked(int row, bool on)
{
    quint8& m = marked_[rows_[row]];
    if ((m != 0) == on)
        return;
    m = on ? 1 : 0;
    markedCount_ += on ? 1 : -1;
    Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
    Q_EMIT marksChanged();
}

void ListModel::markAll(bool on)
{
    for (quint32 idx : rows_)
        marked_[idx] = on ? 1 : 0;
    markedCount_ = on ? static_cast<int>(rows_.size()) : 0;
    rowsRepaint();
}

void ListModel::invertMarks()
{
    markedCount_ = 0;
    for (quint32 idx : rows_) {
        marked_[idx] = marked_[idx] ? 0 : 1;
        markedCount_ += marked_[idx];
    }
    rowsRepaint();
}

QSet<QByteArray> ListModel::markedNames() const
{
    QSet<QByteArray> names;
    for (quint32 i = 0; i < entries_.size(); ++i)
        if (marked_[i])
            names.insert(entries_[i].name);
    return names;
}

void ListModel::applyMarks(const QSet<QByteArray>& names)
{
    markedCount_ = 0;
    for (quint32 i = 0; i < entries_.size(); ++i) {
        marked_[i] = names.contains(entries_[i].name) ? 1 : 0;
        if (marked_[i] && (showHidden_ || !entries_[i].hidden))
            ++markedCount_;
    }
    rowsRepaint();
}

void ListModel::rowsRepaint()
{
    if (!rows_.empty())
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
    Q_EMIT marksChanged();
}

void ListModel::setShowHidden(bool on)
{
    if (on == showHidden_)
        return;
    showHidden_ = on;
    beginResetModel();
    rebuildRows();
    endResetModel();
}

void ListModel::setRecursiveSizes(bool on)
{
    if (on == recursive_)
        return;
    recursive_ = on;
    pauseWork();
    if (!on) {
        // Finished walks keep their direct count and drop the rest, so
        // the folder shows what the plain mode would have found.
        for (auto& d : dirCache_)
            d.recursive = false;
    }
    rowsRepaint();
}

void ListModel::requestSize(int row)
{
    const core::Entry& e = entryAt(row);
    if (!e.isDir())
        return;
    const auto have = dirCache_.constFind(e.name);
    if (have != dirCache_.cend() && have->recursive)
        return;
    if (dirPending_.value(e.name, false))
        return;
    dirPending_.insert(e.name, true);
    sizer_->request(dir_, e.name, metaGeneration_, true);
}

void ListModel::pauseWork()
{
    sizer_->cancelAll();
    dirPending_.clear();
    for (auto it = dirCache_.begin(); it != dirCache_.end();) {
        if (it->complete)
            ++it;
        else
            it = dirCache_.erase(it);
    }
}

namespace {

// 0 prefix, 1 substring, 2 subsequence, -1 no match. Both already folded.
int matchRank(const QString& name, const QString& filter)
{
    if (name.startsWith(filter))
        return 0;
    if (name.contains(filter))
        return 1;
    qsizetype at = 0;
    for (const QChar c : filter) {
        at = name.indexOf(c, at);
        if (at < 0)
            return -1;
        ++at;
    }
    return 2;
}

} // namespace

void ListModel::rebuildRows()
{
    rows_.clear();
    rowOfName_.clear();
    const QString filter = filter_.toCaseFolded();
    std::vector<quint32> later[2];
    for (quint32 i = 0; i < entries_.size(); ++i) {
        if (!showHidden_ && entries_[i].hidden)
            continue;
        if (filter.isEmpty()) {
            rows_.push_back(i);
            continue;
        }
        const int rank = matchRank(entries_[i].display.toCaseFolded(), filter);
        if (rank == 0)
            rows_.push_back(i);
        else if (rank > 0)
            later[rank - 1].push_back(i);
    }
    for (const auto& group : later)
        rows_.insert(rows_.end(), group.begin(), group.end());
    for (quint32 row = 0; row < rows_.size(); ++row)
        rowOfName_.insert(entries_[rows_[row]].name, static_cast<int>(row));
}

void ListModel::setFilter(const QString& text)
{
    if (text == filter_)
        return;
    filter_ = text;
    beginResetModel();
    rebuildRows();
    endResetModel();
    // Marks on rows the filter hides still count; the status says so.
    Q_EMIT marksChanged();
}

void ListModel::markGlob(const QString& pattern, bool on)
{
    const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(pattern),
                                QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid())
        return;
    for (quint32 idx : rows_) {
        if (!re.match(entries_[idx].display).hasMatch())
            continue;
        if ((marked_[idx] != 0) != on) {
            marked_[idx] = on ? 1 : 0;
            markedCount_ += on ? 1 : -1;
        }
    }
    rowsRepaint();
}

Qt::ItemFlags ListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;  // empty space: into this folder
    Qt::ItemFlags f = QAbstractTableModel::flags(index) | Qt::ItemIsDragEnabled;
    if (index.column() == NameColumn)
        f |= Qt::ItemIsEditable;
    if (entryAt(index.row()).isDir())
        f |= Qt::ItemIsDropEnabled;
    return f;
}

QStringList ListModel::mimeTypes() const
{
    return {QStringLiteral("text/uri-list"), QStringLiteral("x-special/gnome-copied-files")};
}

std::vector<core::Location> ListModel::dragLocations(int row) const
{
    std::vector<core::Location> out;
    if (markedCount_ > 0) {
        for (quint32 idx : rows_)
            if (marked_[idx])
                out.push_back(dir_.child(entries_[idx].name));
    } else if (row >= 0 && row < rowCount()) {
        out.push_back(dir_.child(entryAt(row).name));
    }
    return out;
}

QMimeData* ListModel::mimeData(const QModelIndexList& indexes) const
{
    return uris::make(dragLocations(indexes.isEmpty() ? -1 : indexes.first().row()), false);
}

bool ListModel::canDropMimeData(const QMimeData* data, Qt::DropAction, int, int, const QModelIndex&) const
{
    return uris::holdsFiles(data);
}

bool ListModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int, int, const QModelIndex& parent)
{
    std::vector<core::Location> sources;
    bool cut = false;
    if (!uris::read(data, &sources, &cut))
        return false;
    core::Location target = dir_;
    if (parent.isValid() && entryAt(parent.row()).isDir())
        target = dir_.child(entryAt(parent.row()).name);
    for (const core::Location& s : sources)
        if (s == target || s.parent() == target)
            return false;  // onto itself or where it already is
    Q_EMIT dropRequested(sources, target, action == Qt::MoveAction);
    return true;
}

bool ListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.column() != NameColumn)
        return false;
    const core::Entry& e = entryAt(index.row());
    const QString newName = value.toString();
    if (newName.isEmpty() || newName == e.display || newName.contains(QLatin1Char('/')))
        return false;
    Q_EMIT renameRequested(e.name, newName);
    return true;
}

void ListModel::renameInPlace(const QByteArray& name, const QByteArray& newName, const QString& display)
{
    for (quint32 i = 0; i < entries_.size(); ++i) {
        core::Entry& e = entries_[i];
        if (e.name != name)
            continue;
        const int row = rowOf(name);
        e.name = newName;
        e.display = display;
        core::computeSortKey(e);
        metaCache_.remove(name);
        metaPending_.remove(name);
        dirCache_.remove(name);
        dirPending_.remove(name);
        if (row >= 0) {
            rowOfName_.remove(name);
            rowOfName_.insert(newName, row);
            Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
        }
        return;
    }
}

int ListModel::rowOf(const QByteArray& name) const
{
    return rowOfName_.value(name, -1);
}

int ListModel::hiddenCount() const
{
    int n = 0;
    for (const auto& e : entries_)
        n += e.hidden ? 1 : 0;
    return n;
}

int ListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int ListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const core::Entry& e = entryAt(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case NameColumn:
            return e.display;
        case SizeColumn: {
            if (e.isDir()) {
                const core::DirSize* d = dirSize(e);
                if (!d || !d->ok || !d->recursive)
                    return {};
                return QLocale().formattedDataSize(static_cast<qint64>(d->bytes), 1,
                                                   QLocale::DataSizeIecFormat);
            }
            const core::Metadata* m = metadata(e);
            if (!m || !m->ok)
                return {};
            return QLocale().formattedDataSize(static_cast<qint64>(m->size), 1,
                                               QLocale::DataSizeIecFormat);
        }
        case ItemsColumn: {
            if (!e.isDir())
                return {};
            const core::DirSize* d = dirSize(e);
            if (!d || !d->ok)
                return {};
            return QLocale().toString(static_cast<qint64>(d->recursive ? d->total() : d->direct));
        }
        case ModifiedColumn: {
            const core::Metadata* m = metadata(e);
            if (!m || !m->ok)
                return {};
            return QDateTime::fromSecsSinceEpoch(m->modified).toString("yyyy-MM-dd HH:mm");
        }
        default:
            return {};
        }
    case Qt::EditRole:
        return index.column() == NameColumn ? QVariant(e.display) : QVariant();
    case Qt::DecorationRole:
        if (index.column() == NameColumn)
            return icon(e);
        return {};
    case Qt::TextAlignmentRole:
        if (index.column() == SizeColumn || index.column() == ItemsColumn)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    case IsDirRole:
        return e.isDir();
    case PartialRole: {
        if (!e.isDir() || (index.column() != SizeColumn && index.column() != ItemsColumn))
            return false;
        const auto it = dirCache_.constFind(e.name);
        return it != dirCache_.cend() && !it->complete;
    }
    default:
        return {};
    }
}

QVariant ListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case NameColumn:
        return tr("Name");
    case SizeColumn:
        return tr("Size");
    case ItemsColumn:
        return tr("Items");
    case ModifiedColumn:
        return tr("Modified");
    default:
        return {};
    }
}

const core::Metadata* ListModel::metadata(const core::Entry& entry) const
{
    const auto it = metaCache_.constFind(entry.name);
    if (it != metaCache_.cend())
        return &it.value();
    if (!metaPending_.contains(entry.name)) {
        metaPending_.insert(entry.name);
        meta_->request(dir_, entry.name, metaGeneration_);
    }
    return nullptr;
}

const core::DirSize* ListModel::dirSize(const core::Entry& entry) const
{
    const auto it = dirCache_.constFind(entry.name);
    const core::DirSize* have = it != dirCache_.cend() ? &it.value() : nullptr;
    if (have && (have->recursive || !recursive_))
        return have;
    // Automatic work stays on local filesystems (rule 9); F3 may still
    // ask for a remote folder explicitly.
    if (!dir_.isNative())
        return have;
    const auto pending = dirPending_.constFind(entry.name);
    if (pending == dirPending_.cend() || (recursive_ && !pending.value())) {
        dirPending_.insert(entry.name, recursive_);
        sizer_->request(dir_, entry.name, metaGeneration_, recursive_);
    }
    return have;
}

void ListModel::onDirSize(quint64 generation, const QByteArray& name, const core::DirSize& size)
{
    if (generation != metaGeneration_)
        return;
    if (size.complete)
        dirPending_.remove(name);
    const auto have = dirCache_.constFind(name);
    if (have != dirCache_.cend() && have->recursive && !size.recursive)
        return;  // a count that arrived after the walk; the walk wins
    dirCache_.insert(name, size);
    const int row = rowOf(name);
    if (row >= 0)
        Q_EMIT dataChanged(index(row, SizeColumn), index(row, ItemsColumn));
}

QIcon ListModel::icon(const core::Entry& entry) const
{
    if (entry.isDir())
        return themeIcon({QStringLiteral("folder")});
    const core::Metadata* m = metadata(entry);
    if (!m || m->iconNames.isEmpty())
        return themeIcon({QStringLiteral("text-x-generic")});
    if (thumbnails_) {
        const QIcon thumb = thumbnail(entry, *m);
        if (!thumb.isNull())
            return thumb;
    }
    return themeIcon(m->iconNames);
}

QIcon ListModel::themeIcon(const QStringList& names) const
{
    const QString key = names.join(QLatin1Char(','));
    const auto it = iconCache_.constFind(key);
    if (it != iconCache_.cend())
        return it.value();
    QIcon icon;
    for (const QString& n : names) {
        if (QIcon::hasThemeIcon(n)) {
            icon = QIcon::fromTheme(n);
            break;
        }
    }
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("text-x-generic"));
    iconCache_.insert(key, icon);
    return icon;
}

void ListModel::onMetadata(quint64 generation, const QByteArray& name, const core::Metadata& meta)
{
    if (generation != metaGeneration_)
        return;
    metaPending_.remove(name);
    metaCache_.insert(name, meta);
    const int row = rowOf(name);
    if (row >= 0)
        Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
}
