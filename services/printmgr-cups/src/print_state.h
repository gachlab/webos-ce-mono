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

#ifndef PRINTMGR_CUPS_PRINT_STATE_H
#define PRINTMGR_CUPS_PRINT_STATE_H

//
// What CUPS says about printers and jobs, and the payloads com.palm.printmgr
// has to answer with. Header-only and free of both the Luna bus and CUPS on
// purpose, so every payload decision can be checked without ls-hubd and without
// a printing daemon.
//
// com.palm.printmgr was HP's printmgr, built on HP's wprint. Nothing in the CE
// drop answers it, so the one surviving consumer -- enyo's PrintDialog
// (components/enyo-1.0/framework/lib/printdialog) -- calls into a void: the Print
// button in Email and the browser does nothing. This provides the service; the
// Print Manager card (com.palm.app.printmanager) is the other half of the
// ticket.
//
// The field names are not a choice. Each is read by the dialog by name, and the
// dialog was read request-by-request rather than guessed:
//
//   printers/list      PrinterSelector.findPrinterSuccess switches on
//                      eventType ("Add"/"Rmv") and reads printerID,
//                      printerName, printerAddress. A printer with no printerID
//                      is skipped.
//   printers/getCurrent  same three fields; a reply with no printerID is taken
//                      as "no current printer".
//   printers/getCapabilities  PrinterOptions.showPrinterOptions reads mediaType
//                      (array), mediaSize (array), canDuplex (bool), hasColor
//                      (bool), and the quality list.
//   jobs/open          PrintJob.openJobSuccess reads jobID.
//   jobs/getStatus     PrintJob.getStatusSuccess reads jobID, printerState
//                      (acts only on "DONE") and jobStatus, which it matches
//                      with == against "Success", "Cancelled", "Error",
//                      "Corrupt" (anything else is treated as an error).
//   failure            PrintManagerError.getErrorText reads errorCode and keys
//                      its text off exact ranges; see errorCode() below.
//

#include <cstdio>
#include <string>
#include <vector>

namespace PrintState {

// The per-job options the dialog sets with editPrintParams, in the dialog's own
// vocabulary (PrinterOptions.getPrinterSettings). Applied at submit time as CUPS
// job options. 0/empty means "not set", left to the printer default.
struct PrintOptions {
    int numCopies = 0;                 // >0 to set
    std::string mediaSize;             // US_Letter, ISO_A4, Photo_4x6, ...
    std::string mediaType;             // Plain, Photo, ...
    std::string duplex;                // "Book" (two-sided long edge) | "None"
    std::string color;                 // "Color" | "Mono"
    std::string printQuality;          // Fast | Normal | Best
};

// A printer as the dialog lists it. id is the CUPS queue name (opaque to the
// dialog, sent back in setCurrent/open); name is the human label; address is
// the device-uri/location shown under the name.
struct Printer {
    std::string id;
    std::string name;
    std::string address;
};

// What a printer can do, for printers/getCapabilities. Empty arrays and false
// flags mean "option not offered", which the dialog hides. Quality is three
// booleans, not an array: PrintQualityPicker reads caps.canPrintQualityDraft/
// Normal/High off the whole caps object (PrinterOptions passes it setItems(caps)).
struct Capabilities {
    std::vector<std::string> mediaType;   // Plain, Photo, ...
    std::vector<std::string> mediaSize;   // US_Letter, ISO_A4, ...
    bool canDuplex = false;
    bool hasColor = false;
    bool canPrintQualityDraft = false;
    bool canPrintQualityNormal = false;
    bool canPrintQualityHigh = false;
};

// The lifecycle of a print job as the dialog observes it. The dialog acts only
// when printerState == "DONE"; jobStatus is then one of the four fixed words.
enum class JobStatus { Success, Cancelled, Error, Corrupt };

inline const char* jobStatusWord(JobStatus s)
{
    switch (s) {
    case JobStatus::Success:   return "Success";
    case JobStatus::Cancelled: return "Cancelled";
    case JobStatus::Error:     return "Error";
    case JobStatus::Corrupt:   return "Corrupt";
    }
    return "Error";
}

// The error vocabulary PrintManagerError.getErrorText keys its text off. The
// dialog only distinguishes ranges and a handful of specific codes, so these
// are the ones worth naming; any other negative code lands on the generic
// "printing error with number".
enum ErrorCode {
    kErrPrinterNoResponse   = -203, // PM_ERR_PRINTER_NO_RESPONSE_MANUAL
    kErrPrinterDuplicateId  = -204, // PM_ERR_PRINTER_DUPLICATE_ID
    kErrPrinterDuplicateIp  = -205, // PM_ERR_PRINTER_DUPLICATE_IP
    kErrPrinterIpNotValid   = -206, // PM_ERR_PRINTER_IP_NOT_VALID
    kErrJobTempFileNoRoom   = -233, // PM_ERR_JOB_TEMP_FILE_NO_ROOM
    kErrPrinterNotSupported = -238, // PM_ERR_PRINTER_NOT_SUPPORTED
    kErrJobNoJobHandles     = -243, // PM_ERR_JOB_NO_JOB_HANDLES
    kErrNoWifiConnection    = -298, // PM_ERR_NO_WIFI_CONNECTION
    kErrCommunication       = -400, // -400..-499 -> COMMUNICATION_ERROR
    kErrGeneric             = -1,   // anything else -> PRINTING_ERROR_WITH_NUMBER
};

// A JSON string literal escaped for embedding. Printer names and addresses come
// from CUPS and can carry quotes, backslashes and non-ASCII, so every string
// that goes into a payload passes through here.
inline std::string jsonEscape(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 2);
    for (char ch : in) {
        switch (ch) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += ch;     break;
        }
    }
    return out;
}

