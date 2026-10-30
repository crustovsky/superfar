#include "multirename.h"

#include "config.h"
#include "core/fileops.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSet>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <memory>

MultiRenamePanel::MultiRenamePanel(Config* config, QWidget* parent)
    : QWidget(parent)
    , config_(config)
    , mode_(new QComboBox(this))
    , pattern_(new QLineEdit(this))
    , replacement_(new QLineEdit(this))
    , ignoreExt_(new QCheckBox(tr("Ignore e&xtension"), this))
    , caseSensitive_(new QCheckBox(tr("Case &sensitive"), this))
    , caseChange_(new QComboBox(this))
    , start_(new QSpinBox(this))
    , step_(new QSpinBox(this))
    , digits_(new QSpinBox(this))
    , presets_(new QComboBox(this))
    , table_(new QTreeWidget(this))
    , summary_(new QLabel(this))
    , apply_(new QPushButton(tr("&Apply"), this))
    , undoBtn_(new QPushButton(tr("&Undo last batch"), this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    auto* grid = new QGridLayout;
    mode_->addItems({tr("Wildcard (* and ?)"), tr("Regular expression"), tr("Find and replace")});
    caseChange_->addItems({tr("Keep case"), tr("lower case"), tr("UPPER CASE"), tr("Title Case"), tr("Sentence case")});
    start_->setRange(0, 1000000);
    step_->setRange(-1000, 1000);
    digits_->setRange(1, 8);
    grid->addWidget(new QLabel(tr("Mode"), this), 0, 0);
    grid->addWidget(mode_, 0, 1);
    grid->addWidget(new QLabel(tr("Old name"), this), 0, 2);
    grid->addWidget(pattern_, 0, 3);
    grid->addWidget(new QLabel(tr("New name"), this), 0, 4);
    grid->addWidget(replacement_, 0, 5);
    grid->addWidget(ignoreExt_, 1, 1);
    grid->addWidget(caseSensitive_, 1, 2);
    grid->addWidget(caseChange_, 1, 3);
    auto* numbers = new QHBoxLayout;
    numbers->addWidget(new QLabel(tr("{n} from"), this));
    numbers->addWidget(start_);
    numbers->addWidget(new QLabel(tr("step"), this));
    numbers->addWidget(step_);
    numbers->addWidget(new QLabel(tr("digits"), this));
    numbers->addWidget(digits_);
    numbers->addStretch(1);
    grid->addLayout(numbers, 1, 4, 1, 2);
    auto* presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel(tr("Preset"), this));
    presetRow->addWidget(presets_, 1);
    presetName_ = new QLineEdit(this);
    presetName_->setPlaceholderText(tr("name for a new preset"));
    presetRow->addWidget(presetName_, 1);
    auto* savePreset = new QPushButton(tr("Sa&ve"), this);
    auto* delPreset = new QPushButton(tr("&Delete"), this);
    presetRow->addWidget(savePreset);
    presetRow->addWidget(delPreset);
    grid->addLayout(presetRow, 2, 0, 1, 6);
    grid->setColumnStretch(3, 1);
    grid->setColumnStretch(5, 1);
    layout->addLayout(grid);

    table_->setHeaderLabels({tr("Old"), tr("New")});
    table_->setRootIsDecorated(false);
    table_->setUniformRowHeights(true);
    table_->header()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table_, 1);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(summary_, 1);
    auto* closeBtn = new QPushButton(tr("&Close"), this);
    bottom->addWidget(undoBtn_);
    bottom->addWidget(apply_);
    bottom->addWidget(closeBtn);
    layout->addLayout(bottom);

    ignoreExt_->setChecked(true);
    pattern_->setText(QStringLiteral("*"));
    replacement_->setText(QStringLiteral("*"));
    pattern_->setToolTip(tr("* and ? match and capture; the new name reuses them in order"));
    replacement_->setToolTip(tr("{n} numbers the items, {name} and {ext} are the original stem and extension"));
    undoBtn_->setEnabled(false);

    for (QLineEdit* e : {pattern_, replacement_})
        connect(e, &QLineEdit::textChanged, this, &MultiRenamePanel::refresh);
    for (QComboBox* c : {mode_, caseChange_})
        connect(c, &QComboBox::currentIndexChanged, this, &MultiRenamePanel::refresh);
    for (QCheckBox* c : {ignoreExt_, caseSensitive_})
        connect(c, &QCheckBox::toggled, this, &MultiRenamePanel::refresh);
    for (QSpinBox* s : {start_, step_, digits_})
        connect(s, &QSpinBox::valueChanged, this, &MultiRenamePanel::refresh);
    connect(table_, &QTreeWidget::itemChanged, this, &MultiRenamePanel::refresh);
    connect(apply_, &QPushButton::clicked, this, &MultiRenamePanel::apply);
    connect(undoBtn_, &QPushButton::clicked, this, &MultiRenamePanel::undo);
    connect(closeBtn, &QPushButton::clicked, this, &MultiRenamePanel::closed);
    connect(savePreset, &QPushButton::clicked, this, &MultiRenamePanel::savePreset);
    connect(delPreset, &QPushButton::clicked, this, &MultiRenamePanel::deletePreset);
    connect(presets_, &QComboBox::activated, this, [this](int index) {
        const QString text = presets_->itemData(index).toString();
        if (!text.isEmpty())
            controlsFromRule(core::RenameRule::deserialize(text));
    });
    setFocusProxy(pattern_);
    loadPresets();
    controlsFromRule(core::RenameRule());  // the defaults, numbering from 1
}

