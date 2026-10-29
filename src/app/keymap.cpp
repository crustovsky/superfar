#include "keymap.h"

#include <QKeyEvent>
#include <utility>
#include <vector>

namespace keymap {

namespace {

const std::vector<std::pair<QKeySequence, Command>>& table()
{
    static const std::vector<std::pair<QKeySequence, Command>> t = {
        {QKeySequence(Qt::Key_F1), Command::Help},
        {QKeySequence(Qt::Key_F2), Command::Rename},
        {QKeySequence(Qt::Key_F3), Command::View},
        {QKeySequence(Qt::Key_F4), Command::Edit},
        {QKeySequence(Qt::Key_F5), Command::Copy},
        {QKeySequence(Qt::Key_F6), Command::Move},
        {QKeySequence(Qt::Key_F7), Command::Mkdir},
        {QKeySequence(Qt::Key_F8), Command::Trash},
        {QKeySequence(Qt::Key_F9), Command::Menu},
        {QKeySequence(Qt::Key_F10), Command::Quit},
        {QKeySequence(Qt::SHIFT | Qt::Key_F4), Command::NewFile},
        {QKeySequence(Qt::SHIFT | Qt::Key_F8), Command::DeletePermanent},
        {QKeySequence(Qt::ALT | Qt::Key_F7), Command::Find},
        {QKeySequence(Qt::CTRL | Qt::Key_F1), Command::HideLeft},
        {QKeySequence(Qt::CTRL | Qt::Key_F2), Command::HideRight},
        {QKeySequence(Qt::Key_Tab), Command::SwitchPane},
        {QKeySequence(Qt::SHIFT | Qt::Key_Tab), Command::FocusTree},
        {QKeySequence(Qt::CTRL | Qt::Key_T), Command::NewTab},
        {QKeySequence(Qt::CTRL | Qt::Key_W), Command::CloseTab},
        {QKeySequence(Qt::CTRL | Qt::Key_Tab), Command::NextTab},
        {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), Command::PrevTab},
        {QKeySequence(Qt::CTRL | Qt::Key_H), Command::ToggleHidden},
        {QKeySequence(Qt::CTRL | Qt::Key_R), Command::Reload},
        {QKeySequence(Qt::Key_Insert), Command::Mark},
        {QKeySequence(Qt::Key_Space), Command::MarkToggle},
        {QKeySequence(Qt::CTRL | Qt::Key_A), Command::MarkAll},
        {QKeySequence(Qt::Key_Asterisk), Command::InvertMarks},
        {QKeySequence(Qt::Key_Backspace), Command::GoUp},
        {QKeySequence(Qt::Key_Left), Command::GoUp},
        {QKeySequence(Qt::Key_Right), Command::Activate},
        {QKeySequence(Qt::CTRL | Qt::Key_Q), Command::Preview},
    };
    return t;
}

} // namespace

Command lookup(const QKeySequence& key)
{
    for (const auto& [seq, cmd] : table())
        if (seq == key)
            return cmd;
    return Command::None;
}

Command lookup(const QKeyEvent* event)
{
    int key = event->key();
    Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    if (key == Qt::Key_Backtab) {
        key = Qt::Key_Tab;
        mods |= Qt::ShiftModifier;
    }
    const Command direct = lookup(QKeySequence(QKeyCombination(mods, Qt::Key(key)).toCombined()));
    if (direct != Command::None)
        return direct;
    // A shifted symbol such as * arrives with Shift set; the table lists it bare.
    if ((mods & Qt::ShiftModifier) && !event->text().isEmpty() && !event->text().at(0).isLetter())
        return lookup(QKeySequence(QKeyCombination(mods & ~Qt::ShiftModifier, Qt::Key(key)).toCombined()));
    return Command::None;
}

QString label(Command command)
{
    switch (command) {
    case Command::Help: return QStringLiteral("Help");
    case Command::Rename: return QStringLiteral("Rename");
    case Command::View: return QStringLiteral("View");
    case Command::Edit: return QStringLiteral("Edit");
    case Command::Copy: return QStringLiteral("Copy");
    case Command::Move: return QStringLiteral("Move");
    case Command::Mkdir: return QStringLiteral("Mkdir");
    case Command::Trash: return QStringLiteral("Trash");
    case Command::Menu: return QStringLiteral("Menu");
    case Command::Quit: return QStringLiteral("Quit");
    case Command::NewFile: return QStringLiteral("NewFile");
    case Command::DeletePermanent: return QStringLiteral("Delete");
    case Command::Find: return QStringLiteral("Find");
    case Command::HideLeft: return QStringLiteral("Left");
    case Command::HideRight: return QStringLiteral("Right");
    case Command::SwitchPane: return QStringLiteral("Switch pane");
    case Command::FocusTree: return QStringLiteral("Tree");
    case Command::NewTab: return QStringLiteral("New tab");
    case Command::CloseTab: return QStringLiteral("Close tab");
    case Command::NextTab: return QStringLiteral("Next tab");
    case Command::PrevTab: return QStringLiteral("Previous tab");
    case Command::ToggleHidden: return QStringLiteral("Hidden");
    case Command::Reload: return QStringLiteral("Reload");
    case Command::Mark: return QStringLiteral("Select");
    case Command::MarkToggle: return QStringLiteral("Select");
    case Command::MarkAll: return QStringLiteral("Select all");
    case Command::InvertMarks: return QStringLiteral("Invert");
    case Command::GoUp: return QStringLiteral("Up");
    case Command::Activate: return QStringLiteral("Enter");
    case Command::Preview: return QStringLiteral("Preview");
    case Command::None: return {};
    }
    return {};
}

} // namespace keymap
