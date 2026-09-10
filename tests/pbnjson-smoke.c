#include <stdio.h>
#include <string.h>
#include <pbnjson.h>

static void probar(const char *nombre, const char *json, int se_espera_valido) {
    JSchemaInfo info;
    jschema_info_init(&info, jschema_all(), NULL, NULL);
    raw_buffer buf = { .m_str = json, .m_len = strlen(json) };
    jvalue_ref v = jdom_parse(buf, DOMOPT_NOOPT, &info);
    int valido = !jis_null(v);
    printf("%-28s %-30s -> %-9s  %s\n", nombre, json,
           valido ? "PARSEA" : "RECHAZA",
           (valido == se_espera_valido) ? "OK" : "*** MAL ***");
    j_release(&v);
}

int main(void) {
    probar("objeto valido",      "{\"a\":1,\"b\":\"x\"}", 1);
    probar("arreglo valido",     "[1,2,3]",              1);
    probar("TRUNCADO (el riesgo)","{\"a\":1",             0);
    probar("arreglo truncado",   "[1,2",                 0);
    probar("basura",             "{no es json}",         0);
    return 0;
}
