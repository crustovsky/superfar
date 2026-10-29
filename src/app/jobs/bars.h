// In-window bars that stand in for dialogs (rule 10): a prompt with an
// optional text field, a conflict question, an error question, and the
// log shown when a job ends with skips or failures.
#pragma once

#include "core/jobs/job.h"

#include <QFrame>
#include <optional>

class QLineEdit;

class PromptBar : public QFrame {
    Q_OBJECT
public:
    PromptBar(const QString& message, const QString& okLabel, std::optional<QString> input,
              QWidget* parent = nullptr);

Q_SIGNALS:
    void accepted(const QString& text);
    void rejected();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QLineEdit* edit_ = nullptr;
};

class ConflictBar : public QFrame {
    Q_OBJECT
public:
    explicit ConflictBar(const core::Conflict& conflict, QWidget* parent = nullptr);

Q_SIGNALS:
    void answered(core::ConflictAction action, bool applyToAll);

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

class ErrorBar : public QFrame {
    Q_OBJECT
public:
    ErrorBar(const QString& verb, const core::Failure& failure, QWidget* parent = nullptr);

Q_SIGNALS:
    void answered(core::ErrorAction action, bool applyToAll);

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

class LogBar : public QFrame {
    Q_OBJECT
public:
    LogBar(const QString& title, const std::vector<core::LogEntry>& log, QWidget* parent = nullptr);

Q_SIGNALS:
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
};
