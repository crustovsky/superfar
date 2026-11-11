#include "highlight.h"

#include "config.h"

#include <QTextDocument>
#include <algorithm>

namespace {
const QString kGroup = QStringLiteral("view");
const QString kKey = QStringLiteral("highlight");
} // namespace

Highlight::Highlight(Config* config, QTextDocument* document)
    : QSyntaxHighlighter(document)
    , config_(config)
{
}

bool Highlight::enabled(const Config* config)
{
    return config->choice(kGroup, kKey, QStringLiteral("true")) != QLatin1String("false");
}

void Highlight::setEnabled(Config* config, bool on)
{
    config->setChoice(kGroup, kKey, on ? QStringLiteral("true") : QStringLiteral("false"));
}

void Highlight::clear()
{
    ++generation_;
    child_.cancel();
    if (!spans_.empty()) {
        spans_.clear();
        rehighlight();
    }
}

void Highlight::highlight(const QByteArray& bytes, const QByteArray& fileName, bool dark)
{
    clear();
    if (!enabled(config_) || bytes.isEmpty() || bytes.size() > kLimit || fileName.isEmpty())
        return;
    // The name picks the language; bat reads the text from stdin and
    // never opens a file itself, so remote files work the same. The
    // style and the paging are pinned here, whatever bat's config says.
    const QByteArray name = "--file-name=" + fileName;
    const char* const argv[] = {"bat", "--color=always", "--style=plain", "--paging=never", "--tabs=0",
                                "--wrap=never", "--italic-text=always", dark ? "--theme=dark" : "--theme=light",
                                name.constData(), "-", nullptr};
    // Its theme's own colours, not their nearest of 256.
    const char* const env[] = {"COLORTERM", "truecolor", nullptr};
    child_.run(argv, [this, bytes, gen = generation_](const core::Child::Result& r) {
        // A warning on stderr is a bat that did not do as asked: one
        // before 0.25 does not know --theme=dark and falls back to a
        // theme that may not suit the view.
        if (gen != generation_ || !r.ok || !r.err.isEmpty())
            return;
        auto spans = sgr::spans(bytes, r.out);
        if (!spans)
            return;
        spans = sgr::forDocument(std::move(*spans), QString::fromUtf8(bytes), document()->toPlainText());
        if (!spans || spans->empty())
            return;
        spans_ = std::move(*spans);
        rehighlight();
    }, &bytes, env);
}

void Highlight::highlightBlock(const QString& text)
{
    if (spans_.empty())
        return;
    const qsizetype from = currentBlock().position();
    const qsizetype to = from + text.size();
    auto it = std::lower_bound(spans_.begin(), spans_.end(), from,
                               [](const sgr::Span& span, qsizetype pos) { return span.start + span.length <= pos; });
    for (; it != spans_.end() && it->start < to; ++it) {
        QTextCharFormat format;
        if (it->style.coloured)
            format.setForeground(QColor::fromRgb(it->style.rgb));
        if (it->style.bold)
            format.setFontWeight(QFont::Bold);
        if (it->style.italic)
            format.setFontItalic(true);
        if (it->style.underline)
            format.setFontUnderline(true);
        const qsizetype start = std::max(it->start, from);
        setFormat(static_cast<int>(start - from), static_cast<int>(std::min(it->start + it->length, to) - start), format);
    }
}
