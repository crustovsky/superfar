// The tree's roots and what happens to them: bookmarks, mounts and
// volumes from core, mounting a chosen volume or a typed remote URI,
// unmounting from the tree, and the credential and question bars that
// GMountOperation needs (rule 10). The window only navigates on request.
#pragma once

#include "core/location.h"

#include <QObject>
#include <functional>

namespace core {
class Bookmarks;
class Mounts;
}
class JobStrip;
class Tree;

class Places : public QObject {
    Q_OBJECT
public:
    Places(Tree* tree, JobStrip* strip, QObject* parent = nullptr);

    void rebuildRoots();
    // Ctrl+D: adds or removes the folder; says which in a message.
    void toggleBookmark(const core::Location& dir);
    void mountPlace(const QString& id);  // then openFolder() with its root
    void unmountCurrent();               // the mount under the tree cursor, after a confirm
    // Mounts whatever holds a remote location, then runs the continuation.
    void mountFor(const core::Location& dir, std::function<void()> then);

Q_SIGNALS:
    void openFolder(const core::Location& dir);
    void message(const QString& text);
    void leavingMount(const core::Location& root);  // move tabs out before an unmount

private:
    core::Mounts* mounts_;
    core::Bookmarks* bookmarks_;
    Tree* tree_;
    JobStrip* strip_;
};
