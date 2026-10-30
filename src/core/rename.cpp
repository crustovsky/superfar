#include "core/rename.h"

#include <QRegularExpression>
#include <QSet>

namespace core {

namespace {

void splitExtension(const QString& name, QString* stem, QString* ext)
{
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0) {
        *stem = name;
        ext->clear();
    } else {
        *stem = name.left(dot);
        *ext = name.mid(dot);  // with the dot
    }
}

QString changeCase(const QString& s, RenameRule::Case c)
{
    switch (c) {
    case RenameRule::Case::Keep: return s;
    case RenameRule::Case::Lower: return s.toLower();
    case RenameRule::Case::Upper: return s.toUpper();
    case RenameRule::Case::Sentence: return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1).toLower();
    case RenameRule::Case::Title: {
        QString out = s.toLower();
        bool start = true;
        for (QChar& ch : out) {
            if (start && ch.isLetter())
                ch = ch.toUpper();
            start = !ch.isLetterOrNumber();
        }
        return out;
    }
    }
    return s;
}

// Wildcard pattern to an anchored regex with one group per * or ?.
QString wildcardRegex(const QString& pattern)
{
    QString re = QStringLiteral("^");
    for (const QChar c : pattern) {
        if (c == QLatin1Char('*'))
            re += QStringLiteral("(.*)");
        else if (c == QLatin1Char('?'))
            re += QStringLiteral("(.)");
        else
            re += QRegularExpression::escape(QString(c));
    }
    return re + QStringLiteral("$");
}

QString expandTokens(QString text, const RenameRule& rule, int index, const QString& stem, const QString& ext)
{
    const int number = rule.numberStart + index * rule.numberStep;
    text.replace(QStringLiteral("{n}"), QStringLiteral("%1").arg(number, rule.numberDigits, 10, QLatin1Char('0')),
                 Qt::CaseInsensitive);
    text.replace(QStringLiteral("{name}"), stem, Qt::CaseInsensitive);
    text.replace(QStringLiteral("{ext}"), ext.startsWith(QLatin1Char('.')) ? ext.mid(1) : ext, Qt::CaseInsensitive);
    return text;
}

} // namespace

QString RenameRule::serialize() const
{
    return QStringList{QString::number(static_cast<int>(mode)), pattern, replacement,
                       QString::number(ignoreExtension ? 1 : 0), QString::number(caseSensitive ? 1 : 0),
                       QString::number(static_cast<int>(caseChange)), QString::number(numberStart),
                       QString::number(numberStep), QString::number(numberDigits)}
        .join(QLatin1Char('\t'));
}

RenameRule RenameRule::deserialize(const QString& text)
{
    RenameRule r;
    const QStringList f = text.split(QLatin1Char('\t'));
    if (f.size() < 9)
        return r;
    r.mode = static_cast<Mode>(f[0].toInt());
    r.pattern = f[1];
    r.replacement = f[2];
    r.ignoreExtension = f[3] == QLatin1String("1");
    r.caseSensitive = f[4] == QLatin1String("1");
    r.caseChange = static_cast<Case>(f[5].toInt());
    r.numberStart = f[6].toInt();
    r.numberStep = f[7].toInt();
    r.numberDigits = f[8].toInt();
    return r;
}

QString applyRule(const RenameRule& rule, const QString& oldName, int index)
{
    QString stem, ext;
    if (rule.ignoreExtension)
        splitExtension(oldName, &stem, &ext);
    else
        stem = oldName;
    const QString fullStem = stem;

    QString out;
    const auto options = rule.caseSensitive ? QRegularExpression::NoPatternOption
                                            : QRegularExpression::CaseInsensitiveOption;
    switch (rule.mode) {
    case RenameRule::Mode::Wildcard: {
        const QRegularExpression re(wildcardRegex(rule.pattern), options);
        const QRegularExpressionMatch m = re.match(stem);
        if (!m.hasMatch())
            return oldName;
        // Each * or ? in the new name takes the next capture.
        int next = 1;
        for (const QChar c : rule.replacement) {
            if (c == QLatin1Char('*') || c == QLatin1Char('?'))
                out += next <= m.lastCapturedIndex() ? m.captured(next++) : QString();
            else
                out += c;
        }
        break;
    }
    case RenameRule::Mode::Regex: {
        const QRegularExpression re(rule.pattern, options);
        if (!re.isValid() || !re.match(stem).hasMatch())
            return oldName;
        out = stem;
        out.replace(re, rule.replacement);
        break;
    }
    case RenameRule::Mode::Replace: {
        if (rule.pattern.isEmpty() || !stem.contains(rule.pattern, rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive))
            return oldName;
        out = stem;
        out.replace(rule.pattern, rule.replacement, rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
        break;
    }
    }
    out = expandTokens(out, rule, index, fullStem, ext);
    out = changeCase(out, rule.caseChange);
    return out + (rule.ignoreExtension ? ext : QString());
}

RenamePlan planRename(const RenameRule& rule, const QStringList& oldNames, const QStringList& existing)
{
    RenamePlan plan;
    const QSet<QString> olds(oldNames.cbegin(), oldNames.cend());
    QSet<QString> others;
    for (const QString& e : existing)
        if (!olds.contains(e))
            others.insert(e);
    QHash<QString, int> seen;
    for (int i = 0; i < oldNames.size(); ++i) {
        const QString n = applyRule(rule, oldNames[i], i);
        plan.newNames << n;
        seen[n] += 1;
        if (n != oldNames[i])
            ++plan.changed;
    }
    plan.clash.resize(static_cast<size_t>(oldNames.size()));
    for (int i = 0; i < oldNames.size(); ++i) {
        const QString& n = plan.newNames[i];
        plan.clash[static_cast<size_t>(i)] = n.isEmpty() || n.contains(QLatin1Char('/')) || n == QLatin1String(".")
            || n == QLatin1String("..") || seen.value(n) > 1 || others.contains(n);
    }
    return plan;
}

} // namespace core
