#include "core/location.h"

#include "core/gobject.h"

namespace core {

Location::Location(GFile* borrowed)
    : file_(borrowed)
{
    if (file_)
        g_object_ref(file_);
}

Location Location::adopt(GFile* owned)
{
    Location l;
    l.file_ = owned;
    return l;
}

Location Location::fromPath(const QByteArray& nativePath)
{
    if (nativePath.isEmpty())
        return {};
    return adopt(g_file_new_for_path(nativePath.constData()));
}

Location Location::fromUri(const QString& uri)
{
    if (uri.isEmpty())
        return {};
    return adopt(g_file_new_for_uri(uri.toUtf8().constData()));
}

Location Location::fromCommandLine(const QByteArray& arg)
{
    if (arg.isEmpty())
        return {};
    return adopt(g_file_new_for_commandline_arg(arg.constData()));
}

Location Location::home()
{
    return fromPath(g_get_home_dir());
}

Location Location::root()
{
    return fromPath("/");
}

Location::Location(const Location& other)
    : file_(other.file_)
{
    if (file_)
        g_object_ref(file_);
}

Location::Location(Location&& other) noexcept
    : file_(other.file_)
{
    other.file_ = nullptr;
}

Location& Location::operator=(const Location& other)
{
    if (this != &other) {
        if (other.file_)
            g_object_ref(other.file_);
        if (file_)
            g_object_unref(file_);
        file_ = other.file_;
    }
    return *this;
}

Location& Location::operator=(Location&& other) noexcept
{
    if (this != &other) {
        if (file_)
            g_object_unref(file_);
        file_ = other.file_;
        other.file_ = nullptr;
    }
    return *this;
}

Location::~Location()
{
    if (file_)
        g_object_unref(file_);
}

QString Location::uri() const
{
    if (!file_)
        return {};
    GStr s(g_file_get_uri(file_));
    return QString::fromUtf8(s.get());
}

QByteArray Location::path() const
{
    if (!file_)
        return {};
    GStr p(g_file_get_path(file_));
    return p ? QByteArray(p.get()) : QByteArray();
}

QString Location::displayPath() const
{
    if (!file_)
        return {};
    GStr p(g_file_get_path(file_));
    if (p) {
        GStr d(g_filename_display_name(p.get()));
        return QString::fromUtf8(d.get());
    }
    GStr s(g_file_get_parse_name(file_));
    return QString::fromUtf8(s.get());
}

QByteArray Location::basename() const
{
    if (!file_)
        return {};
    GStr b(g_file_get_basename(file_));
    return b ? QByteArray(b.get()) : QByteArray();
}

QString Location::displayName() const
{
    const QByteArray b = basename();
    if (b.isEmpty())
        return {};
    GStr d(g_filename_display_name(b.constData()));
    return QString::fromUtf8(d.get());
}

Location Location::parent() const
{
    if (!file_)
        return {};
    return adopt(g_file_get_parent(file_));
}

Location Location::child(const QByteArray& name) const
{
    if (!file_ || name.isEmpty())
        return {};
    return adopt(g_file_get_child(file_, name.constData()));
}

Location Location::descendant(const QByteArray& relativePath) const
{
    if (!file_ || relativePath.isEmpty())
        return *this;
    return adopt(g_file_resolve_relative_path(file_, relativePath.constData()));
}

bool Location::isNative() const
{
    return file_ && g_file_is_native(file_);
}

QByteArray Location::relativePathFrom(const Location& ancestor) const
{
    if (!file_ || !ancestor.file_)
        return {};
    if (*this == ancestor)
        return QByteArray("");
    GStr r(g_file_get_relative_path(ancestor.file_, file_));
    return r ? QByteArray(r.get()) : QByteArray();
}

bool Location::operator==(const Location& other) const
{
    if (file_ == other.file_)
        return true;
    if (!file_ || !other.file_)
        return false;
    return g_file_equal(file_, other.file_);
}

} // namespace core
