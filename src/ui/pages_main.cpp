// Dashboard, webhook runner helper, API Explorer.
#include <algorithm>
#include <map>

#include "../core/config.h"
#include "../core/plat.h"
#include "../core/routes_data.h"
#include "../core/util.h"
#include "res.h"

namespace ui {

static std::string S(JsonObjectConst o, const char* k) { return util::str(o[k]); }

void runWebhook(Page& owner, const std::string& method, const std::string& path, const std::string& body) {
    toast("Calling " + method + " " + util::trunc(path, 40));
    api::Request r;
    r.method = method;
    r.path = util::encodePath(path);
    r.body = body;
    r.raw = true;
    owner.send(r, [method, path](api::Response& res) {
        std::string head = method + " " + path + "\nHTTP " + std::to_string(res.status) + "  " + std::to_string(res.ms) + " ms  " +
                           std::to_string(res.bytes) + " bytes\n";
        if (res.status <= 0) head += res.error + "\n";
        head += "\n";
        std::string b = res.body;
        if (!b.empty() && (b[0] == '{' || b[0] == '[')) b = util::pretty(b, 20000);
        Tone t = res.ok() ? Tone::Success : Tone::Danger;
        toast(res.ok() ? "Webhook OK (" + std::to_string(res.status) + ")" : "Webhook failed: " + res.message(), t);
        viewer("Webhook result", head + b);
    });
}


class ExplorerPage : public Page {
public:
    int sel = -1;
    std::string pathVals, query, bodyTxt;
    lv_obj_t* box_ = nullptr;
    std::string group;
    void build() override {
        title = "API Explorer";
        box_ = scroller(body);
        render();
    }
    void render() {
        lv_obj_clean(box_);
        if (sel < 0) {
            subtext(box_, std::to_string(kRouteCount) + " endpoints of the n8n public API. Pick a group:");
            std::vector<std::string> groups;
            for (int i = 0; i < kRouteCount; i++)
                if (std::find(groups.begin(), groups.end(), kRoutes[i].group) == groups.end()) groups.push_back(kRoutes[i].group);
            lv_obj_t* chips = row(box_);
            lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
            for (auto& g : groups) chip(chips, g, g == group, [this, g] { group = g; render(); });
            if (!group.empty()) {
                for (int i = 0; i < kRouteCount; i++) {
                    if (group != kRoutes[i].group) continue;
                    lv_obj_t* c = card(box_);
                    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_set_style_bg_color(c, C().surface2, LV_STATE_PRESSED);
                    std::string m = kRoutes[i].method;
                    Tone t = m == "GET" ? Tone::Info : m == "DELETE" ? Tone::Danger : m == "POST" ? Tone::Success : Tone::Warning;
                    lv_obj_t* b = badge(c, m, t);
                    lv_obj_set_width(b, M().compact ? 60 : 110);
                    lv_obj_t* p = text(c, kRoutes[i].path, M().md);
                    ellipsis(p);
                    lv_obj_set_flex_grow(p, 1);
                    onClick(c, [this, i] { choose(i); });
                }
            }
            return;
        }
        const Route& r = kRoutes[sel];
        lv_obj_t* c = card(box_);
        lv_obj_t* h = row(c);
        badge(h, r.method, Tone::Primary);
        lv_obj_t* t = text(h, r.path, M().md);
        lv_obj_set_flex_grow(t, 1);
        std::string tpl = r.path;
        std::vector<std::string> params;
        for (size_t i = 0; i < tpl.size(); i++)
            if (tpl[i] == ':') {
                size_t e = tpl.find('/', i);
                params.push_back(tpl.substr(i + 1, e == std::string::npos ? std::string::npos : e - i - 1));
                i = (e == std::string::npos) ? tpl.size() : e;
            }
        for (auto& pn : params) {
            std::string cur = vals[pn];
            field(box_, pn, cur.empty() ? "tap to set" : cur, [this, pn] {
                prompt(pn, vals[pn], pn, false, false, [this, pn](const std::string& v) { vals[pn] = v; render(); });
            });
        }
        field(box_, "Query string", query.empty() ? "e.g. limit=10" : query, [this] {
            prompt("Query string", query, "limit=10&cursor=...", false, false, [this](const std::string& v) { query = v; render(); });
        });
        bool hasBody = std::string(r.method) != "GET" && std::string(r.method) != "DELETE";
        if (hasBody)
            field(box_, "JSON body", bodyTxt.empty() ? "tap to edit" : util::trunc(bodyTxt, 40), [this] {
                prompt("JSON body", bodyTxt, "{}", false, true, [this](const std::string& v) { bodyTxt = v; render(); });
            });
        lv_obj_t* br = row(box_);
        lv_obj_set_flex_align(br, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(br, "Back", Kind::Secondary, [this] { sel = -1; render(); });
        bool danger = std::string(r.method) == "DELETE";
        button(br, LV_SYMBOL_PLAY "  Send", danger ? Kind::Danger : Kind::Primary, [this, params] { send_(params); });
    }
    std::map<std::string, std::string> vals;
    void choose(int i) {
        sel = i;
        vals.clear();
        query.clear();
        bodyTxt = kRoutes[i].hint;
        render();
    }
    void send_(const std::vector<std::string>& params) {
        const Route& r = kRoutes[sel];
        std::string path = r.path;
        for (auto& pn : params) {
            if (vals[pn].empty()) {
                toast("Set " + pn, Tone::Warning);
                return;
            }
            size_t p = path.find(":" + pn);
            path.replace(p, pn.size() + 1, util::urlEncode(vals[pn]));
        }
        std::string m = r.method, q = query, b = bodyTxt;
        if (!b.empty() && (m == "POST" || m == "PUT" || m == "PATCH")) {
            JsonDocument chk;
            if (deserializeJson(chk, b)) {
                toast("Body is not valid JSON", Tone::Danger);
                return;
            }
        }
        auto go = [this, m, path, q, b] {
            api::Request rq;
            rq.method = m;
            rq.path = path;
            rq.query = q;
            rq.body = b;
            toast("Sending...");
            send(rq, [m, path](api::Response& res) {
                std::string head = m + " " + path + "\nHTTP " + std::to_string(res.status) + "  " + std::to_string(res.ms) + " ms  " + std::to_string(res.bytes) + " bytes";
                if (res.truncated) head += "  (truncated by size limit)";
                head += "\n\n";
                if (res.status <= 0) head += res.error;
                std::string body = res.body.size() > 256 * 1024 ? util::trunc(res.body, 24000) : res.body;
                if (!body.empty() && (body[0] == '{' || body[0] == '[')) body = util::pretty(body, 24000);
                auto keep = std::make_shared<std::string>(std::move(res.body));
                bool trunc = res.truncated;
                viewer(res.ok() ? "Response" : "Error response", head + body, [keep, trunc] {
                    std::string fn = "/response-" + std::to_string(plat::millis()) + ".json";
                    if (trunc) toast("Response was cut by the size limit: not saved", Tone::Warning);
                    else if (plat::sdAvailable() && plat::sdWrite(fn.c_str(), *keep)) toast("Saved " + fn, Tone::Success);
                    else toast("No SD card / write failed", Tone::Danger);
                });
            });
        };
        if (m == "DELETE" || path.find("/stop") != std::string::npos) confirmDanger(m + " " + path, "Send this request?", "Send", go);
        else go();
    }
};
PagePtr makeExplorer() { return std::make_unique<ExplorerPage>(); }

struct DashCache {
    bool valid = false;
    uint32_t at = 0;
    int wfTotal = -1, wfActive = 0, exOk = 0, exErr = 0, exRun = 0, exTotal = -1;
    bool wfMore = false;
    uint32_t pingMs = 0;
    std::vector<std::pair<std::string, std::string>> failures;
};
static DashCache dashCache;

class DashboardPage : public Page {
public:
    lv_obj_t* box_ = nullptr;
    int wfTotal = -1, wfActive = 0, exOk = 0, exErr = 0, exRun = 0, exTotal = -1;
    bool wfMore = false, wfDone = false, exDone = false;
    std::string wfErr, exErr_;
    uint32_t pingMs = 0;
    std::vector<std::pair<std::string, std::string>> failures;  // name, ago

