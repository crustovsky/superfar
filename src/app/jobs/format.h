// Text helpers shared by the job strip and panel.
#pragma once

#include <QLocale>
#include <QString>

namespace jobfmt {

inline QString bytes(quint64 n)
{
    return QLocale().formattedDataSize(static_cast<qint64>(n), 1, QLocale::DataSizeIecFormat);
}

inline QString rate(double perSecond, bool bytesUnit)
{
    if (bytesUnit)
        return bytes(static_cast<quint64>(perSecond)) + QStringLiteral("/s");
    return QStringLiteral("%1 items/s").arg(static_cast<qint64>(perSecond));
}

inline QString duration(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("--:--");
    const qint64 s = ms / 1000;
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace jobfmt
