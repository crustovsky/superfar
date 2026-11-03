#include "packpanel.h"

#include "config.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr QLatin1StringView kGroup("pack");

struct Format {
    const char* name;  // what the user sees and the state keeps
    const char* ext;
    const char* best;  // --options for the Best level; Normal passes none
};
// All verified to write and list back with bsdtar (-a picks the format
// from the extension).
const Format kFormats[] = {
    {"zip", ".zip", "zip:compression-level=9"},
    {"tar.gz", ".tar.gz", "gzip:compression-level=9"},
    {"tar.xz", ".tar.xz", "xz:compression-level=9"},
    {"tar.zst", ".tar.zst", "zstd:compression-level=19"},
    {"7z", ".7z", "7zip:compression=lzma2"},
};
} // namespace

PackPanel::PackPanel(Config* config, QWidget* parent)
    : QWidget(parent)
    , config_(config)
    , title_(new QLabel(this))
    , name_(new QLineEdit(this))
    , ext_(new QLabel(this))
    , format_(new QComboBox(this))
    , level_(new QComboBox(this))
    , destination_(new QComboBox(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);
    title_->setWordWrap(true);
    layout->addWidget(title_);

    auto* nameRow = new QHBoxLayout;
    nameRow->addWidget(new QLabel(tr("&Name"), this));
    nameRow->addWidget(name_, 1);
    nameRow->addWidget(ext_);
    layout->addLayout(nameRow);
    auto* nameLabel = static_cast<QLabel*>(nameRow->itemAt(0)->widget());
    nameLabel->setBuddy(name_);

    auto* choices = new QHBoxLayout;
    auto* formatLabel = new QLabel(tr("&Format"), this);
    formatLabel->setBuddy(format_);
    choices->addWidget(formatLabel);
    choices->addWidget(format_);
    auto* levelLabel = new QLabel(tr("&Level"), this);
    levelLabel->setBuddy(level_);
    choices->addWidget(levelLabel);
    choices->addWidget(level_);
    auto* intoLabel = new QLabel(tr("&Into"), this);
    intoLabel->setBuddy(destination_);
    choices->addWidget(intoLabel);
    choices->addWidget(destination_, 1);
    layout->addLayout(choices);

    auto* actions = new QHBoxLayout;
    actions->addStretch(1);
    auto* packBtn = new QPushButton(tr("&Pack"), this);
    auto* closeBtn = new QPushButton(tr("Clos&e"), this);
    actions->addWidget(packBtn);
    actions->addWidget(closeBtn);
    layout->addLayout(actions);
    layout->addStretch(1);

    for (const Format& f : kFormats)
        format_->addItem(QString::fromLatin1(f.name));
    level_->addItem(tr("Normal"), QStringLiteral("normal"));
    level_->addItem(tr("Best"), QStringLiteral("best"));
    // Where F5 would put a copy, or here.
    destination_->addItem(QString(), QStringLiteral("other"));
    destination_->addItem(QString(), QStringLiteral("here"));

    const QString format = config_->choice(kGroup, QStringLiteral("format"), QStringLiteral("zip"));
    if (const int i = format_->findText(format); i >= 0)
        format_->setCurrentIndex(i);
    level_->setCurrentIndex(std::max(0, level_->findData(config_->choice(kGroup, QStringLiteral("level"), QStringLiteral("normal")))));
    destination_->setCurrentIndex(std::max(0, destination_->findData(config_->choice(kGroup, QStringLiteral("destination"), QStringLiteral("other")))));

    connect(format_, &QComboBox::currentIndexChanged, this, [this] { ext_->setText(QString::fromLatin1(extension())); });
    connect(name_, &QLineEdit::returnPressed, this, &PackPanel::pack);
    connect(packBtn, &QPushButton::clicked, this, &PackPanel::pack);
    connect(closeBtn, &QPushButton::clicked, this, &PackPanel::closed);
    ext_->setText(QString::fromLatin1(extension()));
    setFocusProxy(name_);
}

void PackPanel::show(const QString& what, const QByteArray& stem, const core::Location& here, const core::Location& other)
{
    here_ = here;
    other_ = other;
    title_->setText(tr("Pack %1 from %2").arg(what, here.displayPath()));
    name_->setText(QString::fromUtf8(stem));
    name_->selectAll();
    destination_->setItemText(0, tr("the other pane: %1").arg(other.displayPath()));
    destination_->setItemText(1, tr("this folder"));
}

QByteArray PackPanel::extension() const
{
    const int i = format_->currentIndex();
    return i >= 0 ? QByteArray(kFormats[i].ext) : QByteArray();
}

QByteArray PackPanel::options() const
{
    const int i = format_->currentIndex();
    if (i < 0 || level_->currentData().toString() != QLatin1String("best"))
        return {};
    return QByteArray(kFormats[i].best);
}

void PackPanel::pack()
{
    const QByteArray stem = name_->text().trimmed().toUtf8();
    if (stem.isEmpty() || stem.contains('/')) {
        name_->setFocus();
        return;
    }
    config_->setChoice(kGroup, QStringLiteral("format"), format_->currentText());
    config_->setChoice(kGroup, QStringLiteral("level"), level_->currentData().toString());
    config_->setChoice(kGroup, QStringLiteral("destination"), destination_->currentData().toString());
    const bool other = destination_->currentData().toString() == QLatin1String("other");
    Q_EMIT accepted(stem + extension(), options(), other ? other_ : here_);
}

void PackPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT closed();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        pack();  // from a combo box too
        return;
    }
    QWidget::keyPressEvent(event);
}
