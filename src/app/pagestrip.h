// The strip under the preview's details for content with an extent:
// pages (|<  <  Page 3 / 25  >  >|) or time (play, |<, <, a playhead,
// 0:12 / 1:30, >, >|). Hidden otherwise, so a plain image or text looks
// as it did. Nothing here takes focus: the list keeps the keyboard and
// the preview keys do the same as the buttons.
#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QSlider;
class QTimer;
class QToolButton;

enum class Step { First, Prev, Next, Last };

class PageStrip : public QWidget {
    Q_OBJECT
public:
    explicit PageStrip(QWidget* parent = nullptr);

    // Shows the strip for count > 1 at page (1-based), hides it otherwise.
    void setPages(int page, int count);
    // Shows the strip with a playhead; total 0 means unknown (no slider).
    void setTime(double position, double total, bool playing);
    void clear();

Q_SIGNALS:
    void stepRequested(Step step);
    void playToggled();
    void seekRequested(double fraction);  // of the total, 0 to 1

private:
    QToolButton* button(const char* themeName, int fallback);
    QHBoxLayout* layout_;
    QLabel* label_;
    QToolButton* play_;
    QSlider* slider_;
    QToolButton* first_;
    QToolButton* prev_;
    QToolButton* next_;
    QToolButton* last_;
    QTimer* seekTimer_;  // scrubbing asks once the hand rests
};
