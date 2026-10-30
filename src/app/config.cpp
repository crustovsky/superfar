#include "config.h"

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
    }
    s.endArray();
}

int matchRank(const QString& hay, const QString& needle)
{
    if (hay.startsWith(needle))
        return 0;
    if (hay.contains(needle))
        return 1;
    qsizetype at = 0;
    for (const QChar c : needle) {
        at = hay.indexOf(c, at);
        if (at < 0)
            return -1;
        ++at;
    }
    return 2;
}
} // namespace

Config::Config(const QString& path, QObject* parent)
    : QObject(parent)
    , settings_(path.isEmpty() ? QSettings(QSettings::IniFormat, QSettings::UserScope,
                                           QStringLiteral("superfar"), QStringLiteral("superfar"))
                               : QSettings(path, QSettings::IniFormat))
{
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

void Config::writeDefaultKeys(const QList<QPair<QString, QStringList>>& defaults)
{
    if (settings_.childGroups().contains(QStringLiteral("keys")))
        return;
    settings_.beginGroup(QStringLiteral("keys"));
    for (const auto& [name, seqs] : defaults)
        settings_.setValue(name, seqs.join(QLatin1Char(';')));
    settings_.endGroup();
}

Config::Columns Config::columns(const QString& side) const
{
    Columns c;
    settings_.beginGroup(QStringLiteral("columns/") + side);
    for (const QString& key : settings_.childKeys()) {
        if (key == QLatin1String("hidden"))
            c.hidden = settings_.value(key).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        else
            c.widths.insert(key, settings_.value(key).toInt());
    }
    settings_.endGroup();
    return c;
}

void Config::setColumnWidth(const QString& side, const QString& column, int width)
{
    settings_.setValue(QStringLiteral("columns/%1/%2").arg(side, column), width);
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
    for (const QString& s : settings_.value(QStringLiteral("layout/") + name).toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
        out.append(s.toInt());
    return out;
}

void Config::setSplitterSizes(const QString& name, const QList<int>& sizes)
{
    QStringList parts;
    for (int s : sizes)
        parts << QString::number(s);
    settings_.setValue(QStringLiteral("layout/") + name, parts.join(QLatin1Char(',')));
}

QSize Config::windowSize() const
{
    return settings_.value(QStringLiteral("window/size"), QSize()).toSize();
}

void Config::setWindowSize(const QSize& size)
{
    settings_.setValue(QStringLiteral("window/size"), size);
}

Config::Session Config::session() const
{
    Session s;
    s.left = readTabs(settings_, QStringLiteral("left"));
    s.right = readTabs(settings_, QStringLiteral("right"));
    s.leftActive = settings_.value(QStringLiteral("session/leftActive"), 0).toInt();
    s.rightActive = settings_.value(QStringLiteral("session/rightActive"), 0).toInt();
    s.activePane = settings_.value(QStringLiteral("session/activePane"), QStringLiteral("left")).toString();
    s.valid = !s.left.isEmpty() && !s.right.isEmpty();
    return s;
}

void Config::saveSession(const Session& s)
{
    writeTabs(settings_, QStringLiteral("left"), s.left);
    writeTabs(settings_, QStringLiteral("right"), s.right);
    settings_.setValue(QStringLiteral("session/leftActive"), s.leftActive);
    settings_.setValue(QStringLiteral("session/rightActive"), s.rightActive);
    settings_.setValue(QStringLiteral("session/activePane"), s.activePane);
}

void Config::recordVisit(const QString& uri, qint64 now)
{
    if (uri.isEmpty())
        return;
    settings_.beginGroup(QStringLiteral("history"));
    // Slashes would nest groups; keep the uri as a single key.
    const QString key = QString::fromLatin1(uri.toUtf8().toPercentEncoding("-._~:"));
    const QStringList old = settings_.value(key).toString().split(QLatin1Char(','));
    const int count = old.size() == 2 ? old[0].toInt() + 1 : 1;
    settings_.setValue(key, QStringLiteral("%1,%2").arg(count).arg(now));
    QStringList keys = settings_.childKeys();
    if (keys.size() > kHistoryLimit) {
        // Drop the lowest-scoring entries down to the limit.
        QList<Visit> all;
        for (const QString& k : keys) {
            const QStringList v = settings_.value(k).toString().split(QLatin1Char(','));
            all.append({k, v.value(0).toInt(), v.value(1).toLongLong()});
        }
        std::sort(all.begin(), all.end(), [now](const Visit& a, const Visit& b) { return frecency(a, now) < frecency(b, now); });
        for (int i = 0; i < all.size() - kHistoryLimit; ++i)
            settings_.remove(all[i].uri);
    }
    settings_.endGroup();
}

QList<Config::Visit> Config::history() const
{
    QList<Visit> out;
    settings_.beginGroup(QStringLiteral("history"));
    for (const QString& key : settings_.childKeys()) {
        const QStringList v = settings_.value(key).toString().split(QLatin1Char(','));
        if (v.size() != 2)
            continue;
        out.append({QString::fromUtf8(QByteArray::fromPercentEncoding(key.toLatin1())), v[0].toInt(), v[1].toLongLong()});
    }
    settings_.endGroup();
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
        const int rank = needle.isEmpty() ? 0 : matchRank(v.uri.toCaseFolded(), needle);
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
