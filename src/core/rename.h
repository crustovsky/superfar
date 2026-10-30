// Multi-rename rules, pure and testable. A rule maps an old name to a
// new one: wildcard (* and ? in the pattern are captures reused in the
// new name in order), regex (\1 style groups), or find and replace.
// {n} inserts a counter, {name} and {ext} the original stem and
// extension. The extension can be left alone, and the case changed last.
#pragma once

#include <QString>
#include <QStringList>
#include <vector>

namespace core {

struct RenameRule {
    enum class Mode { Wildcard, Regex, Replace };
    enum class Case { Keep, Lower, Upper, Title, Sentence };

    Mode mode = Mode::Wildcard;
    QString pattern = QStringLiteral("*");
    QString replacement = QStringLiteral("*");
    bool ignoreExtension = true;
    bool caseSensitive = false;
    Case caseChange = Case::Keep;
    int numberStart = 1;
    int numberStep = 1;
    int numberDigits = 1;

    // Serialised for presets: one line, fields separated by tabs.
    QString serialize() const;
    static RenameRule deserialize(const QString& text);
};

// The new name for the index-th item, or the old name when the pattern
// does not match. Index counts from zero.
QString applyRule(const RenameRule& rule, const QString& oldName, int index);

// New names for all old names, with clashes: a new name that repeats
// within the batch, or equals an existing name that is not being
// renamed, or is empty or holds a slash.
struct RenamePlan {
    QStringList newNames;      // same order as the input
    std::vector<bool> clash;   // per item
    int changed = 0;           // items whose name differs
};
RenamePlan planRename(const RenameRule& rule, const QStringList& oldNames, const QStringList& existing);

} // namespace core
