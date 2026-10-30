// Ranking a name against typed text, for the quick filter and the Ctrl+G
// history. 0 prefix, 1 substring, 2 abbreviation, -1 no match.
#pragma once

#include <QString>

namespace core {

// An abbreviation matches when every typed character either continues
// the previous match or starts a word (punctuation counts anywhere):
// "dow", "bd" and "b-d" match bandcamp-download, "pl" does not. Both strings are compared case
// folded by the caller.
int matchRank(const QString& name, const QString& text);

} // namespace core
