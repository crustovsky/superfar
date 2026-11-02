// Two INI files through QSettings. The config
// (~/.config/superfar/superfar.ini) holds what the user edits: key
// bindings, hidden columns, layout choices, rename presets; the app
// writes it once, with the default keys, and never again. The state
// file (~/.local/state/superfar/state.ini) holds what the app writes on
// its own: the session, the folder history behind Ctrl+G, window and
// splitter sizes, dragged column widths. A dotfile manager can own the
// first without seeing churn from the second.
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QStringList>

class Config : public QObject {
    Q_OBJECT
public:
    // Empty paths mean the standard locations; a config path alone puts
    // the state file next to it as state.ini.
    explicit Config(const QString& path = QString(), const QString& statePath = QString(),
                    QObject* parent = nullptr);

    // [keys] command=Seq;Seq. Absent entries keep the default; an empty
    // value unbinds. The defaults are written out once per command so
    // the file shows what can be changed; a line still holding a retired
    // default (name, old value) was never edited and takes the new one.
    QHash<QString, QStringList> keyBindings() const;
    void writeDefaultKeys(const QList<QPair<QString, QStringList>>& defaults,
                          const QList<QPair<QString, QString>>& retired = {});

    // [columns/left] and [columns/right]: the hidden set from the config;
    // widths by column name from the state, else from the config.
    struct Columns {
        QHash<QString, int> widths;
        QStringList hidden;
    };
    Columns columns(const QString& side) const;
    void setColumnWidth(const QString& side, const QString& column, int width);

    // [layout] in the config; the sizes in the state
    bool stackedSplit() const;       // panes one above the other
    bool previewThirdSlot() const;   // preview in its own slot, not a borrowed pane
    QList<int> splitterSizes(const QString& name) const;
    void setSplitterSizes(const QString& name, const QList<int>& sizes);
    QSize windowSize() const;
    void setWindowSize(const QSize& size);

    // [session] in the state
    struct TabState {
        QString uri;
        bool hidden = false;
        bool locked = false;
        bool sizes = false;
        bool thumbs = false;
        QString cursor;  // name under the cursor, percent-encoded bytes
    };
    struct Session {
        QList<TabState> left;
        QList<TabState> right;
        int leftActive = 0;
        int rightActive = 0;
        QString activePane = QStringLiteral("left");
        bool tree = true;  // the folder tree is shown
        bool valid = false;
    };
    Session session() const;
    void saveSession(const Session& s);

    // [history] uri=count,lastSeconds, in the state
    struct Visit {
        QString uri;
        int count = 0;
        qint64 last = 0;
    };
    void recordVisit(const QString& uri, qint64 now);
    QList<Visit> history() const;
    // Visits matching text (prefix, substring, abbreviation), best first,
    // each group by frecency.
    static QList<Visit> rank(const QList<Visit>& visits, const QString& text, qint64 now);
    static double frecency(const Visit& v, qint64 now);

    // [about] in the state: the version that last ran, for a note on the
    // first start of a new one.
    QString lastVersion() const;
    void setLastVersion(const QString& version);

    void sync();
    // The config file itself, for the rename presets.
    QSettings& settings() { return settings_; }
    QString statePath() const { return state_.fileName(); }

private:
    void migrateState();

    mutable QSettings settings_;
    mutable QSettings state_;
};
