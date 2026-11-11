// RAII helpers for GLib and GIO handles. Private to core: never include
// this from app.
#pragma once

#include <gio/gio.h>

#include <QString>
#include <memory>

namespace core {

struct GObjectUnref {
    void operator()(gpointer p) const noexcept
    {
        if (p)
            g_object_unref(p);
    }
};
template <typename T>
using GPtr = std::unique_ptr<T, GObjectUnref>;

struct GFreeDeleter {
    void operator()(gpointer p) const noexcept { g_free(p); }
};
using GStr = std::unique_ptr<char, GFreeDeleter>;

struct GErrorFree {
    void operator()(GError* e) const noexcept
    {
        if (e)
            g_error_free(e);
    }
};
using GErrorPtr = std::unique_ptr<GError, GErrorFree>;

inline QString errorMessage(const GError* e)
{
    return e ? QString::fromUtf8(e->message) : QString();
}

// Hidden and link as an enumerator's info has them. A backend may
// leave either out (GVfs's admin:// does), and GLib logs a critical
// for reading what is not there: a name with a leading dot is hidden
// then, and nothing is a link.
inline bool infoIsHidden(GFileInfo* info)
{
    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN))
        return g_file_info_get_is_hidden(info);
    const char* name = g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_NAME) ? g_file_info_get_name(info) : nullptr;
    return name && name[0] == '.';
}

inline bool infoIsSymlink(GFileInfo* info)
{
    return g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK) && g_file_info_get_is_symlink(info);
}

inline bool isDenied(const GError* e)
{
    return e && g_error_matches(e, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
}

inline bool isCancelled(const GError* e)
{
    return e && g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

} // namespace core
