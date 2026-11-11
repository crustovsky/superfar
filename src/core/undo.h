// What an operation did, item by item, so that it can be taken back.
// Every job records its items as it works; the window keeps the last
// entries on a stack. Taking an entry back (core/jobs/undo.h) records
// the inverse items in turn, so redo is the undo of an undo and both
// directions are one piece of code.
//
// Nothing here deletes: what an operation created goes to the trash,
// what it trashed or overwrote comes back from there.
#pragma once

#include "core/location.h"

#include <QString>
#include <optional>
#include <vector>

namespace core {

struct UndoItem {
    enum class Kind : quint8 {
        Created,     // at was made: taken back by trashing it
        Trashed,     // at went to the trash at `time`: restored from there
        Moved,       // other became at (a move or a rename): moved back
        Folder,      // a move across filesystems made the folder at and
                     // removed the folder other (either may be invalid)
        Attributes,  // mode and owner of `below` under at changed
    };
    Kind kind = Kind::Created;
    Location at;
    Location other;
    QByteArray below;    // Attributes: the path below at, empty for at itself
    quint64 size = 0;    // Created, Moved: as the operation left the item,
    qint64 time = -1;    // to tell a later change; -1 when not known.
                         // Trashed: when, in milliseconds, to tell it from
                         // the same path trashed at another time
    quint32 mode[2] = {0, 0};  // Attributes: before and after
    quint32 uid[2] = {0, 0};
    quint32 gid[2] = {0, 0};

    // stat reads size and time from the item, which blocks: workers
    // always do, the main thread only for a local item.
    static UndoItem created(const Location& at, bool stat = true);
    static UndoItem moved(const Location& from, const Location& to, bool stat = true);
    static UndoItem trashed(const Location& was);
    static UndoItem folder(const Location& made, const Location& removed);
};

struct UndoEntry {
    QString what;  // "move of 12 items"
    std::vector<UndoItem> items;  // in the order they happened
};

// The window's two stacks. In memory, bounded, gone on quit.
class UndoStack {
public:
    enum class Side { Undo, Redo };
    static constexpr size_t kLimit = 50;

    // A new operation: it can be undone, and nothing can be redone.
    void record(UndoEntry entry);
    // The outcome of an undo or a redo; the other side stays.
    void put(Side side, UndoEntry entry);
    std::optional<UndoEntry> take(Side side);
    size_t count(Side side) const { return sides_[static_cast<int>(side)].size(); }

private:
    std::vector<UndoEntry> sides_[2];
};

} // namespace core
