// Who answers to an app id.
//
// In webOS an app id is an address: the shell, notifications, activities and
// HP's own apps all open an app by id. A card of ours that replaces one of
// HP's has to inherit that address, so it declares the old id in its own
// appinfo.json and ApplicationManager::getAppById resolves it (#63).
//
// The rule has one property that everything else rests on -- an app really
// installed under an id always keeps it -- and that is what most of this
// checks. The rule is a template over anything with id() and aliases(), which
// is why it can be exercised here with a two-field struct instead of an
// ApplicationManager that scans directories.
//
// Verified by mutation: with the alias pass moved in front of the exact pass
// for the second list, with an alias matched against id() instead of
// aliases(), or with the fallback returning the first app rather than nothing,
// this turns red.

#include "AppAliases.h"

#include <cstdio>
#include <list>
#include <string>
#include <vector>

namespace {

struct FakeApp {
    std::string appId;
    std::list<std::string> appAliases;

    const std::string& id() const { return appId; }
    const std::list<std::string>& aliases() const { return appAliases; }
};

int g_failures = 0;

void check(const char* what, const FakeApp* got, const char* wanted)
{
    const std::string answer = got ? got->id() : std::string("(nobody)");
    const bool ok = answer == wanted;
    if (!ok)
        ++g_failures;
    std::printf("  %-62s %-26s %s\n", what, answer.c_str(),
                ok ? "OK" : (std::string("<-- FAIL, wanted ") + wanted).c_str());
}

FakeApp* make(const char* id, const char* alias = 0)
{
    FakeApp* app = new FakeApp();
    app->appId = id;
    if (alias)
        app->appAliases.push_back(alias);
    return app;
}

} // namespace

int main()
{
    typedef std::vector<FakeApp*> Apps;

    // Ours replaces HP's Wi-Fi card and answers to its id as well.
    FakeApp* wifi = make("com.gachlab.app.wifi", "com.palm.app.wifi");
    FakeApp* contacts = make("com.gachlab.app.contacts", "com.palm.app.contacts");
    FakeApp* browser = make("com.palm.app.browser");

    Apps registered;
    registered.push_back(wifi);
    registered.push_back(browser);
    Apps system;
    system.push_back(contacts);

    std::printf("the ordinary cases\n");
    check("its own id finds an app", appAnsweringTo(registered, system, "com.gachlab.app.wifi"),
          "com.gachlab.app.wifi");
    check("an app with no alias is unaffected", appAnsweringTo(registered, system, "com.palm.app.browser"),
          "com.palm.app.browser");
    check("an id nobody claims still finds nothing",
          appAnsweringTo(registered, system, "com.palm.app.nothing"), "(nobody)");

    std::printf("the alias\n");
    // The whole point: the system menu asks for HP's id, as HP wrote it, and
    // the rewrite answers. Without this, SystemMenu.cpp had to be edited.
    check("HP's id reaches the rewrite that declared it",
          appAnsweringTo(registered, system, "com.palm.app.wifi"), "com.gachlab.app.wifi");
    check("and it works in the system list too",
          appAnsweringTo(registered, system, "com.palm.app.contacts"), "com.gachlab.app.contacts");

    std::printf("an alias never takes an address away\n");
    // The safety property. HP's own app is installed as well: it keeps its id,
    // and ours is simply not reached by that name. This is why the fallback
    // runs only after BOTH exact passes rather than after the first.
    FakeApp* hpWifi = make("com.palm.app.wifi");
    Apps bothSystem;
    bothSystem.push_back(hpWifi);
    check("an exact match in the system list beats an alias in the registered one",
          appAnsweringTo(registered, bothSystem, "com.palm.app.wifi"), "com.palm.app.wifi");

    Apps bothRegistered;
    bothRegistered.push_back(wifi);       // ours, aliased, first in the list
    bothRegistered.push_back(hpWifi);     // HP's, exact, second
    check("and an exact match behind an alias in the SAME list still wins",
          appAnsweringTo(bothRegistered, system, "com.palm.app.wifi"), "com.palm.app.wifi");

    std::printf("the awkward ones\n");
    // An app claiming its own id as an alias is harmless, not a loop.
    FakeApp* selfish = make("com.palm.app.clock", "com.palm.app.clock");
    Apps one;
    one.push_back(selfish);
    Apps none;
    check("an app aliased to itself answers once", appAnsweringTo(one, none, "com.palm.app.clock"),
          "com.palm.app.clock");
    check("an empty tree finds nothing", appAnsweringTo(none, none, "com.palm.app.wifi"), "(nobody)");
    check("an empty id finds nothing", appAnsweringTo(registered, system, ""), "(nobody)");

    std::printf("%s\n", g_failures == 0 ? "all good" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}
