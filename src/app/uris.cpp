#include "uris.h"

#include <QMimeData>
#include <QUrl>

namespace uris {

namespace {
const char* const kGnome = "x-special/gnome-copied-files";
}

QMimeData* make(const std::vector<core::Location>& files, bool cut)
{
    auto* data = new QMimeData;
    QList<QUrl> urls;
    QByteArray gnome = cut ? "cut" : "copy";
    for (const core::Location& f : files) {
        const QString uri = f.uri();
        urls << QUrl(uri);
        gnome += '\n' + uri.toUtf8();
    }
    data->setUrls(urls);
    data->setData(QString::fromLatin1(kGnome), gnome);
    return data;
}

bool holdsFiles(const QMimeData* data)
{
    return data && (data->hasUrls() || data->hasFormat(QString::fromLatin1(kGnome)));
}

bool read(const QMimeData* data, std::vector<core::Location>* files, bool* cut)
{
    files->clear();
    *cut = false;
    if (!data)
        return false;
    if (data->hasFormat(QString::fromLatin1(kGnome))) {
        const QList<QByteArray> lines = data->data(QString::fromLatin1(kGnome)).split('\n');
        if (!lines.isEmpty()) {
            *cut = lines.first().trimmed() == "cut";
            for (int i = 1; i < lines.size(); ++i)
                if (const QByteArray l = lines[i].trimmed(); !l.isEmpty())
                    files->push_back(core::Location::fromUri(QString::fromUtf8(l)));
        }
    }
    if (files->empty() && data->hasUrls()) {
        for (const QUrl& url : data->urls())
            if (url.isValid())
                files->push_back(core::Location::fromUri(url.toString(QUrl::FullyEncoded)));
    }
    return !files->empty();
}

} // namespace uris
