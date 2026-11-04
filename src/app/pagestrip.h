// The strip under the preview's details for paged content:
// |<  <  Page 3 / 25  >  >|. Hidden for anything that has one page or
// none, so a plain image or text looks as it did. The buttons take no
// focus: the list keeps the keyboard, the preview keys do the same.
#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

class PageStrip : public QWidget {
    Q_OBJECT
public:
    explicit PageStrip(QWidget* parent = nullptr);

    // Shows the strip for count > 1 at page (1-based), hides it otherwise.
    void setPages(int page, int count);
    int page() const { return page_; }
    int count() const { return count_; }

Q_SIGNALS:
    void pageRequested(int page);

private:
    QToolButton* button(const char* themeName, int fallback, const QString& tip);
    QLabel* label_;
    QToolButton* first_;
    QToolButton* prev_;
    QToolButton* next_;
    QToolButton* last_;
    int page_ = 0;
    int count_ = 0;
};
