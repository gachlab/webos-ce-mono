/* @@@LICENSE
 *
 * Copyright (c) 2026 webOS CE modern build
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LICENSE@@@ */

#include "sleep_watch.h"

#include <gio/gunixfdlist.h>

#include <unistd.h>

namespace {

const char kLogin1[] = "org.freedesktop.login1";
const char kLogin1Path[] = "/org/freedesktop/login1";
const char kLogin1Manager[] = "org.freedesktop.login1.Manager";

} // namespace

SleepWatch::SleepWatch(GDBusConnection* bus, Handler handler)
    : m_bus(bus)
    , m_handler(std::move(handler))
{
    if (!m_bus)
        return;
    g_object_ref(m_bus);
    m_subscription = g_dbus_connection_signal_subscribe(
        m_bus, kLogin1, kLogin1Manager, "PrepareForSleep", kLogin1Path, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, onSignal, this, nullptr);
}

SleepWatch::~SleepWatch()
{
    hold(false);
    if (m_bus) {
        if (m_subscription)
            g_dbus_connection_signal_unsubscribe(m_bus, m_subscription);
        g_object_unref(m_bus);
    }
}

void SleepWatch::onSignal(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                          const gchar*, GVariant* parameters, gpointer self)
{
    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(b)")))
        return;
    gboolean sleeping = FALSE;
    g_variant_get(parameters, "(b)", &sleeping);
    SleepWatch* watch = static_cast<SleepWatch*>(self);
    if (watch->m_handler)
        watch->m_handler(sleeping);
}

bool SleepWatch::hold(bool wanted)
{
    if (!wanted) {
        if (m_fd >= 0) {
            close(m_fd);
            m_fd = -1;
        }
        return true;
    }
    if (m_fd >= 0)
        return true;
    if (!m_bus)
        return false;

    GUnixFDList* fds = nullptr;
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_with_unix_fd_list_sync(
        m_bus, kLogin1, kLogin1Path, kLogin1Manager, "Inhibit",
        g_variant_new("(ssss)", "sleep", "webOS", "Switching Wi-Fi off before sleep", "delay"),
        G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &fds, nullptr, &error);
    if (!reply) {
        g_warning("nm-connectionmanager: no sleep inhibitor: %s",
                  error && error->message ? error->message : "(no message)");
        g_clear_error(&error);
        return false;
    }
    gint32 index = -1;
    g_variant_get(reply, "(h)", &index);
    g_variant_unref(reply);
    if (fds) {
        m_fd = g_unix_fd_list_get(fds, index, nullptr);
        g_object_unref(fds);
    }
    return m_fd >= 0;
}
