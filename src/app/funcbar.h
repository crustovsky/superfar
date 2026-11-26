// The function key bar: ten buttons labelled from the keymap, relabelled
// while Shift, Ctrl or Alt is held, with the held modifier named at the
// left end. Clicking one runs what that key would. Where the widest of
// the four layers does not fit one row the bar takes two, F1 to F5 over
// F6 to F10, so it is as high with a modifier held as without.
#pragma once

#include "keymap.h"

#include <QList>
#include <QWidget>

class QLabel;
class QToolButton;

class FuncBar : public QWidget {
    Q_OBJECT
public:
    explicit FuncBar(QWidget* parent = nullptr);

    bool eventFilter(QObject* watched, QEvent* event) override;
    // What button index (0 for F1) paints: its label, cut at the end
    // where the bar is too narrow for it.
    QString shownLabel(int index) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

Q_SIGNALS:
    void commandRequested(keymap::Command command);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void relabel();
    // The width the widest layer asks for on one row; again when the
    // font or the style changes.
    void measure();
    void arrange();
    int rowHeight() const;
    int rows(int width) const;
    static QString labelOf(Qt::KeyboardModifiers mods, int index);
    static Qt::KeyboardModifier modifierOf(int key);

    QLabel* held_;
    QList<QToolButton*> buttons_;
    Qt::KeyboardModifiers mods_ = Qt::NoModifier;
    int oneRow_ = 0;
};
