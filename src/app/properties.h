// Alt+Enter: properties of the marked items or the cursor item, shown in
// place of the pane's list (rule 10): names, type, size (recursive for
// folders through the dirsize pool), dates, and an editor for the
// permissions and the owner of every item, optionally down the folders.
#pragma once

#include "core/dirsize.h"
#include "core/jobs/attributes.h"
#include "core/location.h"
#include "core/metadata.h"

#include <QWidget>
#include <vector>

class AttributesEditor;
class QLabel;

class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(core::DirSizePool* sizes, QWidget* parent = nullptr);

    void show(const std::vector<core::Location>& items);
    // The same items again, after a change to them.
    void reread();
    // While other jobs are there: a Queue button beside Apply.
    void offerQueue(bool on);

Q_SIGNALS:
    void closed();
    void message(const QString& text);
    // Apply: the window runs it as a job and calls reread() at its end.
    void applyRequested(const std::vector<core::Location>& items, const core::AttributeChange& change, bool queued);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void onDetails(const core::Details& d);
    void refresh();

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
    AttributesEditor* editor_;
};
