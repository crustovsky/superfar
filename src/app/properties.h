// Alt+Enter: properties of the marked items or the cursor item, shown in
// place of the pane's list (rule 10): names, type, size (recursive for
// folders through the dirsize pool), dates, owner, and permissions that
// can be changed and applied to every item.
#pragma once

#include "core/dirsize.h"
#include "core/location.h"
#include "core/metadata.h"

#include <QWidget>
#include <array>
#include <vector>

class QCheckBox;
class QLabel;

class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(core::DirSizePool* sizes, QWidget* parent = nullptr);

    void show(const std::vector<core::Location>& items);

Q_SIGNALS:
    void closed();
    void message(const QString& text);
    void changed();  // permissions applied

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void onDetails(const core::Details& d);
    void refresh();
    void apply();
    quint32 modeFromBoxes() const;

    std::vector<core::Location> items_;
    quint64 generation_ = 0;
    int pendingDetails_ = 0;
    int files_ = 0;
    int folders_ = 0;
    quint64 fileBytes_ = 0;
    core::DirSize folderTotals_;
    int foldersCounted_ = 0;
    core::Details first_;
    bool firstSeen_ = false;
    core::DirSizer* sizer_;

    QLabel* title_;
    QLabel* info_;
    std::array<QCheckBox*, 9> bits_{};
    QLabel* modeLabel_;
};
