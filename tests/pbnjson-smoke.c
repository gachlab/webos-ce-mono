// pbnjson's C parser accepts valid JSON and rejects broken JSON.
//
// A truncated document has to come back as an error, not as a value: that is
// what a half-written file or a cut-off bus message looks like.

#include <stdio.h>
#include <string.h>
#include <pbnjson.h>

static int check(const char *name, const char *json, int expectValid) {
    JSchemaInfo info;
    jschema_info_init(&info, jschema_all(), NULL, NULL);
    raw_buffer buf = { .m_str = json, .m_len = strlen(json) };
    jvalue_ref v = jdom_parse(buf, DOMOPT_NOOPT, &info);
    int valid = !jis_null(v);
    int ok = valid == expectValid;
    printf("%-18s %-20s -> %-8s %s\n", name, json, valid ? "parsed" : "rejected", ok ? "OK" : "WRONG");
    j_release(&v);
    return ok ? 0 : 1;
}

int main(void) {
    int failures = 0;
    failures += check("valid object",    "{\"a\":1,\"b\":\"x\"}", 1);
    failures += check("valid array",     "[1,2,3]",             1);
    failures += check("truncated object","{\"a\":1",            0);
    failures += check("truncated array", "[1,2",                0);
    failures += check("garbage",         "{not json}",          0);
    printf("%s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
