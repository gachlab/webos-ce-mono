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

#ifndef WIFILAUNCHPARAMS_H
#define WIFILAUNCHPARAMS_H

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <string>

// The launch parameters the system menu opens the Wi-Fi card with: the network
// the user tapped, as {"target": {...}}.
//
// HP built this with sprintf and a trailing comma -- {"target": {...},} -- which
// JSON.parse refuses, so enyo dropped the whole thing with "Invalid window
// params" and the card opened on its list instead of on the network. The name
// went in unescaped too, so a quote in an SSID broke it the same way. Built as
// JSON here, the name escaped whatever it holds.
//
// profileId and connectState are only sent for the joined network; a secured
// network with no profile gets neither, which is how the card tells the two
// apart.
inline std::string wifiLaunchParams(const QString& ssid, const QString& securityType,
                                    int profileId, const QString& connectState)
{
    QJsonObject target;
    target.insert(QStringLiteral("ssid"), ssid);
    target.insert(QStringLiteral("securityType"), securityType);
    if (!connectState.isEmpty()) {
        target.insert(QStringLiteral("profileId"), profileId);
        target.insert(QStringLiteral("connectState"), connectState);
    }
    QJsonObject params;
    params.insert(QStringLiteral("target"), target);
    return QJsonDocument(params).toJson(QJsonDocument::Compact).toStdString();
}

#endif
