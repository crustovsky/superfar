#include "keymap.h"

#include <QKeyEvent>
#include <utility>
#include <vector>

namespace keymap {

namespace {

std::vector<std::pair<QKeySequence, Command>>& table()
{
    static std::vector<std::pair<QKeySequence, Command>> t = {
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
        {QKeySequence(Qt::CTRL | Qt::Key_F3), Command::ToggleSizes},
        {QKeySequence(Qt::Key_Tab), Command::SwitchPane},
        {QKeySequence(Qt::SHIFT | Qt::Key_Tab), Command::FocusTree},
        {QKeySequence(Qt::CTRL | Qt::Key_T), Command::NewTab},
        {QKeySequence(Qt::CTRL | Qt::Key_W), Command::CloseTab},
        {QKeySequence(Qt::CTRL | Qt::Key_Tab), Command::NextTab},
        {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), Command::PrevTab},
        {QKeySequence(Qt::CTRL | Qt::Key_H), Command::ToggleHidden},
        {QKeySequence(Qt::CTRL | Qt::Key_R), Command::Reload},
        {QKeySequence(Qt::Key_Insert), Command::Mark},
        {QKeySequence(Qt::SHIFT | Qt::Key_Down), Command::Mark},  // keyboards without Insert
        {QKeySequence(Qt::SHIFT | Qt::Key_Up), Command::MarkUp},
        {QKeySequence(Qt::Key_Space), Command::MarkToggle},
        {QKeySequence(Qt::CTRL | Qt::Key_A), Command::MarkAll},
        {QKeySequence(Qt::Key_Asterisk), Command::InvertMarks},
        {QKeySequence(Qt::Key_Backspace), Command::GoUp},
        {QKeySequence(Qt::Key_Left), Command::GoUp},
        {QKeySequence(Qt::Key_Right), Command::Activate},
        {QKeySequence(Qt::CTRL | Qt::Key_Q), Command::Preview},
        {QKeySequence(Qt::Key_Return), Command::Activate},
        {QKeySequence(Qt::Key_Enter), Command::Activate},
        {QKeySequence(Qt::Key_Escape), Command::Cancel},
        {QKeySequence(Qt::CTRL | Qt::Key_Equal), Command::MarkGlob},
        {QKeySequence(Qt::CTRL | Qt::Key_Plus), Command::MarkGlob},
        {QKeySequence(Qt::CTRL | Qt::Key_Minus), Command::UnmarkGlob},
        {QKeySequence(Qt::KeypadModifier | Qt::Key_Plus), Command::MarkGlob},  // keypads, where present
        {QKeySequence(Qt::KeypadModifier | Qt::Key_Minus), Command::UnmarkGlob},
        {QKeySequence(Qt::SHIFT | Qt::Key_Return), Command::OpenWith},
        {QKeySequence(Qt::SHIFT | Qt::Key_Enter), Command::OpenWith},
        {QKeySequence(Qt::CTRL | Qt::Key_Space), Command::FocusCommandLine},
        {QKeySequence(Qt::CTRL | Qt::Key_C), Command::CopyClip},
        {QKeySequence(Qt::CTRL | Qt::Key_X), Command::CutClip},
        {QKeySequence(Qt::CTRL | Qt::Key_V), Command::Paste},
        {QKeySequence(Qt::CTRL | Qt::Key_L), Command::EditPath},
        {QKeySequence(Qt::CTRL | Qt::Key_G), Command::GoTo},
        {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), Command::LockTab},
        {QKeySequence(Qt::ALT | Qt::Key_Return), Command::Properties},
        {QKeySequence(Qt::ALT | Qt::Key_Enter), Command::Properties},
        {QKeySequence(Qt::CTRL | Qt::Key_D), Command::Bookmark},
        {QKeySequence(Qt::CTRL | Qt::Key_F4), Command::ToggleThumbnails},
        {QKeySequence(Qt::SHIFT | Qt::Key_F2), Command::MultiRename},
        {QKeySequence(Qt::SHIFT | Qt::Key_F5), Command::Sync},
        {QKeySequence(Qt::CTRL | Qt::Key_F5), Command::ToggleTree},
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
    Qt::KeyboardModifiers mods = event->modifiers();
    if (key == Qt::Key_Backtab) {
        key = Qt::Key_Tab;
        mods |= Qt::ShiftModifier;
    }
    // Keypad keys match their own entries first (Gray + and Gray -), then
    // the plain ones, so keypad arrows still navigate.
    if (mods & Qt::KeypadModifier) {
        const Command keypad = lookup(QKeySequence(QKeyCombination(mods, Qt::Key(key)).toCombined()));
        if (keypad != Command::None)
            return keypad;
        mods &= ~Qt::KeypadModifier;
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
    case Command::MarkUp: return QStringLiteral("Select");
    case Command::MarkToggle: return QStringLiteral("Select");
    case Command::MarkAll: return QStringLiteral("Select all");
    case Command::InvertMarks: return QStringLiteral("Invert");
    case Command::GoUp: return QStringLiteral("Up");
    case Command::Activate: return QStringLiteral("Enter");
    case Command::Preview: return QStringLiteral("Preview");
    case Command::ToggleSizes: return QStringLiteral("Sizes");
    case Command::Cancel: return QStringLiteral("Cancel");
    case Command::MarkGlob: return QStringLiteral("Select by pattern");
    case Command::UnmarkGlob: return QStringLiteral("Deselect by pattern");
    case Command::OpenWith: return QStringLiteral("Open with");
    case Command::FocusCommandLine: return QStringLiteral("Command line");
    case Command::CopyClip: return QStringLiteral("Copy to clipboard");
    case Command::CutClip: return QStringLiteral("Cut to clipboard");
    case Command::Paste: return QStringLiteral("Paste");
    case Command::EditPath: return QStringLiteral("Path");
    case Command::GoTo: return QStringLiteral("Go to");
    case Command::LockTab: return QStringLiteral("Lock tab");
    case Command::Properties: return QStringLiteral("Properties");
    case Command::Bookmark: return QStringLiteral("Bookmark");
    case Command::ToggleThumbnails: return QStringLiteral("Thumbs");
    case Command::MultiRename: return QStringLiteral("Multi-rename");
    case Command::Sync: return QStringLiteral("Sync");
    case Command::About: return QStringLiteral("About");
    case Command::ToggleTree: return QStringLiteral("Tree");
    case Command::None: return {};
    }
    return {};
}

namespace {
const std::vector<std::pair<Command, const char*>>& names()
{
    static const std::vector<std::pair<Command, const char*>> n = {
        {Command::Help, "Help"}, {Command::Rename, "Rename"}, {Command::View, "View"}, {Command::Edit, "Edit"},
        {Command::Copy, "Copy"}, {Command::Move, "Move"}, {Command::Mkdir, "Mkdir"}, {Command::Trash, "Trash"},
        {Command::Menu, "Menu"}, {Command::Quit, "Quit"}, {Command::NewFile, "NewFile"},
        {Command::DeletePermanent, "DeletePermanent"}, {Command::Find, "Find"}, {Command::HideLeft, "HideLeft"},
        {Command::HideRight, "HideRight"}, {Command::SwitchPane, "SwitchPane"}, {Command::FocusTree, "FocusTree"},
        {Command::NewTab, "NewTab"}, {Command::CloseTab, "CloseTab"}, {Command::NextTab, "NextTab"},
        {Command::PrevTab, "PrevTab"}, {Command::ToggleHidden, "ToggleHidden"}, {Command::Reload, "Reload"},
        {Command::Mark, "Mark"}, {Command::MarkUp, "MarkUp"}, {Command::MarkToggle, "MarkToggle"}, {Command::MarkAll, "MarkAll"},
        {Command::InvertMarks, "InvertMarks"}, {Command::GoUp, "GoUp"}, {Command::Activate, "Activate"},
        {Command::Preview, "Preview"}, {Command::ToggleSizes, "ToggleSizes"}, {Command::Cancel, "Cancel"},
        {Command::MarkGlob, "MarkGlob"}, {Command::UnmarkGlob, "UnmarkGlob"}, {Command::OpenWith, "OpenWith"},
        {Command::FocusCommandLine, "FocusCommandLine"}, {Command::CopyClip, "CopyClip"},
        {Command::CutClip, "CutClip"}, {Command::Paste, "Paste"}, {Command::EditPath, "EditPath"},
        {Command::GoTo, "GoTo"}, {Command::LockTab, "LockTab"}, {Command::Properties, "Properties"},
        {Command::Bookmark, "Bookmark"}, {Command::ToggleThumbnails, "ToggleThumbnails"},
        {Command::MultiRename, "MultiRename"}, {Command::Sync, "Sync"}, {Command::About, "About"},
        {Command::ToggleTree, "ToggleTree"},
    };
    return n;
}
} // namespace

QString name(Command command)
{
    for (const auto& [c, n] : names())
        if (c == command)
            return QString::fromLatin1(n);
    return {};
}

Command fromName(const QString& text)
{
    for (const auto& [c, n] : names())
        if (text == QLatin1String(n))
            return c;
    return Command::None;
}

QList<QPair<QString, QStringList>> defaults()
{
    QList<QPair<QString, QStringList>> out;
    for (const auto& [c, n] : names()) {
        QStringList seqs;
        for (const auto& [seq, cmd] : table())
            if (cmd == c)
                seqs << seq.toString(QKeySequence::PortableText);
        out.append({QString::fromLatin1(n), seqs});
    }
    return out;
}

QList<QPair<QString, QString>> retiredDefaults()
{
    // As the first run wrote them (portable text: Insert is "Ins").
    return {{QStringLiteral("Mark"), QKeySequence(Qt::Key_Insert).toString(QKeySequence::PortableText)}};  // 0.5: Shift+Down joined it
}

QList<QKeySequence> keysOf(Command command)
{
    QList<QKeySequence> out;
    for (const auto& [seq, cmd] : table())
        if (cmd == command)
            out << seq;
    return out;
}

QList<Command> allCommands()
{
    QList<Command> out;
    for (const auto& [c, n] : names())
        out << c;
    return out;
}

QStringList applyBindings(const QHash<QString, QStringList>& bindings)
{
    QStringList problems;
    auto& t = table();
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        const Command cmd = fromName(it.key());
        if (cmd == Command::None) {
            problems << QStringLiteral("unknown command %1").arg(it.key());
            continue;
        }
        std::vector<QKeySequence> seqs;
        bool ok = true;
        for (const QString& text : it.value()) {
            const QKeySequence seq(text.trimmed(), QKeySequence::PortableText);
            // Qt turns text it cannot read into Key_unknown, not an empty sequence.
            if (seq.isEmpty() || seq[0].key() == Qt::Key_unknown) {
                problems << QStringLiteral("%1: cannot read key %2").arg(it.key(), text);
                ok = false;
            } else {
                seqs.push_back(seq);
            }
        }
        if (!ok)
            continue;
        std::erase_if(t, [cmd](const auto& entry) { return entry.second == cmd; });
        for (const QKeySequence& seq : seqs) {
            for (const auto& entry : t)  // one command per key: say whose key this was
                if (entry.first == seq)
                    problems << QStringLiteral("%1 takes %2 from %3").arg(it.key(), seq.toString(), name(entry.second));
            std::erase_if(t, [&seq](const auto& entry) { return entry.first == seq; });
            t.emplace_back(seq, cmd);
        }
    }
    return problems;
}

} // namespace keymap