inline std::string quoted(const std::string& s)
{
    return std::string("\"") + jsonEscape(s) + "\"";
}

// One entry of a printers/list subscription. eventType is "Add" or "Rmv"; the
// dialog adds or removes the printer by printerID accordingly.
inline std::string printerEventPayload(const Printer& p, const char* eventType,
                                       bool withReturnValue)
{
    std::string s = "{";
    if (withReturnValue)
        s += "\"returnValue\":true,";
    s += std::string("\"eventType\":\"") + eventType + "\",";
    s += "\"printerID\":" + quoted(p.id) + ",";
    s += "\"printerName\":" + quoted(p.name) + ",";
    s += "\"printerAddress\":" + quoted(p.address);
    s += "}";
    return s;
}

// printers/getCurrent. A current printer is the three fields; no current printer
// is returnValue:true with no printerID, which the dialog reads as "none".
inline std::string currentPrinterPayload(const Printer* p)
{
    if (!p)
        return "{\"returnValue\":true}";
    std::string s = "{\"returnValue\":true,";
    s += "\"printerID\":" + quoted(p->id) + ",";
    s += "\"printerName\":" + quoted(p->name) + ",";
    s += "\"printerAddress\":" + quoted(p->address);
    s += "}";
    return s;
}

inline std::string jsonStringArray(const std::vector<std::string>& items)
{
    std::string s = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i)
            s += ",";
        s += quoted(items[i]);
    }
    s += "]";
    return s;
}

// printers/getCapabilities. Quality is three booleans the dialog's
// PrintQualityPicker reads off the caps object (canPrintQualityDraft/Normal/
// High), not an array; media type/size are arrays its pickers call setItems on.
inline std::string capabilitiesPayload(const Capabilities& c)
{
    std::string s = "{\"returnValue\":true,";
    s += "\"mediaType\":" + jsonStringArray(c.mediaType) + ",";
    s += "\"mediaSize\":" + jsonStringArray(c.mediaSize) + ",";
    s += std::string("\"canDuplex\":") + (c.canDuplex ? "true" : "false") + ",";
    s += std::string("\"hasColor\":") + (c.hasColor ? "true" : "false") + ",";
    s += std::string("\"canPrintQualityDraft\":") + (c.canPrintQualityDraft ? "true" : "false") + ",";
    s += std::string("\"canPrintQualityNormal\":") + (c.canPrintQualityNormal ? "true" : "false") + ",";
    s += std::string("\"canPrintQualityHigh\":") + (c.canPrintQualityHigh ? "true" : "false");
    s += "}";
    return s;
}

// jobs/open reply: the dialog reads jobID and threads it through every later
// call.
inline std::string jobOpenedPayload(int jobID)
{
    return std::string("{\"returnValue\":true,\"jobID\":") + std::to_string(jobID) + "}";
}

// jobs/getRenderStatus (documents). DocumentPrintJob.getRenderStatusSuccess
// reads jobID, currentPage, totalPages, and acts on renderResultCode: 0 is
// RENDER_STATUS_DONE (close the job), negative is an error. Until the file is
// submitted the dialog just updates its progress; we send done when the pages
// are in CUPS.
inline std::string renderStatusPayload(int jobID, int currentPage, int totalPages,
                                       bool done, int renderResultCode)
{
    std::string s = "{\"returnValue\":true,";
    s += "\"jobID\":" + std::to_string(jobID) + ",";
    s += "\"currentPage\":" + std::to_string(currentPage) + ",";
    s += "\"totalPages\":" + std::to_string(totalPages);
    if (done)
        s += ",\"renderResultCode\":" + std::to_string(renderResultCode);
    s += "}";
    return s;
}

// jobs/getStatus. The dialog acts only on printerState "DONE"; before that a
// job is still running and the dialog keeps its progress popup up.
inline std::string jobStatusPayload(int jobID, bool done, JobStatus status)
{
    std::string s = "{\"returnValue\":true,";
    s += "\"jobID\":" + std::to_string(jobID) + ",";
    s += std::string("\"printerState\":\"") + (done ? "DONE" : "PRINTING") + "\",";
    s += std::string("\"jobStatus\":\"") + jobStatusWord(status) + "\"";
    s += "}";
    return s;
}

// A plain success reply, for the fire-and-forget methods (setCurrent, close,
// cancel, editPrintParams, addFile).
inline std::string okPayload()
{
    return "{\"returnValue\":true}";
}

// A failure the dialog maps through PrintManagerError.getErrorText.
inline std::string errorPayload(int errorCode)
{
    return std::string("{\"returnValue\":false,\"errorCode\":") + std::to_string(errorCode) + "}";
}

} // namespace PrintState

#endif // PRINTMGR_CUPS_PRINT_STATE_H
