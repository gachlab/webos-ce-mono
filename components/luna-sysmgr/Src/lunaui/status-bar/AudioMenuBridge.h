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

#ifndef AUDIOMENUBRIDGE_H
#define AUDIOMENUBRIDGE_H

//
// The system menu's volume slider and output chooser, wired to com.palm.audio.
//
// Ours, not HP's -- the same shape the architecture calls "an object of ours
// plus one install hook", as MouseToTouch and MouseEventEater are. It is NOT in
// adapters/: those are standalone compatibility libraries that map a modern
// library's API and name no HP types; this is a QObject that lives in the
// shell's QML engine, opens its own luna-service2 handle, and is consumed by the
// shell's own SystemMenu/*.qml. So it sits beside SystemMenu, and the only edit
// to HP is the single guarded line that installs it (see install()).
//
// Why a bridge at all: HP's menu rows reach the bus through SystemMenu.cpp and
// StatusBarServicesConnector. Rather than add audio to both of those HP files,
// this exposes com.palm.audio to the two new QML elements directly, as a context
// property they read with Connections { target: AudioMenuBridge }, exactly as
// WiFiElement reads NativeSystemMenuHandler. No audio logic is added to HP.
//
// com.palm.audio is answered by services/audiod-pipewire. The volume half is
// proven; the output chooser's move is pending the PipeWire-vs-WirePlumber
// measurement, so selectOutput may report "not applied" and the current output
// stays put -- this bridge passes that through honestly rather than pretending.
//

#include <QObject>
#include <QString>
#include <QVariantList>

#include <lunaservice.h>

class QQmlEngine;

class AudioMenuBridge : public QObject
{
    Q_OBJECT

public:
    // The one install hook. Called from the shell's menu bring-up with the
    // shared QML engine, before SystemMenu.qml is loaded, so the context
    // property is in place when the QML that reads it is created. Guarded by the
    // caller with the same TARGET_DESKTOP block the other shell-side objects of
    // ours use. Idempotent: a second call is a no-op.
    static void install(QQmlEngine* engine);

    static AudioMenuBridge* instance();

    ~AudioMenuBridge() override;

    // webOS's own volume, 0..100, applied to the session's streams. Called by
    // the slider; the service does the per-stream set.
    Q_INVOKABLE void setVolume(int percent);

    // Send webOS's audio to the output with this id (the opaque id listOutputs
    // handed the chooser). May be refused until the move is implemented.
    Q_INVOKABLE void selectOutput(const QString& outputId);

    // The outputs for the chooser to list: a QVariantList of maps with keys
    // "id", "name", "current". Read once when the drawer opens, and kept fresh
    // by outputsChanged().
    Q_INVOKABLE QVariantList outputs() const;

    // The current volume, 0..100, for the slider's initial position.
    Q_INVOKABLE int volume() const;

Q_SIGNALS:
    // Pushed when the service reports webOS's volume moved (keys, mute, or the
    // host mixer moving our node). normalised is 0.0..1.0 for the slider.
    void volumeChanged(double normalised);
    void mutedChanged(bool muted);
    // The output set or the current output changed; the chooser re-reads
    // outputs().
    void outputsChanged();

private:
    explicit AudioMenuBridge(QObject* parent = nullptr);

    void startService();
    void subscribeStatus();
    void subscribeOutputs();

    // luna-service2 callbacks. The static ones forward to the member.
    static bool statusCb(LSHandle* h, LSMessage* m, void* ctx);
    static bool outputsCb(LSHandle* h, LSMessage* m, void* ctx);
    bool handleStatus(LSMessage* m);
    bool handleOutputs(LSMessage* m);

    static AudioMenuBridge* s_instance;

    LSHandle* m_service = nullptr;
    int m_volume = 100;
    bool m_muted = false;
    QVariantList m_outputs;
};

#endif // AUDIOMENUBRIDGE_H
