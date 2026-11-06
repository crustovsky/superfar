// Completion for a line edit that takes "user:group": user names before
// the colon, group names after it, from the passwd and group databases.
// Inline (the rest of the name appears selected), never a popup.
#pragma once

class QLineEdit;

void completeOwners(QLineEdit* edit);
