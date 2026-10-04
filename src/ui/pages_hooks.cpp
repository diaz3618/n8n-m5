// Webhooks: finds every HTTP entry point of the workflows (Webhook / Form / Chat / MCP triggers), explains how each one works
// (method, path, authentication, what it reads from the request, what it answers) and lets the device call it.
#include <algorithm>
#include <map>

#include "../core/config.h"
#include "../core/plat.h"
#include "../core/util.h"
#include "../core/webhooks.h"
#include "hooks.h"
#include "res.h"

namespace ui {

static std::string S(JsonObjectConst o, const char* k) { return util::str(o[k]); }

static std::vector<wh::Endpoint> g_eps;
static uint32_t g_epsAt = 0;
static bool g_epsInactive = false;   // the cached list included inactive/archived workflows

const std::vector<wh::Endpoint>& endpointCache() { return g_eps; }

struct KV {
    std::string k, v;
};
struct Draft {
    std::string method, bodyMode;   // bodyMode: json | form | text | none
    std::vector<KV> query, headers, body, path;
    std::string raw;                // raw body text (bodyMode text, or json edited by hand)
    bool rawJson = false;
    bool init = false;
};
static std::map<std::string, Draft> drafts;

static bool looksJson(const std::string& v) {
    if (v == "true" || v == "false" || v == "null") return true;
    if (v.empty()) return false;
    if (v[0] == '{' || v[0] == '[') {
        JsonDocument d;
        return deserializeJson(d, v) == DeserializationError::Ok;
    }
    char* end = nullptr;
    strtod(v.c_str(), &end);
    return end && *end == 0 && end != v.c_str();
}

static std::string buildJson(const std::vector<KV>& rows) {
    JsonDocument d;
    JsonObject o = d.to<JsonObject>();
    for (auto& r : rows) {
        if (r.k.empty()) continue;
        if (looksJson(r.v)) {
            JsonDocument t;
            deserializeJson(t, r.v);
            o[r.k] = t.as<JsonVariant>();
        } else o[r.k] = r.v;
    }
    std::string s;
    serializeJson(d, s);
    return s;
}

static std::string buildForm(const std::vector<KV>& rows) {
    std::string s;
    for (auto& r : rows) {
        if (r.k.empty()) continue;
        if (!s.empty()) s += "&";
        s += util::urlEncode(r.k) + "=" + util::urlEncode(r.v);
    }
    return s;
}

static api::Request makeCall(const wh::Endpoint& ep, const Draft& d, const std::string& bodyOverride = "", const std::string& ctOverride = "") {
    api::Request r;
    r.method = d.method.empty() ? ep.method() : d.method;
    std::string path = ep.url();
    for (auto& p : d.path) {   // dynamic path segments ":id"
        size_t i = path.find(":" + p.k);
        if (i != std::string::npos) path.replace(i, p.k.size() + 1, util::urlEncode(p.v));
    }
    r.path = path;
    r.raw = true;
    std::string q;
    for (auto& kv : d.query)
        if (!kv.k.empty()) q += (q.empty() ? "" : "&") + util::urlEncode(kv.k) + "=" + util::urlEncode(kv.v);
    r.query = q;
    r.headers = wh::authHeaders(ep, wh::loadSecret(ep.id()));
    for (auto& h : d.headers)
        if (!h.k.empty()) r.headers.push_back({h.k, h.v});
    bool hasBody = r.method != "GET" && r.method != "HEAD" && r.method != "DELETE" ? true : !d.body.empty() && d.bodyMode != "none";
    if (!bodyOverride.empty()) {
        r.body = bodyOverride;
        r.contentType = ctOverride;
    } else if (hasBody && d.bodyMode != "none") {
        if (d.bodyMode == "form") {
            r.body = buildForm(d.body);
            r.contentType = "application/x-www-form-urlencoded";
        } else if (d.bodyMode == "text") {
            r.body = d.raw;
            r.contentType = "text/plain";
        } else {
            r.body = d.rawJson ? d.raw : buildJson(d.body);
            r.contentType = "application/json";
        }
    }
    if (ep.kind == wh::Kind::Mcp) r.headers.push_back({"Accept", "application/json, text/event-stream"});
    r.maxKb = 512;
    return r;
}

static std::string headOf(const std::string& method, const std::string& path, const api::Response& res) {
    std::string h = method + " " + path + "\nHTTP " + std::to_string(res.status) + "  " + std::to_string(res.ms) + " ms  " + std::to_string(res.bytes) + " bytes";
    if (!res.contentType.empty()) h += "  " + res.contentType;
    h += "\n";
    if (res.status <= 0) h += res.error + "\n";
    if (!res.location.empty()) h += "Location: " + res.location + "\n";
    return h + "\n";
}

static void showResponse(const std::string& method, const std::string& path, api::Response& res) {
    std::string b = res.body;
    if (!b.empty() && (b[0] == '{' || b[0] == '[')) b = util::pretty(b, 20000);
    else if (b.size() > 20000) b.resize(20000);
    bool binary = res.contentType.find("image/") == 0 || res.contentType.find("audio/") == 0 || res.contentType.find("video/") == 0 ||
                  res.contentType.find("application/octet-stream") == 0 || res.contentType.find("application/pdf") == 0;
    if (binary) b = "(binary " + res.contentType + ", " + std::to_string(res.bytes) + " bytes)";
    toast(res.ok() ? "HTTP " + std::to_string(res.status) : res.message(), res.ok() ? Tone::Success : Tone::Danger);
    viewer("Response", headOf(method, path, res) + b);
}

static void kvEditor(lv_obj_t* parent, const std::string& title, std::vector<KV>& rows, const std::string& keyHint, const std::string& valHint,
                     std::function<void()> changed, const std::vector<std::string>& suggestions = {}) {
    lv_obj_t* h = row(parent);
    lv_obj_t* t = text(h, title, M().md);
    lv_obj_set_flex_grow(t, 1);
    iconButton(h, LV_SYMBOL_PLUS, [&rows, keyHint, valHint, changed] {
        prompt(keyHint, "", keyHint, false, false, [&rows, valHint, changed](const std::string& k) {
            if (k.empty()) return;
            prompt(k, "", valHint, false, true, [&rows, k, changed](const std::string& v) {
                rows.push_back({k, v});
                changed();
            });
        });
    });
    for (size_t i = 0; i < rows.size(); i++) {
        lv_obj_t* r = row(parent);
        lv_obj_set_style_pad_column(r, M().gap, 0);
        lv_obj_t* kc = card(r);
        lv_obj_set_width(kc, LV_PCT(35));
        lv_obj_set_style_pad_ver(kc, M().compact ? 4 : 8, 0);
        lv_obj_add_flag(kc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* kl = text(kc, rows[i].k, M().sm);
        ellipsis(kl);
        lv_obj_set_width(kl, LV_PCT(100));
        onClick(kc, [&rows, i, changed] {
            prompt("Name", rows[i].k, "name", false, false, [&rows, i, changed](const std::string& v) {
                if (i < rows.size() && !v.empty()) rows[i].k = v;
                changed();
            });
        });
        lv_obj_t* vc = card(r);
        lv_obj_set_flex_grow(vc, 1);
        lv_obj_set_style_pad_ver(vc, M().compact ? 4 : 8, 0);
        lv_obj_add_flag(vc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* vl = subtext(vc, rows[i].v.empty() ? "(empty)" : rows[i].v, M().sm);
        ellipsis(vl);
        lv_obj_set_width(vl, LV_PCT(100));
        onClick(vc, [&rows, i, changed] {
            std::string cur = i < rows.size() ? rows[i].v : "";
            prompt(i < rows.size() ? rows[i].k : "Value", cur, "value (text, number, true/false or JSON)", false, true, [&rows, i, changed](const std::string& v) {
                if (i < rows.size()) rows[i].v = v;
                changed();
            });
        });
        iconButton(r, LV_SYMBOL_CLOSE, [&rows, i, changed] {
            if (i < rows.size()) rows.erase(rows.begin() + i);
            changed();
        });
    }
    if (!suggestions.empty()) {
        lv_obj_t* sr = row(parent);
        lv_obj_set_flex_flow(sr, LV_FLEX_FLOW_ROW_WRAP);
        subtext(sr, "Detected:", M().sm);
        for (auto& sname : suggestions) {
            bool have = std::any_of(rows.begin(), rows.end(), [&](const KV& r) { return r.k == sname; });
            if (have) continue;
            chip(sr, sname, false, [&rows, sname, changed] {
                rows.push_back({sname, ""});
                changed();
            });
        }
    }
}

class EndpointPage : public Page {
public:
    wh::Endpoint ep;
    lv_obj_t* box_ = nullptr;
    bool analyzed = false, failed = false;

    Draft& dr() { return drafts[ep.id()]; }

    void build() override {
        title = ep.kind == wh::Kind::Webhook ? ep.method() + " /" + ep.path : ep.kindName() + " /" + ep.path;
        actions.push_back({LV_SYMBOL_REFRESH, [this] { analyzed = false; load(); }});
        box_ = scroller(body);
        Draft& d = dr();
        if (!d.init) {
            d.init = true;
            d.method = ep.method();
            d.bodyMode = ep.kind == wh::Kind::Form ? "form" : (d.method == "GET" || d.method == "HEAD" || d.method == "DELETE") ? "none" : "json";
            if (ep.kind == wh::Kind::Mcp) {
                d.bodyMode = "json";
                d.rawJson = true;
                d.raw = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}";
            }
        }
        render();
        load();
    }

    void load() {
        if (analyzed) return;
        get("/workflows/" + util::urlEncode(ep.wfId), "", "*", [this](api::Response& r) {
            if (!r.ok()) {
                failed = true;
                render();
                return;
            }
            wh::analyze(r.json().as<JsonObjectConst>(), ep);
            analyzed = true;
            Draft& d = dr();
            if (ep.kind == wh::Kind::Webhook) {   // dynamic path segments need values
                for (auto& f : ep.fields)
                    if (f.where == "path" && std::none_of(d.path.begin(), d.path.end(), [&](const KV& k) { return k.k == f.name; })) d.path.push_back({f.name, ""});
            }
            if (ep.kind == wh::Kind::Form && d.body.empty()) {
                for (size_t i = 0; i < ep.formFields.size(); i++) d.body.push_back({"field-" + std::to_string(i), ""});
            }
            render();
        });
    }

    bool needsSecret() const { return ep.auth != "none" && ep.auth != "n8nUserAuth"; }

    void render() {
        lv_obj_clean(box_);
        Draft& d = dr();
        lv_obj_t* c = card(box_);
        std::string full = cfg::s().url + ep.url();
        kv(c, "Workflow", ep.wfName + (ep.wfActive ? "" : "  (not published)"));
        kv(c, "Type", ep.kindName() + (ep.ai ? "  - AI" : ""));
        kv(c, "URL", full);
        if (ep.kind == wh::Kind::Webhook) {
            std::string m;
            for (auto& x : ep.methods) m += (m.empty() ? "" : ", ") + x;
            kv(c, "Methods", m);
        }
        if (!ep.authLabel().empty()) kv(c, "Authentication", ep.authLabel() + (ep.credName.empty() ? "" : "  (n8n credential: " + ep.credName + ")"));
        if (ep.kind == wh::Kind::Webhook || ep.kind == wh::Kind::Chat) {
            std::string rm = ep.responseMode == "responseNode" ? "waits for a Respond to Webhook node" + (ep.respond.empty() ? std::string() : " (" + ep.respond + ")")
                             : ep.responseMode == "lastNode"   ? "returns the last node's output"
                             : ep.responseMode == "streaming"  ? "streams the answer"
                                                               : "answers immediately, workflow runs on";
            kv(c, "Replies", rm);
        }
        if (!ep.ipAllow.empty()) kv(c, "IP allow-list", ep.ipAllow);
        if (!ep.origins.empty()) kv(c, "Allowed origins", ep.origins);
        if (ep.rawBody) kv(c, "Body", "raw (sent as is)");
        if (ep.binary) kv(c, "Body", "binary files accepted");
        if (!ep.usable()) {
            std::string why = !ep.wfActive ? "The workflow is not published, so this URL is not live."
                              : ep.wfArchived ? "The workflow is archived."
                              : ep.disabled   ? "The trigger node is disabled."
                                              : "The chat is not public: enable \"Make Chat Publicly Available\" on the trigger.";
            lv_obj_t* w = subtext(c, why, M().sm);
            lv_obj_set_width(w, LV_PCT(100));
            lv_obj_set_style_text_color(w, C().warning, 0);
            if (!ep.wfActive && !ep.wfArchived) button(c, "Publish workflow", Kind::Primary, [this] { publish(); });
        }
        if (!analyzed && !failed) subtext(c, "Reading the workflow...", M().sm);
        if (failed) subtext(c, "Could not read the workflow (offline?). You can still call the URL.", M().sm);

        if (needsSecret()) {
            lv_obj_t* cc = card(box_);
            wh::Secret sec = wh::loadSecret(ep.id());
            bool have = !(sec.user.empty() && sec.pass.empty() && sec.value.empty() && sec.token.empty());
            text(cc, "Credentials", M().md);
            lv_obj_t* hint = subtext(cc, "n8n never exposes secrets through its API: enter them once, they are kept on this device.", M().sm);
            lv_obj_set_width(hint, LV_PCT(100));
            kv(cc, "Status", have ? "saved" : "not set");
            lv_obj_t* r = row(cc);
            lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
            if (ep.auth == "basicAuth") {
                button(r, "Username", Kind::Secondary, [this] { askSecret(0); });
                button(r, "Password", Kind::Secondary, [this] { askSecret(1); });
            } else if (ep.auth == "headerAuth") {
                button(r, "Header name", Kind::Secondary, [this] { askSecret(2); });
                button(r, "Header value", Kind::Secondary, [this] { askSecret(3); });
            } else {
                button(r, "Token", Kind::Secondary, [this] { askSecret(4); });
            }
            if (have) button(r, "Clear", Kind::Ghost, [this] {
                wh::saveSecret(ep.id(), wh::Secret());
                render();
            });
        }

        lv_obj_t* rq = card(box_);
        text(rq, ep.kind == wh::Kind::Form ? "Fill in the form" : "Request", M().md);
        if (ep.kind == wh::Kind::Webhook && ep.methods.size() > 1) {
            lv_obj_t* mr = row(rq);
            lv_obj_set_flex_flow(mr, LV_FLEX_FLOW_ROW_WRAP);
            for (auto& m : ep.methods)
                chip(mr, m, d.method == m, [this, m] {
                    Draft& dd = dr();
                    dd.method = m;
                    if (m == "GET" || m == "HEAD" || m == "DELETE") dd.bodyMode = "none";
                    else if (dd.bodyMode == "none") dd.bodyMode = "json";
                    render();
                });
        }
        auto redraw = [this] { render(); };
        if (!d.path.empty()) kvEditor(rq, "Path values", d.path, "name", "value", redraw);
        std::vector<std::string> qsug, bsug, hsug;
        for (auto& f : ep.fields) (f.where == "query" ? qsug : f.where == "body" ? bsug : f.where == "header" ? hsug : qsug).push_back(f.name);
        if (ep.kind == wh::Kind::Form) {
            for (size_t i = 0; i < d.body.size() && i < ep.formFields.size(); i++) {
                const wh::FormField& ff = ep.formFields[i];
                lv_obj_t* fr = field(rq, ff.label + (ff.required ? " *" : "") + "  (" + ff.type + ")", d.body[i].v.empty() ? "tap to fill" : d.body[i].v, [this, i] { askFormValue(i); });
                (void)fr;
            }
        } else {
            kvEditor(rq, "Query", d.query, "parameter", "value", redraw, (ep.method() == "GET" || d.method == "GET") ? qsug : std::vector<std::string>());
            if (ep.kind != wh::Kind::Mcp) {
                lv_obj_t* br = row(rq);
                lv_obj_set_flex_flow(br, LV_FLEX_FLOW_ROW_WRAP);
                text(br, "Body", M().md);
                for (const char* m : {"json", "form", "text", "none"})
                    chip(br, m, d.bodyMode == m, [this, m] {
                        dr().bodyMode = m;
                        render();
                    });
            } else text(rq, "JSON-RPC body", M().md);
            if (d.bodyMode == "json" && !d.rawJson) {
                kvEditor(rq, "Fields", d.body, "field", "value", redraw, bsug);
                button(rq, "Edit as JSON", Kind::Ghost, [this] {
                    Draft& dd = dr();
                    dd.raw = buildJson(dd.body);
                    prompt("JSON body", util::pretty(dd.raw, 8000), "{}", false, true, [this](const std::string& v) {
                        Draft& d2 = dr();
                        d2.raw = v;
                        d2.rawJson = true;
                        render();
                    });
                });
            } else if (d.bodyMode == "form") {
                kvEditor(rq, "Fields", d.body, "field", "value", redraw, bsug);
            } else if (d.bodyMode == "text" || d.rawJson) {
                lv_obj_t* tc = card(rq);
                lv_obj_add_flag(tc, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_t* tl = subtext(tc, d.raw.empty() ? "(empty) tap to edit" : util::trunc(d.raw, 400), M().sm);
                lv_obj_set_width(tl, LV_PCT(100));
                onClick(tc, [this] {
                    prompt(dr().bodyMode == "text" ? "Text body" : "JSON body", dr().raw, "", false, true, [this](const std::string& v) {
                        dr().raw = v;
                        render();
                    });
                });
                if (d.rawJson && ep.kind != wh::Kind::Mcp) button(rq, "Back to fields", Kind::Ghost, [this] {
                    dr().rawJson = false;
                    render();
                });
            }
            kvEditor(rq, "Headers", d.headers, "header", "value", redraw, hsug);
        }

        lv_obj_t* ar = row(box_);
        lv_obj_set_flex_flow(ar, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_column(ar, M().gap, 0);
        lv_obj_set_style_pad_row(ar, M().gap, 0);
        button(ar, "Send", Kind::Primary, [this] { send_(); });
        if (ep.kind == wh::Kind::Chat || ep.openai || (ep.kind == wh::Kind::Webhook && (ep.ai || !chatField().empty())))
            button(ar, "Chat", Kind::Secondary, [this] { openChat(); });
        if (ep.models) button(ar, "List models", Kind::Secondary, [this] { send_(); });
        button(ar, "Save call", Kind::Ghost, [this] { saveFav(); });
        lv_obj_t* tu = subtext(box_, "Test URL (while the editor listens): " + cfg::s().url + ep.testUrl(), M().sm);
        lv_obj_set_width(tu, LV_PCT(100));
    }

    std::string chatField() const {
        for (const char* k : {"chatInput", "message", "prompt", "question", "query", "text", "input", "content"})
            for (auto& f : ep.fields)
                if (f.where == "body" && f.name == k) return k;
        return "";
    }

    void openChat() { nav::push(makeChat(ep, chatField())); }

    void publish() {
        setWorkflowActive(*this, ep.wfId, true, [this](bool ok) {
            if (ok) {
                ep.wfActive = true;
                for (auto& e : g_eps)
                    if (e.wfId == ep.wfId) e.wfActive = true;
                toast("Published", Tone::Success);
                render();
            }
        });
    }

    void askSecret(int which) {
        wh::Secret s = wh::loadSecret(ep.id());
        static const char* names[] = {"Username", "Password", "Header name", "Header value", "Token"};
        std::string cur = which == 0 ? s.user : which == 1 ? s.pass : which == 2 ? (s.header.empty() ? "Authorization" : s.header) : which == 3 ? s.value : s.token;
        bool secret = which == 1 || which == 3 || which == 4;
        prompt(names[which], secret ? "" : cur, names[which], secret, which == 4, [this, which](const std::string& v) {
            wh::Secret x = wh::loadSecret(ep.id());
            if (which == 0) x.user = v;
            else if (which == 1) x.pass = v;
            else if (which == 2) x.header = v;
            else if (which == 3) x.value = v;
            else x.token = v;
            wh::saveSecret(ep.id(), x);
            render();
        });
    }

    void askFormValue(size_t i) {
        Draft& d = dr();
        if (i >= ep.formFields.size() || i >= d.body.size()) return;
        const wh::FormField& ff = ep.formFields[i];
        if (!ff.options.empty()) {
            std::vector<MenuItem> m;
            for (auto& o : ff.options) m.push_back({o});
            menu(ff.label, m, [this, i, ff](int k) {
                dr().body[i].v = ff.options[k];
                render();
            });
            return;
        }
        if (ff.type == "file") {
            toast("File fields are not supported from the device", Tone::Warning);
            return;
        }
        prompt(ff.label, d.body[i].v, ff.type, ff.type == "password", ff.type == "textarea", [this, i](const std::string& v) {
            dr().body[i].v = v;
            render();
        });
    }

    void send_() {
        Draft& d = dr();
        if (needsSecret()) {
            wh::Secret s = wh::loadSecret(ep.id());
            if (wh::authHeaders(ep, s).empty()) toast("This endpoint needs credentials: set them above first", Tone::Warning);
        }
        api::Request r = makeCall(ep, d);
        toast(r.method + " " + util::trunc(r.path, 40));
        std::string m = r.method, p = r.path;
        Page::send(r, [m, p](api::Response& res) { showResponse(m, p, res); });
    }

    void saveFav() {
        Draft& d = dr();
        api::Request r = makeCall(ep, d);
        std::string path = r.path + (r.query.empty() ? "" : "?" + r.query);
        auto& hs = cfg::s().hooks;
        if (hs.size() >= 12) {
            toast("Max 12 saved calls", Tone::Warning);
            return;
        }
        hs.push_back({ep.wfName + ": " + ep.nodeName, path, r.method, r.body});
        cfg::save();
        toast("Saved to Webhooks > Saved", Tone::Success);
    }
};

PagePtr makeEndpointPage(const wh::Endpoint& e) {
    auto p = std::make_unique<EndpointPage>();
    p->ep = e;
    return p;
}

class WebhooksPage : public Page {
public:
    int kindSel = 0;                // 0 all, 1 webhooks, 2 AI/chat, 3 forms, 4 MCP, 5 saved
    bool inactive = false;
    std::string needle, err;
    bool loading_ = false;
    lv_obj_t *chipRow = nullptr, *list = nullptr, *filterLbl = nullptr;
    int pages = 0;

    void build() override {
        title = "Webhooks";
        actions.push_back({LV_SYMBOL_PLUS, [this] { adhoc(); }});
        actions.push_back({LV_SYMBOL_REFRESH, [this] { load(); }});
        chipRow = row(body);
        lv_obj_set_flex_flow(chipRow, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_hor(chipRow, M().pad, 0);
        lv_obj_set_style_pad_top(chipRow, M().gap, 0);
        lv_obj_t* bar = row(body);
        lv_obj_set_style_pad_hor(bar, M().pad, 0);
        lv_obj_set_style_pad_top(bar, M().gap, 0);
        lv_obj_t* f = card(bar);
        lv_obj_set_flex_grow(f, 1);
        lv_obj_set_width(f, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_ver(f, M().compact ? 5 : 12, 0);
        lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(f, LV_OBJ_FLAG_CLICKABLE);
        filterLbl = subtext(f, "Search");
        lv_obj_set_flex_grow(filterLbl, 1);
        onClick(f, [this] {
            prompt("Search", needle, "path, workflow, type", false, false, [this](const std::string& v) {
                needle = v;
                setText(filterLbl, needle.empty() ? "Search" : needle);
                render();
            });
        });
        list = scroller(body);
        renderChips();
        if (!g_eps.empty()) render();
        if (g_eps.empty() || plat::millis() - g_epsAt > 20000 || g_epsInactive != inactive) load();
    }
    bool autoRefresh() override { return false; }

    void renderChips() {
        lv_obj_clean(chipRow);
        static const char* names[] = {"All", "Webhooks", "Chat & AI", "Forms", "MCP", "Saved"};
        for (int i = 0; i < 6; i++)
            chip(chipRow, names[i], kindSel == i, [this, i] {
                kindSel = i;
                renderChips();
                render();
            });
        chip(chipRow, "Include unpublished", inactive, [this] {
            inactive = !inactive;
            renderChips();
            load();
        });
    }

    void load() {
        if (loading_) return;
        loading_ = true;
        err.clear();
        pages = 0;
        auto acc = std::make_shared<std::vector<wh::Endpoint>>();
        fetch("", acc);
    }

    void fetch(const std::string& cursor, std::shared_ptr<std::vector<wh::Endpoint>> acc) {
        std::string q = "limit=100";
        if (!inactive) q += "&active=true";
        if (!cursor.empty()) q += "&cursor=" + util::urlEncode(cursor);
        if (g_eps.empty() && acc->empty()) {
            lv_obj_clean(list);
            loading(list, "Looking for webhooks...");
        }
        get("/workflows", q, wh::listFilter(), [this, acc](api::Response& r) {
            if (!r.ok()) {
                loading_ = false;
                err = r.message();
                render();
                return;
            }
            for (JsonObjectConst w : r.json()["data"].as<JsonArrayConst>()) wh::discover(w, *acc);
            const char* nc = r.json()["nextCursor"] | (const char*)nullptr;
            if (nc && *nc && ++pages < 20) {
                fetch(nc, acc);
                return;
            }
            loading_ = false;
            std::vector<wh::Endpoint>& v = *acc;
            if (!inactive) v.erase(std::remove_if(v.begin(), v.end(), [](const wh::Endpoint& e) { return e.wfArchived; }), v.end());
            std::stable_sort(v.begin(), v.end(), [](const wh::Endpoint& a, const wh::Endpoint& b) {
                if (a.usable() != b.usable()) return a.usable();
                if (a.wfName != b.wfName) return a.wfName < b.wfName;
                return a.path < b.path;
            });
            g_eps = v;
            g_epsAt = plat::millis();
            g_epsInactive = inactive;
            render();
        });
    }

    static std::string lower(std::string x) {
        for (auto& c : x) c = (char)tolower((unsigned char)c);
        return x;
    }

    void render() {
        lv_obj_clean(list);
        if (kindSel == 5) {
            renderSaved();
            return;
        }
        if (!err.empty() && g_eps.empty()) {
            empty(list, LV_SYMBOL_WARNING, "Could not load", err);
            button(list, "Retry", Kind::Primary, [this] { load(); });
            return;
        }
        std::string nl = lower(needle);
        int shown = 0;
        for (size_t i = 0; i < g_eps.size(); i++) {
            const wh::Endpoint& e = g_eps[i];
            bool ok = kindSel == 0 || (kindSel == 1 && e.kind == wh::Kind::Webhook && !e.openai) ||
                      (kindSel == 2 && (e.kind == wh::Kind::Chat || e.openai || (e.ai && e.kind == wh::Kind::Webhook))) ||
                      (kindSel == 3 && e.kind == wh::Kind::Form) || (kindSel == 4 && e.kind == wh::Kind::Mcp);
            if (!ok) continue;
            std::string hay = lower(e.path + " " + e.wfName + " " + e.kindName() + " " + e.nodeName);
            if (!nl.empty() && hay.find(nl) == std::string::npos) continue;
            shown++;
            addRow(i);
        }
        if (!shown) {
            if (loading_) loading(list, "Looking for webhooks...");
            else empty(list, LV_SYMBOL_UPLOAD, needle.empty() ? "No webhooks found" : "No match",
                       needle.empty() ? (inactive ? "No workflow has a Webhook, Form, Chat or MCP trigger." : "No published workflow has a Webhook, Form, Chat or MCP trigger. Try \"Include unpublished\".") : "");
        }
    }

    void addRow(size_t i) {
        const wh::Endpoint& e = g_eps[i];
        lv_obj_t* c = card(list);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(c, M().gap, 0);
        lv_obj_set_style_pad_ver(c, M().compact ? 6 : 12, 0);
        lv_obj_set_style_bg_color(c, C().surface2, LV_STATE_PRESSED);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        dot(c, e.usable() ? Tone::Success : Tone::Neutral);
        lv_obj_t* tc = col(c);
        lv_obj_set_width(tc, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(tc, 1);
        lv_obj_set_style_pad_row(tc, 2, 0);
        std::string m;
        if (e.kind == wh::Kind::Webhook)
            for (auto& x : e.methods) m += (m.empty() ? "" : "/") + x;
        lv_obj_t* t = text(tc, (m.empty() ? "" : m + "  ") + "/" + e.path, M().md);
        ellipsis(t);
        lv_obj_set_width(t, LV_PCT(100));
        lv_obj_t* st = subtext(tc, e.wfName + (e.wfActive ? "" : "  - not published"), M().sm);
        ellipsis(st);
        lv_obj_set_width(st, LV_PCT(100));
        badge(c, e.kindName(), e.openai || e.ai ? Tone::Primary : Tone::Neutral);
        if (!e.authLabel().empty() && !M().compact) badge(c, e.authLabel(), Tone::Warning);
        onClick(c, [i] {
            if (i < g_eps.size()) nav::push(makeEndpointPage(g_eps[i]));
        });
    }

    void renderSaved() {
        auto& hooks = cfg::s().hooks;
        if (hooks.empty()) empty(list, LV_SYMBOL_UPLOAD, "No saved calls yet", "Open a webhook and tap \"Save call\", or tap + for an ad-hoc call.");
        for (size_t i = 0; i < hooks.size(); i++) {
            lv_obj_t* c = card(list);
            lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_bg_color(c, C().surface2, LV_STATE_PRESSED);
            lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t* t = col(c);
            lv_obj_set_width(t, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(t, 1);
            lv_obj_set_style_pad_row(t, 2, 0);
            ellipsis(text(t, hooks[i].name.empty() ? hooks[i].path : hooks[i].name));
            ellipsis(subtext(t, hooks[i].method + " " + hooks[i].path));
            iconButton(c, LV_SYMBOL_TRASH, [this, i] {
                cfg::s().hooks.erase(cfg::s().hooks.begin() + i);
                cfg::save();
                render();
            });
            iconButton(c, LV_SYMBOL_PLAY, [this, i] { runSaved(cfg::s().hooks[i]); }, true);
        }
    }
    void runSaved(const cfg::Hook& h) { runWebhook(*this, h.method.empty() ? "POST" : h.method, h.path, h.body); }

    void adhoc() {
        prompt("Path on the server (e.g. /webhook/abc)", "/webhook/", "/webhook/...", false, false, [this](const std::string& p) {
            menu("Method", {{"GET"}, {"POST"}, {"PUT"}, {"DELETE"}}, [this, p](int i) {
                static const char* m[] = {"GET", "POST", "PUT", "DELETE"};
                std::string method = m[i];
                if (i == 0 || i == 3) runWebhook(*this, method, p, "");
                else prompt("Body (JSON)", "{}", "{}", false, true, [this, method, p](const std::string& b) { runWebhook(*this, method, p, b); });
            });
        });
    }
};
PagePtr makeWebhooks() { return std::make_unique<WebhooksPage>(); }

}  // namespace ui
