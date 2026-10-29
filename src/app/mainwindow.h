#pragma once

#include "core/location.h"
#include "keymap.h"

#include <QMainWindow>

class FuncBar;
class Pane;
class QSplitter;
class Tree;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(const core::Location& left, const core::Location& right, QWidget* parent = nullptr);

    void execute(keymap::Command command);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setActivePane(Pane* pane);
    Pane* otherPane() const { return active_ == left_ ? right_ : left_; }
    void togglePane(Pane* pane);

    Tree* tree_;
    QSplitter* panes_;
    Pane* left_;
    Pane* right_;
    Pane* active_;
    FuncBar* funcbar_;
    bool painted_ = false;
};
