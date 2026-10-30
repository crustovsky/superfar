#include "core/match.h"

namespace core {

namespace {

// Punctuation is a boundary itself and matches anywhere.
bool wordStart(const QString& s, qsizetype at)
{
    const QChar cur = s.at(at);
    if (at == 0 || !cur.isLetterOrNumber())
        return true;
    const QChar prev = s.at(at - 1);
    if (!prev.isLetterOrNumber())
        return true;
    return prev.isDigit() != cur.isDigit();
}

} // namespace

int matchRank(const QString& name, const QString& text)
{
    if (text.isEmpty() || name.startsWith(text))
        return 0;
    if (name.contains(text))
        return 1;
    qsizetype at = 0;
    bool first = true;
    for (const QChar c : text) {
        qsizetype pos = name.indexOf(c, at);
        while (pos >= 0 && !((pos == at && !first) || wordStart(name, pos)))
            pos = name.indexOf(c, pos + 1);
        if (pos < 0)
            return -1;
        at = pos + 1;
        first = false;
    }
    return 2;
}

} // namespace core
