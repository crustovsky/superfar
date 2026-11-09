// Tab, the commands a tab runs itself. See tab.cpp for the rest of the
// class.
#include "pane/tab.h"

#include "core/apps.h"
#include "pane/listmodel.h"
#include "pane/listview.h"

#include <QKeyEvent>

bool Tab::execute(keymap::Command command)
{
    using keymap::Command;
    const QModelIndex cur = view_->currentIndex();
    switch (command) {
    case Command::Mark:
        if (cur.isValid()) {
            model_->toggleMarked(cur.row());
            if (cur.row() + 1 < model_->rowCount())
                view_->setCurrentIndex(model_->index(cur.row() + 1, 0));
        }
        return true;
    case Command::MarkUp:
        if (cur.isValid()) {
            model_->toggleMarked(cur.row());
            if (cur.row() > 0)
                view_->setCurrentIndex(model_->index(cur.row() - 1, 0));
        }
        return true;
    case Command::MarkToggle:
        if (!model_->filter().isEmpty())
            typed(QStringLiteral(" "));  // Space selects only while no filter is set
        else if (cur.isValid())
            model_->toggleMarked(cur.row());
        return true;
    case Command::Cancel:
        if (model_->filter().isEmpty())
            return false;
        setFilter(QString());
        return true;
    case Command::PreviewFirst:
    case Command::PreviewPrev:
    case Command::PreviewNext:
    case Command::PreviewLast: {
        // No paged preview took the key: it keeps the list meaning of its
        // unmodified twin, as Ctrl+Home moved to the first row before.
        const Qt::Key key = command == Command::PreviewFirst ? Qt::Key_Home
            : command == Command::PreviewLast                ? Qt::Key_End
            : command == Command::PreviewPrev                ? Qt::Key_PageUp
                                                             : Qt::Key_PageDown;
        QKeyEvent plain(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(view_, &plain);
        return true;
    }
    case Command::Rename:
        if (isArchive()) {
            Q_EMIT message(tr("Archives are read-only here"));
            return true;
        }
        if (cur.isValid())
            view_->edit(cur.siblingAtColumn(ListModel::NameColumn));
        return true;
    case Command::Edit: {
        const QByteArray name = currentName();
        if (name.isEmpty() || cursorIsDir() || isArchive())
            return true;
        QString error;
        const QString editor = core::editorCommand(&error);
        if (editor.isEmpty()
            || !core::runInTerminal(location_, editor + QLatin1Char(' ') + QString::fromUtf8(core::shellQuote(name)),
                                    false, &error))
            Q_EMIT message(error);
        return true;
    }
    case Command::MarkAll:
        model_->markAll(true);
        return true;
    case Command::InvertMarks:
        model_->invertMarks();
        return true;
    case Command::ClearMarks:
        model_->markAll(false);
        return true;
    case Command::GoUp:
        if (!model_->filter().isEmpty())
            setFilter(model_->filter().chopped(1));  // Backspace edits the filter first
        else
            goUp();
        return true;
    case Command::Activate:
        activate(cur);
        return true;
    case Command::Reload:
        reload();
        return true;
    case Command::ToggleHidden:
        setShowHidden(!showHidden());
        return true;
    case Command::ToggleSizes:
        model_->setRecursiveSizes(!model_->recursiveSizes());
        updateStatus();
        return true;
    case Command::ToggleThumbnails:
        setThumbnails(!thumbnails());
        return true;
    case Command::View: {
        // F3 on folders walks them; files get the viewer in milestone 5.
        bool any = false;
        if (model_->markedCount() > 0) {
            for (int row = 0; row < model_->rowCount(); ++row)
                if (model_->isMarked(row) && model_->entryAt(row).isDir()) {
                    model_->requestSize(row);
                    any = true;
                }
        } else if (cur.isValid() && model_->entryAt(cur.row()).isDir()) {
            model_->requestSize(cur.row());
            any = true;
        }
        return any;
    }
    default:
        return false;
    }
}
