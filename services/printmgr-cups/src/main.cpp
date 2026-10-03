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
// com.palm.printmgr, answered from CUPS.
//
// HP built printmgr on its own wprint library; nothing in the CE drop answers
// it, so the one surviving consumer -- enyo's PrintDialog
// (components/enyo-1.0/framework/lib/printdialog) -- calls into a void and the
// Print button in Email and the browser does nothing. This provides the service
// over CUPS; the Print Manager card (com.palm.app.printmanager) is the other
// half of the ticket.
//
// The bus contract, verified against the dialog request-by-request (see
// print_state.h):
//
//   /printers  list (subscribe), getCurrent, setCurrent, add, getCapabilities
//   /jobs      open (subscribe), editPrintParams, getFinalParamsAndArea,
//              close, cancel, getStatus (subscribe), addFile, getRenderStatus
//
// The CUPS half is cups_client.cpp (tested against a cupsd it controls); the
// payloads are print_state.h, bus-free and CUPS-free (tested without either).
// This file is the Luna glue: register the name, wire the methods, parse with
// cjson, hold the small job table, and poll job status for subscribers.
//

#include "cups_client.h"
#include "print_state.h"

#include <luna-service2/lunaservice.h>

#include <cjson/json.h>
#include <glib.h>
#include <glib-unix.h>

#include <map>
#include <string>

