#include "flowdoc.h"

#include <algorithm>

#include "plat.h"

namespace flow {

static std::string S(JsonVariantConst v, const char* k) {
    const char* c = v[k] | (const char*)nullptr;
    return c ? c : "";
}

std::string shortType(const std::string& type) {
    size_t i = type.rfind('.');
    return i == std::string::npos ? type : type.substr(i + 1);
}

static bool has(const std::string& s, const char* t) { return s.find(t) != std::string::npos; }

Cat category(const std::string& type) {
    std::string st = shortType(type);
    if (has(type, "n8n-nodes-langchain") && !has(st, "Trigger") && st != "chat") return Cat::Ai;
    if (has(st, "Trigger") || has(st, "trigger") || st == "webhook" || st == "cron" || st == "start" || st == "form") return Cat::Trigger;
    if (st == "if" || st == "switch" || st == "merge" || st == "filter" || st == "splitInBatches" || st == "wait" || st == "stopAndError" ||
        st == "executeWorkflow" || st == "respondToWebhook" || st == "noOp")
        return Cat::Logic;
    if (st == "code" || st == "function" || st == "functionItem" || st == "executeCommand") return Cat::Code;
    if (st == "httpRequest" || st == "graphql" || st == "ssh" || st == "ftp") return Cat::Http;
    if (st == "set" || st == "editFields" || st == "aggregate" || st == "sort" || st == "limit" || st == "removeDuplicates" || st == "summarize" ||
        st == "splitOut" || st == "dateTime" || st == "itemLists" || st == "html" || st == "xml" || st == "crypto" || st == "convertToFile")
        return Cat::Data;
    return Cat::Other;
}

std::string label(const std::string& type) {
    std::string s = shortType(type);
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (i == 0) o += (char)toupper((unsigned char)c);
        else if (isupper((unsigned char)c) && !isupper((unsigned char)s[i - 1])) {
            o += ' ';
            o += c;
        } else o += c;
    }
    return o;
}

const std::vector<Template>& templates() {
    static const std::vector<Template> t = {
        {"Manual Trigger", "n8n-nodes-base.manualTrigger", 1, "{}"},
        {"Webhook", "n8n-nodes-base.webhook", 2, "{\"httpMethod\":\"POST\",\"path\":\"\",\"options\":{}}"},
        {"Schedule Trigger", "n8n-nodes-base.scheduleTrigger", 1.2, "{\"rule\":{\"interval\":[{\"field\":\"hours\"}]}}"},
        {"Edit Fields (Set)", "n8n-nodes-base.set", 3.4, "{\"assignments\":{\"assignments\":[]},\"options\":{}}"},
        {"HTTP Request", "n8n-nodes-base.httpRequest", 4.2, "{\"url\":\"\",\"options\":{}}"},
        {"Code", "n8n-nodes-base.code", 2, "{\"jsCode\":\"return $input.all();\"}"},
        {"If", "n8n-nodes-base.if", 2.2,
         "{\"conditions\":{\"options\":{\"caseSensitive\":true,\"leftValue\":\"\",\"typeValidation\":\"strict\"},\"conditions\":[],\"combinator\":\"and\"},\"options\":{}}"},
        {"Respond to Webhook", "n8n-nodes-base.respondToWebhook", 1.1, "{\"respondWith\":\"json\",\"responseBody\":\"={{ $json }}\",\"options\":{}}"},
        {"No Operation", "n8n-nodes-base.noOp", 1, "{}"},
    };
    return t;
}

int nodeIndex(JsonDocument& wf, const std::string& name) {
    int i = 0;
    for (JsonObject n : wf["nodes"].as<JsonArray>()) {
        if (name == (n["name"] | "")) return i;
        i++;
    }
    return -1;
}

JsonObject node(JsonDocument& wf, const std::string& name) {
    for (JsonObject n : wf["nodes"].as<JsonArray>())
        if (name == (n["name"] | "")) return n;
    return JsonObject();
}

bool isAiInput(const std::string& t) { return t.compare(0, 3, "ai_") == 0; }

int outputs(JsonDocument& wf, JsonObject n) {
    std::string st = shortType(S(n, "type"));
    if (st == "if" || st == "filter") return st == "if" ? 2 : 1;
    int cnt = 1;
    if (st == "switch") {
        JsonVariant rules = n["parameters"]["rules"]["values"];
        if (!rules.isNull()) cnt = (int)rules.size() + 1;
        else cnt = 4;
    }
    JsonVariant c = wf["connections"][S(n, "name")]["main"];
    if (c.is<JsonArray>()) cnt = std::max(cnt, (int)c.size());
    return std::max(cnt, 1);
}

