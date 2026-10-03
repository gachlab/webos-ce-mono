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

#ifndef PRINTMGR_CUPS_CUPS_CLIENT_H
#define PRINTMGR_CUPS_CUPS_CLIENT_H

//
// The CUPS half of com.palm.printmgr: talk to the printing daemon with libcups,
// turning its printers and jobs into the plain PrintState structs the bus half
// then renders. This is the analogue of nm_client in nm-connectionmanager --
// the client-of-the-host-daemon layer, kept apart from the Luna glue.
//
// Every call takes the CUPS connection (http_t*) it should use rather than
// reaching for the default daemon itself. main.cpp hands it the default
// connection (CUPS_HTTP_DEFAULT); a test can hand it a connection to a cupsd it
// controls. Passing CUPS_HTTP_DEFAULT (nullptr) is valid and means "the local
// daemon", exactly as the CUPS API intends.
//
// libcups is the host's, linked the way audiod-pipewire links libpulse: a .pc
// on the host, resolved through the stage's PKG_CONFIG_PATH. The daemon it talks
// to is whatever cupsd the session can reach.
//

#include "print_state.h"

#include <cups/cups.h>

#include <string>
#include <vector>

namespace CupsClient {

// All printers CUPS knows (local queues and discovered IPP Everywhere ones).
std::vector<PrintState::Printer> listPrinters(http_t* http);

// The user's default destination, or nullptr-equivalent (printer.id empty) when
// there is none. Returns whether a default exists via the bool.
bool getDefaultPrinter(http_t* http, PrintState::Printer& out);

// A printer's capabilities, read from its IPP attributes / PPD. Unknown options
// come back as empty arrays / false, which the dialog hides.
PrintState::Capabilities getCapabilities(http_t* http, const std::string& printerId);

// Submit a file to a printer with the job's options. Returns the CUPS job id
// (>0) on success, or 0 and fills errorCode with a PrintState::ErrorCode on
// failure.
int printFile(http_t* http, const std::string& printerId, const std::string& file,
              const std::string& title, const PrintState::PrintOptions& options,
              int& errorCode);

// The state of a job: whether it has left the queue (done) and how it ended.
// Returns false if the job is unknown.
bool getJobStatus(http_t* http, int jobId, bool& done, PrintState::JobStatus& status);

// Cancel a job. Returns whether CUPS accepted the cancel.
bool cancelJob(http_t* http, const std::string& printerId, int jobId);

// Map a CUPS/IPP status code to the PrintManagerError vocabulary the dialog
// understands. Exposed for the bus half and for tests.
int errorCodeFromIpp(ipp_status_t status);

} // namespace CupsClient

#endif // PRINTMGR_CUPS_CUPS_CLIENT_H
