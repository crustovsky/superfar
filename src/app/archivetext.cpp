#include "archivetext.h"

#include "counted.h"
#include "core/entry.h"
#include "core/sort.h"
#include "jobs/format.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <functional>

namespace archivetext {

namespace {
QString tr(const char* text, int n = -1)
{
    return QCoreApplication::translate("archivetext", text, nullptr, n);
}
} // namespace

QString summary(const core::ArchiveSummary& s, const core::ArchiveDropped& dropped, quint64 packedBytes)
{
    QStringList lines;
    QString sizes = counted(s.files, tr("file"), tr("files")) + QStringLiteral(", ")
        + counted(s.dirs, tr("folder"), tr("folders")) + QStringLiteral(", ")
        + tr("%1 unpacked").arg(jobfmt::bytes(s.bytes));
    if (packedBytes > 0)
        sizes += tr(" (%1 packed, %2x)").arg(jobfmt::bytes(packedBytes))
                     .arg(static_cast<double>(s.bytes) / static_cast<double>(packedBytes), 0, 'f', 1);
    lines << sizes;
    // The second form is the tarbomb warning: F5 would spill the members
    // into the destination.
    if (!s.topFolder.isEmpty())
        lines << tr("Extracts into one folder: %1/").arg(QString::fromUtf8(s.topFolder));
    else
        lines << tr("Extracts %1 into the current folder").arg(counted(s.topLevel, tr("item"), tr("items")));
    if (s.newest > 0)
        lines << tr("Newest member: %1").arg(QDateTime::fromSecsSinceEpoch(s.newest).toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    if (dropped.absolute > 0)
        lines << tr("Members with absolute paths: %1 (extracted relative)").arg(dropped.absolute);
    if (dropped.climbing > 0)
        lines << tr("Members climbing out with ..: %1 (refused)").arg(dropped.climbing);
    if (s.symlinks > 0)
        lines << tr("Symlinks: %1").arg(s.symlinks);
    if (s.executables > 0)
        lines << tr("Executable at the top level: %1").arg(s.executables);
    return lines.join(QLatin1Char('\n'));
}

QString tree(const std::vector<core::ArchiveEntry>& entries, int maxLines)
{
    // Children by parent path; a folder may be explicit, implied by the
    // paths below it, or both, and is listed once.
    QHash<QByteArray, std::vector<core::Entry>> children;
    QSet<QByteArray> seen;
    for (const core::ArchiveEntry& a : entries) {
        int start = 0;
        for (;;) {
            const int slash = a.path.indexOf('/', start);
            const QByteArray full = slash < 0 ? a.path : a.path.left(slash);
            if (!seen.contains(full)) {
                seen.insert(full);
                core::Entry e;
                e.name = full.mid(start);
                e.display = QString::fromUtf8(e.name);
                e.type = slash >= 0 || a.isDir ? core::EntryType::Directory : core::EntryType::File;
                e.size = slash < 0 ? a.size : 0;
                core::computeSortKey(e);
                children[start == 0 ? QByteArray() : full.left(start - 1)].push_back(std::move(e));
            }
            if (slash < 0)
                break;
            start = slash + 1;
        }
    }
    QStringList lines;
    std::function<void(const QByteArray&, int)> walk = [&](const QByteArray& dir, int depth) {
        auto it = children.find(dir);
        if (it == children.end())
            return;
        core::sortByName(it.value());
        for (const core::Entry& e : it.value()) {
            if (lines.size() >= maxLines)
                return;
            const QString indent(static_cast<qsizetype>(depth) * 2, QLatin1Char(' '));
            if (e.isDir()) {
                lines << QStringLiteral("%1  %2%3/").arg(QString(), 10).arg(indent, e.display);
                walk(dir.isEmpty() ? e.name : dir + '/' + e.name, depth + 1);
            } else {
                lines << QStringLiteral("%1  %2%3").arg(jobfmt::bytes(e.size), 10).arg(indent, e.display);
            }
        }
    };
    walk(QByteArray(), 0);
    const qsizetype more = seen.size() - lines.size();
    if (more > 0)
        lines << tr("and %1 more").arg(more);
    return lines.join(QLatin1Char('\n'));
}

} // namespace archivetext