void MultiRenamePanel::show(const std::vector<core::Location>& items, const QStringList& existing)
{
    items_ = items;
    existing_ = existing;
    undo_.clear();
    undoBtn_->setEnabled(false);
    {
        QSignalBlocker block(table_);
        table_->clear();
        for (const core::Location& item : items) {
            auto* row = new QTreeWidgetItem(table_, {item.displayName(), QString()});
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
            row->setCheckState(0, Qt::Checked);
        }
    }
    refresh();
    pattern_->setFocus();
    pattern_->selectAll();
}

core::RenameRule MultiRenamePanel::ruleFromControls() const
{
    core::RenameRule r;
    r.mode = static_cast<core::RenameRule::Mode>(mode_->currentIndex());
    r.pattern = pattern_->text();
    r.replacement = replacement_->text();
    r.ignoreExtension = ignoreExt_->isChecked();
    r.caseSensitive = caseSensitive_->isChecked();
    r.caseChange = static_cast<core::RenameRule::Case>(caseChange_->currentIndex());
    r.numberStart = start_->value();
    r.numberStep = step_->value();
    r.numberDigits = digits_->value();
    return r;
}

void MultiRenamePanel::controlsFromRule(const core::RenameRule& r)
{
    const QSignalBlocker b1(mode_), b2(pattern_), b3(replacement_), b4(ignoreExt_), b5(caseSensitive_),
        b6(caseChange_), b7(start_), b8(step_), b9(digits_);
    mode_->setCurrentIndex(static_cast<int>(r.mode));
    pattern_->setText(r.pattern);
    replacement_->setText(r.replacement);
    ignoreExt_->setChecked(r.ignoreExtension);
    caseSensitive_->setChecked(r.caseSensitive);
    caseChange_->setCurrentIndex(static_cast<int>(r.caseChange));
    start_->setValue(r.numberStart);
    step_->setValue(r.numberStep);
    digits_->setValue(r.numberDigits);
    refresh();
}

void MultiRenamePanel::refresh()
{
    QStringList olds;
    for (const core::Location& item : items_)
        olds << item.displayName();
    plan_ = core::planRename(ruleFromControls(), olds, existing_);
    int clashes = 0;
    int checked = 0;
    const QSignalBlocker block(table_);
    for (int i = 0; i < table_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = table_->topLevelItem(i);
        const bool on = row->checkState(0) == Qt::Checked;
        const bool clash = plan_.clash[static_cast<size_t>(i)];
        row->setText(1, plan_.newNames[i] + (clash && on ? tr("   (clash)") : QString()));
        QFont f = row->font(1);
        f.setBold(on && plan_.newNames[i] != olds[i]);
        f.setStrikeOut(clash && on);
        row->setFont(1, f);
        if (on) {
            ++checked;
            clashes += clash ? 1 : 0;
        }
    }
    summary_->setText(clashes > 0 ? tr("%n items, %1 clash", nullptr, checked).arg(clashes)
                                  : tr("%n items, %1 change", nullptr, checked).arg(plan_.changed));
    apply_->setEnabled(clashes == 0 && plan_.changed > 0 && checked > 0);
}

void MultiRenamePanel::apply()
{
    std::vector<std::pair<core::Location, QString>> moves;
    for (int i = 0; i < table_->topLevelItemCount(); ++i) {
        if (table_->topLevelItem(i)->checkState(0) != Qt::Checked)
            continue;
        if (plan_.newNames[i] != items_[static_cast<size_t>(i)].displayName())
            moves.emplace_back(items_[static_cast<size_t>(i)], plan_.newNames[i]);
    }
    runBatch(std::move(moves), true);
}

