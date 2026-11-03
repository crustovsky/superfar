// The marks a tab leaves behind when it goes somewhere else. Mark ten
// files, step into a subfolder or an archive to check something, come
// back: the cursor returns to the folder just left (rule 3) and so do
// the marks. The store is bounded to the last few folders of the tab,
// goes with the tab, and is never written to the session.
#pragma once

#include <QByteArray>
#include <QSet>
#include <utility>
#include <vector>

class MarkStore {
public:
    static constexpr int kLimit = 16;

    // Keeps names under key; an empty set forgets the key instead.
    void remember(const QByteArray& key, QSet<QByteArray> names)
    {
        forget(key);
        if (names.isEmpty())
            return;
        if (static_cast<int>(folders_.size()) >= kLimit)
            folders_.erase(folders_.begin());
        folders_.emplace_back(key, std::move(names));
    }

    // The names kept under key, removed from the store; empty when none.
    QSet<QByteArray> take(const QByteArray& key)
    {
        for (auto it = folders_.begin(); it != folders_.end(); ++it) {
            if (it->first == key) {
                QSet<QByteArray> names = std::move(it->second);
                folders_.erase(it);
                return names;
            }
        }
        return {};
    }

    void forget(const QByteArray& key)
    {
        std::erase_if(folders_, [&key](const auto& f) { return f.first == key; });
    }

    int size() const { return static_cast<int>(folders_.size()); }

private:
    std::vector<std::pair<QByteArray, QSet<QByteArray>>> folders_;  // oldest first
};
