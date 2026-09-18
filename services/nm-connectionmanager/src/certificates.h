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
// The user's certificates, for the Wi-Fi card's enterprise TLS login.
//
// HP's certificate manager kept an installed-certificates store. Here the store
// is a directory of PEM files, each holding a certificate and, for TLS, its
// unencrypted private key: $WEBOS_CERTIFICATE_DIR, or webos-ce/certificates in
// the user's data directory. Reading them needs nothing beyond gio.
//

#ifndef CERTIFICATES_H
#define CERTIFICATES_H

#include "network_state.h"

#include <string>
#include <vector>

namespace Certificates {

std::string directory();

// Every readable certificate in dir, sorted by file name, numbered from 1.
// Files that are not certificates are skipped.
std::vector<NmNet::Certificate> list(const std::string& dir);

} // namespace Certificates

#endif