void MultiRenamePanel::undo()
{
    std::vector<std::pair<core::Location, QString>> moves = undo_;
    undo_.clear();
    undoBtn_->setEnabled(false);
    runBatch(std::move(moves), false);
}

void MultiRenamePanel::runBatch(std::vector<std::pair<core::Location, QString>> moves, bool recordUndo)
{
    if (moves.empty())
        return;
    // When a new name is another item's old name, every item goes through
    // a temporary name first, so a swap or a rotation never collides.
    QSet<QString> olds;
    for (const auto& [loc, name] : moves)
        olds.insert(loc.displayName());
    bool cycle = false;
    for (const auto& [loc, name] : moves)
        cycle = cycle || olds.contains(name);

    struct State {
        std::vector<std::pair<core::Location, QString>> moves;  // current location -> final name
        std::vector<QString> originals;
        std::vector<std::pair<core::Location, QString>> undo;   // final location -> original name
        bool cycle = false;
        bool temporaries = false;  // phase one of a cycle
        bool recordUndo = false;
        size_t index = 0;
        int failures = 0;
        QString firstNew;
    };
    auto st = std::make_shared<State>();
    st->moves = std::move(moves);
    for (const auto& [loc, name] : st->moves)
        st->originals.push_back(loc.displayName());
    st->cycle = st->temporaries = cycle;
    st->recordUndo = recordUndo;

    QPointer<MultiRenamePanel> self(this);
    auto step = std::make_shared<std::function<void()>>();
    *step = [self, st, step] {
        if (!self)
            return;
        if (st->index >= st->moves.size()) {
            if (st->temporaries) {
                st->temporaries = false;  // second pass: temporaries to final names
                st->index = 0;
            } else {
                if (st->recordUndo) {
                    self->undo_ = st->undo;
                    self->undoBtn_->setEnabled(!self->undo_.empty());
                }
                Q_EMIT self->message(st->failures > 0
                                         ? tr("%n renames failed", nullptr, st->failures)
                                         : tr("%n items renamed", nullptr, static_cast<int>(st->moves.size())));
                Q_EMIT self->renamed(st->firstNew.toUtf8());
                return;
            }
        }
        const size_t i = st->index;
        const QString target = st->temporaries
            ? QStringLiteral(".superfar-rename-%1-%2").arg(i).arg(st->moves[i].second)
            : st->moves[i].second;
        core::renameEntry(st->moves[i].first, target,
                          [self, st, step, i](const QString& error, const QByteArray& actual, const QString&) {
                              if (!self)
                                  return;
                              if (!error.isEmpty()) {
                                  ++st->failures;
                              } else {
                                  st->moves[i].first = st->moves[i].first.parent().child(actual);
                                  if (!st->temporaries) {
                                      if (st->firstNew.isEmpty())
                                          st->firstNew = QString::fromUtf8(actual);
                                      st->undo.emplace_back(st->moves[i].first, st->originals[i]);
                                  }
                              }
                              ++st->index;
                              (*step)();
                          });
    };
    (*step)();
}

void MultiRenamePanel::loadPresets()
{
    presets_->clear();
    presets_->addItem(tr("(choose)"), QString());
    QSettings& s = config_->settings();
    s.beginGroup(QStringLiteral("rename-presets"));
    for (const QString& key : s.childKeys())
        presets_->addItem(key, s.value(key).toString());
    s.endGroup();
}

void MultiRenamePanel::savePreset()
{
    QString name = presetName_->text().trimmed();
    if (name.isEmpty() && presets_->currentIndex() > 0)
        name = presets_->currentText();  // overwrite the chosen one
    if (name.isEmpty()) {
        Q_EMIT message(tr("Type a name for the preset first"));
        presetName_->setFocus();
        return;
    }
    config_->settings().setValue(QStringLiteral("rename-presets/") + name, ruleFromControls().serialize());
    config_->sync();
    loadPresets();
    presets_->setCurrentText(name);
    presetName_->clear();
}

void MultiRenamePanel::deletePreset()
{
    if (presets_->currentIndex() <= 0)
        return;
    config_->settings().remove(QStringLiteral("rename-presets/") + presets_->currentText());
    config_->sync();
    loadPresets();
}

void MultiRenamePanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT closed();
        return;
    }
    QWidget::keyPressEvent(event);
}
