#include "cmdline.h"

#include <QKeyEvent>

CommandLine::CommandLine(QWidget* parent)
    : QLineEdit(parent)
{
    setClearButtonEnabled(false);
    setFrame(true);
}

void CommandLine::setFolder(const QString& displayPath)
{
    setPlaceholderText(displayPath + QStringLiteral(" $"));
}

void CommandLine::keyPressEvent(QKeyEvent* event)
{
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (enter && (event->modifiers() & Qt::ControlModifier)) {
        Q_EMIT insertNameRequested();
        return;
    }
    if (enter) {
        const QString text = this->text().trimmed();
        if (!text.isEmpty()) {
            Q_EMIT run(text);
            clear();
        }
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT dismissed();
        return;
    }
    QLineEdit::keyPressEvent(event);
}
