#include "core/unixattrs.h"

#include <QByteArray>

#include <algorithm>
#include <cerrno>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <vector>

namespace core {

namespace {

QChar typeLetter(quint32 mode)
{
    switch (mode & S_IFMT) {
    case S_IFDIR: return QLatin1Char('d');
    case S_IFLNK: return QLatin1Char('l');
    case S_IFCHR: return QLatin1Char('c');
    case S_IFBLK: return QLatin1Char('b');
    case S_IFIFO: return QLatin1Char('p');
    case S_IFSOCK: return QLatin1Char('s');
    default: return QLatin1Char('-');
    }
}

// The database calls want a buffer and say ERANGE when it is small.
template <typename Record, typename Call>
QString lookupName(Call call, char* Record::*field)
{
    std::vector<char> buffer(1024);
    for (;;) {
        Record record{};
        Record* found = nullptr;
        const int rc = call(&record, buffer.data(), buffer.size(), &found);
        if (rc == ERANGE && buffer.size() < (1u << 20)) {
            buffer.resize(buffer.size() * 4);
            continue;
        }
        return rc == 0 && found ? QString::fromLocal8Bit(found->*field) : QString();
    }
}

template <typename Record, typename Id, typename Call>
std::optional<quint32> lookupId(Call call, Id Record::*field)
{
    std::vector<char> buffer(1024);
    for (;;) {
        Record record{};
        Record* found = nullptr;
        const int rc = call(&record, buffer.data(), buffer.size(), &found);
        if (rc == ERANGE && buffer.size() < (1u << 20)) {
            buffer.resize(buffer.size() * 4);
            continue;
        }
        if (rc != 0 || !found)
            return std::nullopt;
        return static_cast<quint32>(found->*field);
    }
}

// A name or a number; nothing when the text is neither.
template <typename ByName>
std::optional<quint32> idOf(const QString& text, ByName byName)
{
    bool number = false;
    const quint32 id = text.toUInt(&number);
    if (number)
        return id;
    return byName(text.toLocal8Bit());
}

QStringList sortedUnique(QStringList names)
{
    names.sort();
    names.removeDuplicates();
    return names;
}

} // namespace

QString modeString(quint32 mode)
{
    if (mode == 0)
        return {};
    QString s(10, QLatin1Char('-'));
    s[0] = typeLetter(mode);
    static const char letters[] = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i)
        if (mode & (0400u >> i))
            s[i + 1] = QLatin1Char(letters[i]);
    // setuid, setgid and sticky sit in the three execute places.
    const struct { quint32 bit; int at; char with; char without; } special[] = {
        {S_ISUID, 3, 's', 'S'}, {S_ISGID, 6, 's', 'S'}, {S_ISVTX, 9, 't', 'T'}};
    for (const auto& sp : special)
        if (mode & sp.bit)
            s[sp.at] = QLatin1Char(s[sp.at] == QLatin1Char('x') ? sp.with : sp.without);
    return s;
}

std::optional<quint32> parseMode(const QString& input)
{
    QString text = input.trimmed();
    if (text.size() == 3 || text.size() == 4) {
        bool octal = true;
        for (const QChar c : text)
            octal = octal && c >= QLatin1Char('0') && c <= QLatin1Char('7');
        if (octal)
            return text.toUInt(nullptr, 8);
    }
    if (text.size() == 10 && QStringLiteral("-dlcbps").contains(text.at(0)))
        text.remove(0, 1);
    if (text.size() != 9)
        return std::nullopt;
    quint32 mode = 0;
    for (int i = 0; i < 9; ++i) {
        const char c = text.at(i).toLatin1();
        const quint32 bit = 0400u >> i;
        if (c == '-')
            continue;
        if (i % 3 == 0 && c == 'r') {
            mode |= bit;
        } else if (i % 3 == 1 && c == 'w') {
            mode |= bit;
        } else if (i % 3 == 2) {
            const quint32 special = i == 2 ? S_ISUID : i == 5 ? S_ISGID : S_ISVTX;
            const char with = i == 8 ? 't' : 's';
            const char without = i == 8 ? 'T' : 'S';
            if (c == 'x')
                mode |= bit;
            else if (c == with)
                mode |= bit | special;
            else if (c == without)
                mode |= special;
            else
                return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    return mode;
}

const QString& IdNames::user(quint32 uid)
{
    auto it = users_.find(uid);
    if (it == users_.end()) {
        QString name = lookupName<passwd>(
            [uid](passwd* r, char* b, size_t n, passwd** f) { return getpwuid_r(uid, r, b, n, f); }, &passwd::pw_name);
        it = users_.insert(uid, name.isEmpty() ? QString::number(uid) : name);
    }
    return it.value();
}

const QString& IdNames::group(quint32 gid)
{
    auto it = groups_.find(gid);
    if (it == groups_.end()) {
        QString name = lookupName<::group>(
            [gid](::group* r, char* b, size_t n, ::group** f) { return getgrgid_r(gid, r, b, n, f); }, &::group::gr_name);
        it = groups_.insert(gid, name.isEmpty() ? QString::number(gid) : name);
    }
    return it.value();
}

QString IdNames::owner(quint32 uid, quint32 gid)
{
    return user(uid) + QLatin1Char(':') + group(gid);
}

std::optional<OwnerChange> parseOwner(const QString& input, QString* error)
{
    const QString text = input.trimmed();
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    const QString user = (colon < 0 ? text : text.left(colon)).trimmed();
    const QString group = colon < 0 ? QString() : text.mid(colon + 1).trimmed();
    auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return std::nullopt;
    };
    OwnerChange change;
    if (!user.isEmpty()) {
        change.uid = idOf(user, [](const QByteArray& name) {
            return lookupId<passwd, uid_t>(
                [&name](passwd* r, char* b, size_t n, passwd** f) { return getpwnam_r(name.constData(), r, b, n, f); },
                &passwd::pw_uid);
        });
        if (!change.uid)
            return fail(QStringLiteral("No user named %1").arg(user));
    }
    if (!group.isEmpty()) {
        change.gid = idOf(group, [](const QByteArray& name) {
            return lookupId<::group, gid_t>(
                [&name](::group* r, char* b, size_t n, ::group** f) { return getgrnam_r(name.constData(), r, b, n, f); },
                &::group::gr_gid);
        });
        if (!change.gid)
            return fail(QStringLiteral("No group named %1").arg(group));
    }
    if (change.isEmpty())
        return fail(QStringLiteral("Give a user, a group, or user:group"));
    return change;
}

QStringList userNames()
{
    QStringList names;
    setpwent();
    while (const passwd* p = getpwent())
        names << QString::fromLocal8Bit(p->pw_name);
    endpwent();
    return sortedUnique(names);
}

QStringList groupNames()
{
    QStringList names;
    setgrent();
    while (const ::group* g = getgrent())
        names << QString::fromLocal8Bit(g->gr_name);
    endgrent();
    return sortedUnique(names);
}

} // namespace core
