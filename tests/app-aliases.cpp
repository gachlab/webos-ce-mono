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
// aliases(), with the fallback returning the first app rather than nothing, or
// with appAnsweringToIn counting aliases, this turns red.

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

void checkTrust(const char* what, bool got, bool wanted)
{
    const bool ok = got == wanted;
    if (!ok)
        ++g_failures;
    std::printf("  %-62s %-26s %s\n", what, got ? "trusted" : "not trusted",
                ok ? "OK" : (wanted ? "<-- FAIL, wanted trusted" : "<-- FAIL, wanted not trusted"));
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

    std::printf("the other question: is this id already taken\n");
    // THE ONE THAT WAS MISSING, and the reason a green test sat on top of a
    // real bug. getAppById answers "who opens when this id is asked for" and
    // includes aliases. Registering and installing ask something else --
    // "is an app registered under exactly this id?" -- and they used the same
    // function. So when the scanner reached HP's own folder, our card's alias
    // made the id look taken and HP's REAL app was discarded. The rule was
    // never wrong; the callers were asking it the wrong question, and no
    // assertion here could see that because none of them asked the second one.
    check("an alias does NOT make an id look taken",
          appAnsweringToIn(registered, "com.palm.app.wifi"), "(nobody)");
    check("an app's own id does", appAnsweringToIn(registered, "com.gachlab.app.wifi"),
          "com.gachlab.app.wifi");
    check("and HP's own app is found by it", appAnsweringToIn(bothSystem, "com.palm.app.wifi"),
          "com.palm.app.wifi");

    std::printf("the awkward ones\n");
    Apps none;
    check("an empty tree finds nothing", appAnsweringTo(none, none, "com.palm.app.wifi"), "(nobody)");
    // An app that lists its own id as an alias: the exact pass answers first,
    // so the alias is never consulted and there is no second answer to give.
    FakeApp* selfish = make("com.gachlab.app.clock", "com.gachlab.app.clock");
    Apps one;
    one.push_back(selfish);
    check("an app aliased to itself is found by its id", appAnsweringTo(one, none, "com.gachlab.app.clock"),
          "com.gachlab.app.clock");
    // An empty alias, which is what a stray comma in an appinfo.json makes.
    // It must not answer to the empty id, and must not answer to everything.
    FakeApp* blank = make("com.gachlab.app.blank", "");
    Apps withBlank;
    withBlank.push_back(blank);
    check("an empty alias answers to nothing", appAnsweringTo(withBlank, none, ""), "(nobody)");
    check("and does not swallow another id", appAnsweringTo(withBlank, none, "com.palm.app.wifi"),
          "(nobody)");

    std::printf("standing in for one of HP's\n");
    // HP decided an app was a platform app by its id alone. Our cards replace
    // HP's and carry our own ids, so the claim moved to the alias -- without
    // this, renaming the packages to com.gachlab.* took their standing away in
    // silence, which is exactly what happened to the Wi-Fi card: it stopped
    // being a platform app and dropped off the launcher's Settings page.
    checkTrust("one of HP's own is trusted, as before", claimsPalmId(browser), true);
    checkTrust("and so is a card that declares HP's id", claimsPalmId(wifi), true);
    checkTrust("an app of ours that claims nothing is not", claimsPalmId(make("com.gachlab.app.kit")), false);
    checkTrust("nor one aliased to something that is not HP's",
               claimsPalmId(make("com.gachlab.app.kit", "com.example.thing")), false);
    checkTrust("and nothing at all is not", claimsPalmId<FakeApp>(0), false);

    std::printf("%s\n", g_failures == 0 ? "all good" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}
