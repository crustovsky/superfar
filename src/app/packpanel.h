// Alt+F5: pack the marked items into a new archive, a panel in place of
// the list (rule 10). Name, format, level and destination; Enter packs,
// Esc closes. The choices other than the name come back next time from
// the state file.
#pragma once

#include "core/location.h"

#include <QWidget>

class Config;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class PackPanel : public QWidget {
    Q_OBJECT
public:
    explicit PackPanel(Config* config, QWidget* parent = nullptr);

    // what: the selection text; stem: the suggested name without an
    // extension; here and other: the two folders to choose between.
    void show(const QString& what, const QByteArray& stem, const core::Location& here, const core::Location& other);
    // While other jobs are there: a Queue button, and Shift+Enter.
    void offerQueue(bool on);

Q_SIGNALS:
    void closed();
    // The archive name with its extension, bsdtar's --options value
    // (empty for none) and the folder it goes into.
    void accepted(const QByteArray& archiveName, const QByteArray& options, const core::Location& destination,
                  bool queued);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void pack(bool queued);
    QByteArray extension() const;
    QByteArray options() const;

    Config* config_;
    core::Location here_;
    core::Location other_;
    QLabel* title_;
    QLineEdit* name_;
    QLabel* ext_;
    QComboBox* format_;
    QComboBox* level_;
    QComboBox* destination_;
    QPushButton* queue_;
};
