// Folder sync, step one: compare two trees. A worker walks both sides
// (never following symlinks), pairs entries by relative path and decides
// how each differs: present on one side only, newer on one side, a
// different size, or, when asked, different content by SHA-256. Folders
// that exist on both sides are descended, not listed; a folder on one
// side only is one row, to be copied whole.
#pragma once

#include "core/location.h"

#include <QObject>
#include <QString>
#include <memory>
#include <vector>

namespace core {

struct CompareOptions {
    bool size = true;
    bool mtime = true;
    bool hash = false;      // SHA-256 of files whose size matches; slow
    int mtimeSlackSeconds = 2;  // FAT and remotes round timestamps
};

enum class Difference : quint8 { Same, LeftOnly, RightOnly, LeftNewer, RightNewer, SizeDiffers, ContentDiffers };

struct CompareEntry {
    QByteArray relPath;   // raw bytes with '/' separators
    QString display;
    bool isDir = false;
    bool leftExists = false;
    bool rightExists = false;
    quint64 leftSize = 0;
    quint64 rightSize = 0;
    qint64 leftModified = 0;
    qint64 rightModified = 0;
    Difference difference = Difference::Same;
};

class Comparer : public QObject {
    Q_OBJECT
public:
    explicit Comparer(QObject* parent = nullptr);
    ~Comparer() override;

    void start(const Location& left, const Location& right, const CompareOptions& options, quint64 generation);
    void cancel();

Q_SIGNALS:
    void progress(quint64 generation, quint64 scanned);
    void finished(quint64 generation, std::vector<core::CompareEntry> entries);
    void failed(quint64 generation, const QString& message);

private:
    struct Run;
    std::shared_ptr<Run> run_;
};

} // namespace core
