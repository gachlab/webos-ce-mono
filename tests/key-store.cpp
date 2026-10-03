// What com.palm.keymanager promises, and how a keyname maps onto a Secret
// Service item, checked without a keyring and without the bus.
//
// secret-client.cpp is the half that talks to org.freedesktop.secrets; this is
// key_store.h -- the validation that decides whether a request is answerable,
// and the payloads the accounts service's KeyStore reads named fields out of
// (keydata on fetch) or treats as a thrown future (a missing key). The field
// names and the returnValue are the contract.
#include "key_store.h"

#include <cstdio>
#include <string>

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
    std::printf("a keyname is required; an empty one is not a key but the whole store\n");
    {
        check(!KeyStore::validKeyname(""), "empty keyname is rejected");
        check(KeyStore::validKeyname("com.palm.app.email:bob@example.com"), "a real keyname is accepted");
    }

    std::printf("a store needs a keyname and a blob\n");
    {
        KeyStore::StoreRequest ok;
        ok.keyname = "acct1";
        ok.keydata = "{\"token\":\"abc\"}";
        check(KeyStore::validStore(ok), "keyname + keydata is storable");

        KeyStore::StoreRequest noKey;
        noKey.keydata = "x";
        check(!KeyStore::validStore(noKey), "no keyname is refused");

        KeyStore::StoreRequest noData;
        noData.keyname = "acct1";
        check(!KeyStore::validStore(noData), "no keydata is refused");
    }

    std::printf("type and nohide are carried but never gate a store\n");
    {
        // HP's callers send type:\"ASCIIBLOB\" and nohide:true; the host keyring
        // stores an opaque secret, so refusing one for its declared type would
        // reject credentials the accounts service depends on keeping.
        KeyStore::StoreRequest r;
        r.keyname = "acct1";
        r.keydata = "x";
        r.type = "SOMETHING_UNKNOWN";
        r.nohide = false;
        check(KeyStore::validStore(r), "an unknown type does not block the store");
    }

    std::printf("fetchKey returns the blob verbatim under keydata\n");
    {
        const std::string blob = "{\"token\":\"abc\"}";
        const std::string payload = KeyStore::fetchPayload(blob);
        check(contains(payload, "\"returnValue\":true"), "a found key is a success");
        // getCredentials does JSON.parse(result.keydata), so the blob rides as a
        // JSON string value and its quotes are escaped.
        check(contains(payload, "\"keydata\":\"{\\\"token\\\":\\\"abc\\\"}\""),
              "the blob is the keydata value, escaped");
    }

    std::printf("a missing key is a failure the accounts KeyStore catches\n");
    {
        // getCredentials and hasCredentials both key off the call failing, not
        // off the text; returnValue:false is what their future turns into
        // "not found" / has()=false.
        const std::string payload = KeyStore::errorPayload(KeyStore::notFoundMessage());
        check(contains(payload, "\"returnValue\":false"), "not a success");
        check(contains(payload, KeyStore::notFoundMessage()), "with the honest reason");
    }

    std::printf("keyInfo for an existing key is a bare success\n");
    {
        // hasCredentials reads nothing out of it beyond the call not throwing.
        const std::string payload = KeyStore::keyInfoPayload();
        check(contains(payload, "\"returnValue\":true"), "exists -> success");
    }

    std::printf("a keydata blob with a quote survives the round trip into JSON\n");
    {
        // The accounts service hands JSON.stringify output; without escaping, a
        // quote in it would break the reply it then JSON.parses.
        const std::string payload = KeyStore::fetchPayload("a\"b\\c");
        check(contains(payload, "\"keydata\":\"a\\\"b\\\\c\""), "quote and backslash escaped");
    }

    std::printf("\n%s\n", g_failures ? "FAILED" : "all good");
    return g_failures ? 1 : 0;
}
