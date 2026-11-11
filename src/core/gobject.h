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

inline bool isDenied(const GError* e)
{
    return e && g_error_matches(e, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
}

inline bool isCancelled(const GError* e)
{
    return e && g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

} // namespace core
