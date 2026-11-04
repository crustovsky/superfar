// The Ctrl+Q preview: follows the cursor of the active tab and shows
// whatever fits (rule 8): an image through libglycin, a text head as
// plain bytes capped at 1 MiB, a folder's count and recursive size, an
// archive's summary and the top of its tree from bsdtar's listing, a
// PDF's pages rendered by pdftoppm in a child, and for anything else
// type, size, dates and permissions.
#pragma once

#include "core/archive.h"
#include "core/dirsize.h"
#include "core/image.h"
#include "core/location.h"
#include "core/metadata.h"
#include "core/reader.h"
#include "core/thumbnails.h"

#include <QFont>
#include <QWidget>

struct Services;

class ImageView;
class PdfPages;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;

class Preview : public QWidget {
    Q_OBJECT
public:
    explicit Preview(const Services& services, QWidget* parent = nullptr);

    void show(const core::Location& item);
    void clear();

    // Paged content (a PDF with more than one page) takes the page keys.
    int pageCount() const;
    int page() const;
    void goToPage(int page);  // clamped to the document

private:
    void onDetails(const core::Details& d);
    void showImage(const core::ImageFrame& frame);
    void showArchive(const std::vector<core::ArchiveEntry>& entries, const core::ArchiveDropped& dropped);
    void showThumbnail(const QString& contentType);
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
    QLabel* title_;
    QLabel* info_;      // details, always above the content
    QStackedWidget* stack_;
    QWidget* empty_;
    ImageView* picture_;
    QPlainTextEdit* text_;
    QFont textFont_;  // the text view's own; the archive tree needs a fixed pitch
};