std::vector<Link> links(JsonDocument& wf) {
    std::vector<Link> out;
    for (JsonPair src : wf["connections"].as<JsonObject>()) {
        for (JsonPair kind : src.value().as<JsonObject>()) {
            int o = 0;
            for (JsonVariant branch : kind.value().as<JsonArray>()) {
                for (JsonObject t : branch.as<JsonArray>()) out.push_back({src.key().c_str(), S(t, "node"), kind.key().c_str(), o, t["index"] | 0});
                o++;
            }
        }
    }
    return out;
}

Bounds bounds(JsonDocument& wf) {
    Bounds b;
    bool first = true;
    for (JsonObject n : wf["nodes"].as<JsonArray>()) {
        double x = n["position"][0] | 0.0, y = n["position"][1] | 0.0;
        if (first) {
            b = {x, y, x, y};
            first = false;
        } else {
            b.x0 = std::min(b.x0, x);
            b.y0 = std::min(b.y0, y);
            b.x1 = std::max(b.x1, x);
            b.y1 = std::max(b.y1, y);
        }
    }
    return b;
}

std::string uniqueName(JsonDocument& wf, const std::string& base) {
    if (nodeIndex(wf, base) < 0) return base;
    // "HTTP Request" -> "HTTP Request1": strip a trailing number first so copies don't become "Name11"
    std::string stem = base;
    while (!stem.empty() && isdigit((unsigned char)stem.back())) stem.pop_back();
    for (int i = 1; i < 1000; i++) {
        std::string n = stem + std::to_string(i);
        if (nodeIndex(wf, n) < 0) return n;
    }
    return base + "_new";
}

static std::string uuid() {
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) s += '-';
        else s += hex[plat::random32() & 15];
    }
    s[14] = '4';
    return s;
}

JsonObject addNode(JsonDocument& wf, const Template& t, double x, double y) {
    JsonArray nodes = wf["nodes"].is<JsonArray>() ? wf["nodes"].as<JsonArray>() : wf["nodes"].to<JsonArray>();
    JsonObject n = nodes.add<JsonObject>();
    n["id"] = uuid();
    std::string base = t.label;
    if (size_t par = base.find(" ("); par != std::string::npos) base.resize(par);   // "Edit Fields (Set)" is named "Edit Fields" in n8n
    n["name"] = uniqueName(wf, base);
    n["type"] = t.type;
    n["typeVersion"] = t.version;
    JsonArray pos = n["position"].to<JsonArray>();
    pos.add((long)x);
    pos.add((long)y);
    JsonDocument p;
    deserializeJson(p, t.params);
    n["parameters"] = p.as<JsonVariant>();
    if (shortType(t.type) == "webhook") {
        std::string id = uuid();
        n["webhookId"] = id;
        n["parameters"]["path"] = id;
    }
    return n;
}

bool removeNode(JsonDocument& wf, const std::string& name) {
    int i = nodeIndex(wf, name);
    if (i < 0) return false;
    wf["nodes"].remove(i);
    wf["connections"].remove(name);
    for (JsonPair src : wf["connections"].as<JsonObject>())
        for (JsonPair kind : src.value().as<JsonObject>())
            for (JsonVariant branch : kind.value().as<JsonArray>()) {
                JsonArray a = branch.as<JsonArray>();
                for (int k = (int)a.size() - 1; k >= 0; k--)
                    if (name == (a[k]["node"] | "")) a.remove(k);
            }
    if (wf["pinData"].is<JsonObject>()) wf["pinData"].remove(name);
    return true;
}

bool connect(JsonDocument& wf, const std::string& from, int out, const std::string& to, int in, const std::string& type) {
    if (from == to || nodeIndex(wf, from) < 0 || nodeIndex(wf, to) < 0 || out < 0) return false;
    JsonObject conns = wf["connections"].is<JsonObject>() ? wf["connections"].as<JsonObject>() : wf["connections"].to<JsonObject>();
    JsonObject bySrc = conns[from].is<JsonObject>() ? conns[from].as<JsonObject>() : conns[from].to<JsonObject>();
    JsonArray outs = bySrc[type].is<JsonArray>() ? bySrc[type].as<JsonArray>() : bySrc[type].to<JsonArray>();
    while ((int)outs.size() <= out) outs.add<JsonArray>();
    JsonArray targets = outs[out].as<JsonArray>();
    for (JsonObject t : targets)
        if (to == (t["node"] | "") && type == (t["type"] | "")) return false;   // already linked
    JsonObject t = targets.add<JsonObject>();
    t["node"] = to;
    t["type"] = type;
    t["index"] = in;
    return true;
}

