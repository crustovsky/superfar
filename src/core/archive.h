// Archives as folders without a decoder in the process (rule 8): bsdtar
// lists and extracts in a child. The listing is the parsed output of
// "bsdtar -tvf" run under LC_ALL=C.UTF-8; a tab turns it into a folder view.
#pragma once

#include "core/location.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <memory>
#include <vector>

typedef struct _GCancellable GCancellable;
typedef struct _GError GError;

namespace core {

struct ArchiveEntry {
    QByteArray path;  // inside the archive, no trailing slash
    bool isDir = false;
    bool symlink = false;
    quint64 size = 0;
    qint64 modified = 0;
};

// By extension: zip, tar and its compressions, 7z, rar, iso, cpio, ar,
// deb, jar and friends.
bool isArchiveName(const QByteArray& name);

// The message for a failed bsdtar spawn: names the package when the
// program is missing, which is the common case.
QString bsdtarError(const GError* error);

// Parses "bsdtar -tvf" output (C.UTF-8 locale). Lines that do not fit are
// skipped. Exposed for tests.
std::vector<ArchiveEntry> parseArchiveListing(const QByteArray& output, int currentYear);

class ArchiveLister : public QObject {
    Q_OBJECT
public:
    explicit ArchiveLister(QObject* parent = nullptr);
    ~ArchiveLister() override;

    // Local files only: bsdtar needs a path.
    void start(const Location& archive, quint64 generation);
    void cancel();

Q_SIGNALS:
    void listed(quint64 generation, std::vector<core::ArchiveEntry> entries);
    void failed(quint64 generation, const QString& message);

private:
    struct Call;
    GCancellable* cancellable_ = nullptr;
};

} // namespace core
