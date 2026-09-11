// pbnjson's JValue built from a std::string, on a modern libstdc++.
//
// HP's constructor asserted that copying the string shared its buffer. Only the
// copy-on-write strings of 2012's libstdc++ did that; today's never do, so the
// assertion aborted, and it took LunaSysMgr down the moment the virtual keyboard
// was enabled. This builds values from a short string (inside the small-string
// buffer) and a long one (on the heap), copies them, lets the originals go and
// reads the copies back.

#include <cstdio>
#include <string>

#include <pbnjson.hpp>

static int check(const char* name, const std::string& text)
{
    pbnjson::JValue* original = new pbnjson::JValue(text);
    pbnjson::JValue copy(*original);
    delete original;

    const bool ok = copy.isString() && copy.asString() == text;
    printf("%-38s %s\n", name, ok ? "OK" : "WRONG");
    return ok ? 0 : 1;
}

int main()
{
    int failures = 0;
    failures += check("short string (small-string buffer)", "abc");
    failures += check("long string (heap buffer)", std::string(200, 'x'));
    const std::string named = "a named std::string, not a temporary";
    failures += check("named string", named);
    printf("%s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
