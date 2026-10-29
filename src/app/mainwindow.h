#pragma once

#include "core/location.h"

#include <QMainWindow>

class Pane;
class Tree;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const core::Location& start, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Tree* tree_;
    Pane* pane_;
    bool painted_ = false;
};
