// What com.palm.printmgr puts on the bus, checked without ls-hubd or a printing
// daemon.
//
// enyo's PrintDialog reads each field by name and acts on it: PrinterSelector
// switches on eventType and reads printerID/printerName/printerAddress; PrintJob
// reads jobID and matches jobStatus with == against four fixed words;
// PrintManagerError keys its text off errorCode ranges. A wrong name here is a
// dialog that silently does nothing, not an error, so the fields are checked as
// strings, the way power-state.cpp checks com.palm.power's.
#include "print_state.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-70s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

int main()
{
    using namespace PrintState;

    std::printf("the fields PrinterSelector reads from a printers/list event\n");
    {
        Printer p{ "HP_LaserJet", "HP LaserJet Pro", "ipp://host/printers/hp" };
        const std::string ev = printerEventPayload(p, "Add", true);
        check(contains(ev, "\"eventType\":\"Add\""), "carries the eventType the dialog switches on");
        check(contains(ev, "\"printerID\":\"HP_LaserJet\""), "carries printerID (the dialog skips an event without it)");
        check(contains(ev, "\"printerName\":\"HP LaserJet Pro\""), "carries printerName");
        check(contains(ev, "\"printerAddress\":\"ipp://host/printers/hp\""), "carries printerAddress");
        check(contains(ev, "\"returnValue\":true"), "the first reply carries returnValue");
        const std::string rmv = printerEventPayload(p, "Rmv", false);
        check(contains(rmv, "\"eventType\":\"Rmv\""), "a removal is eventType Rmv");
        check(!contains(rmv, "returnValue"), "a pushed event carries no returnValue");
    }

    std::printf("\nprinters/getCurrent: the three fields, or none\n");
    {
        Printer p{ "epson", "Epson WF", "" };
        const std::string cur = currentPrinterPayload(&p);
        check(contains(cur, "\"printerID\":\"epson\""), "a current printer carries printerID");
        check(contains(cur, "\"returnValue\":true"), "and returnValue");
        const std::string none = currentPrinterPayload(nullptr);
        check(contains(none, "\"returnValue\":true"), "no current printer is still returnValue:true");
        check(!contains(none, "printerID"), "no current printer carries no printerID (the dialog reads that as 'none')");
    }

    std::printf("\nprinters/getCapabilities: the options PrinterOptions reads\n");
    {
        Capabilities c;
        c.mediaType = { "Plain", "Photo" };
        c.mediaSize = { "US_Letter", "ISO_A4" };
        c.quality = { "Normal", "Best" };
        c.canDuplex = true;
        c.hasColor = true;
        const std::string s = capabilitiesPayload(c);
        check(contains(s, "\"mediaType\":[\"Plain\",\"Photo\"]"), "mediaType is a string array");
        check(contains(s, "\"mediaSize\":[\"US_Letter\",\"ISO_A4\"]"), "mediaSize is a string array");
        check(contains(s, "\"quality\":[\"Normal\",\"Best\"]"), "quality is a string array");
        check(contains(s, "\"canDuplex\":true"), "canDuplex is a bool");
        check(contains(s, "\"hasColor\":true"), "hasColor is a bool");

        Capabilities empty;
        const std::string e = capabilitiesPayload(empty);
        check(contains(e, "\"mediaType\":[]"), "an option not offered is an empty array");
        check(contains(e, "\"canDuplex\":false"), "and a false flag");
    }

    std::printf("\njobs/open and jobs/getStatus\n");
    {
        check(contains(jobOpenedPayload(7), "\"jobID\":7"), "jobs/open returns the jobID the dialog threads through");

        const std::string running = jobStatusPayload(7, false, JobStatus::Success);
        check(contains(running, "\"printerState\":\"PRINTING\""), "a running job is not DONE, so the dialog keeps its popup up");

        const std::string done = jobStatusPayload(7, true, JobStatus::Success);
        check(contains(done, "\"printerState\":\"DONE\""), "a finished job is DONE");
        check(contains(done, "\"jobStatus\":\"Success\""), "with a jobStatus matched by == against Success");
        check(contains(jobStatusPayload(7, true, JobStatus::Cancelled), "\"jobStatus\":\"Cancelled\""), "Cancelled is spelled as the dialog matches it");
        check(contains(jobStatusPayload(7, true, JobStatus::Error), "\"jobStatus\":\"Error\""), "Error too");
        check(contains(jobStatusPayload(7, true, JobStatus::Corrupt), "\"jobStatus\":\"Corrupt\""), "Corrupt too");
    }

    std::printf("\nthe error vocabulary PrintManagerError keys off\n");
    {
        check(contains(errorPayload(kErrPrinterNotSupported), "\"errorCode\":-238"), "not-supported is -238");
        check(contains(errorPayload(kErrPrinterNoResponse), "\"errorCode\":-203"), "no-response is -203");
        check(contains(errorPayload(kErrCommunication), "\"errorCode\":-400"), "communication is in the -400s");
        check(contains(errorPayload(kErrGeneric), "\"errorCode\":-1"), "a generic failure is -1");
        check(contains(errorPayload(kErrPrinterNotSupported), "\"returnValue\":false"), "a failure is returnValue:false");
    }

    std::printf("\nescaping, for printer names the host hands us\n");
    {
        Printer p{ "q", "He said \"hi\"", "a\\b" };
        const std::string ev = printerEventPayload(p, "Add", false);
        check(contains(ev, "\\\"hi\\\""), "a quote in a printer name is escaped");
        check(contains(ev, "a\\\\b"), "a backslash is escaped");
    }

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
