// "1 item", "3 items". Qt's %n picks the singular only from a
// translation file, English included, and there is none before 1.0.
#pragma once

#include <QString>

inline QString counted(qint64 n, const QString& one, const QString& many)
{
    return QString::number(n) + QLatin1Char(' ') + (n == 1 ? one : many);
}
