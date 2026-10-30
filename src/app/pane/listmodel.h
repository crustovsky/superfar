// Table model over core's vector<Entry>. Rows are an index vector into the
// entries so hiding and sorting never copy entries, and nothing is
// allocated per row (rule 2). Size, date and icon are fetched when a row
// is first painted and cached by name (rule 1). Folders get their item
// count the same way; with recursive sizes on, a walk instead (rule 9).
#pragma once

#include "core/dirsize.h"
#include "core/entry.h"
#include "core/thumbnails.h"
#include "core/location.h"
#include "core/metadata.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QIcon>
#include <QSet>
#include <vector>

class ListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { NameColumn, SizeColumn, ItemsColumn, ModifiedColumn, ColumnCount };
    enum Role { PartialRole = Qt::UserRole + 1, IsDirRole };  // PartialRole: a total still being walked

    ListModel(core::DirSizePool* sizes, core::Thumbnailer* thumbs, QObject* parent = nullptr);

    // A new folder: drops entries and caches.
    void reset(const core::Location& dir);
    // Appends a batch while loading; the batch is sorted on its own.
    void append(std::vector<core::Entry> batch);
    // Known values for rows no query can answer (archive members).
    void presetMetadata(const QByteArray& name, const core::Metadata& meta);
    void presetDirCount(const QByteArray& name, quint64 direct);
    // The load is complete: one full sort and reset.
    void finishLoad();

    void setShowHidden(bool on);
    bool showHidden() const { return showHidden_; }

    // Quick filter: a row stays when the text matches its name as a
    // subsequence, case-insensitive; prefix matches sort first, then
    // substrings, then the rest, each group in name order.
    void setFilter(const QString& text);
    const QString& filter() const { return filter_; }
    // Marks every visible row whose name matches the glob.
    void markGlob(const QString& pattern, bool on);

    // Recursive sizes and counts for every folder row as it is painted.
    // Off, folders show their direct item count and only an explicit
    // requestSize() (F3) walks a tree.
    void setRecursiveSizes(bool on);
    bool recursiveSizes() const { return recursive_; }
    // Thumbnails from tumbler for rows whose type it supports; the view
    // grows its rows to show them.
    void setThumbnails(bool on);
    core::Thumbnailer* thumbnailer() const { return thumbs_; }
    bool thumbnails() const { return thumbnails_; }
    void requestSize(int row);
    // Stops walks and counts in flight; rows ask again when painted.
    void pauseWork();

    // Marks (the selection) live here, separate from the cursor. They are
    // per entry, so hiding rows keeps them; a load clears them and the
    // tab restores them by name.
    bool isMarked(int row) const { return marked_[rows_[row]] != 0; }
    void setMarked(int row, bool on);
    void toggleMarked(int row) { setMarked(row, !isMarked(row)); }
    void markAll(bool on);
    void invertMarks();
    int markedCount() const { return markedCount_; }
    QSet<QByteArray> markedNames() const;
    std::vector<QByteArray> markedNamesInOrder() const;  // list order, hidden included
    void applyMarks(const QSet<QByteArray>& names);

    // A rename that already happened on disk: the entry changes in place
    // and keeps its row until the next reload re-sorts, so an editor on
    // a neighbouring row survives.
    void renameInPlace(const QByteArray& name, const QByteArray& newName, const QString& display);

    int rowOf(const QByteArray& name) const;
    const core::Entry& entryAt(int row) const { return entries_[rows_[row]]; }
    const core::Entry& entryAtIndex(int index) const { return entries_[static_cast<size_t>(index)]; }  // hidden too
    int totalCount() const { return static_cast<int>(entries_.size()); }
    int hiddenCount() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    // Editing the name column asks for a rename; the tab performs it.
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;

    // Drag and drop: the marks travel, or the dragged row when nothing
    // is marked; a drop lands in the folder row under it or in this
    // folder. The tab runs the resulting job.
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction | Qt::MoveAction; }
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction | Qt::MoveAction; }
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;
    std::vector<core::Location> dragLocations(int row) const;

Q_SIGNALS:
    void marksChanged();
    void renameRequested(const QByteArray& name, const QString& newName);
    void dropRequested(const std::vector<core::Location>& sources, const core::Location& target, bool move);

private:
    void rebuildRows();
    void rowsRepaint();
    const core::Metadata* metadata(const core::Entry& entry) const;
    const core::DirSize* dirSize(const core::Entry& entry) const;
    void onDirSize(quint64 generation, const QByteArray& name, const core::DirSize& size);
    QIcon thumbnail(const core::Entry& entry, const core::Metadata& meta) const;
    void onThumbnail(const QString& uri, const QString& path);
    QIcon icon(const core::Entry& entry) const;
    QIcon themeIcon(const QStringList& names) const;
    void onMetadata(quint64 generation, const QByteArray& name, const core::Metadata& meta);

    core::Location dir_;
    std::vector<core::Entry> entries_;
    std::vector<quint32> rows_;  // visible entries, in display order
    std::vector<quint8> marked_;  // per entry
    int markedCount_ = 0;
    QHash<QByteArray, int> rowOfName_;
    bool showHidden_ = false;
    QString filter_;

    core::MetadataQuery* meta_;
    quint64 metaGeneration_ = 0;
    mutable QHash<QByteArray, core::Metadata> metaCache_;
    mutable QSet<QByteArray> metaPending_;
    mutable QHash<QString, QIcon> iconCache_;

    core::DirSizer* sizer_;
    core::Thumbnailer* thumbs_;
    bool recursive_ = false;
    bool thumbnails_ = false;
    mutable QHash<QByteArray, QIcon> thumbCache_;   // name -> icon, empty icon = none
    mutable QHash<QString, QByteArray> thumbPending_;  // uri -> name
    mutable QHash<QByteArray, core::DirSize> dirCache_;
    mutable QHash<QByteArray, bool> dirPending_;  // name -> recursive
};
