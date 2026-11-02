// One directory entry as the listing produces it. Only what the first
// paint and a sort need; the icon and content type come later per
// visible row. Size and mtime ride along because the local enumerator
// stats every entry anyway, so they cost no syscall, only 16 bytes.
#pragma once

#include <QByteArray>
#include <QString>

namespace core {

enum class EntryType : quint8 {
    Unknown,
    File,
    Directory,
    Symlink,  // a link whose target could not be resolved
    Special,  // device, socket, fifo, mountable
};

struct Entry {
    QByteArray name;     // raw bytes, what the filesystem knows
    QString display;     // for painting only
    QByteArray sortKey;  // natural collation key, computed once
    quint64 size = 0;    // bytes; a folder's is not meaningful
    qint64 modified = 0; // seconds since the epoch
    EntryType type = EntryType::Unknown;
    bool hidden = false;
    bool symlink = false;  // true also when the target resolved

    bool isDir() const { return type == EntryType::Directory; }
};

} // namespace core
