#include "sgr.h"

#include <algorithm>

namespace sgr {

namespace {

constexpr char kEsc = '\x1b';

// The terminal palette as xterm defines it: the 16 base colours by
// their bits, a 6x6x6 cube, a grey ramp. These are the standard's
// numbers, used only when the child names a colour by index; with
// COLORTERM=truecolor in its environment bat sends its theme's own.
quint32 indexed(int n)
{
    auto rgb = [](int r, int g, int b) { return static_cast<quint32>((r << 16) | (g << 8) | b); };
    if (n < 8)
        return rgb(n & 1 ? 205 : 0, n & 2 ? 205 : 0, n & 4 ? 205 : 0);
    if (n < 16)
        return rgb(n & 1 ? 255 : 85, n & 2 ? 255 : 85, n & 4 ? 255 : 85);
    if (n < 232) {
        const int c = n - 16;
        auto level = [](int v) { return v == 0 ? 0 : 55 + 40 * v; };
        return rgb(level(c / 36), level(c / 6 % 6), level(c % 6));
    }
    const int grey = 8 + 10 * (n - 232);
    return rgb(grey, grey, grey);
}

void apply(const std::vector<int>& codes, Style& style)
{
    for (size_t i = 0; i < codes.size(); ++i) {
        const int c = codes[i];
        // 38 and 48 take a colour after them: 5;n or 2;r;g;b.
        if (c == 38 || c == 48) {
            const int form = i + 1 < codes.size() ? codes[i + 1] : -1;
            const size_t take = form == 5 ? 2 : form == 2 ? 4 : 0;
            if (take == 0 || i + take >= codes.size())
                return;  // not a form known here: the rest cannot be read
            if (c == 38) {
                style.coloured = true;
                if (form == 5)
                    style.rgb = indexed(codes[i + 2] & 255);
                else
                    style.rgb = static_cast<quint32>(((codes[i + 2] & 255) << 16) | ((codes[i + 3] & 255) << 8)
                                                     | (codes[i + 4] & 255));
            }
            i += take;
        } else if (c == 0) {
            style = Style();
        } else if (c == 1) {
            style.bold = true;
        } else if (c == 3) {
            style.italic = true;
        } else if (c == 4) {
            style.underline = true;
        } else if (c == 22) {
            style.bold = false;
        } else if (c == 23) {
            style.italic = false;
        } else if (c == 24) {
            style.underline = false;
        } else if (c == 39) {
            style.coloured = false;
        } else if ((c >= 30 && c <= 37) || (c >= 90 && c <= 97)) {
            style.coloured = true;
            style.rgb = indexed(c >= 90 ? c - 90 + 8 : c - 30);
        }
    }
}

} // namespace

std::optional<std::vector<Span>> spans(const QByteArray& original, const QByteArray& coloured)
{
    std::vector<Span> out;
    Style style;
    qsizetype i = 0, j = 0;   // in coloured, in original
    qsizetype runStart = 0;   // in original: where the current style began
    qsizetype chars = 0;      // QString units before runStart
    // Decoding a stretch at a time is the same as decoding the whole
    // as long as no code sits inside a character; the count at the end
    // says whether one did.
    auto close = [&] {
        if (j == runStart)
            return;
        const qsizetype n = QString::fromUtf8(original.constData() + runStart, j - runStart).size();
        if (!style.plain()) {
            if (!out.empty() && out.back().style == style && out.back().start + out.back().length == chars)
                out.back().length += n;
            else
                out.push_back({chars, n, style});
        }
        chars += n;
        runStart = j;
    };
    while (i < coloured.size()) {
        const char c = coloured[i];
        const bool same = j < original.size() && original[j] == c;
        if (c == kEsc) {
            // The file's own escape byte, or the start of a code? There
            // is no telling, so such a file gets no colours.
            if (same)
                return std::nullopt;
            if (i + 1 >= coloured.size() || coloured[i + 1] != '[')
                return std::nullopt;
            qsizetype k = i + 2;
            std::vector<int> codes;
            int value = 0;
            bool digits = false, numeric = true;
            for (; k < coloured.size(); ++k) {
                const char p = coloured[k];
                if (p >= '0' && p <= '9') {
                    value = std::min(value * 10 + (p - '0'), 100000);
                    digits = true;
                } else if (p == ';') {
                    codes.push_back(value);
                    value = 0;
                    digits = false;
                } else if (p >= 0x40 && p <= 0x7e) {
                    break;  // the final byte
                } else {
                    numeric = false;  // a private or intermediate byte: not a code read here
                }
            }
            if (k >= coloured.size())
                break;  // cut off at the end: what came before stands
            if (digits || codes.empty())
                codes.push_back(value);  // "ESC[m" is a reset
            if (coloured[k] == 'm' && numeric) {
                close();
                apply(codes, style);
            }
            i = k + 1;
            continue;
        }
        if (!same)
            return std::nullopt;
        ++i;
        ++j;
    }
    if (j != original.size())
        return std::nullopt;
    close();
    if (chars != QString::fromUtf8(original).size())
        return std::nullopt;
    return out;
}

std::optional<std::vector<Span>> forDocument(std::vector<Span> spans, const QString& text, const QString& shown)
{
    auto isBreak = [](QChar c) {
        return c == QLatin1Char('\n') || c == QLatin1Char('\r') || c == QChar::ParagraphSeparator
               || c == QChar::LineSeparator;
    };
    std::vector<qsizetype> lost;  // positions in text of a CR whose LF shares its place in shown
    qsizetype at = 0;             // in shown
    for (qsizetype i = 0; i < text.size(); ++i, ++at) {
        if (text[i] == QLatin1Char('\r') && i + 1 < text.size() && text[i + 1] == QLatin1Char('\n')) {
            lost.push_back(i);
            ++i;
        }
        if (at >= shown.size())
            return std::nullopt;
        // A document keeps its own kind of line break; all else is as read.
        if (shown[at] != text[i] && !(isBreak(text[i]) && isBreak(shown[at])))
            return std::nullopt;
    }
    if (at != shown.size())
        return std::nullopt;
    if (lost.empty())
        return spans;
    auto moved = [&lost](qsizetype pos) {
        return pos - (std::upper_bound(lost.begin(), lost.end(), pos - 1) - lost.begin());
    };
    for (Span& span : spans) {
        const qsizetype end = moved(span.start + span.length);
        span.start = moved(span.start);
        span.length = end - span.start;
    }
    std::erase_if(spans, [](const Span& span) { return span.length <= 0; });
    return spans;
}

} // namespace sgr
