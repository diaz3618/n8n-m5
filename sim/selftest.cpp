// g++ selftest: placeholder substitution + ISO parsing + URL normalization. Run: make selftest
#include <cassert>
#include <cstdio>
#include "../src/core/util.h"
int main() {
    JsonDocument d;
    deserializeJson(d, "{\"id\":\"a b\",\"name\":\"x\"}");
    assert(util::subst("/t/{id}", d.as<JsonVariantConst>()) == "/t/a%20b");
    assert(util::subst("{\"loadWorkflow\":true}", d.as<JsonVariantConst>(), false) == "{\"loadWorkflow\":true}");
    assert(util::subst("{}", d.as<JsonVariantConst>(), false) == "{}");
    assert(util::subst("Delete {name}?", d.as<JsonVariantConst>(), false) == "Delete x?");
    assert(util::parseIso("2026-10-03T12:00:00.000Z") == 1791028800);
    assert(util::normalizeUrl(" n8n.example.com/api/v1/ ") == "https://n8n.example.com");
    assert(util::normalizeUrl("192.168.1.5:5678") == "http://192.168.1.5:5678");
    assert(util::normalizeUrl("n8n.local") == "http://n8n.local");
    assert(util::normalizeUrl("https://x.io/api/v1") == "https://x.io");
    assert(util::encodePath("/webhook/a b?x=1") == "/webhook/a%20b?x=1");
    puts("selftest ok");
}