    void build() override {
        title = "Overview";
        actions.push_back({LV_SYMBOL_REFRESH, [this] { dashCache.at = 0; refresh(); }});
        box_ = scroller(body);
        lastW = plat::wifiState() == plat::WifiState::Connected;
        lastC = cfg::configured();
        if (dashCache.valid) {  // show what we knew instantly; only refetch when it is stale
            auto& c = dashCache;
            wfTotal = c.wfTotal; wfActive = c.wfActive; wfMore = c.wfMore; exOk = c.exOk; exErr = c.exErr; exRun = c.exRun;
            exTotal = c.exTotal; pingMs = c.pingMs; failures = c.failures;
            wfDone = exDone = true;
            render();
            if (plat::millis() - c.at > 120000) refresh();
        } else refresh();
    }
    bool autoRefresh() override { return true; }
    bool lastW = false, lastC = false, inflight = false;
    void poll() override {  // follow WiFi / setup changes without a manual refresh
        bool w = plat::wifiState() == plat::WifiState::Connected, c = cfg::configured();
        if (w != lastW || c != lastC) {
            lastW = w;
            lastC = c;
            refresh();
        }
    }

    void refresh() override {
        if (!cfg::configured()) {
            render();
            return;
        }
        if (plat::wifiState() != plat::WifiState::Connected) {  // wait for the link instead of showing a bogus error
            render();
            return;
        }
        if (inflight) return;  // auto-refresh must not pile requests up behind each other
        inflight = true;
        wfDone = exDone = false;
        wfErr.clear();
        exErr_.clear();
        render();
        uint32_t t0 = plat::millis();
        ensureWorkflowNames(*this, [this, t0] {
            pingMs = plat::millis() - t0;
            wfDone = true;
            wfTotal = workflowCount(&wfActive, &wfMore);
            wfErr = workflowCacheError();
            render();
            get("/executions", "limit=50", "{\"data\":[{\"id\":true,\"workflowId\":true,\"status\":true,\"startedAt\":true}]}", [this](api::Response& r) {
                exDone = true;
                exOk = exErr = exRun = 0;
                failures.clear();
                if (r.ok()) {
                    exTotal = 0;
                    for (JsonObjectConst o : r.json()["data"].as<JsonArrayConst>()) {
                        exTotal++;
                        std::string st = S(o, "status");
                        if (st == "success") exOk++;
                        else if (st == "error" || st == "crashed") {
                            exErr++;
                            if (failures.size() < 5) {
                                std::string wn = workflowName(S(o, "workflowId"));
                                failures.push_back({wn.empty() ? "Workflow " + S(o, "workflowId") : wn, util::isoToEpochAgo(S(o, "startedAt").c_str())});
                            }
                        } else if (st == "running" || st == "waiting") exRun++;
                    }
                } else exErr_ = r.message();
                inflight = false;
                if (exErr_.empty() && wfErr.empty()) {
                    dashCache = {true, plat::millis(), wfTotal, wfActive, exOk, exErr, exRun, exTotal, wfMore, pingMs, failures};
                }
                render();
            });
        }, true);
    }

