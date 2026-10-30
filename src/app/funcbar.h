// The function key bar: ten buttons labelled from the keymap, relabelled
// while Shift, Ctrl or Alt is held, with the held modifier named at the
// left end. Clicking one runs what that key would.
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

Q_SIGNALS:
    void commandRequested(keymap::Command command);

private:
    void relabel();
    static Qt::KeyboardModifier modifierOf(int key);

    QLabel* held_;
    QList<QToolButton*> buttons_;
    Qt::KeyboardModifiers mods_ = Qt::NoModifier;
};
