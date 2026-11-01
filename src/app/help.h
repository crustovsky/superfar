// The F1 help: the current key table rendered as text, shown in the
// viewer overlay so it needs no window of its own (rule 10). The about
// text goes the same way, once per version and from the Help menu.
#pragma once

#include <QString>

namespace help {

QString text();
QString about();  // name, version, build date, what it is

} // namespace help
