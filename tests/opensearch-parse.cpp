// An OpenSearch description's search and suggestion URLs land where they
// belong (#9).
//
// HP's parser read xmlStrcmp's result the wrong way round, so a <Url
// rel="suggestions"> became the search URL and rel="results" was refused.
// Compiled from the service's own sources, less its main().
//
// Verified by mutation: putting either comparison back as it was turns this
// red.

#include <cstdio>
#include <cstdlib>
#include <string>

#include <glib.h>
#include <cjson/json.h>
#include <lunaservice.h>

#include "OpenSearchHandler.h"

// Main.cpp's, which the service's other sources refer to.
GMainLoop* gMainLoop = NULL;

static int failures = 0;

static void check(const char* what, const std::string& got, const std::string& expected)
{
    const bool ok = got == expected;
    if (!ok)
        ++failures;
    std::printf("%-52s %-40s %s\n", what, got.c_str(), ok ? "ok" : ("FAILED, wanted " + expected).c_str());
}

static std::string field(json_object* list, const std::string& id, const char* name)
{
    json_object* options = json_object_object_get(list, "Options");
    for (int i = 0; options && i < json_object_array_length(options); ++i) {
        json_object* item = json_object_array_get_idx(options, i);
        if (id == json_object_get_string(json_object_object_get(item, "id")))
            return json_object_get_string(json_object_object_get(item, name));
    }
    return "<missing>";
}

static std::string write(const gchar* dir, const char* name, const char* text)
{
    gchar* path = g_build_filename(dir, name, NULL);
    g_file_set_contents(path, text, -1, NULL);
    std::string result(path);
    g_free(path);
    return result;
}

int main()
{
    gchar* dir = g_dir_make_tmp("opensearch-XXXXXX", NULL);
    if (!dir)
        return 1;

    // As engines publish them: rel on both, the suggestions one first.
    const std::string both = write(dir, "both.xml",
        "<?xml version='1.0'?>"
        "<OpenSearchDescription xmlns='http://a9.com/-/spec/opensearch/1.1/'>"
        "<ShortName>Both</ShortName>"
        "<Url rel='suggestions' type='application/x-suggestions+json' template='https://s.example/ac?q={searchTerms}'/>"
        "<Url rel='results' type='text/html' template='https://s.example/?q={searchTerms}'/>"
        "</OpenSearchDescription>");
    // Without rel, the type alone decides, as before.
    const std::string typed = write(dir, "typed.xml",
        "<?xml version='1.0'?>"
        "<OpenSearchDescription xmlns='http://a9.com/-/spec/opensearch/1.1/'>"
        "<ShortName>Typed</ShortName>"
        "<Url type='text/html' template='https://t.example/?q={searchTerms}'/>"
        "<Url type='application/x-suggestions+json' template='https://t.example/ac?q={searchTerms}'/>"
        "</OpenSearchDescription>");
    // Any other rel is not a URL this cares about.
    const std::string other = write(dir, "other.xml",
        "<?xml version='1.0'?>"
        "<OpenSearchDescription xmlns='http://a9.com/-/spec/opensearch/1.1/'>"
        "<ShortName>Other</ShortName>"
        "<Url rel='self' type='application/opensearchdescription+xml' template='https://o.example/os.xml'/>"
        "<Url rel='results' type='text/html' template='https://o.example/?q={searchTerms}'/>"
        "</OpenSearchDescription>");

    OpenSearchHandler* handler = OpenSearchHandler::instance();
    check("a description with rel on both URLs is accepted", handler->parseXml(both, true) ? "yes" : "no", "yes");
    handler->parseXml(typed, true);
    handler->parseXml(other, true);

    json_object* list = handler->getOpenSearchList();
    check("rel=results is the search URL", field(list, both, "searchUrl"), "https://s.example/?q={searchTerms}");
    check("rel=suggestions is the suggestion URL", field(list, both, "suggestionUrl"),
          "https://s.example/ac?q={searchTerms}");
    check("without rel, text/html is the search URL", field(list, typed, "searchUrl"),
          "https://t.example/?q={searchTerms}");
    check("and the suggestions type the suggestion URL", field(list, typed, "suggestionUrl"),
          "https://t.example/ac?q={searchTerms}");
    check("another rel does not replace the search URL", field(list, other, "searchUrl"),
          "https://o.example/?q={searchTerms}");
    json_object_put(list);

    return failures == 0 ? 0 : 1;
}
