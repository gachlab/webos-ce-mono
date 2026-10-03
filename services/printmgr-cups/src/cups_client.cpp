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

#include "cups_client.h"

#include <cups/ppd.h>

#include <cstring>

namespace CupsClient {

namespace {

// The human label for a dest: printer-info if CUPS has it, else the queue name.
std::string displayName(cups_dest_t* dest)
{
    const char* info = cupsGetOption("printer-info", dest->num_options, dest->options);
    if (info && *info)
        return info;
    return dest->name ? dest->name : "";
}

// The address line shown under the name: printer-location, else the device-uri.
std::string address(cups_dest_t* dest)
{
    const char* loc = cupsGetOption("printer-location", dest->num_options, dest->options);
    if (loc && *loc)
        return loc;
    const char* uri = cupsGetOption("device-uri", dest->num_options, dest->options);
    return uri ? uri : "";
}

} // namespace

std::vector<PrintState::Printer> listPrinters(http_t* http)
{
    std::vector<PrintState::Printer> printers;
    cups_dest_t* dests = nullptr;
    const int n = cupsGetDests2(http, &dests);
    for (int i = 0; i < n; ++i) {
        cups_dest_t* d = &dests[i];
        if (!d->name)
            continue;
        PrintState::Printer p;
        p.id = d->name;
        p.name = displayName(d);
        p.address = address(d);
        printers.push_back(std::move(p));
    }
    cupsFreeDests(n, dests);
    return printers;
}

bool getDefaultPrinter(http_t* http, PrintState::Printer& out)
{
    cups_dest_t* dests = nullptr;
    const int n = cupsGetDests2(http, &dests);
    bool found = false;
    for (int i = 0; i < n; ++i) {
        if (dests[i].is_default && dests[i].name) {
            out.id = dests[i].name;
            out.name = displayName(&dests[i]);
            out.address = address(&dests[i]);
            found = true;
            break;
        }
    }
    cupsFreeDests(n, dests);
    return found;
}

PrintState::Capabilities getCapabilities(http_t* http, const std::string& printerId)
{
    PrintState::Capabilities caps;

    cups_dest_t* dests = nullptr;
    const int n = cupsGetDests2(http, &dests);
    cups_dest_t* dest = cupsGetDest(printerId.c_str(), nullptr, n, dests);
    if (!dest) {
        cupsFreeDests(n, dests);
        return caps;
    }

    cups_dinfo_t* dinfo = cupsCopyDestInfo(http, dest);
    if (dinfo) {
        // Colour: supported if the printer advertises more than one
        // print-color-mode, or a non-monochrome one.
        if (cupsCheckDestSupported(http, dest, dinfo, CUPS_PRINT_COLOR_MODE, nullptr)) {
            ipp_attribute_t* modes =
                cupsFindDestSupported(http, dest, dinfo, CUPS_PRINT_COLOR_MODE);
            if (modes) {
                const int count = ippGetCount(modes);
                for (int i = 0; i < count; ++i) {
                    const char* m = ippGetString(modes, i, nullptr);
                    if (m && std::strcmp(m, "monochrome") != 0)
                        caps.hasColor = true;
                }
            }
        }

        // Duplex: supported if sides offers a two-sided value.
        if (cupsCheckDestSupported(http, dest, dinfo, CUPS_SIDES, nullptr)) {
            ipp_attribute_t* sides = cupsFindDestSupported(http, dest, dinfo, CUPS_SIDES);
            if (sides) {
                const int count = ippGetCount(sides);
                for (int i = 0; i < count; ++i) {
                    const char* s = ippGetString(sides, i, nullptr);
                    if (s && std::strncmp(s, "two-sided", 9) == 0)
                        caps.canDuplex = true;
                }
            }
        }

        // Media sizes: the ready/ supported media names, mapped to the dialog's
        // vocabulary where it has one, else passed through.
        if (cupsCheckDestSupported(http, dest, dinfo, CUPS_MEDIA, nullptr)) {
            ipp_attribute_t* media = cupsFindDestSupported(http, dest, dinfo, CUPS_MEDIA);
            if (media) {
                const int count = ippGetCount(media);
                for (int i = 0; i < count; ++i) {
                    const char* m = ippGetString(media, i, nullptr);
                    if (!m)
                        continue;
                    if (std::strstr(m, "letter"))
                        caps.mediaSize.push_back("US_Letter");
                    else if (std::strstr(m, "legal"))
                        caps.mediaSize.push_back("US_Legal");
                    else if (std::strstr(m, "a4"))
                        caps.mediaSize.push_back("ISO_A4");
                    else if (std::strstr(m, "4x6") || std::strstr(m, "photo"))
                        caps.mediaSize.push_back("Photo_4x6");
                }
            }
        }

        // Media types.
        if (cupsCheckDestSupported(http, dest, dinfo, CUPS_MEDIA_TYPE, nullptr)) {
            ipp_attribute_t* types = cupsFindDestSupported(http, dest, dinfo, CUPS_MEDIA_TYPE);
            if (types) {
                const int count = ippGetCount(types);
                for (int i = 0; i < count; ++i) {
                    const char* t = ippGetString(types, i, nullptr);
                    if (!t)
                        continue;
                    if (std::strstr(t, "photographic") || std::strstr(t, "photo"))
                        caps.mediaType.push_back("Photo");
                    else if (std::strstr(t, "stationery") || std::strstr(t, "plain"))
                        caps.mediaType.push_back("Plain");
                }
            }
        }

        // Quality: print-quality draft/normal/high -> the three bool flags the
        // dialog's PrintQualityPicker reads.
        if (cupsCheckDestSupported(http, dest, dinfo, CUPS_PRINT_QUALITY, nullptr)) {
            ipp_attribute_t* q = cupsFindDestSupported(http, dest, dinfo, CUPS_PRINT_QUALITY);
            if (q) {
                const int count = ippGetCount(q);
                for (int i = 0; i < count; ++i) {
                    const int v = ippGetInteger(q, i);
                    if (v == IPP_QUALITY_DRAFT)
                        caps.canPrintQualityDraft = true;
                    else if (v == IPP_QUALITY_NORMAL)
                        caps.canPrintQualityNormal = true;
                    else if (v == IPP_QUALITY_HIGH)
                        caps.canPrintQualityHigh = true;
                }
            }
        }

        cupsFreeDestInfo(dinfo);
    }

    cupsFreeDests(n, dests);
    return caps;
}

int errorCodeFromIpp(ipp_status_t status)
{
    switch (status) {
    case IPP_STATUS_OK:
        return 0;
    case IPP_STATUS_ERROR_NOT_FOUND:
    case IPP_STATUS_ERROR_NOT_POSSIBLE:
        return PrintState::kErrPrinterNotSupported;
    case IPP_STATUS_ERROR_NOT_AUTHORIZED:
    case IPP_STATUS_ERROR_FORBIDDEN:
        return PrintState::kErrPrinterNoResponse;
    case IPP_STATUS_ERROR_SERVICE_UNAVAILABLE:
    case IPP_STATUS_ERROR_BUSY:
        return PrintState::kErrCommunication;
    default:
        // CUPS also reports transport failures as negative http-ish codes via
        // cupsLastError(); anything we did not name maps to the generic
        // communication error if it is in that family, else generic.
        if (status >= IPP_STATUS_ERROR_INTERNAL)
            return PrintState::kErrGeneric;
        return PrintState::kErrCommunication;
    }
}

int printFile(http_t* /*http*/, const std::string& printerId, const std::string& file,
              const std::string& title, const PrintState::PrintOptions& options,
              int& errorCode)
{
    // Translate the dialog's option vocabulary to CUPS/IPP options, which
    // cupsPrintFile applies to the job. Only the ones the dialog set are passed;
    // the rest fall to the printer default.
    cups_option_t* opts = nullptr;
    int numOpts = 0;
    if (options.numCopies > 0)
        numOpts = cupsAddOption("copies", std::to_string(options.numCopies).c_str(), numOpts, &opts);
    if (!options.mediaSize.empty()) {
        // The dialog's names map to PWG media keywords CUPS understands.
        const char* pwg = nullptr;
        if (options.mediaSize == "US_Letter")      pwg = "na_letter_8.5x11in";
        else if (options.mediaSize == "US_Legal")  pwg = "na_legal_8.5x14in";
        else if (options.mediaSize == "ISO_A4")    pwg = "iso_a4_210x297mm";
        else if (options.mediaSize == "Photo_4x6") pwg = "na_index-4x6_4x6in";
        if (pwg)
            numOpts = cupsAddOption("media", pwg, numOpts, &opts);
    }
    if (!options.mediaType.empty()) {
        const char* mt = (options.mediaType == "Photo") ? "photographic" : "stationery";
        numOpts = cupsAddOption("media-type", mt, numOpts, &opts);
    }
    if (!options.duplex.empty()) {
        const char* sides = (options.duplex == "None") ? "one-sided" : "two-sided-long-edge";
        numOpts = cupsAddOption("sides", sides, numOpts, &opts);
    }
    if (!options.color.empty()) {
        const char* mode = (options.color == "Mono") ? "monochrome" : "color";
        numOpts = cupsAddOption("print-color-mode", mode, numOpts, &opts);
    }
    if (!options.printQuality.empty()) {
        const char* q = (options.printQuality == "Fast") ? "3"
                      : (options.printQuality == "Best") ? "5" : "4"; // draft/high/normal
        numOpts = cupsAddOption("print-quality", q, numOpts, &opts);
    }

    const int jobId = cupsPrintFile(printerId.c_str(), file.c_str(),
                                    title.empty() ? "webOS" : title.c_str(),
                                    numOpts, opts);
    cupsFreeOptions(numOpts, opts);
    if (jobId == 0) {
        errorCode = errorCodeFromIpp(cupsLastError());
        if (errorCode == 0)
            errorCode = PrintState::kErrGeneric;
        return 0;
    }
    errorCode = 0;
    return jobId;
}

bool getJobStatus(http_t* http, int jobId, bool& done, PrintState::JobStatus& status)
{
    // Ask for all jobs (not just active) so a finished one is still reported.
    cups_job_t* jobs = nullptr;
    const int n = cupsGetJobs2(http, &jobs, nullptr, 0, CUPS_WHICHJOBS_ALL);
    bool found = false;
    for (int i = 0; i < n; ++i) {
        if (jobs[i].id != jobId)
            continue;
        found = true;
        switch (jobs[i].state) {
        case IPP_JSTATE_COMPLETED:
            done = true;
            status = PrintState::JobStatus::Success;
            break;
        case IPP_JSTATE_CANCELED:
            done = true;
            status = PrintState::JobStatus::Cancelled;
            break;
        case IPP_JSTATE_ABORTED:
            done = true;
            status = PrintState::JobStatus::Error;
            break;
        case IPP_JSTATE_PENDING:
        case IPP_JSTATE_HELD:
        case IPP_JSTATE_PROCESSING:
        case IPP_JSTATE_STOPPED:
        default:
            done = false;
            status = PrintState::JobStatus::Success; // not read while !done
            break;
        }
        break;
    }
    cupsFreeJobs(n, jobs);
    return found;
}

bool cancelJob(http_t* http, const std::string& printerId, int jobId)
{
    const ipp_status_t r = cupsCancelJob2(http, printerId.c_str(), jobId, 0);
    return r == IPP_STATUS_OK;
}

} // namespace CupsClient
