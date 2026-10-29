#include "pane/pane.h"

#include "pane/tab.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

Pane::Pane(QWidget* parent)
    : QWidget(parent)
    , path_(new QLabel(this))
    , tabs_(new QStackedWidget(this))
    , status_(new QLabel(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(path_);
    layout->addWidget(tabs_, 1);
    layout->addWidget(status_);

    path_->setTextFormat(Qt::PlainText);
    path_->setContentsMargins(4, 2, 4, 2);
    status_->setTextFormat(Qt::PlainText);
    status_->setContentsMargins(4, 2, 4, 2);

    auto* tab = new Tab(this);
    tabs_->addWidget(tab);
    setFocusProxy(tab);

    connect(tab, &Tab::locationChanged, this, [this](const core::Location& dir) {
        path_->setText(dir.displayPath());
        Q_EMIT locationChanged(dir);
    });
    connect(tab, &Tab::statusChanged, status_, &QLabel::setText);
}

Tab* Pane::activeTab() const
{
    return qobject_cast<Tab*>(tabs_->currentWidget());
}
