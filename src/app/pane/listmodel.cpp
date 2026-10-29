#include "pane/listmodel.h"

#include "core/sort.h"

#include <QDateTime>
#include <QLocale>

ListModel::ListModel(QObject* parent)
    : QAbstractTableModel(parent)
    , meta_(new core::MetadataQuery(this))
{
    connect(meta_, &core::MetadataQuery::result, this, &ListModel::onMetadata);
}

void ListModel::reset(const core::Location& dir)
{
    beginResetModel();
    dir_ = dir;
    entries_.clear();
    rows_.clear();
    rowOfName_.clear();
    metaCache_.clear();
    metaPending_.clear();
    ++metaGeneration_;
    meta_->cancelAll();
    endResetModel();
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
    rebuildRows();
    endResetModel();
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

void ListModel::rebuildRows()
{
    rows_.clear();
    rowOfName_.clear();
    for (quint32 i = 0; i < entries_.size(); ++i) {
        if (showHidden_ || !entries_[i].hidden) {
            rowOfName_.insert(entries_[i].name, static_cast<int>(rows_.size()));
            rows_.push_back(i);
        }
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
            if (e.isDir())
                return {};
            const core::Metadata* m = metadata(e);
            if (!m || !m->ok)
                return {};
            return QLocale().formattedDataSize(static_cast<qint64>(m->size), 1,
                                               QLocale::DataSizeIecFormat);
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
    case Qt::DecorationRole:
        if (index.column() == NameColumn)
            return icon(e);
        return {};
    case Qt::TextAlignmentRole:
        if (index.column() == SizeColumn)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return {};
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

QIcon ListModel::icon(const core::Entry& entry) const
{
    if (entry.isDir())
        return themeIcon({QStringLiteral("folder")});
    const core::Metadata* m = metadata(entry);
    if (!m || m->iconNames.isEmpty())
        return themeIcon({QStringLiteral("text-x-generic")});
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
