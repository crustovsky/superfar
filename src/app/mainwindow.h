#pragma once

#include "core/jobs/job.h"
#include "core/location.h"
#include "keymap.h"

#include <QMainWindow>

namespace core {
class DirSizePool;
}
class CommandLine;
class FuncBar;
class JobStrip;
class Preview;
class Pane;
class Tab;
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
    void startTransfer(bool move);
    void promptMkdir();
    void confirmRemove(bool permanent);
    void runJob(core::Job* job, Tab* sourceTab, const core::Location& destination);
    void refreshAt(const core::Location& dir);
    void promptNewFile();
    void promptGlob(bool on);
    void openWith();
    void togglePreview();
    void placePreview();
    void runCommand(const QString& command);

    core::DirSizePool* sizes_;  // one worker pool for every tab (rule 9)
    Tree* tree_;
    QSplitter* panes_;
    Pane* left_;
    Pane* right_;
    Pane* active_;
    FuncBar* funcbar_;
    JobStrip* strip_;
    CommandLine* cmdline_;
    Preview* preview_ = nullptr;  // exists while Ctrl+Q is on
    bool painted_ = false;
};
