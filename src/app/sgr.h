// The colours of a highlighter that ran in a child (bat), read off its
// output. The child gets a file's bytes and prints them back with SGR
// escape codes in between. What is shown is always the bytes read from
// the file; the child's output only says which stretches get which
// colour. It is walked beside the original and has to match it byte
// for byte around its codes: on any difference there are no colours at
// all, so a hostile file, or a child that rewrites what it is given,
// cannot change what is displayed.
//
// Read: foreground colours (the 16, the 256 and 24-bit), bold, italic,
// underline, and the resets. Dropped: backgrounds and everything else.
// No widgets and no QtGui here, so it is tested as plain code.
#pragma once

#include <QByteArray>
#include <QString>
#include <optional>
#include <vector>

namespace sgr {

struct Style {
    quint32 rgb = 0;  // 0xRRGGBB, when coloured
    bool coloured = false;
    bool bold = false;
    bool italic = false;
    bool underline = false;

    bool plain() const { return !coloured && !bold && !italic && !underline; }
    bool operator==(const Style&) const = default;
};

struct Span {
    qsizetype start = 0;  // in QString units of the text decoded as UTF-8
    qsizetype length = 0;
    Style style;
};

// The styled stretches of original as coloured marks them, in order
// and not overlapping; plain stretches are left out. Nothing when
// coloured is not original with codes in between.
std::optional<std::vector<Span>> spans(const QByteArray& original, const QByteArray& coloured);

// The same spans for text as a text document holds it: a CR LF pair is
// one position there. Nothing when shown is not text with only that
// difference.
std::optional<std::vector<Span>> forDocument(std::vector<Span> spans, const QString& text, const QString& shown);

} // namespace sgr
