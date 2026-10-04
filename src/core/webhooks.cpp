#include "webhooks.h"

#include <algorithm>

#include "plat.h"
#include "util.h"

namespace wh {

static std::string S(JsonVariantConst v, const char* k) { return util::str(v[k]); }

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
static std::string upper(std::string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}
static bool endsWith(const std::string& s, const std::string& t) { return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0; }
static std::string trimSlash(std::string p) {
    while (!p.empty() && p[0] == '/') p.erase(0, 1);
    return p;
}

// keys GET /workflows must keep for discover(); parameters are cut down to what describes an entry point
const char* listFilter() {
    return "{\"data\":[{\"id\":true,\"name\":true,\"active\":true,\"isArchived\":true,\"nodes\":[{\"name\":true,\"type\":true,\"disabled\":true,"
           "\"webhookId\":true,\"credentials\":true,\"parameters\":{\"path\":true,\"httpMethod\":true,\"multipleMethods\":true,"
           "\"authentication\":true,\"responseMode\":true,\"responseCode\":true,\"public\":true,\"formTitle\":true,\"respondWith\":true,"
           "\"options\":{\"path\":true,\"ipWhitelist\":true,\"allowedOrigins\":true,\"rawBody\":true,\"binaryData\":true,\"responseCode\":true}}}]}],"
           "\"nextCursor\":true}";
}

const char* agentFilter() {
    return "{\"data\":[{\"id\":true,\"name\":true,\"active\":true,\"isArchived\":true,\"connections\":true,\"nodes\":[{\"name\":true,\"type\":true,\"disabled\":true,"
           "\"webhookId\":true,\"credentials\":true,\"parameters\":{\"path\":true,\"httpMethod\":true,\"multipleMethods\":true,"
           "\"authentication\":true,\"responseMode\":true,\"responseCode\":true,\"public\":true,\"formTitle\":true,\"respondWith\":true,"
           "\"model\":true,\"modelName\":true,\"systemMessage\":true,\"agentId\":true,"
           "\"options\":{\"path\":true,\"ipWhitelist\":true,\"allowedOrigins\":true,\"rawBody\":true,\"binaryData\":true,\"responseCode\":true,\"systemMessage\":true}}}]}],"
           "\"nextCursor\":true}";
}

std::string Endpoint::kindName() const {
    switch (kind) {
        case Kind::Webhook: return openai ? "OpenAI API" : models ? "OpenAI models" : "Webhook";
        case Kind::Form: return "Form";
        case Kind::Chat: return "Chat";
        case Kind::Mcp: return "MCP";
    }
    return "";
}

std::string Endpoint::authLabel() const {
    if (auth == "basicAuth") return "Basic auth";
    if (auth == "headerAuth") return "Header auth";
    if (auth == "jwtAuth") return "JWT";
    if (auth == "bearerAuth") return "Bearer token";
    if (auth == "n8nUserAuth") return "n8n login";
    return "";
}

std::string Endpoint::url() const {
    switch (kind) {
        case Kind::Form: return "/form/" + path;
        case Kind::Mcp: return "/mcp/" + path;
        default: return "/webhook/" + path;
    }
}
std::string Endpoint::testUrl() const {
    switch (kind) {
        case Kind::Form: return "/form-test/" + path;
        case Kind::Mcp: return "/mcp-test/" + path;
        default: return "/webhook-test/" + path;
    }
}

// n8n's getNodeWebhookUrl(): nodes without a webhookId are addressed by workflow id + node name; dynamic paths (":id") get the webhookId prefix
static std::string registeredPath(const std::string& wfId, const std::string& node, const std::string& webhookId, std::string path) {
    path = trimSlash(path);
    if (webhookId.empty()) return wfId + "/" + util::urlEncode(lower(node)) + "/" + path;
    if (!path.empty() && (path[0] == ':' || path.find("/:") != std::string::npos)) return webhookId + "/" + path;
    return path;
}

static std::string credFor(JsonObjectConst node, std::string& name) {
    JsonObjectConst cr = node["credentials"];
    for (JsonPairConst kv : cr) {
        name = S(kv.value(), "name");
        return kv.key().c_str();
    }
    return "";
}

static bool aiNodeType(const std::string& t) {
    return t.find("n8n-nodes-langchain.agent") != std::string::npos || t.find("n8n-nodes-langchain.lmChat") != std::string::npos ||
           t.find("n8n-nodes-langchain.chainLlm") != std::string::npos || t.find("messageAnAgent") != std::string::npos;
}

void discover(JsonObjectConst w, std::vector<Endpoint>& out) {
    std::string wfId = S(w, "id"), wfName = S(w, "name");
    bool active = w["active"] | false, archived = w["isArchived"] | false;
    for (JsonObjectConst n : w["nodes"].as<JsonArrayConst>()) {
        std::string type = S(n, "type");
        Endpoint e;
        e.wfId = wfId;
        e.wfName = wfName;
        e.wfActive = active;
        e.wfArchived = archived;
        e.nodeName = S(n, "name");
        e.webhookId = S(n, "webhookId");
        e.disabled = n["disabled"] | false;
        JsonObjectConst p = n["parameters"];
        JsonObjectConst o = p["options"];
        std::string credName;
        std::string cred = credFor(n, credName);
        e.credName = credName;
        if (type == "n8n-nodes-base.webhook") {
            e.kind = Kind::Webhook;
            if (p["httpMethod"].is<JsonArrayConst>()) {
                for (JsonVariantConst m : p["httpMethod"].as<JsonArrayConst>()) e.methods.push_back(upper(util::str(m)));
            } else {
                std::string m = S(p, "httpMethod");
                e.methods.push_back(m.empty() ? "GET" : upper(m));
            }
            e.path = registeredPath(wfId, e.nodeName, e.webhookId, S(p, "path"));
            std::string a = S(p, "authentication");
            e.auth = a.empty() ? "none" : a;
            std::string rm = S(p, "responseMode");
            if (!rm.empty()) e.responseMode = rm;
            e.respondCode = p["responseCode"] | 0;
            if (!o.isNull()) {
                e.rawBody = o["rawBody"] | false;
                e.binary = o["binaryData"] | false;
                e.ipAllow = S(o, "ipWhitelist");
                e.origins = S(o, "allowedOrigins");
                if (!e.respondCode) e.respondCode = o["responseCode"] | 0;
            }
            std::string lp = lower(e.path);
            e.openai = endsWith(lp, "chat/completions") || endsWith(lp, "/completions") || endsWith(lp, "/responses");
            e.models = endsWith(lp, "/models") || lp == "models";
            out.push_back(e);
        } else if (type == "n8n-nodes-base.formTrigger") {
            e.kind = Kind::Form;
            e.methods = {"GET", "POST"};
            std::string path = S(o, "path");
            e.path = path.empty() ? e.webhookId : trimSlash(path);
            e.title = S(p, "formTitle");
            std::string a = S(p, "authentication");
            e.auth = a.empty() ? "none" : a;
            e.responseMode = S(p, "responseMode").empty() ? "onReceived" : S(p, "responseMode");
            out.push_back(e);
        } else if (type == "@n8n/n8n-nodes-langchain.chatTrigger") {
            e.kind = Kind::Chat;
            e.methods = {"POST"};
            e.path = e.webhookId + "/chat";
            e.isPublic = p["public"] | false;
            std::string a = S(p, "authentication");
            e.auth = a.empty() ? "none" : a;
            e.origins = S(o, "allowedOrigins");
            e.responseMode = S(p, "responseMode").empty() ? "lastNode" : S(p, "responseMode");
            e.ai = true;
            out.push_back(e);
        } else if (type == "@n8n/n8n-nodes-langchain.mcpTrigger") {
            e.kind = Kind::Mcp;
            e.methods = {"POST"};
            e.path = S(p, "path").empty() ? e.webhookId : S(p, "path");
            std::string a = S(p, "authentication");
            e.auth = a.empty() || a == "none" ? "none" : a == "bearerAuth" ? "bearerAuth" : a;
            e.ai = true;
            out.push_back(e);
        }
        (void)cred;
    }
    // is there an AI node anywhere in the workflow? (list-level hint; analyze() refines it)
    bool ai = false;
    for (JsonObjectConst n : w["nodes"].as<JsonArrayConst>())
        if (aiNodeType(S(n, "type"))) ai = true;
    for (auto& e : out)
        if (e.wfId == wfId && ai) e.ai = true;
}

static std::string providerOf(const std::string& type) {
    static const struct { const char* k; const char* v; } map[] = {{"lmChatOpenAi", "OpenAI"}, {"lmChatAnthropic", "Anthropic"}, {"lmChatGoogleGemini", "Gemini"},
        {"lmChatOpenRouter", "OpenRouter"}, {"lmChatOllama", "Ollama"}, {"lmChatGroq", "Groq"}, {"lmChatMistralCloud", "Mistral"}, {"lmChatAzureOpenAi", "Azure OpenAI"},
        {"lmChatDeepSeek", "DeepSeek"}, {"lmChatXAiGrok", "xAI"}, {"lmChatVercelAiGateway", "Vercel AI"}, {"lmChatAwsBedrock", "Bedrock"}, {"lmChatVertex", "Vertex"}};
    for (auto& m : map)
        if (type.find(m.k) != std::string::npos) return m.v;
    size_t i = type.rfind('.');
    return i == std::string::npos ? type : type.substr(i + 1);
}

void discoverAgents(JsonObjectConst w, std::vector<Agent>& out) {
    std::string wfId = S(w, "id"), wfName = S(w, "name");
    bool active = w["active"] | false, archived = w["isArchived"] | false;
    JsonObjectConst conns = w["connections"];
    for (JsonObjectConst n : w["nodes"].as<JsonArrayConst>()) {
        std::string type = S(n, "type");
        bool isAgent = type == "@n8n/n8n-nodes-langchain.agent", isTool = type == "@n8n/n8n-nodes-langchain.agentTool",
             isMsg = type == "n8n-nodes-base.messageAnAgent";
        if (!isAgent && !isTool && !isMsg) continue;
        if (n["disabled"] | false) continue;
        Agent a;
        a.wfId = wfId;
        a.wfName = wfName;
        a.wfActive = active;
        a.wfArchived = archived;
        a.nodeName = S(n, "name");
        a.isSub = isTool;
        a.messagesAgent = isMsg;
        JsonObjectConst p = n["parameters"];
        a.prompt = S(p["options"], "systemMessage");
        if (a.prompt.empty()) a.prompt = S(p, "systemMessage");
        // whatever feeds this node through an ai_* connection
        for (JsonPairConst src : conns) {
            for (JsonPairConst kind : src.value().as<JsonObjectConst>()) {
                std::string ck = kind.key().c_str();
                if (ck.compare(0, 3, "ai_") != 0) continue;
                for (JsonVariantConst branch : kind.value().as<JsonArrayConst>())
                    for (JsonObjectConst tgt : branch.as<JsonArrayConst>()) {
                        if (S(tgt, "node") != a.nodeName) continue;
                        std::string name = src.key().c_str();
                        if (ck == "ai_tool") a.tools.push_back(name);
                        else if (ck == "ai_languageModel" || ck == "ai_memory") {
                            for (JsonObjectConst sn : w["nodes"].as<JsonArrayConst>()) {
                                if (S(sn, "name") != name) continue;
                                std::string st = S(sn, "type");
                                if (ck == "ai_memory") {
                                    size_t i = st.rfind('.');
                                    a.memory = i == std::string::npos ? st : st.substr(i + 1);
                                } else {
                                    a.provider = providerOf(st);
                                    JsonVariantConst m = sn["parameters"]["model"];
                                    if (m.is<const char*>()) a.model = m.as<const char*>();
                                    else if (m.is<JsonObjectConst>()) a.model = S(m.as<JsonObjectConst>(), "value");
                                    if (a.model.empty()) a.model = S(sn["parameters"], "modelName");
                                }
                            }
                        }
                    }
            }
        }
        out.push_back(a);
    }
}

static void addField(Endpoint& ep, const std::string& name, const std::string& where) {
    if (name.empty() || name.size() > 40) return;
    static const char* jsMembers[] = {"length", "map", "filter", "find", "forEach", "join", "push", "toString", "toJsonString", "keys", "values", "trim", "split",
                                      "includes", "slice", "indexOf", "replace", "startsWith", "endsWith", "toLowerCase", "toUpperCase", "sort", "reduce", "some",
                                      "every", "concat", "entries", "hasOwnProperty", "at", "flat", "isEmpty", "first", "last", "item", "all"};
    for (const char* m : jsMembers)
        if (name == m) return;
    for (auto& f : ep.fields)
        if (f.name == name && f.where == where) return;
    if (ep.fields.size() < 24) ep.fields.push_back({name, where});
}

static std::string ident(const std::string& s, size_t i) {
    size_t j = i;
    while (j < s.size() && (isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '-')) j++;
    return s.substr(i, j - i);
}

static bool identChar(char c) { return isalnum((unsigned char)c) || c == '_' || c == '$'; }

// Code-node style aliases:  const b = $json.body ?? $json;  then b.chatInput    /    const { a, b: x = 1 } = $json.body;
static void scanAliases(const std::string& s, const std::string& root, const char* where, Endpoint& ep) {
    const std::string needle = "json." + root;
    for (size_t eq = s.find('='); eq != std::string::npos; eq = s.find('=', eq + 1)) {
        if (eq + 1 < s.size() && (s[eq + 1] == '=' || s[eq + 1] == '>')) continue;
        if (eq > 0 && (s[eq - 1] == '=' || s[eq - 1] == '!' || s[eq - 1] == '<' || s[eq - 1] == '>')) continue;
        size_t lim = std::min(s.size(), eq + 50);
        size_t hit = s.find(needle, eq + 1);
        if (hit == std::string::npos || hit > lim) continue;
        size_t after = hit + needle.size();
        if (after < s.size() && (identChar(s[after]) || s[after] == '.' || s[after] == '[')) continue;   // json.body.x is handled elsewhere
        if (s.substr(eq + 1, hit - eq - 1).find_first_of(";\n") != std::string::npos) continue;
        size_t e = eq;
        while (e > 0 && s[e - 1] == ' ') e--;
        if (e == 0) continue;
        if (s[e - 1] == '}') {   // destructuring
            size_t b = s.rfind('{', e);
            if (b == std::string::npos || e - b > 300) continue;
            std::string inner = s.substr(b + 1, e - b - 2);
            size_t i = 0;
            while (i < inner.size()) {
                while (i < inner.size() && (inner[i] == ' ' || inner[i] == ',' || inner[i] == '\n')) i++;
                size_t j = i;
                while (j < inner.size() && identChar(inner[j])) j++;
                if (j > i) addField(ep, inner.substr(i, j - i), where);
                while (j < inner.size() && inner[j] != ',') j++;   // skip "= default" / ": alias"
                i = j;
            }
        } else if (identChar(s[e - 1])) {
            size_t b = e;
            while (b > 0 && identChar(s[b - 1])) b--;
            std::string alias = s.substr(b, e - b);
            if (alias == "const" || alias == "let" || alias == "var") continue;
            for (size_t pos = 0; (pos = s.find(alias + ".", pos)) != std::string::npos; pos += alias.size() + 1) {
                if (pos > 0 && (identChar(s[pos - 1]) || s[pos - 1] == '.')) continue;
                addField(ep, ident(s, pos + alias.size() + 1), where);
            }
        }
    }
}

// collects request keys referenced in expression text: json.body.x  json.query.x  json.headers.x  json["body"]["x"]  + aliases
static void scanText(const std::string& s, Endpoint& ep) {
    static const struct { const char* key; const char* where; } roots[] = {{"body", "body"}, {"query", "query"}, {"headers", "header"}, {"params", "path"}};
    for (auto& r : roots) {
        std::string dotted = std::string("json.") + r.key + ".";
        for (size_t pos = 0; (pos = s.find(dotted, pos)) != std::string::npos; pos += dotted.size()) addField(ep, ident(s, pos + dotted.size()), r.where);
        for (char q : {'"', '\''}) {
            std::string br = std::string("json[") + q + r.key + q + "][" + q;
            for (size_t pos = 0; (pos = s.find(br, pos)) != std::string::npos; pos += br.size()) {
                size_t e = s.find(q, pos + br.size());
                if (e != std::string::npos) addField(ep, s.substr(pos + br.size(), e - pos - br.size()), r.where);
            }
            std::string mix = std::string("json.") + r.key + "[" + q;
            for (size_t pos = 0; (pos = s.find(mix, pos)) != std::string::npos; pos += mix.size()) {
                size_t e = s.find(q, pos + mix.size());
                if (e != std::string::npos) addField(ep, s.substr(pos + mix.size(), e - pos - mix.size()), r.where);
            }
        }
        scanAliases(s, r.key, r.where, ep);
    }
}

static void scanValue(JsonVariantConst v, Endpoint& ep, int depth = 0) {
    if (depth > 12) return;
    if (v.is<const char*>()) {
        const char* c = v.as<const char*>();
        if (c && strstr(c, "json")) scanText(c, ep);
    } else if (v.is<JsonObjectConst>()) {
        for (JsonPairConst kv : v.as<JsonObjectConst>()) scanValue(kv.value(), ep, depth + 1);
    } else if (v.is<JsonArrayConst>()) {
        for (JsonVariantConst x : v.as<JsonArrayConst>()) scanValue(x, ep, depth + 1);
    }
}

void analyze(JsonObjectConst w, Endpoint& ep) {
    for (JsonObjectConst n : w["nodes"].as<JsonArrayConst>()) {
        std::string t = S(n, "type");
        JsonObjectConst p = n["parameters"];
        if (n["disabled"] | false) continue;
        if (aiNodeType(t)) ep.ai = true;
        if (t == "n8n-nodes-base.respondToWebhook") {
            std::string rw = S(p, "respondWith");
            if (rw.empty()) rw = "firstIncomingItem";
            static const struct { const char* k; const char* v; } map[] = {{"allIncomingItems", "JSON (all items)"}, {"firstIncomingItem", "JSON (first item)"},
                                                                         {"json", "JSON"}, {"text", "text"}, {"binary", "file"}, {"redirect", "redirect"},
                                                                         {"noData", "no body"}, {"jwt", "JWT"}};
            ep.respond = rw;
            for (auto& m : map)
                if (rw == m.k) ep.respond = m.v;
            JsonVariantConst rc = p["options"]["responseCode"];
            if (!rc.isNull()) ep.respondCode = rc.as<int>();
        }
        if (t == "n8n-nodes-base.formTrigger" && S(n, "name") == ep.nodeName) {
            ep.formFields.clear();
            for (JsonObjectConst f : p["formFields"]["values"].as<JsonArrayConst>()) {
                FormField ff;
                ff.label = S(f, "fieldLabel");
                ff.type = S(f, "fieldType").empty() ? "text" : S(f, "fieldType");
                ff.required = f["requiredField"] | false;
                for (JsonObjectConst op : f["fieldOptions"]["values"].as<JsonArrayConst>()) ff.options.push_back(S(op, "option"));
                ep.formFields.push_back(ff);
            }
        }
        // anything that mentions the request: only count nodes after the trigger conceptually; a text scan is good enough
        if (t != "n8n-nodes-base.webhook" && t != "n8n-nodes-base.respondToWebhook") {
            scanValue(p, ep);
        }
    }
}

static std::string secretKey(const std::string& id) {
    uint32_t h = 2166136261u;
    for (unsigned char c : id) h = (h ^ c) * 16777619u;
    char b[16];
    snprintf(b, sizeof b, "wa%08x", (unsigned)h);
    return b;
}

Secret loadSecret(const std::string& id) {
    Secret s;
    JsonDocument d;
    if (deserializeJson(d, plat::getStr(secretKey(id).c_str(), "{}")) != DeserializationError::Ok) return s;
    s.user = S(d, "u");
    s.pass = S(d, "p");
    s.header = S(d, "hn");
    s.value = S(d, "hv");
    s.token = S(d, "t");
    return s;
}

void saveSecret(const std::string& id, const Secret& s) {
    JsonDocument d;
    if (!s.user.empty()) d["u"] = s.user;
    if (!s.pass.empty()) d["p"] = s.pass;
    if (!s.header.empty()) d["hn"] = s.header;
    if (!s.value.empty()) d["hv"] = s.value;
    if (!s.token.empty()) d["t"] = s.token;
    std::string o;
    serializeJson(d, o);
    plat::putStr(secretKey(id).c_str(), o == "null" ? "{}" : o);
}

static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string base64(const std::string& in) {
    std::string o;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        o += B64[v >> 18];
        o += B64[(v >> 12) & 63];
        o += B64[(v >> 6) & 63];
        o += B64[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16;
        o += B64[v >> 18];
        o += B64[(v >> 12) & 63];
        o += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8);
        o += B64[v >> 18];
        o += B64[(v >> 12) & 63];
        o += B64[(v >> 6) & 63];
        o += '=';
    }
    return o;
}

