// The part of the properties panel that changes things: nine permission
// boxes, an owner field and "the contents too". It shows what the items
// agree on: a bit they differ in is half checked and stays as each item
// has it until the box is set either way. Apply reports only what was
// changed here, so editing the owner never rewrites a mode.
#pragma once

#include "core/jobs/attributes.h"
#include "core/metadata.h"

#include <QWidget>
#include <array>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class AttributesEditor : public QWidget {
    Q_OBJECT
public:
    explicit AttributesEditor(QWidget* parent = nullptr);

    // A new set of items: clear(), add() each one's details, finish().
    void clear();
    void add(const core::Details& d);
    void finish();
    void offerQueue(bool on);

Q_SIGNALS:
    void applyRequested(const core::AttributeChange& change, bool queued);
    void closeRequested();
    void message(const QString& text);

private:
    void apply(bool queued);
    void showMode();

    std::array<QCheckBox*, 9> bits_{};
    QLabel* modeLabel_;
    QLineEdit* owner_;
    QCheckBox* recursive_;
    QLabel* recursiveNote_;
    QPushButton* queue_;
    QPushButton* applyBtn_;
    quint32 allSet_ = 0777;  // bits every item has
    quint32 anySet_ = 0;     // bits some item has
    QString user_;           // what the items share; empty when they differ
    QString group_;
    int count_ = 0;
    bool anyDir_ = false;
    bool modeTouched_ = false;
    bool ready_ = false;  // every item has answered
};
