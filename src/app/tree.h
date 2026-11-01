// Folder tree: one for the window, folders only, loaded on expand. Its
// roots are the filesystem, the trash, the GTK bookmarks and every mount
// or mountable volume. It follows the active tab and navigates it when
// the user picks a folder; a guard keeps the two from feeding each other
// (rule 14).
#pragma once

#include "core/entry.h"
#include "core/location.h"
#include "keymap.h"

#include <QStringList>

#include <QAbstractItemModel>
#include <QHash>
#include <QIcon>
#include <QTreeView>
#include <memory>
#include <vector>

class TreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit TreeModel(QObject* parent = nullptr);
    ~TreeModel() override;

    struct Node;

    // A top-level entry. An unmounted volume has no location yet.
    struct Root {
        QString key;  // stable identity across refreshes
        QString name;
        QStringList iconNames;
        core::Location location;
        QString placeId;  // set for mounts and volumes
        bool canUnmount = false;
        bool canEject = false;
    };
    // Replaces the roots, keeping nodes whose key stays so expansion survives.
    void setRoots(const std::vector<Root>& roots);
    const Root* rootAt(const QModelIndex& index) const;  // null below the top level

    // The root that holds dir, by longest match; invalid when none does.
    QModelIndex rootFor(const core::Location& dir) const;
    QModelIndex rootIndex() const;  // the first root
    core::Location locationAt(const QModelIndex& index) const;
    bool isLoaded(const QModelIndex& index) const;
    // Child with that raw name, or invalid when absent or not loaded.
    QModelIndex childNamed(const QModelIndex& parent, const QByteArray& name) const;
    // Starts loading the folder's children unless already loaded or loading.
    void load(const QModelIndex& index);
    // Index of a location reached through loaded nodes only, else invalid.
    QModelIndex indexFor(const core::Location& dir) const;
    // Re-lists a loaded folder and merges: gone folders leave, new ones
    // slot in sorted, the rest keep their expansion.
    void refresh(const QModelIndex& index);

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool hasChildren(const QModelIndex& parent = {}) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction | Qt::MoveAction; }
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

Q_SIGNALS:
    void loaded(const QModelIndex& index);
    void dropRequested(const std::vector<core::Location>& sources, const core::Location& target, bool move);

private:
    Node* nodeAt(const QModelIndex& index) const;
    QModelIndex indexOf(Node* node) const;
    void onLoaded(Node* node, std::vector<core::Entry> dirs);
    void onRefreshed(Node* node, std::vector<core::Entry> dirs);
    void listDirs(Node* node, void (TreeModel::*done)(Node*, std::vector<core::Entry>));

    QIcon iconFor(const QStringList& names) const;

    std::unique_ptr<Node> root_;  // invisible; its children are the roots
    QIcon folderIcon_;
    mutable QHash<QString, QIcon> icons_;
};

class Tree : public QTreeView {
    Q_OBJECT
public:
    explicit Tree(QWidget* parent = nullptr);

    // Expands down to dir and puts the cursor on it, loading on the way.
    void reveal(const core::Location& dir);
    // Re-lists dir if the tree shows it.
    void refresh(const core::Location& dir);
    void setRoots(const std::vector<TreeModel::Root>& roots) { model_->setRoots(roots); }
    // The mount or volume holding the cursor, else null.
    const TreeModel::Root* currentPlace() const;

Q_SIGNALS:
    void folderChosen(const core::Location& dir);
    void placeChosen(const QString& placeId);  // an unmounted volume: mount it first
    void dropRequested(const std::vector<core::Location>& sources, const core::Location& target, bool move);
    void commandRequested(keymap::Command command);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void continueReveal();

    TreeModel* model_;
    core::Location pending_;
    bool guard_ = false;
};
