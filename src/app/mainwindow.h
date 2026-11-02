#pragma once

#include "core/jobs/job.h"
#include "core/location.h"
#include "keymap.h"
#include "services.h"

#include <QMainWindow>

namespace core {
class DirSizePool;
class Thumbnailer;
}
class CommandLine;
class Config;
class FuncBar;
class JobStrip;
class Preview;
class Pane;
class Places;
class Tab;
class QSplitter;
class QTimer;
class Tree;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // Invalid locations mean "not given": the session or home decides.
    MainWindow(Config* config, const core::Location& left, const core::Location& right,
               QWidget* parent = nullptr);

    void execute(keymap::Command command);
    // For org.freedesktop.FileManager1: open the folder, or the folder
    // holding the item with the cursor on it, and raise the window.
    void showFolder(const core::Location& dir);
    void showItem(const core::Location& item);
    // For the dev aids in devaids.cpp.
    Pane* leftPane() const { return left_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void setActivePane(Pane* pane);
    Pane* otherPane() const { return active_ == left_ ? right_ : left_; }
    void togglePane(Pane* pane);
    void toggleTree();
    void showTree();
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
    void copyToClipboard(bool cut);
    void paste();
    void transfer(const std::vector<core::Location>& sources, const core::Location& target, bool move);
    void promptPath();
    void raiseWindow();
    void goTo();
    void saveSession();
    void saveLayout();

    Config* config_;
    core::DirSizePool* sizes_;  // one worker pool for every tab (rule 9)
    core::Thumbnailer* thumbs_;
    Services services_;
    Tree* tree_;
    QSplitter* panes_;
    QSplitter* split_;   // tree | panes
    QSplitter* vsplit_;  // above | bar area
    QWidget* previewSlot_;  // third slot, used when config says so
    Pane* left_;
    Pane* right_;
    Pane* active_;
    FuncBar* funcbar_;
    JobStrip* strip_;
    CommandLine* cmdline_;
    Places* places_;
    Preview* preview_ = nullptr;  // exists while Ctrl+Q is on
    QTimer* sessionTimer_;
    QList<int> treeSizes_;  // tree | panes while the tree is shown
    bool painted_ = false;
};
