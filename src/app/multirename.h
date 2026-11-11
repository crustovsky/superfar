// Shift+F2: the multi-rename panel, shown in place of the list (rule 10).
// A rule from core::rename is applied to the marked items with a live
// old / new table, a checkbox per row, clash marking, presets kept in
// the config and a cycle-safe apply. A batch is undone like any other
// operation, from the window's undo stack.
#pragma once

#include "core/location.h"
#include "core/rename.h"

#include <QWidget>
#include <vector>

class Config;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;

class MultiRenamePanel : public QWidget {
    Q_OBJECT
public:
    MultiRenamePanel(Config* config, QWidget* parent = nullptr);

    // The items to rename and every name in their folder, for clashes.
    void show(const std::vector<core::Location>& items, const QStringList& existing);

Q_SIGNALS:
    void closed();
    void message(const QString& text);
    void renamed(const QByteArray& firstNewName);  // a batch was applied
    // Every rename a batch made, in order, temporary names included:
    // what undo walks back.
    void batchDone(const std::vector<std::pair<core::Location, core::Location>>& steps);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    core::RenameRule ruleFromControls() const;
    void controlsFromRule(const core::RenameRule& rule);
    void refresh();
    void apply();
    void runBatch(std::vector<std::pair<core::Location, QString>> moves);
    // After a batch: the items are where the batch left them.
    void adoptLocations(const std::vector<std::pair<core::Location, QString>>& finals);
    void loadPresets();
    void savePreset();
    void deletePreset();

    Config* config_;
    std::vector<core::Location> items_;
    QStringList existing_;
    core::RenamePlan plan_;

    QComboBox* mode_;
    QLineEdit* pattern_;
    QLineEdit* replacement_;
    QCheckBox* ignoreExt_;
    QCheckBox* caseSensitive_;
    QComboBox* caseChange_;
    QSpinBox* start_;
    QSpinBox* step_;
    QSpinBox* digits_;
    QComboBox* presets_;
    QLineEdit* presetName_ = nullptr;
    QTreeWidget* table_;
    QLabel* summary_;
    QPushButton* apply_;
};
