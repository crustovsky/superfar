// Getting past "Permission denied" on a local path through GVfs's
// admin:// backend: the same path, served by a daemon that does the
// file operation as root after polkit has authorised the user. The
// password dialog is the polkit agent's, the system's own: the app
// never sees a password and nothing of ours runs as root. To GIO it
// is a location like any other remote one, so listing, jobs and
// monitors work on it unchanged.
#pragma once

#include "core/location.h"

#include <QString>
#include <QStringList>

namespace core::admin {

// Whether the backend is installed, so that offering it makes sense.
bool available();
bool availableIn(const QStringList& dataDirs);  // the check itself, for tests

bool isAdmin(const Location& location);
// The admin:// form of a local location. A location that is not local
// (remote, or admin:// already) comes back as it is.
Location of(const Location& location);
// And back: the local location an admin:// one stands for.
Location plain(const Location& location);

// Whether escalation is worth offering after a failed change in a
// folder: it is local, the backend is there, and the folder is not
// ours to write to. Asks the filesystem, so call it after a failure,
// not before every operation.
bool offerFor(const Location& folder);

// A refusal that came back through admin://, in words: GVfs only says
// "permission denied" whether polkit said no or nobody was there to ask.
QString refusal(const QString& message);

} // namespace core::admin
