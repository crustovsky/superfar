// The Ctrl+Q preview: follows the cursor of the active tab and shows
// whatever fits (rule 8): an image through libglycin, a text head as
// plain bytes capped at 1 MiB, a folder's count and recursive size, an
// archive's summary and the top of its tree from bsdtar's listing, a
// PDF's pages rendered by pdftoppm in a child, a video's frames decoded
// by ffmpeg in a child, and for anything else type, size, dates and
// permissions.
#pragma once

#include "core/archive.h"
#include "core/dirsize.h"
#include "core/image.h"
#include "core/location.h"
#include "core/metadata.h"
#include "core/reader.h"
#include "core/thumbnails.h"
#include "pagestrip.h"

#include <QFont>
#include <QWidget>

struct Services;

class Highlight;
class ImageView;
class PdfPages;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class VideoPlayer;

class Preview : public QWidget {
    Q_OBJECT
public:
    explicit Preview(const Services& services, QWidget* parent = nullptr);

    void show(const core::Location& item);
    void clear();

    // The step keys go to paged content (a PDF with more than one page)
    // or to a video; false when neither is shown, so the key keeps its
    // list meaning. Play toggles a video; false otherwise.
    bool step(Step step);
    bool togglePlay();
    // The text's syntax colours again: the switch was flipped, or the
    // theme changed.
    void rehighlight();

protected:
    void changeEvent(QEvent* event) override;

private:
    void onDetails(const core::Details& d);
    void showImage(const core::ImageFrame& frame);
    void showArchive(const std::vector<core::ArchiveEntry>& entries, const core::ArchiveDropped& dropped);
    void showThumbnail(const QString& contentType);
    void showPicture(const QImage& image);
    void addNote(const QString& text);
    QString detailsText(const core::Details& d) const;

    QString details_;  // the lines shown under the title for this item

    core::Location item_;
    quint64 generation_ = 0;
    core::ImageLoader* image_;
    core::Reader* reader_;
    core::DirSizer* sizer_;
    core::ArchiveLister* lister_;
    quint64 packedBytes_ = 0;  // the archive file's own size
    core::Thumbnailer* thumbs_;
    QString thumbUri_;  // the thumbnail awaited for the current item
    PdfPages* pdf_;
    VideoPlayer* video_;
    QLabel* title_;
    QLabel* info_;      // details, always above the content
    PageStrip* strip_;  // pages or transport, below the details
    QStackedWidget* stack_;
    QWidget* empty_;
    ImageView* picture_;
    QPlainTextEdit* text_;
    Highlight* colours_;
    QByteArray textBytes_;   // the text shown, when it is the whole file
    quint64 textSize_ = 0;   // the size of the file being read
    QFont textFont_;  // the text view's own; the archive tree needs a fixed pitch
};
