// The INI config (~/.config/superfar/superfar.ini via QSettings): key
// bindings, column widths per pane, layout, window size, the session
// (tabs per pane) and the folder history behind Ctrl+G. Reads are
// cheap; writes go through QSettings and sync on destruction.
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
    // Empty path means the standard location.
    explicit Config(const QString& path = QString(), QObject* parent = nullptr);

    // [keys] command=Seq;Seq. Absent entries keep the default; an empty
    // value unbinds. The defaults are written out once so the file shows
    // what can be changed.
    QHash<QString, QStringList> keyBindings() const;
    void writeDefaultKeys(const QList<QPair<QString, QStringList>>& defaults);

    // [columns/left] and [columns/right]: width per column name, and the
    // hidden ones.
    struct Columns {
        QHash<QString, int> widths;
        QStringList hidden;
    };
    Columns columns(const QString& side) const;
    void setColumnWidth(const QString& side, const QString& column, int width);

    // [layout]
    bool stackedSplit() const;       // panes one above the other
    bool previewThirdSlot() const;   // preview in its own slot, not a borrowed pane
    QList<int> splitterSizes(const QString& name) const;
    void setSplitterSizes(const QString& name, const QList<int>& sizes);
    QSize windowSize() const;
    void setWindowSize(const QSize& size);

    // [session]
    struct TabState {
        QString uri;
        bool hidden = false;
        bool locked = false;
        bool sizes = false;
        bool thumbs = false;
    };
    struct Session {
        QList<TabState> left;
        QList<TabState> right;
        int leftActive = 0;
        int rightActive = 0;
        QString activePane = QStringLiteral("left");
        bool valid = false;
    };
    Session session() const;
    void saveSession(const Session& s);

    // [history] uri=count,lastSeconds
    struct Visit {
        QString uri;
        int count = 0;
        qint64 last = 0;
    };
    void recordVisit(const QString& uri, qint64 now);
    QList<Visit> history() const;
    // Visits matching text (subsequence, case-insensitive), best first:
    // prefix matches, then substrings, then the rest, each by frecency.
    static QList<Visit> rank(const QList<Visit>& visits, const QString& text, qint64 now);
    static double frecency(const Visit& v, qint64 now);

    void sync() { settings_.sync(); }
    QSettings& settings() { return settings_; }

private:
    mutable QSettings settings_;
};
