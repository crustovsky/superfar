// A filesystem location, local or remote, backed by a GFile. Value type
// with reference semantics underneath; cheap to copy. This is the only way
// app refers to a place on disk, so GIO stays inside core.
#pragma once

#include <QByteArray>
#include <QString>

typedef struct _GFile GFile;

namespace core {

class Location {
public:
    Location() = default;
    explicit Location(GFile* borrowed);
    static Location adopt(GFile* owned);
    static Location fromPath(const QByteArray& nativePath);
    static Location fromUri(const QString& uri);
    static Location fromCommandLine(const QByteArray& arg);
    static Location home();
    static Location root();

    Location(const Location& other);
    Location(Location&& other) noexcept;
    Location& operator=(const Location& other);
    Location& operator=(Location&& other) noexcept;
    ~Location();

    bool isValid() const { return file_ != nullptr; }
    GFile* gfile() const { return file_; }

    QString uri() const;
    QByteArray path() const;      // native path bytes, empty when remote
    QString displayPath() const;  // for people: path when local, else uri
    QByteArray basename() const;  // raw name bytes
    QString displayName() const;
    Location parent() const;      // invalid at the top
    Location child(const QByteArray& name) const;
    bool isNative() const;

    // Path from ancestor down to here, raw bytes with '/' separators.
    // Null when this is not under ancestor, empty when they are equal.
    QByteArray relativePathFrom(const Location& ancestor) const;

    bool operator==(const Location& other) const;
    bool operator!=(const Location& other) const { return !(*this == other); }

private:
    GFile* file_ = nullptr;
};

} // namespace core
