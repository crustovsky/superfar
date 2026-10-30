// The command line under the panes: Enter runs the text in a terminal in
// the active tab's folder, Ctrl+Enter inserts the cursor's name, Esc
// hands focus back to the list.
#pragma once

#include <QLineEdit>

class CommandLine : public QLineEdit {
    Q_OBJECT
public:
    explicit CommandLine(QWidget* parent = nullptr);

    void setFolder(const QString& displayPath);

Q_SIGNALS:
    void run(const QString& command);
    void insertNameRequested();
    void dismissed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
};