std::vector<std::pair<std::string, std::string>> authHeaders(const Endpoint& ep, const Secret& s) {
    std::vector<std::pair<std::string, std::string>> h;
    if (ep.auth == "basicAuth" && (!s.user.empty() || !s.pass.empty())) h.push_back({"Authorization", "Basic " + base64(s.user + ":" + s.pass)});
    else if (ep.auth == "headerAuth" && !s.value.empty()) h.push_back({s.header.empty() ? "Authorization" : s.header, s.value});
    else if ((ep.auth == "jwtAuth" || ep.auth == "bearerAuth") && !s.token.empty()) h.push_back({"Authorization", "Bearer " + s.token});
    return h;
}

std::string replyText(const std::string& body) {
    JsonDocument d;
    if (body.empty() || (body[0] != '{' && body[0] != '[') || deserializeJson(d, body) != DeserializationError::Ok) return body;
    JsonVariantConst v = d.as<JsonVariantConst>();
    if (v.is<JsonArrayConst>() && v.size() > 0) v = v[0];
    JsonVariantConst c = v["choices"][0]["message"]["content"];
    if (c.is<const char*>()) return c.as<const char*>();
    if (c.is<JsonArrayConst>()) {   // content parts
        std::string t;
        for (JsonVariantConst part : c.as<JsonArrayConst>()) t += util::str(part["text"]);
        if (!t.empty()) return t;
    }
    for (const char* k : {"output", "text", "message", "response", "answer", "reply", "content", "result"}) {
        if (v[k].is<const char*>()) return v[k].as<const char*>();
    }
    JsonVariantConst t = v["choices"][0]["text"];
    if (t.is<const char*>()) return t.as<const char*>();
    return util::pretty(body, 12000);
}

}  // namespace wh