    void stat(lv_obj_t* parent, int w, const char* icon, const std::string& value, const char* name, Tone t) {
        lv_obj_t* c = card(parent);
        lv_obj_set_width(c, w);
        lv_obj_t* r = row(c);
        label(r, icon, M().lg, toneColor(t));
        subtext(r, name);
        text(c, value, M().compact ? M().xl : M().title);
    }

    void render() {
        lv_obj_clean(box_);
        const Palette& c = C();
        if (!cfg::configured()) {
            setupChecklist(*this, box_, [this] { refresh(); });
            return;
        }
        int w = lv_display_get_horizontal_resolution(nullptr);
        int avail = M().compact ? w - 2 * M().pad : w - M().navW - 2 * M().pad;
        lv_obj_set_flex_flow(box_, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(box_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        int cols = M().compact ? 2 : 4;
        int tw = (avail - (cols - 1) * M().gap) / cols;

        lv_obj_t* cb = card(box_);
        lv_obj_set_width(cb, avail);
        lv_obj_set_flex_flow(cb, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cb, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        bool bad = !wfErr.empty() || !exErr_.empty();
        bool ok = wfDone && !bad;
        dot(cb, bad ? Tone::Danger : (ok ? Tone::Success : Tone::Warning));
        lv_obj_t* t = col(cb);
        lv_obj_set_width(t, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(t, 1);
        lv_obj_set_style_pad_row(t, 2, 0);
        std::string host = cfg::s().url;
        text(t, bad ? "Connection problem" : (ok ? "Connected" : "Connecting..."), M().lg);
        lv_obj_t* hl = subtext(t, host + (ok && !bad ? "  -  " + std::to_string(pingMs) + " ms" : ""));
        ellipsis(hl);
        lv_obj_set_width(hl, LV_PCT(100));
        if (bad) {  // full error text, wrapped (it is the main clue when a connection fails)
            lv_obj_t* er = label(t, wfErr.empty() ? exErr_ : wfErr, M().sm, C().danger);
            lv_obj_set_width(er, LV_PCT(100));
            lv_obj_t* br = row(t);
            button(br, "Diagnose", Kind::Primary, [] { nav::push(makeDiagnostics()); });
            button(br, "Settings", Kind::Secondary, [] { nav::section(nav::Settings); });
        }

        std::string wt = wfTotal < 0 ? "-" : std::to_string(wfTotal) + (wfMore ? "+" : "");
        stat(box_, tw, LV_SYMBOL_SHUFFLE, wt, "Workflows", Tone::Info);
        stat(box_, tw, LV_SYMBOL_OK, wfTotal < 0 ? "-" : std::to_string(wfActive), "Active", Tone::Success);
        stat(box_, tw, LV_SYMBOL_PLAY, exTotal < 0 ? "-" : std::to_string(exOk), "Succeeded", Tone::Success);
        stat(box_, tw, LV_SYMBOL_WARNING, exTotal < 0 ? "-" : std::to_string(exErr), "Failed", exErr ? Tone::Danger : Tone::Neutral);

        lv_obj_t* fc = card(box_);
        lv_obj_set_width(fc, M().compact ? avail : avail * 60 / 100);
        heading(fc, "Recent failures (last 50 runs)");
        if (!exDone) loading(fc, "Loading...");
        else if (failures.empty()) subtext(fc, "No recent failed executions.");
        for (auto& f : failures) {
            lv_obj_t* r = row(fc);
            dot(r, Tone::Danger);
            lv_obj_t* n = text(r, f.first);
            ellipsis(n);
            lv_obj_set_flex_grow(n, 1);
            subtext(r, f.second);
        }
        if (exErr) button(fc, "View errors", Kind::Secondary, [] { nav::section(nav::Executions); });

        lv_obj_t* dc = card(box_);
        lv_obj_set_width(dc, M().compact ? avail : avail - avail * 60 / 100 - M().gap);
        heading(dc, "Device");
        kv(dc, "WiFi", plat::wifiSsid() + "  (" + std::to_string(plat::wifiRssi()) + " dBm)");
        kv(dc, "IP", plat::wifiIp());
        kv(dc, "Heap", std::to_string(plat::freeHeap() / 1024) + " KB");
        kv(dc, "PSRAM", std::to_string(plat::freePsram() / 1024) + " KB");
        (void)c;
    }
};
PagePtr makeDashboard() { return std::make_unique<DashboardPage>(); }

}  // namespace ui
