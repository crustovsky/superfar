#include "core/undo.h"

#include "core/gobject.h"

#include <QDateTime>

namespace core {

namespace {

void stamp(UndoItem& item)
{
    GPtr<GFileInfo> info(g_file_query_info(item.at.gfile(), "standard::type,standard::size,time::modified",
                                           G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, nullptr));
    if (!info)
        return;
    if (g_file_info_get_file_type(info.get()) == G_FILE_TYPE_REGULAR)
        item.size = static_cast<quint64>(g_file_info_get_size(info.get()));
    item.time = static_cast<qint64>(g_file_info_get_attribute_uint64(info.get(), G_FILE_ATTRIBUTE_TIME_MODIFIED));
}

} // namespace

UndoItem UndoItem::created(const Location& at, bool stat)
{
    UndoItem item;
    item.kind = Kind::Created;
    item.at = at;
    if (stat)
        stamp(item);
    return item;
}

UndoItem UndoItem::moved(const Location& from, const Location& to, bool stat)
{
    UndoItem item;
    item.kind = Kind::Moved;
    item.at = to;
    item.other = from;
    if (stat)
        stamp(item);
    return item;
}

UndoItem UndoItem::trashed(const Location& was)
{
    UndoItem item;
    item.kind = Kind::Trashed;
    item.at = was;
    item.time = QDateTime::currentMSecsSinceEpoch();
    return item;
}

UndoItem UndoItem::folder(const Location& made, const Location& removed)
{
    UndoItem item;
    item.kind = Kind::Folder;
    item.at = made;
    item.other = removed;
    return item;
}

void UndoStack::record(UndoEntry entry)
{
    sides_[static_cast<int>(Side::Redo)].clear();
    put(Side::Undo, std::move(entry));
}

void UndoStack::put(Side side, UndoEntry entry)
{
    if (entry.items.empty())
        return;
    auto& stack = sides_[static_cast<int>(side)];
    stack.push_back(std::move(entry));
    if (stack.size() > kLimit)
        stack.erase(stack.begin());
}

std::optional<UndoEntry> UndoStack::take(Side side)
{
    auto& stack = sides_[static_cast<int>(side)];
    if (stack.empty())
        return std::nullopt;
    UndoEntry entry = std::move(stack.back());
    stack.pop_back();
    return entry;
}

} // namespace core
