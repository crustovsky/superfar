// Runs a function on the main thread from a worker, as a GLib idle
// source on the default main context: the road GIO completions take, and
// one that always wakes the GLib main loop Qt runs on.
#pragma once

#include <gio/gio.h>

#include <functional>
#include <memory>

namespace core {

inline void postToMain(std::function<void()> fn)
{
    auto* boxed = new std::function<void()>(std::move(fn));
    GSource* source = g_idle_source_new();
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_set_callback(
        source,
        [](gpointer data) -> gboolean {
            (*static_cast<std::function<void()>*>(data))();
            return G_SOURCE_REMOVE;
        },
        boxed, [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
    g_source_attach(source, nullptr);
    g_source_unref(source);
}

} // namespace core
