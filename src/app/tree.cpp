#include "tree.h"

#include "core/listing.h"
#include "core/sort.h"
#include "uris.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QSet>
#include <algorithm>

struct TreeModel::Node {
    enum State { Unloaded, Loading, Loaded };

    core::Location location;
    QByteArray name;
    QString display;
    QByteArray sortKey;
    Node* parent = nullptr;
    int row = 0;
    State state = Unloaded;
    std::vector<std::unique_ptr<Node>> children;
    std::vector<core::Entry> pending;
    core::Listing* listing = nullptr;
    std::unique_ptr<TreeModel::Root> root;  // set on top-level nodes
};

TreeModel::TreeModel(QObject* parent)
    : QAbstractItemModel(parent)
    , root_(std::make_unique<Node>())
    , folderIcon_(QIcon::fromTheme(QStringLiteral("folder")))
{
    root_->state = Node::Loaded;
}

namespace {
std::unique_ptr<TreeModel::Node> makeRootNode(TreeModel::Node* parent, const TreeModel::Root& r, int row)
{
    auto node = std::make_unique<TreeModel::Node>();
    node->location = r.location;
    node->name = r.key.toUtf8();
    node->display = r.name;
    node->parent = parent;
    node->row = row;
    node->root = std::make_unique<TreeModel::Root>(r);
    if (!r.location.isValid())
        node->state = TreeModel::Node::Loaded;  // nothing to expand until mounted
    return node;
}
} // namespace

void TreeModel::setRoots(const std::vector<Root>& roots)
{
    Node* top = root_.get();
    // Remove roots that are gone.
    for (int i = static_cast<int>(top->children.size()) - 1; i >= 0; --i) {
        bool keep = false;
        for (const Root& r : roots)
            keep = keep || r.key == top->children[i]->root->key;
        if (keep)
            continue;
        beginRemoveRows({}, i, i);
        top->children.erase(top->children.begin() + i);
        for (size_t j = i; j < top->children.size(); ++j)
            top->children[j]->row = static_cast<int>(j);
        endRemoveRows();
    }
    // Insert new ones at their position; update the rest in place.
    for (size_t pos = 0; pos < roots.size(); ++pos) {
        const Root& r = roots[pos];
        Node* have = nullptr;
        for (const auto& c : top->children)
            if (c->root->key == r.key)
                have = c.get();
        if (have) {
            const bool mountedNow = r.location.isValid() && !have->location.isValid();
            *have->root = r;
            have->display = r.name;
            have->location = r.location;
            if (mountedNow)
                have->state = Node::Unloaded;
            const QModelIndex idx = indexOf(have);
            Q_EMIT dataChanged(idx, idx);
            continue;
        }
        const int at = static_cast<int>(std::min(pos, top->children.size()));
        beginInsertRows({}, at, at);
        top->children.insert(top->children.begin() + at, makeRootNode(top, r, at));
        for (size_t j = at + 1; j < top->children.size(); ++j)
            top->children[j]->row = static_cast<int>(j);
        endInsertRows();
    }
}

const TreeModel::Root* TreeModel::rootAt(const QModelIndex& index) const
{
    if (!index.isValid())
        return nullptr;
    return nodeAt(index)->root.get();
}

QModelIndex TreeModel::rootFor(const core::Location& dir) const
{
    QModelIndex best;
    int bestDepth = -1;
    for (const auto& c : root_->children) {
        if (!c->location.isValid())
            continue;
        const QByteArray rel = dir.relativePathFrom(c->location);
        if (rel.isNull())
            continue;
        const int depth = rel.isEmpty() ? 1000 : 1000 - static_cast<int>(rel.count('/'));
        // The deepest root (fewest remaining components) wins.
        if (depth > bestDepth) {
            bestDepth = depth;
            best = indexOf(c.get());
        }
    }
    return best;
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
    return root_->children.empty() ? QModelIndex() : indexOf(root_->children.front().get());
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
        if (child->name == name)
            return indexOf(child.get());
    return {};
}

