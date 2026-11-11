// Syntax colours for a text view, from bat in a child process. The
// view shows its plain text first; highlight() hands the same bytes to
// bat and, when it answers, lays its colours over the text (sgr.h). A
// reply for text since replaced is dropped. bat missing, too old,
// failing or printing anything but the text changes nothing: the view
// stays plain.
//
// The app holds no colour and no theme (rule 11): bat is told only
// whether the view is dark or light, and uses the theme the user set
// for that case in bat's own config.
#pragma once

#include "core/child.h"
#include "sgr.h"

#include <QSyntaxHighlighter>

class Config;

class Highlight : public QSyntaxHighlighter {
    Q_OBJECT
public:
    // Texts past this are left plain.
    static constexpr qsizetype kLimit = 1024 * 1024;

    Highlight(Config* config, QTextDocument* document);

    // The document now shows these bytes, decoded as UTF-8, read from
    // a file of this name. An empty name or text clears.
    void highlight(const QByteArray& bytes, const QByteArray& fileName, bool dark);
    void clear();
    // The switch (the ToggleHighlight command), kept in the state file.
    static bool enabled(const Config* config);
    static void setEnabled(Config* config, bool on);

    bool hasColours() const { return !spans_.empty(); }

protected:
    void highlightBlock(const QString& text) override;

private:
    Config* config_;
    core::Child child_;
    std::vector<sgr::Span> spans_;  // in document positions, in order
    quint64 generation_ = 0;
};
