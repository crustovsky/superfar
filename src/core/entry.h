// One directory entry as the listing produces it. Only what the first
// paint and a sort need; the icon and content type come later per
// visible row. Size, mtime, mode and owner ride along because the local
// enumerator stats every entry anyway, so they cost no syscall, only
// 24 bytes (mode and the ids fill what was padding, bar 8).
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
    quint32 mode = 0;    // unix mode with its type bits; 0: the backend did not say
    quint32 uid = 0;     // these two mean something only with a mode
    quint32 gid = 0;
    EntryType type = EntryType::Unknown;
    bool hidden = false;
    bool symlink = false;  // true also when the target resolved

    bool isDir() const { return type == EntryType::Directory; }
};

} // namespace core
