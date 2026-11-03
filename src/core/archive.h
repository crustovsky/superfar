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
    bool executable = false;  // the owner's x bit
    quint64 size = 0;
    qint64 modified = 0;
};

// Members the parser left out because extraction would not put them
// where their path says: bsdtar strips a leading slash and refuses "..".
struct ArchiveDropped {
    int absolute = 0;
    int climbing = 0;
};

// What the preview says about an archive before it is opened.
struct ArchiveSummary {
    quint64 files = 0;    // members that are not folders, links included
    quint64 dirs = 0;     // folders, explicit or implied by a path
    quint64 bytes = 0;    // unpacked
    qint64 newest = 0;    // the latest member time
    int topLevel = 0;     // what extraction puts into the destination
    QByteArray topFolder; // set when that is a single folder
    int symlinks = 0;
    int executables = 0;  // files with the x bit at the top level
};
ArchiveSummary summarizeArchive(const std::vector<ArchiveEntry>& entries);

// By extension: zip, tar and its compressions, 7z, rar, iso, cpio, ar,
// deb, jar and friends.
bool isArchiveName(const QByteArray& name);

// The message for a failed bsdtar spawn: names the package when the
// program is missing, which is the common case.
QString bsdtarError(const GError* error);

// Parses "bsdtar -tvf" output (C.UTF-8 locale). Lines that do not fit are
// skipped; dropped, when given, counts the members left out on purpose.
// Exposed for tests.
std::vector<ArchiveEntry> parseArchiveListing(const QByteArray& output, int currentYear,
                                              ArchiveDropped* dropped = nullptr);

class ArchiveLister : public QObject {
    Q_OBJECT
public:
    explicit ArchiveLister(QObject* parent = nullptr);
    ~ArchiveLister() override;

    // Local files only: bsdtar needs a path.
    void start(const Location& archive, quint64 generation);
    void cancel();

Q_SIGNALS:
    void listed(quint64 generation, std::vector<core::ArchiveEntry> entries, core::ArchiveDropped dropped);
    void failed(quint64 generation, const QString& message);

private:
    struct Call;
    GCancellable* cancellable_ = nullptr;
};

} // namespace core
