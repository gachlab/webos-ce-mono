// What com.palm.nettools/findMxRecords returns, checked without a resolver.
//
// mx_resolver.cpp is the half that touches DNS; this is the other half --
// mx_records.h, the ordering and the payload the Email account wizard reads.
// AccountWizard.js keeps the record with the lowest mxPreference, so the field
// names and the number are the contract, and a server name is escaped because
// it comes from the queried domain's own DNS.
#include "mx_records.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-66s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// The index at which needle appears, or std::string::npos.
static size_t at(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle);
}

int main()
{
    std::printf("a bad domain is a request error, not a DNS miss\n");
    {
        check(!NmMx::validDomain(""), "empty domain is rejected");
        check(!NmMx::validDomain("   "), "whitespace-only domain is rejected");
        check(NmMx::validDomain("example.com"), "a real domain is accepted");
    }

    std::printf("records come back ordered by preference, lowest first\n");
    {
        std::vector<NmMx::Record> records = {
            { "mx-backup.example.com", 20 },
            { "mx-primary.example.com", 10 },
            { "mx-mid.example.com", 15 },
        };
        const std::string payload = NmMx::recordsPayload(records);
        // The wizard reads the lowest; an ordered list is the honest answer.
        const size_t primary = at(payload, "mx-primary.example.com");
        const size_t mid = at(payload, "mx-mid.example.com");
        const size_t backup = at(payload, "mx-backup.example.com");
        check(primary != std::string::npos && mid != std::string::npos
                  && backup != std::string::npos, "every server is present");
        check(primary < mid && mid < backup, "ordered by preference, lowest first");
        check(contains(payload, "\"mxPreference\":10"), "the preference number is carried");
        check(contains(payload, "\"returnValue\":true"), "a successful lookup says so");
    }

    std::printf("the field names are the ones AccountWizard.js reads\n");
    {
        std::vector<NmMx::Record> records = { { "mail.example.com", 5 } };
        const std::string payload = NmMx::recordsPayload(records);
        check(contains(payload, "\"mxServer\":\"mail.example.com\""), "mxServer");
        check(contains(payload, "\"mxPreference\":5"), "mxPreference");
    }

    std::printf("a domain that resolves with no MX is an empty success\n");
    {
        std::vector<NmMx::Record> none;
        const std::string payload = NmMx::recordsPayload(none);
        check(contains(payload, "\"returnValue\":true"), "still a success");
        check(contains(payload, "\"mxRecords\":[]"), "with an empty list to try");
    }

    std::printf("a server name from DNS is escaped before it reaches the JSON\n");
    {
        // DNS almost never carries these, but the name is attacker-influenced
        // (it is whatever the queried domain publishes), and an unescaped quote
        // would break the JSON the wizard parses.
        std::vector<NmMx::Record> records = { { "ev\"il\\.example.com", 10 } };
        const std::string payload = NmMx::recordsPayload(records);
        check(contains(payload, "ev\\\"il\\\\.example.com"), "quote and backslash escaped");
    }

    std::printf("\n%s\n", g_failures ? "FAILED" : "all good");
    return g_failures ? 1 : 0;
}