QModelIndex TreeModel::indexFor(const core::Location& dir) const
{
    QModelIndex idx = rootFor(dir);
    if (!idx.isValid())
        return {};
    const QByteArray rel = dir.relativePathFrom(locationAt(idx));
    if (rel.isNull())
        return {};
    for (const QByteArray& part : rel.split('/')) {
        if (part.isEmpty())
            continue;
        if (!isLoaded(idx))
            return {};
        idx = childNamed(idx, part);
        if (!idx.isValid())
            return {};
    }
    return idx;
}

void TreeModel::listDirs(Node* node, void (TreeModel::*done)(Node*, std::vector<core::Entry>))
{
    node->pending.clear();
    node->listing = new core::Listing(this);
    connect(node->listing, &core::Listing::batch, this,
            [node](quint64, std::vector<core::Entry> entries) {
                for (auto& e : entries)
                    if (e.isDir() && !e.hidden)
                        node->pending.push_back(std::move(e));
            });
    connect(node->listing, &core::Listing::finished, this,
            [this, node, done](quint64) { (this->*done)(node, std::move(node->pending)); });
    connect(node->listing, &core::Listing::failed, this,
            [this, node, done](quint64, const QString&, core::ListFailure) { (this->*done)(node, {}); });
    node->listing->start(node->location, 0);
}

void TreeModel::load(const QModelIndex& index)
{
    Node* node = nodeAt(index);
    if (node->state != Node::Unloaded)
        return;
    node->state = Node::Loading;
    listDirs(node, &TreeModel::onLoaded);
}

void TreeModel::refresh(const QModelIndex& index)
{
    Node* node = nodeAt(index);
    if (node->state != Node::Loaded || node->listing)
        return;
    listDirs(node, &TreeModel::onRefreshed);
}

namespace {
std::unique_ptr<TreeModel::Node> makeNode(TreeModel::Node* parent, const core::Entry& e, int row)
{
    auto child = std::make_unique<TreeModel::Node>();
    child->location = parent->location.child(e.name);
    child->name = e.name;
    child->display = e.display;
    child->sortKey = e.sortKey;
    child->parent = parent;
    child->row = row;
    return child;
}
} // namespace

void TreeModel::onLoaded(Node* node, std::vector<core::Entry> dirs)
{
    node->listing->deleteLater();
    node->listing = nullptr;
    node->pending.clear();
    core::sortByName(dirs);

    const QModelIndex parent = indexOf(node);
    if (!dirs.empty()) {
        beginInsertRows(parent, 0, static_cast<int>(dirs.size()) - 1);
        for (auto& e : dirs)
            node->children.push_back(makeNode(node, e, static_cast<int>(node->children.size())));
        node->state = Node::Loaded;
        endInsertRows();
    } else {
        node->state = Node::Loaded;
        Q_EMIT dataChanged(parent, parent);  // expander goes away
    }
    Q_EMIT loaded(parent);
}

void TreeModel::onRefreshed(Node* node, std::vector<core::Entry> dirs)
{
    node->listing->deleteLater();
    node->listing = nullptr;
    node->pending.clear();
    core::sortByName(dirs);
    const QModelIndex parent = indexOf(node);

    // Remove what is gone.
    QSet<QByteArray> fresh;
    for (const auto& e : dirs)
        fresh.insert(e.name);
    for (int i = static_cast<int>(node->children.size()) - 1; i >= 0; --i) {
        if (fresh.contains(node->children[i]->name))
            continue;
        beginRemoveRows(parent, i, i);
        node->children.erase(node->children.begin() + i);
        for (size_t j = i; j < node->children.size(); ++j)
            node->children[j]->row = static_cast<int>(j);
        endRemoveRows();
    }
    // Insert what is new, in sorted position.
    for (const auto& e : dirs) {
        bool have = false;
        for (const auto& c : node->children)
            if (c->name == e.name) {
                have = true;
                break;
            }
        if (have)
            continue;
        int pos = 0;
        while (pos < static_cast<int>(node->children.size())
               && node->children[pos]->sortKey < e.sortKey)
            ++pos;
        beginInsertRows(parent, pos, pos);
        node->children.insert(node->children.begin() + pos, makeNode(node, e, pos));
        for (size_t j = pos + 1; j < node->children.size(); ++j)
            node->children[j]->row = static_cast<int>(j);
        endInsertRows();
    }
    if (node->children.empty())
        Q_EMIT dataChanged(parent, parent);
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
    case Qt::DecorationRole: {
        const Node* node = nodeAt(index);
        return node->root && !node->root->iconNames.isEmpty() ? iconFor(node->root->iconNames) : folderIcon_;
    }
    default:
        return {};
    }
}

