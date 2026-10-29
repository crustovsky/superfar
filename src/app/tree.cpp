#include "tree.h"

#include "core/listing.h"
#include "core/sort.h"

#include <QHeaderView>
#include <QKeyEvent>

struct TreeModel::Node {
    enum State { Unloaded, Loading, Loaded };

    core::Location location;
    QString display;
    Node* parent = nullptr;
    int row = 0;
    State state = Unloaded;
    std::vector<std::unique_ptr<Node>> children;
    std::vector<core::Entry> pending;
    core::Listing* listing = nullptr;
};

TreeModel::TreeModel(QObject* parent)
    : QAbstractItemModel(parent)
    , root_(std::make_unique<Node>())
    , folderIcon_(QIcon::fromTheme(QStringLiteral("folder")))
{
    root_->state = Node::Loaded;
    auto fs = std::make_unique<Node>();
    fs->location = core::Location::root();
    fs->display = QStringLiteral("/");
    fs->parent = root_.get();
    fs->row = 0;
    root_->children.push_back(std::move(fs));
}

TreeModel::~TreeModel() = default;

TreeModel::Node* TreeModel::nodeAt(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : root_.get();
}

QModelIndex TreeModel::indexOf(Node* node) const
{
    if (!node || node == root_.get())
        return {};
    return createIndex(node->row, 0, node);
}

QModelIndex TreeModel::rootIndex() const
{
    return indexOf(root_->children.front().get());
}

core::Location TreeModel::locationAt(const QModelIndex& index) const
{
    return index.isValid() ? nodeAt(index)->location : core::Location();
}

bool TreeModel::isLoaded(const QModelIndex& index) const
{
    return nodeAt(index)->state == Node::Loaded;
}

QModelIndex TreeModel::childNamed(const QModelIndex& parent, const QByteArray& name) const
{
    Node* node = nodeAt(parent);
    for (const auto& child : node->children)
        if (child->location.basename() == name)
            return indexOf(child.get());
    return {};
}

void TreeModel::load(const QModelIndex& index)
{
    Node* node = nodeAt(index);
    if (node->state != Node::Unloaded)
        return;
    node->state = Node::Loading;
    node->pending.clear();
    node->listing = new core::Listing(this);
    connect(node->listing, &core::Listing::batch, this,
            [node](quint64, std::vector<core::Entry> entries) {
                for (auto& e : entries)
                    if (e.isDir() && !e.hidden)
                        node->pending.push_back(std::move(e));
            });
    connect(node->listing, &core::Listing::finished, this,
            [this, node](quint64) { onLoaded(node, std::move(node->pending)); });
    connect(node->listing, &core::Listing::failed, this,
            [this, node](quint64, const QString&) { onLoaded(node, {}); });
    node->listing->start(node->location, 0);
}

void TreeModel::onLoaded(Node* node, std::vector<core::Entry> dirs)
{
    node->listing->deleteLater();
    node->listing = nullptr;
    node->pending.clear();
    core::sortByName(dirs);

    const QModelIndex parent = indexOf(node);
    if (!dirs.empty()) {
        beginInsertRows(parent, 0, static_cast<int>(dirs.size()) - 1);
        for (auto& e : dirs) {
            auto child = std::make_unique<Node>();
            child->location = node->location.child(e.name);
            child->display = e.display;
            child->parent = node;
            child->row = static_cast<int>(node->children.size());
            node->children.push_back(std::move(child));
        }
        node->state = Node::Loaded;
        endInsertRows();
    } else {
        node->state = Node::Loaded;
        Q_EMIT dataChanged(parent, parent);  // expander goes away
    }
    Q_EMIT loaded(parent);
}

QModelIndex TreeModel::index(int row, int column, const QModelIndex& parent) const
{
    Node* node = nodeAt(parent);
    if (column != 0 || row < 0 || row >= static_cast<int>(node->children.size()))
        return {};
    return createIndex(row, column, node->children[row].get());
}

QModelIndex TreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return {};
    return indexOf(nodeAt(child)->parent);
}

int TreeModel::rowCount(const QModelIndex& parent) const
{
    return static_cast<int>(nodeAt(parent)->children.size());
}

int TreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant TreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    switch (role) {
    case Qt::DisplayRole:
        return nodeAt(index)->display;
    case Qt::DecorationRole:
        return folderIcon_;
    default:
        return {};
    }
}

bool TreeModel::hasChildren(const QModelIndex& parent) const
{
    Node* node = nodeAt(parent);
    return node->state != Node::Loaded || !node->children.empty();
}

bool TreeModel::canFetchMore(const QModelIndex& parent) const
{
    return nodeAt(parent)->state == Node::Unloaded;
}

void TreeModel::fetchMore(const QModelIndex& parent)
{
    load(parent);
}

Tree::Tree(QWidget* parent)
    : QTreeView(parent)
    , model_(new TreeModel(this))
{
    setModel(model_);
    setHeaderHidden(true);
    setUniformRowHeights(true);
    setSelectionMode(SingleSelection);
    setEditTriggers(NoEditTriggers);
    setExpandsOnDoubleClick(true);
    setTabKeyNavigation(true);  // so Tab reaches keyPressEvent
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    connect(model_, &TreeModel::loaded, this, [this](const QModelIndex&) {
        if (pending_.isValid())
            continueReveal();
    });
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (!guard_ && current.isValid())
                    Q_EMIT folderChosen(model_->locationAt(current));
            });
}

void Tree::keyPressEvent(QKeyEvent* event)
{
    using keymap::Command;
    const Command cmd = keymap::lookup(event);
    switch (cmd) {
    case Command::None:
    case Command::Mark:
    case Command::MarkToggle:
    case Command::MarkAll:
    case Command::InvertMarks:
    case Command::GoUp:
        QTreeView::keyPressEvent(event);  // the tree's own keys
        return;
    default:
        event->accept();
        Q_EMIT commandRequested(cmd);
    }
}

void Tree::reveal(const core::Location& dir)
{
    pending_ = dir;
    continueReveal();
}

void Tree::continueReveal()
{
    if (!pending_.isValid())
        return;
    QModelIndex idx = model_->rootIndex();
    const QByteArray rel = pending_.relativePathFrom(model_->locationAt(idx));
    if (rel.isNull()) {
        pending_ = {};
        return;
    }
    for (const QByteArray& part : rel.split('/')) {
        if (part.isEmpty())
            continue;
        if (!model_->isLoaded(idx)) {
            model_->load(idx);
            return;  // continues from loaded()
        }
        const QModelIndex child = model_->childNamed(idx, part);
        if (!child.isValid())
            break;  // hidden or gone: stop at the deepest visible folder
        expand(idx);
        idx = child;
    }
    guard_ = true;
    setCurrentIndex(idx);
    scrollTo(idx);
    guard_ = false;
    pending_ = {};
}
