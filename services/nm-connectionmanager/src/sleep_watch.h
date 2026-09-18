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

//
// The machine going to sleep and waking up, from logind.
//
// logind announces both with PrepareForSleep(bool). A process that has to act
// before the machine sleeps takes a "delay" inhibitor first: logind then waits
// -- up to InhibitDelayMaxSec -- until that lock is released, so the radio can
// be switched off before the machine stops rather than racing it.
//
// Like nm_client, it is given the connection to use, so tests/nm-client.cpp can
// run it against a fake logind on a private bus.
//

#ifndef SLEEP_WATCH_H
#define SLEEP_WATCH_H

#include <gio/gio.h>

#include <functional>

class SleepWatch {
public:
    using Handler = std::function<void(bool goingToSleep)>;

    // Handler runs on the connection's main context for every
    // PrepareForSleep. A null connection watches nothing.
    SleepWatch(GDBusConnection* bus, Handler handler);
    ~SleepWatch();

    SleepWatch(const SleepWatch&) = delete;
    SleepWatch& operator=(const SleepWatch&) = delete;

    // Takes the delay lock, or releases it. Taking it twice keeps one.
    bool hold(bool wanted);
    bool holding() const { return m_fd >= 0; }

private:
    static void onSignal(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                         const gchar*, GVariant* parameters, gpointer self);

    GDBusConnection* m_bus;
    Handler m_handler;
    guint m_subscription = 0;
    int m_fd = -1;
};

#endif
