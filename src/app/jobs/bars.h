// In-window bars that stand in for dialogs (rule 10): a prompt with an
// optional text field, a conflict question, an error question, and the
// log shown when a job ends with skips or failures.
#pragma once

#include "core/jobs/job.h"

#include <QFrame>
#include <optional>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QListWidget;

class PromptBar : public QFrame {
    Q_OBJECT
public:
    PromptBar(const QString& message, const QString& okLabel, std::optional<QString> input,
              QWidget* parent = nullptr);

    // Adds Queue (Shift+Enter) beside the button: the job this prompt
    // is for can wait behind the ones already there.
    void offerQueue();
    // Whether accepted() came by Queue; for the slot it is emitted to.
    bool queued() const { return queued_; }

Q_SIGNALS:
    void accepted(const QString& text);
    void rejected();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLineEdit* edit_ = nullptr;
    QPushButton* ok_ = nullptr;
    bool queued_ = false;
};

// A short list to pick from, Enter chooses, Esc cancels.
class ChoiceBar : public QFrame {
    Q_OBJECT
public:
    ChoiceBar(const QString& message, const QStringList& items, QWidget* parent = nullptr);

Q_SIGNALS:
    void chosen(int index);
    void rejected();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QListWidget* list_;
};

// A list of switches, and an action as its last row: Space or Enter
// flips the row under the cursor, Shift+Up and Shift+Down ask to move
// it, Enter on the action runs it, Esc closes. What a key does happens
// at once; there is nothing to accept.
class CheckBar : public QFrame {
    Q_OBJECT
public:
    explicit CheckBar(const QString& message, QWidget* parent = nullptr);

    // Replaces the rows, keeping the cursor where it was.
    void setItems(const QStringList& labels, const QList<bool>& checked, const QString& action);
    void setCurrent(int index);

Q_SIGNALS:
    void toggled(int index, bool on);
    void moveRequested(int index, int delta);  // -1 up, +1 down; the owner refills
    void actionChosen();
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QListWidget* list_;
};

// A mount asking for credentials.
class AuthBar : public QFrame {
    Q_OBJECT
public:
    AuthBar(const QString& message, const QString& user, const QString& domain, bool needUser,
            bool needDomain, bool needPassword, bool anonymousOk, QWidget* parent = nullptr);

Q_SIGNALS:
    void answered(const QString& user, const QString& domain, const QString& password, bool anonymous);
    void rejected();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void submit();

    QLineEdit* user_ = nullptr;
    QLineEdit* domain_ = nullptr;
    QLineEdit* password_ = nullptr;
    QCheckBox* anonymous_ = nullptr;
};

// Ctrl+G: a text field over a list of folders that narrows as you type.
// Enter takes the highlighted entry, or the text itself when it is a
// path; Up and Down move in the list from the field.
class GoToBar : public QFrame {
    Q_OBJECT
public:
    explicit GoToBar(QWidget* parent = nullptr);

    // Replaces the list; the first entry is highlighted.
    void setEntries(const QStringList& entries);
    QString text() const;

Q_SIGNALS:
    void textChanged(const QString& text);
    void chosen(const QString& entryOrText);
    void rejected();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void accept();

    QLineEdit* edit_;
    QListWidget* list_;
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