QIcon TreeModel::iconFor(const QStringList& names) const
{
    const QString key = names.join(QLatin1Char(','));
    auto it = icons_.constFind(key);
    if (it != icons_.cend())
        return it.value();
    QIcon icon;
    for (const QString& n : names)
        if (QIcon::hasThemeIcon(n)) {
            icon = QIcon::fromTheme(n);
            break;
        }
    if (icon.isNull())
        icon = folderIcon_;
    icons_.insert(key, icon);
    return icon;
}

Qt::ItemFlags TreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    if (nodeAt(index)->location.isValid())
        f |= Qt::ItemIsDropEnabled;
    return f;
}

QStringList TreeModel::mimeTypes() const
{
    return {QStringLiteral("text/uri-list"), QStringLiteral("x-special/gnome-copied-files")};
}

bool TreeModel::canDropMimeData(const QMimeData* data, Qt::DropAction, int, int, const QModelIndex& parent) const
{
    return parent.isValid() && nodeAt(parent)->location.isValid() && uris::holdsFiles(data);
}

bool TreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int, int, const QModelIndex& parent)
{
    std::vector<core::Location> sources;
    bool cut = false;
    if (!parent.isValid() || !uris::read(data, &sources, &cut))
        return false;
    const core::Location target = nodeAt(parent)->location;
    for (const core::Location& s : sources)
        if (s == target || s.parent() == target)
            return false;
    Q_EMIT dropRequested(sources, target, action == Qt::MoveAction);
    return true;
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
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(DropOnly);
    setAutoExpandDelay(600);
    connect(model_, &TreeModel::dropRequested, this, &Tree::dropRequested);

    connect(model_, &TreeModel::loaded, this, [this](const QModelIndex&) {
        if (pending_.isValid())
            continueReveal();
    });
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (guard_ || !current.isValid())
                    return;
                const core::Location loc = model_->locationAt(current);
                if (loc.isValid())
                    Q_EMIT folderChosen(loc);
                else if (const TreeModel::Root* r = model_->rootAt(current))
                    Q_EMIT placeChosen(r->placeId);
            });
}

void Tree::keyPressEvent(QKeyEvent* event)
{
    using keymap::Command;
    const Command cmd = keymap::lookup(event);
    switch (cmd) {
    case Command::None:
    case Command::Mark:
    case Command::MarkUp:
    case Command::MarkToggle:
    case Command::MarkAll:
    case Command::InvertMarks:
    case Command::GoUp:
    case Command::Activate:
        QTreeView::keyPressEvent(event);  // the tree's own keys
        return;
    default:
        event->accept();
        Q_EMIT commandRequested(cmd);
    }
}

const TreeModel::Root* Tree::currentPlace() const
{
    // The mount holding the cursor, however deep the cursor sits in it.
    QModelIndex idx = currentIndex();
    while (idx.parent().isValid())
        idx = idx.parent();
    const TreeModel::Root* r = model_->rootAt(idx);
    return r && !r->placeId.isEmpty() ? r : nullptr;
}

void Tree::reveal(const core::Location& dir)
{
    pending_ = dir;
    continueReveal();
}

void Tree::refresh(const core::Location& dir)
{
    const QModelIndex idx = model_->indexFor(dir);
    if (idx.isValid())
        model_->refresh(idx);
}

void Tree::continueReveal()
{
    if (!pending_.isValid())
        return;
    QModelIndex idx = model_->rootFor(pending_);
    if (!idx.isValid()) {
        pending_ = {};
        return;
    }
    const QByteArray rel = pending_.relativePathFrom(model_->locationAt(idx));
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
