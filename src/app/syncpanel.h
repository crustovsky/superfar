// Shift+F5: folder sync between the two active tabs, shown in place of
// the active pane's list (rule 10). Compare lists the differences with
// a checkbox per row; the buttons run transfer and remove jobs for the
// checked rows in one direction or the other, through the job engine.
#pragma once

#include "core/compare.h"
#include "core/jobs/job.h"
#include "core/location.h"

#include <QWidget>
#include <vector>

class QCheckBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class SyncPanel : public QWidget {
    Q_OBJECT
public:
    explicit SyncPanel(QWidget* parent = nullptr);

    void show(const core::Location& left, const core::Location& right);

Q_SIGNALS:
    void closed();
    void message(const QString& text);
    // The panel builds jobs; the window runs them in the strip and calls
    // back so the panel can compare again when all are done.
    void runJobs(std::vector<core::Job*> jobs);

public Q_SLOTS:
    void compare();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class Direction { ToRight, ToLeft };
    void onFinished(quint64 generation, std::vector<core::CompareEntry> entries);
    void copy(Direction direction);
    void removeOrphans(bool onRight);
    std::vector<const core::CompareEntry*> checkedEntries() const;
    static QString describe(core::Difference d);

    core::Comparer* comparer_;
    core::Location left_, right_;
    quint64 generation_ = 0;
    std::vector<core::CompareEntry> entries_;

    QLabel* title_;
    QCheckBox* bySize_;
    QCheckBox* byTime_;
    QCheckBox* byHash_;
    QCheckBox* onlyDifferent_;
    QTreeWidget* table_;
    QLabel* summary_;
    QPushButton* toRight_;
    QPushButton* toLeft_;
    QPushButton* deleteRight_;
    QPushButton* deleteLeft_;
};
