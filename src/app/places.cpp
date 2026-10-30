#include "places.h"

#include "core/bookmarks.h"
#include "core/mounts.h"
#include "jobs/bars.h"
#include "jobs/strip.h"
#include "tree.h"

#include <QPointer>

Places::Places(Tree* tree, JobStrip* strip, QObject* parent)
    : QObject(parent)
    , mounts_(new core::Mounts(this))
    , bookmarks_(new core::Bookmarks(QByteArray(), this))
    , tree_(tree)
    , strip_(strip)
{
    connect(tree_, &Tree::placeChosen, this, [this](const QString& id) { mountPlace(id); });
    connect(mounts_, &core::Mounts::changed, this, &Places::rebuildRoots);
    connect(bookmarks_, &core::Bookmarks::changed, this, &Places::rebuildRoots);
    connect(mounts_, &core::Mounts::askPassword, this,
            [this](quint64 token, const QString& message, const QString& user, const QString& domain,
                   bool needUser, bool needDomain, bool needPassword, bool anonymousOk) {
                AuthBar* bar = strip_->auth(message, user, domain, needUser, needDomain, needPassword, anonymousOk);
                connect(bar, &AuthBar::answered, this,
                        [this, token](const QString& u, const QString& d, const QString& p, bool anon) {
                            mounts_->answerPassword(token, u, d, p, anon);
                        });
                connect(bar, &AuthBar::rejected, this, [this, token] { mounts_->abortPrompt(token); });
            });
    connect(mounts_, &core::Mounts::askQuestion, this,
            [this](quint64 token, const QString& message, const QStringList& choices) {
                ChoiceBar* bar = strip_->choose(message, choices);
                connect(bar, &ChoiceBar::chosen, this, [this, token](int i) { mounts_->answerQuestion(token, i); });
                connect(bar, &ChoiceBar::rejected, this, [this, token] { mounts_->abortPrompt(token); });
            });
    rebuildRoots();
}

void Places::rebuildRoots()
{
    // Bookmarks, mounts and the trash first; the filesystem last, since
    // it is usually expanded deep and would push the rest out of view.
    std::vector<TreeModel::Root> roots;
    for (const core::Bookmark& b : bookmarks_->list())
        roots.push_back({QStringLiteral("bookmark:") + b.location.uri(), b.name,
                         {QStringLiteral("folder-symbolic"), QStringLiteral("folder")}, b.location, QString(), false});
    for (const core::Place& p : mounts_->places())
        roots.push_back({p.id, p.name, p.iconNames, p.location, p.id, p.canUnmount});
    roots.push_back({QStringLiteral("trash"), tr("Trash"), {QStringLiteral("user-trash-symbolic"), QStringLiteral("user-trash")},
                     core::Location::fromUri(QStringLiteral("trash:///")), QString(), false});
    roots.push_back({QStringLiteral("fs"), tr("Filesystem"), {QStringLiteral("drive-harddisk-symbolic"), QStringLiteral("drive-harddisk")},
                     core::Location::root(), QString(), false});
    tree_->setRoots(roots);
}

void Places::mountPlace(const QString& id)
{
    QPointer<Places> guard(this);
    mounts_->mountPlace(id, [guard, id](const QString& error) {
        if (!guard)
            return;
        if (!error.isEmpty()) {
            Q_EMIT guard->message(error);
            return;
        }
        guard->rebuildRoots();
        for (const core::Place& p : guard->mounts_->places())
            if (p.id == id) {
                Q_EMIT guard->openFolder(p.location);
                return;
            }
    });
}

void Places::unmountCurrent()
{
    const TreeModel::Root* place = tree_->currentPlace();
    if (!place || !place->canUnmount)
        return;
    const QString id = place->placeId;
    const core::Location root = place->location;
    PromptBar* bar = strip_->prompt(tr("Unmount %1?").arg(place->name), tr("Unmount"), std::nullopt);
    connect(bar, &PromptBar::accepted, this, [this, id, root] {
        Q_EMIT leavingMount(root);  // tabs inside it move out first
        QPointer<Places> guard(this);
        mounts_->unmountPlace(id, [guard](const QString& error) {
            if (guard && !error.isEmpty())
                Q_EMIT guard->message(error);
        });
    });
}

void Places::mountFor(const core::Location& dir, std::function<void()> then)
{
    QPointer<Places> self(this);
    mounts_->mountLocation(dir, [self, then = std::move(then)](const QString& error) {
        if (!self)
            return;
        if (!error.isEmpty()) {
            Q_EMIT self->message(error);
            return;
        }
        then();
    });
}
