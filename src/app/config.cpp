#include "config.h"

#include "core/match.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>

namespace {
constexpr int kHistoryLimit = 500;

QList<Config::TabState> readTabs(QSettings& s, const QString& side)
{
    QList<Config::TabState> out;
    const int n = s.beginReadArray(QStringLiteral("session/") + side);
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        Config::TabState t;
        t.uri = s.value(QStringLiteral("uri")).toString();
        t.hidden = s.value(QStringLiteral("hidden"), false).toBool();
        t.locked = s.value(QStringLiteral("locked"), false).toBool();
        t.sizes = s.value(QStringLiteral("sizes"), false).toBool();
        t.thumbs = s.value(QStringLiteral("thumbs"), false).toBool();
        t.cursor = s.value(QStringLiteral("cursor")).toString();
        t.sort = s.value(QStringLiteral("sort")).toString();
        t.descending = s.value(QStringLiteral("descending"), false).toBool();
        if (!t.uri.isEmpty())
            out.append(t);
    }
    s.endArray();
    return out;
}

void writeTabs(QSettings& s, const QString& side, const QList<Config::TabState>& tabs)
{
    s.remove(QStringLiteral("session/") + side);
    s.beginWriteArray(QStringLiteral("session/") + side, static_cast<int>(tabs.size()));
    for (int i = 0; i < tabs.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("uri"), tabs[i].uri);
        s.setValue(QStringLiteral("hidden"), tabs[i].hidden);
        s.setValue(QStringLiteral("locked"), tabs[i].locked);
        s.setValue(QStringLiteral("sizes"), tabs[i].sizes);
        s.setValue(QStringLiteral("thumbs"), tabs[i].thumbs);
        if (!tabs[i].cursor.isEmpty())
            s.setValue(QStringLiteral("cursor"), tabs[i].cursor);
        if (!tabs[i].sort.isEmpty() && tabs[i].sort != QLatin1String("name"))
            s.setValue(QStringLiteral("sort"), tabs[i].sort);
        if (tabs[i].descending)
            s.setValue(QStringLiteral("descending"), true);
    }
    s.endArray();
}

// Keys the app writes on its own; they belong in the state file.
bool isState(const QString& key)
{
    if (key.startsWith(QLatin1String("session/")) || key.startsWith(QLatin1String("history/"))
        || key.startsWith(QLatin1String("window/")))
        return true;
    if (key == QLatin1String("layout/tree") || key == QLatin1String("layout/panes"))
        return true;
    return key.startsWith(QLatin1String("columns/")) && !key.endsWith(QLatin1String("/hidden"));
}

QSettings openConfig(const QString& path)
{
    return path.isEmpty() ? QSettings(QSettings::IniFormat, QSettings::UserScope,
                                      QStringLiteral("superfar"), QStringLiteral("superfar"))
                          : QSettings(path, QSettings::IniFormat);
}

QSettings openState(const QString& configPath, const QString& statePath)
{
    if (!statePath.isEmpty())
        return QSettings(statePath, QSettings::IniFormat);
    if (!configPath.isEmpty())
        return QSettings(QFileInfo(configPath).absolutePath() + QStringLiteral("/state.ini"), QSettings::IniFormat);
    return QSettings(QStandardPaths::writableLocation(QStandardPaths::StateLocation) + QStringLiteral("/state.ini"),
                     QSettings::IniFormat);
}
} // namespace

Config::Config(const QString& path, const QString& statePath, QObject* parent)
    : QObject(parent)
    , settings_(openConfig(path))
    , state_(openState(path, statePath))
{
    migrateState();
}

void Config::migrateState()
{
    // Versions before 0.6 kept everything in the config. Move the state
    // over once, so the config stops changing under the user.
    if (!state_.allKeys().isEmpty())
        return;
    bool moved = false;
    for (const QString& key : settings_.allKeys()) {
        if (!isState(key))
            continue;
        state_.setValue(key, settings_.value(key));
        settings_.remove(key);
        moved = true;
    }
    if (moved)
        sync();
}

QString Config::choice(const QString& group, const QString& key, const QString& fallback) const
{
    return state_.value(group + QLatin1Char('/') + key, fallback).toString();
}

void Config::setChoice(const QString& group, const QString& key, const QString& value)
{
    state_.setValue(group + QLatin1Char('/') + key, value);
}

QString Config::lastVersion() const
{
    return state_.value(QStringLiteral("about/lastVersion")).toString();
}

void Config::setLastVersion(const QString& version)
{
    state_.setValue(QStringLiteral("about/lastVersion"), version);
}

void Config::sync()
{
    settings_.sync();
    state_.sync();
}

QHash<QString, QStringList> Config::keyBindings() const
{
    QHash<QString, QStringList> out;
    settings_.beginGroup(QStringLiteral("keys"));
    for (const QString& key : settings_.childKeys()) {
        const QString value = settings_.value(key).toString();
        out.insert(key, value.isEmpty() ? QStringList() : value.split(QLatin1Char(';'), Qt::SkipEmptyParts));
    }
    settings_.endGroup();
    return out;
}

void Config::writeDefaultKeys(const QList<QPair<QString, QStringList>>& defaults,
                              const QList<QPair<QString, QString>>& retired)
{
    // Once per command: a command added in a later version gets its line
    // too, and nothing the user wrote is touched. A line that still reads
    // as an older default was not written by the user either.
    settings_.beginGroup(QStringLiteral("keys"));
    for (const auto& [name, seqs] : defaults) {
        const QString value = seqs.join(QLatin1Char(';'));
        if (!settings_.contains(name)) {
            settings_.setValue(name, value);
            continue;
        }
        for (const auto& [retiredName, old] : retired)
            if (retiredName == name && settings_.value(name).toString() == old && old != value)
                settings_.setValue(name, value);
    }
    settings_.endGroup();
}

