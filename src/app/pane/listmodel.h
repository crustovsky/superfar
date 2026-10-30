// Table model over core's vector<Entry>. Rows are an index vector into the
// entries so hiding and sorting never copy entries, and nothing is
// allocated per row (rule 2). Size, date and icon are fetched when a row
// is first painted and cached by name (rule 1). Folders get their item
// count the same way; with recursive sizes on, a walk instead (rule 9).
#pragma once

#include "core/dirsize.h"
#include "core/entry.h"
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

    explicit ListModel(core::DirSizePool* sizes, QObject* parent = nullptr);

    // A new folder: drops entries and caches.
    void reset(const core::Location& dir);
    // Appends a batch while loading; the batch is sorted on its own.
    void append(std::vector<core::Entry> batch);
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
    void applyMarks(const QSet<QByteArray>& names);

    int rowOf(const QByteArray& name) const;
    const core::Entry& entryAt(int row) const { return entries_[rows_[row]]; }
    int totalCount() const { return static_cast<int>(entries_.size()); }
    int hiddenCount() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    // Editing the name column asks for a rename; the tab performs it.
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;

Q_SIGNALS:
    void marksChanged();
    void renameRequested(const QByteArray& name, const QString& newName);

private:
    void rebuildRows();
    void rowsRepaint();
    const core::Metadata* metadata(const core::Entry& entry) const;
    const core::DirSize* dirSize(const core::Entry& entry) const;
    void onDirSize(quint64 generation, const QByteArray& name, const core::DirSize& size);
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
    bool recursive_ = false;
    mutable QHash<QByteArray, core::DirSize> dirCache_;
    mutable QHash<QByteArray, bool> dirPending_;  // name -> recursive
};
