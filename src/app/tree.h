// Folder tree: one for the window, folders only, loaded on expand. It
// follows the active tab and navigates it when the user picks a folder;
// a guard keeps the two from feeding each other (rule 14).
#pragma once

#include "core/entry.h"
#include "core/location.h"
#include "keymap.h"

#include <QAbstractItemModel>
#include <QIcon>
#include <QTreeView>
#include <memory>
#include <vector>

class TreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit TreeModel(QObject* parent = nullptr);
    ~TreeModel() override;

    QModelIndex rootIndex() const;
    core::Location locationAt(const QModelIndex& index) const;
    bool isLoaded(const QModelIndex& index) const;
    // Child with that raw name, or invalid when absent or not loaded.
    QModelIndex childNamed(const QModelIndex& parent, const QByteArray& name) const;
    // Starts loading the folder's children unless already loaded or loading.
    void load(const QModelIndex& index);

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool hasChildren(const QModelIndex& parent = {}) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

Q_SIGNALS:
    void loaded(const QModelIndex& index);

private:
    struct Node;
    Node* nodeAt(const QModelIndex& index) const;
    QModelIndex indexOf(Node* node) const;
    void onLoaded(Node* node, std::vector<core::Entry> dirs);

    std::unique_ptr<Node> root_;  // invisible; its children are the roots
    QIcon folderIcon_;
};

class Tree : public QTreeView {
    Q_OBJECT
public:
    explicit Tree(QWidget* parent = nullptr);

    // Expands down to dir and puts the cursor on it, loading on the way.
    void reveal(const core::Location& dir);

Q_SIGNALS:
    void folderChosen(const core::Location& dir);
    void commandRequested(keymap::Command command);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void continueReveal();

    TreeModel* model_;
    core::Location pending_;
    bool guard_ = false;
};