bool disconnect(JsonDocument& wf, const std::string& from, int out, const std::string& to, const std::string& type) {
    JsonVariant outs = wf["connections"][from][type];
    if (!outs.is<JsonArray>() || out < 0 || out >= (int)outs.size()) return false;
    JsonArray a = outs[out].as<JsonArray>();
    for (int k = (int)a.size() - 1; k >= 0; k--)
        if (to == (a[k]["node"] | "")) {
            a.remove(k);
            return true;
        }
    return false;
}

static void replaceRefs(JsonVariant v, const std::string& from, const std::string& to) {
    if (v.is<JsonObject>()) {
        for (JsonPair kv : v.as<JsonObject>()) {
            if (kv.value().is<const char*>()) {
                std::string s = kv.value().as<const char*>();
                bool changed = false;
                for (const char* pre : {"$('", "$(\"", "$node[\"", "$node['"}) {
                    std::string needle = pre + from;
                    for (size_t pos = 0; (pos = s.find(needle, pos)) != std::string::npos; pos += strlen(pre) + to.size()) {
                        // only whole names: the char after must be the closing quote
                        char q = pre[strlen(pre) - 1];
                        if (pos + needle.size() < s.size() && s[pos + needle.size()] == q) {
                            s.replace(pos + strlen(pre), from.size(), to);
                            changed = true;
                        }
                    }
                }
                if (changed) kv.value().set(s);
            } else replaceRefs(kv.value(), from, to);
        }
    } else if (v.is<JsonArray>()) {
        for (JsonVariant x : v.as<JsonArray>()) replaceRefs(x, from, to);
    }
}

bool rename(JsonDocument& wf, const std::string& from, const std::string& to) {
    if (to.empty() || from == to || nodeIndex(wf, from) < 0 || nodeIndex(wf, to) >= 0) return false;
    node(wf, from)["name"] = to;
    JsonObject conns = wf["connections"].as<JsonObject>();
    if (conns[from].is<JsonObject>()) {
        JsonDocument tmp;
        tmp.set(conns[from]);
        conns[to] = tmp.as<JsonVariant>();
        conns.remove(from);
    }
    for (JsonPair src : conns)
        for (JsonPair kind : src.value().as<JsonObject>())
            for (JsonVariant branch : kind.value().as<JsonArray>())
                for (JsonObject t : branch.as<JsonArray>())
                    if (from == (t["node"] | "")) t["node"] = to;
    if (wf["pinData"].is<JsonObject>() && wf["pinData"][from].is<JsonArray>()) {
        JsonDocument tmp;
        tmp.set(wf["pinData"][from]);
        wf["pinData"][to] = tmp.as<JsonVariant>();
        wf["pinData"].remove(from);
    }
    replaceRefs(wf["nodes"], from, to);
    return true;
}

void move(JsonObject n, double x, double y) {
    n["position"][0] = (long)x;
    n["position"][1] = (long)y;
}

std::string putBody(JsonDocument& wf) {
    static const char* allowed[] = {"saveExecutionProgress", "saveManualExecutions", "saveDataErrorExecution", "saveDataSuccessExecution", "executionTimeout",
                                    "errorWorkflow", "timezone", "executionOrder", "callerPolicy", "callerIds", "timeSavedMode", "timeSavedPerExecution",
                                    "redactionPolicy", "availableInMCP", "customTelemetryTags"};
    JsonDocument out;
    out["name"] = wf["name"];
    out["nodes"] = wf["nodes"];
    if (wf["connections"].isNull()) out["connections"].to<JsonObject>();
    else out["connections"] = wf["connections"];
    JsonObject st = out["settings"].to<JsonObject>();
    for (const char* k : allowed)
        if (!wf["settings"][k].isNull()) st[k] = wf["settings"][k];
    if (wf["staticData"].is<JsonObject>()) out["staticData"] = wf["staticData"];
    if (wf["pinData"].is<JsonObject>() && wf["pinData"].size() > 0) out["pinData"] = wf["pinData"];
    std::string s;
    serializeJson(out, s);
    return s;
}

}  // namespace flow
