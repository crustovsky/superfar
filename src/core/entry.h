// One directory entry as the listing produces it. Only what the first
// paint needs; size, date and icon come later per visible row.
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
    EntryType type = EntryType::Unknown;
    bool hidden = false;
    bool symlink = false;  // true also when the target resolved

    bool isDir() const { return type == EntryType::Directory; }
};

} // namespace core
