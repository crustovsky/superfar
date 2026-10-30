// The Ctrl+Q preview: follows the cursor of the active tab and shows
// whatever fits (rule 8): an image through libglycin, a text head as
// plain bytes capped at 1 MiB, a folder's count and recursive size, and
// for anything else type, size, dates and permissions.
#pragma once

#include "core/dirsize.h"
#include "core/image.h"
#include "core/location.h"
#include "core/metadata.h"
#include "core/reader.h"

#include <QWidget>

class ImageView;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;

class Preview : public QWidget {
    Q_OBJECT
public:
    explicit Preview(core::DirSizePool* sizes, QWidget* parent = nullptr);

    void show(const core::Location& item);
    void clear();

private:
    void onDetails(const core::Details& d);
    void showImage(const core::ImageFrame& frame);
    QString detailsText(const core::Details& d) const;

    QString details_;  // the lines shown under the title for this item

    core::Location item_;
    quint64 generation_ = 0;
    core::ImageLoader* image_;
    core::Reader* reader_;
    core::DirSizer* sizer_;
    QLabel* title_;
    QLabel* info_;      // details, always above the content
    QStackedWidget* stack_;
    QWidget* empty_;
    ImageView* picture_;
    QPlainTextEdit* text_;
};
