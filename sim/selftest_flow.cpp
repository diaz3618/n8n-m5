// g++ selftest for core/flowdoc + core/webhooks (no LVGL).  Run: sim/run_selftest.sh
#include <cassert>
#include <cstdio>
#include <map>
#include "../src/core/flowdoc.h"
#include "../src/core/webhooks.h"
#include "../src/core/plat.h"
namespace plat {
time_t now() { return 1791000000; }
uint32_t random32() { static uint32_t x = 12345; x = x * 1664525u + 1013904223u; return x >> 8; }
static std::map<std::string, std::string> kv;
std::string getStr(const char* k, const char* d) { return kv.count(k) ? kv[k] : d; }
void putStr(const char* k, const std::string& v) { kv[k] = v; }
}
int main() {
    JsonDocument wf;
    deserializeJson(wf, R"({"name":"T","nodes":[
      {"id":"1","name":"Webhook","type":"n8n-nodes-base.webhook","typeVersion":2,"position":[0,0],"webhookId":"abc","parameters":{"httpMethod":"POST","path":"hello","authentication":"headerAuth","responseMode":"responseNode"},"credentials":{"httpHeaderAuth":{"id":"1","name":"My key"}}},
      {"id":"2","name":"Code","type":"n8n-nodes-base.code","typeVersion":2,"position":[200,0],"parameters":{"jsCode":"const b = $json.body ?? $json;\nreturn { text: b.message, who: $json.query.user, id: $json.headers['x-id'] };"}},
      {"id":"3","name":"Respond","type":"n8n-nodes-base.respondToWebhook","typeVersion":1.1,"position":[400,0],"parameters":{"respondWith":"json","options":{"responseCode":201}}}],
      "connections":{"Webhook":{"main":[[{"node":"Code","type":"main","index":0}]]},"Code":{"main":[[{"node":"Respond","type":"main","index":0}]]}},
      "settings":{"executionOrder":"v1","bogus":1},"active":true,"id":"W1"})");
    std::vector<wh::Endpoint> eps;
    wh::discover(wf.as<JsonObjectConst>(), eps);
    assert(eps.size() == 1 && eps[0].path == "hello" && eps[0].auth == "headerAuth" && eps[0].methods[0] == "POST" && eps[0].url() == "/webhook/hello");
    assert(eps[0].credName == "My key");
    wh::analyze(wf.as<JsonObjectConst>(), eps[0]);
    bool hasMsg = false, hasUser = false, hasHdr = false;
    for (auto& f : eps[0].fields) { hasMsg |= f.name == "message" && f.where == "body"; hasUser |= f.name == "user" && f.where == "query"; hasHdr |= f.name == "x-id" && f.where == "header"; }
    assert(hasMsg && hasUser && hasHdr);
    assert(eps[0].respondCode == 201 && eps[0].respond == "JSON");
    wh::Secret s; s.header = "X-Key"; s.value = "v";
    auto h = wh::authHeaders(eps[0], s); assert(h.size() == 1 && h[0].first == "X-Key");
    wh::saveSecret("a/b", s); assert(wh::loadSecret("a/b").value == "v");
    assert(wh::replyText("{\"choices\":[{\"message\":{\"content\":\"hi\"}}]}") == "hi");
    assert(wh::replyText("[{\"output\":\"yo\"}]") == "yo");
    // no webhookId: workflow id + lower-case node name prefix
    JsonDocument w2; deserializeJson(w2, R"({"id":"W9","name":"X","active":true,"nodes":[{"name":"My Hook","type":"n8n-nodes-base.webhook","parameters":{"path":"p"}}]})");
    eps.clear(); wh::discover(w2.as<JsonObjectConst>(), eps); assert(eps[0].path == "W9/my%20hook/p");
    assert(flow::nodeIndex(wf, "Code") == 1);
    assert(flow::uniqueName(wf, "Code") == "Code1");
    JsonObject n = flow::addNode(wf, flow::templates()[4], 600, 0);   // HTTP Request
    assert(std::string(n["name"].as<const char*>()) == "HTTP Request");
    assert(flow::connect(wf, "Respond", 0, "HTTP Request"));
    assert(!flow::connect(wf, "Respond", 0, "HTTP Request"));   // duplicate
    assert(flow::links(wf).size() == 3);
    assert(flow::rename(wf, "Code", "Transform"));
    assert(flow::links(wf)[0].to == "Transform" || flow::links(wf)[1].to == "Transform");
    assert(flow::removeNode(wf, "Transform"));
    assert(flow::links(wf).size() == 1 && wf["nodes"].size() == 3);
    std::string body = flow::putBody(wf);
    JsonDocument b; deserializeJson(b, body);
    assert(b["settings"]["executionOrder"] == "v1" && b["settings"]["bogus"].isNull() && b["active"].isNull() && b["id"].isNull());
    assert(flow::category("n8n-nodes-base.webhook") == flow::Cat::Trigger && flow::category("@n8n/n8n-nodes-langchain.agent") == flow::Cat::Ai);
    puts("selftest_flow ok");
}