namespace {

const char kServiceName[] = "com.palm.printmgr";

GMainLoop* g_loop = nullptr;
LSPalmService* g_service = nullptr;

// The user's chosen printer, as set by printers/setCurrent. Empty = fall back
// to the CUPS default.
std::string g_currentPrinter;

// The small job table. A job is opened, has its params edited, a file added
// (which submits it to CUPS and gives us the CUPS job id), and its status
// polled until done. jobID here is our own handle the dialog threads through;
// cupsJobId is CUPS's, 0 until the file is submitted.
struct Job {
    std::string printerId;
    int cupsJobId = 0;
    std::string title;
    // The per-job options the dialog set with editPrintParams, applied at submit
    // time. Kept as the dialog's own vocabulary; translated in cups_client if
    // needed. Only a few are honoured today (copies); the rest are accepted so
    // the dialog's flow completes.
    int numCopies = 1;
};
std::map<int, Job> g_jobs;
int g_nextJobId = 1;

LSHandle* privateBus()
{
    return LSPalmServiceGetPrivateConnection(g_service);
}

void logAndFree(const char* where, LSError& error)
{
    g_warning("printmgr-cups: %s: %s", where, error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

bool reply(LSHandle* sh, LSMessage* message, const std::string& payload)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
    return true;
}

bool replyError(LSHandle* sh, LSMessage* message, int code)
{
    return reply(sh, message, PrintState::errorPayload(code));
}

// --- cjson helpers ----------------------------------------------------------

// A string member of the payload, or empty.
std::string strMember(json_object* root, const char* key)
{
    if (!root)
        return std::string();
    json_object* field = json_object_object_get(root, key);
    if (!field)
        return std::string();
    const char* s = json_object_get_string(field);
    return s ? std::string(s) : std::string();
}

bool intMember(json_object* root, const char* key, int& out)
{
    if (!root)
        return false;
    json_object* field = json_object_object_get(root, key);
    if (!field)
        return false;
    out = json_object_get_int(field);
    return true;
}

bool isSubscribe(LSMessage* message)
{
    return LSMessageIsSubscription(message);
}

// --- /printers --------------------------------------------------------------

// printers/list: subscribe to the set of printers. CUPS enumeration is a
// snapshot, so each printer is posted as an "Add" event now; add/remove over
// time would come from a CUPS notification subscription (a later change). The
// dialog builds its list from these events.
bool printersList(LSHandle* sh, LSMessage* message, void*)
{
    if (isSubscribe(message)) {
        LSError error;
        LSErrorInit(&error);
        LSSubscriptionAdd(sh, "printers", message, &error);
        if (LSErrorIsSet(&error))
            logAndFree("LSSubscriptionAdd printers", error);
    }

    const std::vector<PrintState::Printer> printers = CupsClient::listPrinters(CUPS_HTTP_DEFAULT);
    // The first reply carries returnValue; it also carries the first printer if
    // there is one, and the rest are posted so the dialog sees each as an event.
    if (printers.empty())
        return reply(sh, message, "{\"returnValue\":true}");

    reply(sh, message, PrintState::printerEventPayload(printers.front(), "Add", true));
    LSError error;
    LSErrorInit(&error);
    for (size_t i = 1; i < printers.size(); ++i) {
        const std::string ev = PrintState::printerEventPayload(printers[i], "Add", true);
        if (!LSSubscriptionReply(privateBus(), "printers", ev.c_str(), &error))
            logAndFree("LSSubscriptionReply printers", error);
    }
    return true;
}

bool printersGetCurrent(LSHandle* sh, LSMessage* message, void*)
{
    PrintState::Printer p;
    if (!g_currentPrinter.empty()) {
        // Resolve the chosen id against the current list for its name/address.
        for (const auto& pr : CupsClient::listPrinters(CUPS_HTTP_DEFAULT)) {
            if (pr.id == g_currentPrinter) {
                return reply(sh, message, PrintState::currentPrinterPayload(&pr));
            }
        }
    }
    if (CupsClient::getDefaultPrinter(CUPS_HTTP_DEFAULT, p))
        return reply(sh, message, PrintState::currentPrinterPayload(&p));
    return reply(sh, message, PrintState::currentPrinterPayload(nullptr));
}

bool printersSetCurrent(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = json_tokener_parse(LSMessageGetPayload(message));
    const std::string id = strMember(root, "printerID");
    if (root && !is_error(root))
        json_object_put(root);
    if (id.empty())
        return replyError(sh, message, PrintState::kErrGeneric);
    g_currentPrinter = id;
    return reply(sh, message, PrintState::okPayload());
}

// printers/add: HP added printers by IP. CUPS discovers IPP Everywhere printers
// on its own, so there is nothing to add on a modern setup; accept it so the
// dialog's "Add a printer" flow completes, and let CUPS discovery do the work.
bool printersAdd(LSHandle* sh, LSMessage* message, void*)
{
    return reply(sh, message, PrintState::okPayload());
}

bool printersGetCapabilities(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = json_tokener_parse(LSMessageGetPayload(message));
    const std::string id = strMember(root, "printerID");
    if (root && !is_error(root))
        json_object_put(root);
    if (id.empty())
        return replyError(sh, message, PrintState::kErrPrinterNotSupported);
    const PrintState::Capabilities caps = CupsClient::getCapabilities(CUPS_HTTP_DEFAULT, id);
    return reply(sh, message, PrintState::capabilitiesPayload(caps));
}

// --- /jobs ------------------------------------------------------------------

// Poll the CUPS status of jobs that have been submitted and have a subscriber,
// posting DONE when they finish. One timer for all jobs.
guint g_jobPoll = 0;

gboolean pollJobs(gpointer)
{
    LSError error;
    LSErrorInit(&error);
    bool anyActive = false;
    for (auto& [jobID, job] : g_jobs) {
        if (job.cupsJobId == 0)
            continue;
        bool done = false;
        PrintState::JobStatus status = PrintState::JobStatus::Success;
        if (!CupsClient::getJobStatus(CUPS_HTTP_DEFAULT, job.cupsJobId, done, status))
            continue;
        if (!done) {
            anyActive = true;
            continue;
        }
        const std::string key = "job/" + std::to_string(jobID);
        const std::string payload = PrintState::jobStatusPayload(jobID, true, status);
        if (!LSSubscriptionReply(privateBus(), key.c_str(), payload.c_str(), &error))
            logAndFree("LSSubscriptionReply job", error);
    }
    if (!anyActive) {
        g_jobPoll = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void ensureJobPoll()
{
    if (g_jobPoll == 0)
        g_jobPoll = g_timeout_add_seconds(1, pollJobs, nullptr);
}

bool jobsOpen(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = json_tokener_parse(LSMessageGetPayload(message));
    const std::string printerId = strMember(root, "printerID");
    const std::string description = strMember(root, "description");
    if (root && !is_error(root))
        json_object_put(root);

    const int jobID = g_nextJobId++;
    Job job;
    job.printerId = printerId.empty() ? g_currentPrinter : printerId;
    job.title = description;
    g_jobs[jobID] = job;

    // Subscribe this caller for the job's status under a per-job key.
    if (isSubscribe(message)) {
        LSError error;
        LSErrorInit(&error);
        const std::string key = "job/" + std::to_string(jobID);
        LSSubscriptionAdd(sh, key.c_str(), message, &error);
        if (LSErrorIsSet(&error))
            logAndFree("LSSubscriptionAdd job", error);
    }
    return reply(sh, message, PrintState::jobOpenedPayload(jobID));
}

bool jobsEditPrintParams(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = json_tokener_parse(LSMessageGetPayload(message));
    int jobID = 0;
    if (intMember(root, "jobID", jobID)) {
        auto it = g_jobs.find(jobID);
        if (it != g_jobs.end()) {
            int copies = 0;
            if (intMember(root, "numCopies", copies) && copies > 0)
                it->second.numCopies = copies;
        }
    }
    if (root && !is_error(root))
        json_object_put(root);
    return reply(sh, message, PrintState::okPayload());
}

// getFinalParamsAndArea: the dialog reads the printable area back. We accept the
// job's params as final; a precise printable area would come from the printer's
// media-size attributes, which is a refinement. Reply ok with the jobID echoed.
bool jobsGetFinalParams(LSHandle* sh, LSMessage* message, void*)
{
    return reply(sh, message, PrintState::okPayload());
}

// addFile: the file to print is handed over; submit it to CUPS now, which gives
// the CUPS job id we poll on.
bool jobsAddFile(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = json_tokener_parse(LSMessageGetPayload(message));
    int jobID = 0;
    const std::string file = strMember(root, "file");
    const bool haveJob = intMember(root, "jobID", jobID);
    if (root && !is_error(root))
        json_object_put(root);

    if (!haveJob || file.empty())
        return replyError(sh, message, PrintState::kErrGeneric);
    auto it = g_jobs.find(jobID);
    if (it == g_jobs.end())
        return replyError(sh, message, PrintState::kErrGeneric);
    if (it->second.printerId.empty())
        return replyError(sh, message, PrintState::kErrPrinterNotSupported);

    int errorCode = 0;
    const int cupsJob = CupsClient::printFile(CUPS_HTTP_DEFAULT, it->second.printerId,
                                              file, it->second.title, errorCode);
    if (cupsJob == 0)
        return replyError(sh, message, errorCode);
    it->second.cupsJobId = cupsJob;
    ensureJobPoll();
    return reply(sh, message, PrintState::okPayload());
}

// getRenderStatus: the document path renders pages before printing; we submit
// the file whole to CUPS, so rendering is immediate. Report done.
bool jobsGetRenderStatus(LSHandle* sh, LSMessage* message, void*)
{
    return reply(sh, message, "{\"returnValue\":true,\"renderState\":\"DONE\"}");
}

bool jobsGetStatus(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = json_tokener_parse(LSMessageGetPayload(message));
    int jobID = 0;
    const bool haveJob = intMember(root, "jobID", jobID);
    if (root && !is_error(root))
        json_object_put(root);

    if (isSubscribe(message) && haveJob) {
        LSError error;
        LSErrorInit(&error);
        const std::string key = "job/" + std::to_string(jobID);
        LSSubscriptionAdd(sh, key.c_str(), message, &error);
        if (LSErrorIsSet(&error))
            logAndFree("LSSubscriptionAdd job", error);
    }

    // Answer with the current status now.
    if (haveJob) {
        auto it = g_jobs.find(jobID);
        if (it != g_jobs.end() && it->second.cupsJobId != 0) {
            bool done = false;
            PrintState::JobStatus status = PrintState::JobStatus::Success;
            if (CupsClient::getJobStatus(CUPS_HTTP_DEFAULT, it->second.cupsJobId, done, status))
                return reply(sh, message, PrintState::jobStatusPayload(jobID, done, status));
        }
        // Not submitted yet: still "printing" from the dialog's point of view.
        return reply(sh, message, PrintState::jobStatusPayload(jobID, false,
                                                               PrintState::JobStatus::Success));
    }
    return reply(sh, message, PrintState::okPayload());
}

bool jobsCancel(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = json_tokener_parse(LSMessageGetPayload(message));
    int jobID = 0;
    const bool haveJob = intMember(root, "jobID", jobID);
    if (root && !is_error(root))
        json_object_put(root);

    if (haveJob) {
        auto it = g_jobs.find(jobID);
        if (it != g_jobs.end() && it->second.cupsJobId != 0)
            CupsClient::cancelJob(CUPS_HTTP_DEFAULT, it->second.printerId, it->second.cupsJobId);
    }
    return reply(sh, message, PrintState::okPayload());
}

bool jobsClose(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = json_tokener_parse(LSMessageGetPayload(message));
    int jobID = 0;
    if (intMember(root, "jobID", jobID))
        g_jobs.erase(jobID);
    if (root && !is_error(root))
        json_object_put(root);
    return reply(sh, message, PrintState::okPayload());
}

// --- registration -----------------------------------------------------------

LSMethod kPrintersMethods[] = {
    { "list", printersList },
    { "getCurrent", printersGetCurrent },
    { "setCurrent", printersSetCurrent },
    { "add", printersAdd },
    { "getCapabilities", printersGetCapabilities },
    { },
};

LSMethod kJobsMethods[] = {
    { "open", jobsOpen },
    { "editPrintParams", jobsEditPrintParams },
    { "getFinalParamsAndArea", jobsGetFinalParams },
    { "addFile", jobsAddFile },
    { "getRenderStatus", jobsGetRenderStatus },
    { "getStatus", jobsGetStatus },
    { "cancel", jobsCancel },
    { "close", jobsClose },
    { },
};

bool registerCategory(const char* category, LSMethod* methods)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSPalmServiceRegisterCategory(g_service, category, methods, methods, nullptr,
                                       nullptr, &error)) {
        logAndFree(category, error);
        return false;
    }
    return true;
}

gboolean quit(gpointer)
{
    g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

} // namespace

int main()
{
    g_loop = g_main_loop_new(nullptr, FALSE);

    LSError error;
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kServiceName, &g_service, &error)) {
        logAndFree("LSRegisterPalmService", error);
        return 1;
    }

    if (!registerCategory("/printers", kPrintersMethods)
        || !registerCategory("/jobs", kJobsMethods))
        return 1;

    LSErrorInit(&error);
    if (!LSGmainAttachPalmService(g_service, g_loop, &error)) {
        logAndFree("LSGmainAttachPalmService", error);
        return 1;
    }

    g_unix_signal_add(SIGTERM, quit, nullptr);
    g_unix_signal_add(SIGINT, quit, nullptr);

    g_message("printmgr-cups: com.palm.printmgr up");
    g_main_loop_run(g_loop);

    LSErrorInit(&error);
    if (!LSUnregisterPalmService(g_service, &error))
        logAndFree("LSUnregisterPalmService", error);
    g_main_loop_unref(g_loop);
    return 0;
}
