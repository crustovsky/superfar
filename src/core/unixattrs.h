// Unix mode and ownership as text, and back. Nothing here touches a
// file: the listing supplies mode, uid and gid from the stat it does
// anyway, fileops and the attributes job apply a change.
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>

namespace core {

// "drwxr-sr-t": the type letter and the nine permission letters, with
// setuid, setgid and sticky the ls way (s and t with execute, S and T
// without). Empty for mode 0, which is "not known".
QString modeString(quint32 mode);

// The permission bits (07777) of "rwxr-xr-x" (a leading type letter is
// accepted and ignored) or of octal "755", "0755", "2755". Anything
// else is not a mode.
std::optional<quint32> parseMode(const QString& text);

// Names for numeric ids from the passwd and group databases, each
// looked up once. An id without a name is shown as its number.
class IdNames {
public:
    const QString& user(quint32 uid);
    const QString& group(quint32 gid);
    QString owner(quint32 uid, quint32 gid);  // "user:group"

private:
    QHash<quint32, QString> users_;
    QHash<quint32, QString> groups_;
};

// What to change: either half may be absent.
struct OwnerChange {
    std::optional<quint32> uid;
    std::optional<quint32> gid;

    bool isEmpty() const { return !uid && !gid; }
};

// "user:group", "user" or ":group", each a name or a number. On text
// that names nobody, nothing is returned and error says why.
std::optional<OwnerChange> parseOwner(const QString& text, QString* error = nullptr);

// Every name in the passwd and the group database, sorted, for
// completion.
QStringList userNames();
QStringList groupNames();

} // namespace core
