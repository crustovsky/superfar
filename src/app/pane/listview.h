// The file list of one tab: a flat tree view over ListModel with the keys
// a pane needs. Cursor is the current index; marks come in milestone 2.
#pragma once

#include <QTreeView>

class ListView : public QTreeView {
    Q_OBJECT
public:
    explicit ListView(QWidget* parent = nullptr);

    // Fixed widths for the numeric columns from font metrics (rule 12).
    void sizeColumns();

Q_SIGNALS:
    void goUpRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    bool paintedRows_ = false;
};
