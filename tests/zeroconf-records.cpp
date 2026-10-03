// What com.palm.zeroconf answers, and the DNS-SD parsing behind it, checked
// without an Avahi daemon.
//
// avahi_client.cpp is the half that talks to Avahi; this is
// zeroconf_records.h -- the validation that decides whether a request is
// answerable, the TXT splitting, and the payloads a caller reads. The API is
// provisional (no HP caller fixes it), so what is pinned here is the internal
// consistency: a browse list, a resolve with its TXT, an escaped name, and the
// request guards.
#include "zeroconf_records.h"

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

int main()
{
    std::printf("a service type is _service._tcp or _udp\n");
    {
        check(Zeroconf::validServiceType("_ipp._tcp"), "_ipp._tcp is valid");
        check(Zeroconf::validServiceType("_daap._udp"), "_daap._udp is valid");
        check(!Zeroconf::validServiceType("ipp._tcp"), "a type without a leading _ is refused");
        check(!Zeroconf::validServiceType("_ipp._sctp"), "a proto that is not tcp/udp is refused");
        check(!Zeroconf::validServiceType("_ipp"), "a type with no proto is refused");
        check(!Zeroconf::validServiceType(""), "an empty type is refused");
    }

    std::printf("register needs a name, a type and a port in range\n");
    {
        check(Zeroconf::validName("Office Printer"), "a name is accepted");
        check(!Zeroconf::validName(""), "an empty name is refused");
        check(Zeroconf::validPort(631), "a real port is accepted");
        check(!Zeroconf::validPort(0), "port 0 is refused -- nobody can reach it");
        check(!Zeroconf::validPort(70000), "an out-of-range port is refused");
    }

    std::printf("a browse list carries each instance's name, type and domain\n");
    {
        std::vector<Zeroconf::Service> services = {
            { "Office Printer", "_ipp._tcp", "local" },
            { "Lab Printer", "_ipp._tcp", "local" },
        };
        const std::string payload = Zeroconf::browsePayload(services, true, "added");
        check(contains(payload, "\"returnValue\":true"), "a success");
        check(contains(payload, "\"subscribed\":true"), "subscribed is marked when it is");
        check(contains(payload, "\"changed\":\"added\""), "what moved is named");
        check(contains(payload, "\"name\":\"Office Printer\""), "the first instance");
        check(contains(payload, "\"name\":\"Lab Printer\""), "the second instance");
        check(contains(payload, "\"type\":\"_ipp._tcp\""), "the type");
    }

    std::printf("a one-shot browse carries no subscribed/changed, just the set\n");
    {
        std::vector<Zeroconf::Service> none;
        const std::string payload = Zeroconf::browsePayload(none, false, nullptr);
        check(contains(payload, "\"services\":[]"), "an empty set is an empty list");
        check(!contains(payload, "\"subscribed\""), "no subscribed flag on a one-shot");
        check(!contains(payload, "\"changed\""), "no changed on a one-shot");
    }

    std::printf("a resolve carries host, address, port and the TXT pairs\n");
    {
        Zeroconf::Resolved r;
        r.name = "Office Printer";
        r.type = "_ipp._tcp";
        r.domain = "local";
        r.hostname = "printer.local";
        r.address = "192.168.1.20";
        r.port = 631;
        r.txt["rp"] = "ipp/print";
        r.txt["ty"] = "HP LaserJet";
        const std::string payload = Zeroconf::resolvePayload(r);
        check(contains(payload, "\"hostname\":\"printer.local\""), "the hostname");
        check(contains(payload, "\"address\":\"192.168.1.20\""), "the address");
        check(contains(payload, "\"port\":631"), "the port, as a number");
        check(contains(payload, "\"rp\":\"ipp/print\""), "a TXT pair");
        check(contains(payload, "\"ty\":\"HP LaserJet\""), "another TXT pair");
    }

    std::printf("TXT entries split on the first = ; a bare key has an empty value\n");
    {
        std::string key, value;
        check(Zeroconf::parseTxtEntry("rp=ipp/print", key, value) && key == "rp"
                  && value == "ipp/print", "key=value splits on the first =");
        check(Zeroconf::parseTxtEntry("path=/a=b", key, value) && key == "path"
                  && value == "/a=b", "only the first = is the separator");
        check(Zeroconf::parseTxtEntry("airprint", key, value) && key == "airprint"
                  && value.empty(), "a bare key has an empty value");
        check(!Zeroconf::parseTxtEntry("=novalue", key, value), "a leading = (empty key) is dropped");
        check(!Zeroconf::parseTxtEntry("", key, value), "an empty entry is dropped");
    }

    std::printf("an instance name from the network is escaped before the JSON\n");
    {
        // Instance names are free text chosen by whoever published the service,
        // so a quote in one must not break the browse payload a caller parses.
        std::vector<Zeroconf::Service> services = { { "weird\"name\\here", "_ipp._tcp", "local" } };
        const std::string payload = Zeroconf::browsePayload(services, false, nullptr);
        check(contains(payload, "weird\\\"name\\\\here"), "quote and backslash escaped");
    }

    std::printf("\n%s\n", g_failures ? "FAILED" : "all good");
    return g_failures ? 1 : 0;
}
