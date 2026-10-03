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

#include "AudioMenuBridge.h"

#include "HostBase.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QVariantMap>

#include <cjson/json.h>

AudioMenuBridge* AudioMenuBridge::s_instance = 0;

AudioMenuBridge* AudioMenuBridge::instance()
{
    if (!s_instance)
        s_instance = new AudioMenuBridge();
    return s_instance;
}

// The one install hook. Idempotent, and it leaves the shell's own setup alone:
// it only adds a context property to the shared engine, which SystemMenu.cpp
// later loads SystemMenu.qml into.
void AudioMenuBridge::install(QQmlEngine* engine)
{
    if (!engine)
        return;
    AudioMenuBridge* bridge = instance();
    QQmlContext* context = engine->rootContext();
    if (context)
        context->setContextProperty("AudioMenuBridge", bridge);
}

AudioMenuBridge::AudioMenuBridge(QObject* parent)
    : QObject(parent)
{
    startService();
}

AudioMenuBridge::~AudioMenuBridge()
{
    if (m_service) {
        LSError error;
        LSErrorInit(&error);
        if (!LSUnregister(m_service, &error))
            LSErrorFree(&error);
    }
}

void AudioMenuBridge::startService()
{
    LSError error;
    LSErrorInit(&error);

    // Our own anonymous handle on the bus, like NativeAlertManager's: a
    // registered name is not needed to call com.palm.audio and subscribe.
    if (!LSRegister("com.palm.audio.menu", &m_service, &error)) {
        g_warning("AudioMenuBridge: LSRegister failed: %s", error.message);
        LSErrorFree(&error);
        return;
    }
    if (!LSGmainAttach(m_service, HostBase::instance()->mainLoop(), &error)) {
        g_warning("AudioMenuBridge: LSGmainAttach failed: %s", error.message);
        LSErrorFree(&error);
        return;
    }

    subscribeStatus();
    subscribeOutputs();
}

void AudioMenuBridge::subscribeStatus()
{
    LSError error;
    LSErrorInit(&error);
    // The system category carries webOS's own volume and mute.
    if (!LSCall(m_service, "palm://com.palm.audio/system/status",
                "{\"subscribe\":true}", &AudioMenuBridge::statusCb, this, NULL, &error)) {
        g_warning("AudioMenuBridge: subscribe status failed: %s", error.message);
        LSErrorFree(&error);
    }
}

void AudioMenuBridge::subscribeOutputs()
{
    LSError error;
    LSErrorInit(&error);
    if (!LSCall(m_service, "palm://com.palm.audio/outputs/listOutputs",
                "{\"subscribe\":true}", &AudioMenuBridge::outputsCb, this, NULL, &error)) {
        g_warning("AudioMenuBridge: subscribe outputs failed: %s", error.message);
        LSErrorFree(&error);
    }
}

bool AudioMenuBridge::statusCb(LSHandle*, LSMessage* m, void* ctx)
{
    return static_cast<AudioMenuBridge*>(ctx)->handleStatus(m);
}

bool AudioMenuBridge::outputsCb(LSHandle*, LSMessage* m, void* ctx)
{
    return static_cast<AudioMenuBridge*>(ctx)->handleOutputs(m);
}

bool AudioMenuBridge::handleStatus(LSMessage* m)
{
    const char* payload = LSMessageGetPayload(m);
    if (!payload)
        return true;
    json_object* root = json_tokener_parse(payload);
    if (!root || is_error(root))
        return true;

    json_object* label = 0;
    if ((label = json_object_object_get(root, "volume"))) {
        int v = json_object_get_int(label);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        if (v != m_volume) {
            m_volume = v;
            Q_EMIT volumeChanged((double)m_volume / 100.0);
        }
    }
    if ((label = json_object_object_get(root, "muted"))) {
        bool mute = json_object_get_boolean(label);
        if (mute != m_muted) {
            m_muted = mute;
            Q_EMIT mutedChanged(m_muted);
        }
    }

    json_object_put(root);
    return true;
}

bool AudioMenuBridge::handleOutputs(LSMessage* m)
{
    const char* payload = LSMessageGetPayload(m);
    if (!payload)
        return true;
    json_object* root = json_tokener_parse(payload);
    if (!root || is_error(root))
        return true;

    json_object* arr = json_object_object_get(root, "outputs");
    if (arr && json_object_is_type(arr, json_type_array)) {
        QVariantList list;
        for (int i = 0; i < json_object_array_length(arr); ++i) {
            json_object* entry = json_object_array_get_idx(arr, i);
            if (!entry || is_error(entry))
                continue;
            QVariantMap map;
            json_object* f = 0;
            if ((f = json_object_object_get(entry, "id")))
                map["id"] = QString::fromUtf8(json_object_get_string(f));
            if ((f = json_object_object_get(entry, "name")))
                map["name"] = QString::fromUtf8(json_object_get_string(f));
            if ((f = json_object_object_get(entry, "current")))
                map["current"] = (bool)json_object_get_boolean(f);
            list.append(map);
        }
        m_outputs = list;
        Q_EMIT outputsChanged();
    }

    json_object_put(root);
    return true;
}

void AudioMenuBridge::setVolume(int percent)
{
    if (!m_service)
        return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    char payload[64];
    ::snprintf(payload, sizeof payload, "{\"volume\":%d}", percent);

    LSError error;
    LSErrorInit(&error);
    // setVolume is served on the system category; the service applies it to the
    // session's streams and re-announces, which comes back through statusCb.
    if (!LSCallOneReply(m_service, "palm://com.palm.audio/system/setVolume",
                        payload, NULL, NULL, NULL, &error)) {
        // setVolume may not be a served method name on every build of the
        // service; fall back is not needed here, just log.
        g_warning("AudioMenuBridge: setVolume failed: %s", error.message);
        LSErrorFree(&error);
    }
}

void AudioMenuBridge::selectOutput(const QString& outputId)
{
    if (!m_service)
        return;
    QByteArray id = outputId.toUtf8();

    json_object* obj = json_object_new_object();
    json_object_object_add(obj, "id", json_object_new_string(id.constData()));
    const char* payload = json_object_to_json_string(obj);

    LSError error;
    LSErrorInit(&error);
    if (!LSCallOneReply(m_service, "palm://com.palm.audio/outputs/selectOutput",
                        payload, NULL, NULL, NULL, &error)) {
        g_warning("AudioMenuBridge: selectOutput failed: %s", error.message);
        LSErrorFree(&error);
    }
    json_object_put(obj);
}

QVariantList AudioMenuBridge::outputs() const
{
    return m_outputs;
}

int AudioMenuBridge::volume() const
{
    return m_volume;
}
