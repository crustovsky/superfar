// Volumes and mounts through GVolumeMonitor, plus mounting a remote
// location. Password and question prompts from GMountOperation are
// routed out as signals and answered by token, so the window can show
// them in a bar (rule 10).
#pragma once

#include "core/location.h"

#include <QObject>
#include <QStringList>
#include <functional>
#include <vector>

typedef struct _GVolumeMonitor GVolumeMonitor;
typedef struct _GMountOperation GMountOperation;

namespace core {

struct Place {
    QString id;        // stable across refreshes: the mount root uri or the volume's identifier
    QString name;
    QStringList iconNames;
    Location location;  // invalid while a volume is not mounted
    bool mounted = false;
    bool canUnmount = false;
    bool canEject = false;
};

class Mounts : public QObject {
    Q_OBJECT
public:
    explicit Mounts(QObject* parent = nullptr);
    ~Mounts() override;

    // Mounted mounts and mountable volumes, mounted first.
    std::vector<Place> places() const;

    using Done = std::function<void(const QString& error)>;
    void mountPlace(const QString& id, Done done);
    void unmountPlace(const QString& id, Done done);
    // Mounts whatever holds a remote location, for a typed URI.
    void mountLocation(const Location& location, Done done);

    void answerPassword(quint64 token, const QString& user, const QString& domain, const QString& password,
                        bool anonymous);
    void answerQuestion(quint64 token, int choice);
    void abortPrompt(quint64 token);

Q_SIGNALS:
    void changed();
    void askPassword(quint64 token, const QString& message, const QString& defaultUser,
                     const QString& defaultDomain, bool needUser, bool needDomain, bool needPassword,
                     bool anonymousOk);
    void askQuestion(quint64 token, const QString& message, const QStringList& choices);

private:
    struct Op;
    GMountOperation* newOperation();
    void finishPrompt(quint64 token, GMountOperation** op);

    GVolumeMonitor* monitor_;
    std::vector<unsigned long> handlers_;
    quint64 nextToken_ = 1;
    std::vector<std::pair<quint64, GMountOperation*>> prompts_;  // open, op is ref'd
};

} // namespace core
