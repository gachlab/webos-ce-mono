#include <stdio.h>
#include <string.h>
#include <pbnjson.h>

static void check(const char *name, const char *json, int expectValid) {
    JSchemaInfo info;
    jschema_info_init(&info, jschema_all(), NULL, NULL);
    raw_buffer buf = { .m_str = json, .m_len = strlen(json) };
    jvalue_ref v = jdom_parse(buf, DOMOPT_NOOPT, &info);
    int valido = !jis_null(v);
    printf("%-28s %-30s -> %-9s  %s\n", name, json,
           valido ? "PARSEA" : "RECHAZA",
           (valido == expectValid) ? "OK" : "*** MAL ***");
    j_release(&v);
}

int main(void) {
    check("valid object",      "{\"a\":1,\"b\":\"x\"}", 1);
    check("valid array",     "[1,2,3]",              1);
    check("TRUNCATED (the risk)","{\"a\":1",             0);
    check("truncated array",   "[1,2",                 0);
    check("garbage",             "{not json}",         0);
    return 0;
}
