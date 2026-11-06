#include "ownercompletion.h"

#include "core/unixattrs.h"

#include <QCompleter>
#include <QLineEdit>
#include <QStringListModel>

void completeOwners(QLineEdit* edit)
{
    auto* model = new QStringListModel(edit);
    auto* completer = new QCompleter(model, edit);
    completer->setCompletionMode(QCompleter::InlineCompletion);
    completer->setCaseSensitivity(Qt::CaseSensitive);
    edit->setCompleter(completer);
    // The candidates are whole texts: users, or what is typed up to the
    // colon followed by each group. The databases are read on the first
    // key that needs them.
    auto fill = [model, users = QStringList(), groups = QStringList(), shown = QString(QChar(0))](const QString& text) mutable {
        const qsizetype colon = text.indexOf(QLatin1Char(':'));
        const QString prefix = colon < 0 ? QString() : text.left(colon + 1);
        if (prefix == shown)
            return;
        shown = prefix;
        if (colon < 0) {
            if (users.isEmpty())
                users = core::userNames();
            model->setStringList(users);
            return;
        }
        if (groups.isEmpty())
            groups = core::groupNames();
        QStringList candidates;
        for (const QString& g : groups)
            candidates << prefix + g;
        model->setStringList(candidates);
    };
    QObject::connect(edit, &QLineEdit::textEdited, edit, fill);
}