Config::Columns Config::columns(const QString& side) const
{
    Columns c;
    for (QSettings* s : {&settings_, &state_}) {
        s->beginGroup(QStringLiteral("columns/") + side);
        for (const QString& key : s->childKeys()) {
            if (key == QLatin1String("hidden")) {
                if (s == &settings_)
                    c.hidden = s->value(key).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
            } else {
                c.widths.insert(key, s->value(key).toInt());  // the state, read last, wins
            }
        }
        s->endGroup();
    }
    return c;
}

void Config::setColumnWidth(const QString& side, const QString& column, int width)
{
    state_.setValue(QStringLiteral("columns/%1/%2").arg(side, column), width);
}

bool Config::stackedSplit() const
{
    return settings_.value(QStringLiteral("layout/split"), QStringLiteral("side-by-side")).toString()
        == QLatin1String("stacked");
}

bool Config::previewThirdSlot() const
{
    return settings_.value(QStringLiteral("layout/preview"), QStringLiteral("borrow")).toString()
        == QLatin1String("third");
}

QList<int> Config::splitterSizes(const QString& name) const
{
    QList<int> out;
    for (const QString& s : state_.value(QStringLiteral("layout/") + name).toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
        out.append(s.toInt());
    return out;
}

void Config::setSplitterSizes(const QString& name, const QList<int>& sizes)
{
    QStringList parts;
    for (int s : sizes)
        parts << QString::number(s);
    state_.setValue(QStringLiteral("layout/") + name, parts.join(QLatin1Char(',')));
}

QSize Config::windowSize() const
{
    return state_.value(QStringLiteral("window/size"), QSize()).toSize();
}

void Config::setWindowSize(const QSize& size)
{
    state_.setValue(QStringLiteral("window/size"), size);
}

Config::Session Config::session() const
{
    Session s;
    s.left = readTabs(state_, QStringLiteral("left"));
    s.right = readTabs(state_, QStringLiteral("right"));
    s.leftActive = state_.value(QStringLiteral("session/leftActive"), 0).toInt();
    s.rightActive = state_.value(QStringLiteral("session/rightActive"), 0).toInt();
    s.activePane = state_.value(QStringLiteral("session/activePane"), QStringLiteral("left")).toString();
    s.tree = state_.value(QStringLiteral("session/tree"), true).toBool();
    s.valid = !s.left.isEmpty() && !s.right.isEmpty();
    return s;
}

void Config::saveSession(const Session& s)
{
    writeTabs(state_, QStringLiteral("left"), s.left);
    writeTabs(state_, QStringLiteral("right"), s.right);
    state_.setValue(QStringLiteral("session/leftActive"), s.leftActive);
    state_.setValue(QStringLiteral("session/rightActive"), s.rightActive);
    state_.setValue(QStringLiteral("session/activePane"), s.activePane);
    state_.setValue(QStringLiteral("session/tree"), s.tree);
}

void Config::recordVisit(const QString& uri, qint64 now)
{
    if (uri.isEmpty())
        return;
    state_.beginGroup(QStringLiteral("history"));
    // Slashes would nest groups; keep the uri as a single key.
    const QString key = QString::fromLatin1(uri.toUtf8().toPercentEncoding("-._~:"));
    const QStringList old = state_.value(key).toString().split(QLatin1Char(','));
    const int count = old.size() == 2 ? old[0].toInt() + 1 : 1;
    state_.setValue(key, QStringLiteral("%1,%2").arg(count).arg(now));
    QStringList keys = state_.childKeys();
    if (keys.size() > kHistoryLimit) {
        // Drop the lowest-scoring entries down to the limit.
        QList<Visit> all;
        for (const QString& k : keys) {
            const QStringList v = state_.value(k).toString().split(QLatin1Char(','));
            all.append({k, v.value(0).toInt(), v.value(1).toLongLong()});
        }
        std::sort(all.begin(), all.end(), [now](const Visit& a, const Visit& b) { return frecency(a, now) < frecency(b, now); });
        for (int i = 0; i < all.size() - kHistoryLimit; ++i)
            state_.remove(all[i].uri);
    }
    state_.endGroup();
}

QList<Config::Visit> Config::history() const
{
    QList<Visit> out;
    state_.beginGroup(QStringLiteral("history"));
    for (const QString& key : state_.childKeys()) {
        const QStringList v = state_.value(key).toString().split(QLatin1Char(','));
        if (v.size() != 2)
            continue;
        out.append({QString::fromUtf8(QByteArray::fromPercentEncoding(key.toLatin1())), v[0].toInt(), v[1].toLongLong()});
    }
    state_.endGroup();
    return out;
}

double Config::frecency(const Visit& v, qint64 now)
{
    const qint64 age = std::max<qint64>(0, now - v.last);
    double weight = 10;
    if (age < 4 * 3600)
        weight = 100;
    else if (age < 24 * 3600)
        weight = 70;
    else if (age < 4 * 24 * 3600)
        weight = 50;
    else if (age < 14 * 24 * 3600)
        weight = 30;
    return v.count * weight;
}

QList<Config::Visit> Config::rank(const QList<Visit>& visits, const QString& text, qint64 now)
{
    struct Scored {
        Visit visit;
        int rank;
        double score;
    };
    QList<Scored> scored;
    const QString needle = text.toCaseFolded();
    for (const Visit& v : visits) {
        const int rank = core::matchRank(v.uri.toCaseFolded(), needle);
        if (rank >= 0)
            scored.append({v, rank, frecency(v, now)});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        return a.rank != b.rank ? a.rank < b.rank : a.score > b.score;
    });
    QList<Visit> out;
    for (const Scored& s : scored)
        out.append(s.visit);
    return out;
}
