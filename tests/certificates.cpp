// The certificate store the Wi-Fi card's TLS login lists.
//
// A directory of PEM files, read with gio: certificates named by their common
// name or, without one, their organization, in file-name order, numbered from
// 1, with anything that is not a certificate left out. The two certificates
// below were made for this test and hold nothing of value.
#include "certificates.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <cstdio>
#include <string>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-66s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static const char kLaptop[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIBsjCCAVmgAwIBAgIUKoYcc0RlTX7N6M2oA9nhjDD9gPgwCgYIKoZIzj0EAwIw
LjEVMBMGA1UEAwwMTGFwdG9wIFdpLUZpMRUwEwYDVQQKDAxFeGFtcGxlIENvcnAw
IBcNMjYwOTE2MTE0NDMwWhgPMjEyNjA4MjMxMTQ0MzBaMC4xFTATBgNVBAMMDExh
cHRvcCBXaS1GaTEVMBMGA1UECgwMRXhhbXBsZSBDb3JwMFkwEwYHKoZIzj0CAQYI
KoZIzj0DAQcDQgAEUMBYDWesxl9k8+d2ON9+d4Qu+lSoSyouhlGfNHXqmQbIDstZ
fwKe5yTC18/6EroUYMiARL/2x+WaH6FBpxN5U6NTMFEwHQYDVR0OBBYEFGjhiOrz
JQ+hZ8zzNh+3D6QmCH64MB8GA1UdIwQYMBaAFGjhiOrzJQ+hZ8zzNh+3D6QmCH64
MA8GA1UdEwEB/wQFMAMBAf8wCgYIKoZIzj0EAwIDRwAwRAIgNhnIYKlGawng5vj2
jNlIKRkyvi8RplfQ7uuUnKU9YlwCIFV8WJtDcmqg/bWzHy3SBnFeAvqC9mjubBAz
7uYx3idC
-----END CERTIFICATE-----
-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg6B2rrLAvhw2SoKv6
4fmyALixlr4Fw0py5gYSV8OehnehRANCAARQwFgNZ6zGX2Tz53Y43353hC76VKhL
Ki6GUZ80deqZBsgOy1l/Ap7nJMLXz/oSuhRgyIBEv/bH5ZofoUGnE3lT
-----END PRIVATE KEY-----
)PEM";

static const char kAcme[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIBgjCCASegAwIBAgIUW6l/FlfsMWqoJPDK4DBwQb8iqhkwCgYIKoZIzj0EAwIw
FTETMBEGA1UECgwKQWNtZSwgSW5jLjAgFw0yNjA5MTYxMTQ0MzBaGA8yMTI2MDgy
MzExNDQzMFowFTETMBEGA1UECgwKQWNtZSwgSW5jLjBZMBMGByqGSM49AgEGCCqG
SM49AwEHA0IABLEzOq2PEt08FZdNuxQfB7rEzmnKo0oW5H/VedZYeycpW2qPgZK0
zExVS/GsCXJH1WsMViHbzmFd66uoUHPhtA6jUzBRMB0GA1UdDgQWBBTQd8oD8c1a
n4vq895dM4nnYkBlHDAfBgNVHSMEGDAWgBTQd8oD8c1an4vq895dM4nnYkBlHDAP
BgNVHRMBAf8EBTADAQH/MAoGCCqGSM49BAMCA0kAMEYCIQCllVxuheFYeYEpxJCJ
wb4g6+kXDaGDI0lPwhM4Z7V6kgIhANiHpEMOshwr+9oDa98bHcx1zxISzITGBQns
l+0C8DBp
-----END CERTIFICATE-----
)PEM";

static void write(const std::string& dir, const char* name, const char* content)
{
    const std::string path = dir + "/" + name;
    g_file_set_contents(path.c_str(), content, -1, nullptr);
}

int main()
{
    gchar* dir = g_dir_make_tmp("certificates-XXXXXX", nullptr);
    const std::string d = dir;
    write(d, "b-laptop.pem", kLaptop);
    write(d, "a-acme.crt", kAcme);
    write(d, "c-junk.pem", "not a certificate\n");
    write(d, "d-notes.txt", kLaptop);

    std::printf("the store\n");
    const std::vector<NmNet::Certificate> found = Certificates::list(d);
    check(found.size() == 2, "the certificates are listed, junk and other files are not");
    check(found.size() == 2 && found[0].path == d + "/a-acme.crt" && found[1].path == d + "/b-laptop.pem",
          "in file-name order, by full path");
    check(found.size() == 2 && found[0].certificateId == 1 && found[1].certificateId == 2, "numbered from 1");
    check(found.size() == 2 && found[1].commonName == "Laptop Wi-Fi" && found[1].organization == "Example Corp",
          "a certificate's common name and organization are read");
    check(found.size() == 2 && found[0].commonName.empty() && found[0].organization == "Acme, Inc.",
          "one with no common name keeps its organization, comma and all");
    check(Certificates::list(d + "/missing").empty(), "a missing directory is an empty store");

    std::printf("add and remove\n");
    gchar* otherDir = g_dir_make_tmp("certificates-src-XXXXXX", nullptr);
    const std::string src = std::string(otherDir) + "/new-laptop.pem";
    write(std::string(otherDir), "new-laptop.pem", kLaptop);
    NmNet::Certificate added;
    std::string error;
    check(Certificates::add(d, src, "", added, error), "add copies a PEM into the store");
    check(added.certificateId == 3 && added.commonName == "Laptop Wi-Fi",
          "the new certificate is listed after the two that were already there");
    check(!Certificates::add(d, src, "secret", added, error)
          && error.find("encrypted") != std::string::npos,
          "a passphrase is refused — the store is plaintext PEM");
    check(Certificates::remove(d, added.certificateId, error), "remove deletes by certificateId");
    check(Certificates::list(d).size() == 2, "after remove the store is back to two");
    check(!Certificates::remove(d, 99, error), "an unknown id fails");
    g_unlink(src.c_str());
    g_rmdir(otherDir);
    g_free(otherDir);

    std::printf("where it is\n");
    g_setenv("WEBOS_CERTIFICATE_DIR", "/somewhere/certs", TRUE);
    check(Certificates::directory() == "/somewhere/certs", "WEBOS_CERTIFICATE_DIR decides");
    g_unsetenv("WEBOS_CERTIFICATE_DIR");
    const std::string fallback = std::string(g_get_user_data_dir()) + "/webos-ce/certificates";
    check(Certificates::directory() == fallback, "otherwise the user's data directory");

    for (const char* name : { "a-acme.crt", "b-laptop.pem", "c-junk.pem", "d-notes.txt" })
        g_unlink((d + "/" + name).c_str());
    g_rmdir(dir);
    g_free(dir);

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
