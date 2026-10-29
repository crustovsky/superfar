// Table model over core's vector<Entry>. Rows are an index vector into the
// entries so hiding and sorting never copy entries, and nothing is
// allocated per row (rule 2). Size, date and icon are fetched when a row
// is first painted and cached by name (rule 1).
#pragma once

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
    enum Column { NameColumn, SizeColumn, ModifiedColumn, ColumnCount };

    explicit ListModel(QObject* parent = nullptr);

    // A new folder: drops entries and caches.
    void reset(const core::Location& dir);
    // Appends a batch while loading; the batch is sorted on its own.
    void append(std::vector<core::Entry> batch);
    // The load is complete: one full sort and reset.
    void finishLoad();

    void setShowHidden(bool on);
    bool showHidden() const { return showHidden_; }

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

Q_SIGNALS:
    void marksChanged();

private:
    void rebuildRows();
    void rowsRepaint();
    const core::Metadata* metadata(const core::Entry& entry) const;
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

    core::MetadataQuery* meta_;
    quint64 metaGeneration_ = 0;
    mutable QHash<QByteArray, core::Metadata> metaCache_;
    mutable QSet<QByteArray> metaPending_;
    mutable QHash<QString, QIcon> iconCache_;
};
