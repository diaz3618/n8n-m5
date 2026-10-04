// Settings, WiFi picker, onboarding checklist.
#include <cstdio>

#include "../core/cache.h"
#include "../core/config.h"
#include "../core/plat.h"
#include "../core/util.h"
#include "../core/version.h"
#include "res.h"

namespace ui {

bool applyTheme() {
    bool d = cfg::isDark();
    if (d != C().dark && nav::depth() <= 1 && !overlayOpen()) {  // never wipe a form in progress
        setDark(d);
        nav::rebuild();
        return true;
    }
    return false;
}

void testConnection(Page& owner, std::function<void(bool, const std::string&)> done) {
    if (plat::wifiState() != plat::WifiState::Connected) {
        done(false, "WiFi is not connected");
        return;
    }
    if (!cfg::configured()) {
        done(false, "Set the server URL and API key first");
        return;
    }
    owner.get("/workflows", "limit=1", "{\"data\":[{\"id\":true}]}", [done](api::Response& r) {
        if (r.ok()) done(true, "Connected in " + std::to_string(r.ms) + " ms");
        else done(false, r.message());
    });
}

class WifiPage : public Page {
public:
    lv_obj_t* list = nullptr;
    std::vector<plat::Ap> aps;
    bool scanning = false;
    std::string target, pendPass, oldSsid, oldPass;
    bool waiting = false;
    uint32_t t0 = 0;
    std::function<void()> onDone;

    void build() override {
        title = "WiFi";
        actions.push_back({LV_SYMBOL_REFRESH, [this] { scan(); }});
        list = scroller(body);
        scan();
    }
    void scan() {
        scanning = true;
        plat::wifiScanStart();
        render();
    }
    void poll() override {
        if (scanning && plat::wifiScanDone(aps)) {
            scanning = false;
            render();
        }
        if (waiting) {
            auto st = plat::wifiState();
            if (st == plat::WifiState::Connected) {
                waiting = false;
                toast("Connected to " + target, Tone::Success);
                cfg::s().ssid = target;
                cfg::s().wifiPass = pendPass;  // only persisted once the join worked
                cfg::save();
                if (onDone) onDone();
                nav::pop();
            } else if (st == plat::WifiState::Failed || plat::millis() - t0 > 20000) {
                waiting = false;
                toast("Could not connect to " + target, Tone::Danger);
                if (!cfg::s().ssid.empty()) plat::wifiConnect(cfg::s().ssid, cfg::s().wifiPass);  // back to the known network
                render();
            }
        }
    }
    void render() {
        lv_obj_clean(list);
        lv_obj_t* cur = card(list);
        lv_obj_t* r = row(cur);
        dot(r, plat::wifiState() == plat::WifiState::Connected ? Tone::Success : Tone::Neutral);
        lv_obj_t* t = text(r, plat::wifiState() == plat::WifiState::Connected ? plat::wifiSsid() + "   " + plat::wifiIp() : "Not connected");
        lv_obj_set_flex_grow(t, 1);
        if (waiting) loading(list, "Connecting to " + target + "...");
        else if (scanning) loading(list, "Scanning...");
        for (auto& ap : aps) {
            lv_obj_t* c = card(list);
            lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(c, C().surface2, LV_STATE_PRESSED);
            lv_obj_set_style_pad_ver(c, M().compact ? 6 : 14, 0);
            label(c, LV_SYMBOL_WIFI, M().md, ap.rssi > -60 ? C().success : (ap.rssi > -75 ? C().warning : C().danger));
            lv_obj_t* n = text(c, ap.ssid.empty() ? "(hidden)" : ap.ssid);
            ellipsis(n);
            lv_obj_set_flex_grow(n, 1);
            subtext(c, std::to_string(ap.rssi) + " dBm");
            if (ap.secure) label(c, LV_SYMBOL_EYE_CLOSE, M().sm, C().muted);
            std::string ssid = ap.ssid;
            bool sec = ap.secure;
            onClick(c, [this, ssid, sec] { pick(ssid, sec); });
        }
        if (!scanning && aps.empty()) empty(list, LV_SYMBOL_WIFI, "No networks found", "Pull the refresh button to scan again.");
        button(list, "Enter network manually", Kind::Secondary, [this] {
            prompt("Network name (SSID)", "", "ssid", false, false, [this](const std::string& s) { pick(s, true); });
        });
    }
    void pick(const std::string& ssid, bool secure) {
        if (!secure) {
            join(ssid, "");
            return;
        }
        std::string init = (ssid == cfg::s().ssid) ? cfg::s().wifiPass : "";
        prompt("Password for " + ssid, init, "password", true, false, [this, ssid](const std::string& p) { join(ssid, p); });
    }
    void join(const std::string& ssid, const std::string& pass) {
        target = ssid;
        pendPass = pass;
        plat::wifiConnect(ssid, pass);
        waiting = true;
        t0 = plat::millis();
        render();
    }
};

void setupChecklist(Page& owner, lv_obj_t* parent, Fn changed) {
    auto& s = cfg::s();
    lv_obj_t* c = card(parent);
    heading(c, "Welcome to n8n Remote");
    lv_obj_set_width(subtext(c, "Connect the device to WiFi, then point it at your n8n server."), LV_PCT(100));
    auto step = [&](const char* num, const std::string& name, const std::string& value, bool ok, Fn fn) {
        lv_obj_t* r = row(c);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_ver(r, M().compact ? 6 : 12, 0);
        label(r, ok ? LV_SYMBOL_OK : num, M().lg, ok ? C().success : C().muted);
        lv_obj_t* t = col(r);
        lv_obj_set_width(t, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(t, 1);
        lv_obj_set_style_pad_row(t, 0, 0);
        text(t, name);
        lv_obj_t* v = subtext(t, value);
        ellipsis(v);
        lv_obj_set_width(v, LV_PCT(100));
        label(r, LV_SYMBOL_RIGHT, M().sm, C().muted);
        onClick(r, std::move(fn));
    };
    Page* po = &owner;
    std::shared_ptr<bool> life = owner.life;
    step("1", "WiFi", plat::wifiState() == plat::WifiState::Connected ? plat::wifiSsid() : "Not connected", plat::wifiState() == plat::WifiState::Connected,
         [changed] {
             auto w = std::make_unique<WifiPage>();
             w->onDone = changed;
             nav::push(std::move(w));
         });
    step("2", "Server URL", s.url.empty() ? "e.g. https://n8n.example.com" : s.url, !s.url.empty(), [changed] {
        prompt("n8n server URL", cfg::s().url, "https://n8n.example.com", false, false, [changed](const std::string& v) {
            cfg::s().url = util::normalizeUrl(v);
            cfg::save();
            if (changed) changed();
        });
    });
    step("3", "API key", util::mask(s.apiKey), !s.apiKey.empty(), [changed] {
        prompt("n8n API key", cfg::s().apiKey, "paste or type your key", true, true, [changed](const std::string& v) {
            cfg::s().apiKey = util::trim(v);
            cfg::save();
            if (changed) changed();
        });
    });
    lv_obj_t* r = row(c);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_width(subtext(c, "Typing a long key is slow: use Settings > Setup from phone/PC, or import n8n-remote.json from the SD card."), LV_PCT(100));
    button(r, "Test connection", Kind::Primary, [po, life] {
        testConnection(*po, [](bool ok, const std::string& m) { toast(m, ok ? Tone::Success : Tone::Danger); });
    });
}

class SettingsPage : public Page {
public:
    lv_obj_t* box_ = nullptr;
    void build() override {
        title = "Settings";
        box_ = scroller(body);
        render();
    }
    void sect(const std::string& name) {
        lv_obj_t* h = subtext(box_, name);
        lv_obj_set_style_pad_top(h, M().gap, 0);
        lv_obj_set_style_text_font(h, M().md, 0);
    }
    void choice(const char* name, const std::vector<std::string>& opts, int cur, std::function<void(int)> set) {
        field(box_, name, opts[std::max(0, std::min<int>(cur, (int)opts.size() - 1))], [this, name, opts, set] {
            std::vector<MenuItem> m;
            for (auto& o : opts) m.push_back({o});
            menu(name, m, [this, set](int i) {
                set(i);
                cfg::save();
                render();
            });
        });
    }
    void number(const char* name, int cur, const char* unit, int lo, int hi, std::function<void(int)> set) {
        field(box_, name, std::to_string(cur) + (unit ? std::string(" ") + unit : ""), [this, name, cur, lo, hi, set] {
            prompt(name, std::to_string(cur), std::to_string(lo) + " - " + std::to_string(hi), false, false, [this, lo, hi, set](const std::string& v) {
                int n = atoi(v.c_str());
                set(std::max(lo, std::min(hi, n)));
                cfg::save();
                render();
            });
        });
    }

    void render() {
        auto& s = cfg::s();
        lv_obj_clean(box_);
        sect("CONNECTION");
        field(box_, "Server URL", s.url.empty() ? "not set" : s.url, [this] {
            prompt("n8n server URL", cfg::s().url, "https://n8n.example.com", false, false, [this](const std::string& v) {
                cfg::s().url = util::normalizeUrl(v);
                cfg::save();
                render();
            });
        });
        field(box_, "API key", util::mask(s.apiKey), [this] {
            prompt("n8n API key", cfg::s().apiKey, "paste or type your key", true, true, [this](const std::string& v) {
                cfg::s().apiKey = util::trim(v);
                cfg::save();
                render();
            });
        });
        choice("TLS verification", {"Verify (CA bundle)", "Custom CA certificate", "Insecure (skip verify)"}, s.tls, [](int i) { cfg::s().tls = i; });
        if (s.tls == 1)
            field(box_, "Custom CA (PEM)", s.caPem.empty() ? "not set" : std::to_string(s.caPem.size()) + " bytes", [this] {
                menu("Custom CA", {{"Paste PEM"}, {"Load /n8n-ca.pem from SD"}, {"Clear"}}, [this](int i) {
                    if (i == 0) prompt("CA certificate (PEM)", cfg::s().caPem, "-----BEGIN CERTIFICATE-----", false, true, [this](const std::string& v) { cfg::s().caPem = v; cfg::save(); render(); });
                    else if (i == 1) {
                        std::string t;
                        if (plat::sdRead("/n8n-ca.pem", t)) { cfg::s().caPem = t; cfg::save(); toast("CA loaded", Tone::Success); }
                        else toast("n8n-ca.pem not found on SD", Tone::Danger);
                        render();
                    } else { cfg::s().caPem.clear(); cfg::save(); render(); }
                });
            });
        lv_obj_t* tr = row(box_);
        lv_obj_set_flex_flow(tr, LV_FLEX_FLOW_ROW_WRAP);
        button(tr, "Diagnose", Kind::Primary, [] { nav::push(makeDiagnostics()); });
        button(tr, "Test", Kind::Secondary, [this] {
            toast("Testing...");
            testConnection(*this, [](bool ok, const std::string& m) { toast(m, ok ? Tone::Success : Tone::Danger); });
        });
        button(tr, M().compact ? "Phone setup" : "Setup from phone/PC", Kind::Secondary, [this] { portal(); });

        sect("NETWORK");
        field(box_, "WiFi", plat::wifiState() == plat::WifiState::Connected ? plat::wifiSsid() + "  " + plat::wifiIp() : (s.ssid.empty() ? "not set" : s.ssid + " (offline)"), [this] {
            auto w = std::make_unique<WifiPage>();
            w->onDone = [this] { render(); };
            nav::push(std::move(w));
        });
        number("HTTP timeout", s.timeoutSec, "s", 3, 120, [](int v) { cfg::s().timeoutSec = v; });
        number("Max response size", s.maxKb, "KB", 64, plat::info().compact ? 2048 : 8192, [](int v) { cfg::s().maxKb = v; });

        {
            lv_obj_t* sc = card(box_);
            lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(sc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t* st = text(sc, "Cache responses on SD card");
            lv_obj_set_flex_grow(st, 1);
            toggle(sc, s.sdCache, [](bool on) { cfg::s().sdCache = on; cfg::save(); });
            size_t e, b, f;
            cache::stats(e, b, f);
            kv(box_, "Cache", std::to_string(e) + " in RAM (" + std::to_string(b / 1024) + " KB), " + std::to_string(f) + " on SD");
            lv_obj_t* cr = row(box_);
            button(cr, "Clear cache", Kind::Secondary, [this] { cache::clear(); toast("Cache cleared", Tone::Success); render(); });
        }

        sect("APPEARANCE");
        {
            static const char* tn[] = {"Light", "Dark", "Auto (dark 19:00-07:00)"};
            field(box_, "Theme", tn[s.theme % 3], [this] {
                menu("Theme", {{tn[0]}, {tn[1]}, {tn[2]}}, [this](int i) {
                    cfg::s().theme = i;
                    cfg::save();
                    if (!applyTheme()) render();  // applyTheme rebuilds (and destroys this page) when the look changes
                });
            });
        }
        lv_obj_t* fc = card(box_);
        lv_obj_set_flex_flow(fc, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(fc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t* ft = text(fc, "Flip screen 180" "\xC2\xB0");
        lv_obj_set_flex_grow(ft, 1);
        toggle(fc, s.flip, [](bool on) { cfg::s().flip = on; cfg::save(); plat::setFlip(on); });
        lv_obj_t* bc = card(box_);
        lv_obj_t* br = row(bc);
        lv_obj_t* bt = text(br, "Brightness");
        lv_obj_set_flex_grow(bt, 1);
        lv_obj_t* bv = subtext(br, std::to_string(s.brightness) + "%");
        lv_obj_t* sl = lv_slider_create(bc);
        lv_obj_set_width(sl, LV_PCT(96));
        lv_obj_set_height(sl, M().compact ? 8 : 14);
        lv_obj_set_style_pad_all(sl, 0, LV_PART_KNOB);
        lv_obj_set_style_bg_color(sl, C().border, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sl, C().primary, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sl, C().primary, LV_PART_KNOB);
        lv_slider_set_range(sl, 5, 100);
        lv_slider_set_value(sl, s.brightness, LV_ANIM_OFF);
        onChange(sl, [sl, bv] {
            int v = lv_slider_get_value(sl);
            cfg::s().brightness = v;
            plat::setBrightness(v);
            setText(bv, std::to_string(v) + "%");
        });
        lv_obj_add_event_cb(sl, [](lv_event_t*) { cfg::save(); }, LV_EVENT_RELEASED, nullptr);
        choice("Screen sleep", {"Never", "30 s", "1 min", "2 min", "5 min", "10 min"},
               s.sleepSec == 0 ? 0 : s.sleepSec <= 30 ? 1 : s.sleepSec <= 60 ? 2 : s.sleepSec <= 120 ? 3 : s.sleepSec <= 300 ? 4 : 5,
               [](int i) { static const int v[] = {0, 30, 60, 120, 300, 600}; cfg::s().sleepSec = v[i]; });

        sect("BEHAVIOUR");
        number("List page size", s.pageSize, "items", 5, 100, [](int v) { cfg::s().pageSize = v; });
        choice("Auto-refresh", {"Off", "15 s", "30 s", "1 min", "5 min"}, s.refreshSec == 0 ? 0 : s.refreshSec <= 15 ? 1 : s.refreshSec <= 30 ? 2 : s.refreshSec <= 60 ? 3 : 4,
               [](int i) { static const int v[] = {0, 15, 30, 60, 300}; cfg::s().refreshSec = v[i]; });
        lv_obj_t* cc = card(box_);
        lv_obj_set_flex_flow(cc, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t* ct = text(cc, "Confirm destructive actions");
        lv_obj_set_flex_grow(ct, 1);
        toggle(cc, s.confirmDestructive, [](bool on) { cfg::s().confirmDestructive = on; cfg::save(); });

        sect("TIME");
        static const int tzv[] = {-480, -420, -360, -300, -240, -180, 0, 60, 120, 180, 240, 330, 480, 540, 600};
        static const char* tzn[] = {"UTC-8 Pacific", "UTC-7 Mountain", "UTC-6 Central", "UTC-5 Eastern", "UTC-4 Atlantic", "UTC-3 Brasilia", "UTC+0 London",
                                    "UTC+1 Berlin", "UTC+2 Athens", "UTC+3 Moscow", "UTC+4 Dubai", "UTC+5:30 India", "UTC+8 Singapore", "UTC+9 Tokyo", "UTC+10 Sydney"};
        std::string cur = "custom (" + std::to_string(s.tzMinutes) + " min)";
        for (int i = 0; i < 15; i++) if (tzv[i] == s.tzMinutes) cur = tzn[i];
        field(box_, "Timezone", cur, [this] {
            std::vector<MenuItem> m;
            for (auto n : tzn) m.push_back({n});
            menu("Timezone", m, [this](int i) { cfg::s().tzMinutes = tzv[i]; cfg::save(); plat::setTz(tzv[i], cfg::s().ntp.c_str()); render(); });
        });
        field(box_, "NTP server", s.ntp, [this] {
            prompt("NTP server", cfg::s().ntp, "pool.ntp.org", false, false, [this](const std::string& v) { cfg::s().ntp = v.empty() ? "pool.ntp.org" : v; cfg::save(); plat::setTz(cfg::s().tzMinutes, cfg::s().ntp.c_str()); render(); });
        });
        time_t n = plat::now();
        kv(box_, "Device time", n > 1600000000 ? util::fmtDateTime(n + s.tzMinutes * 60).replace(16, 4, "") : "not synced");

        sect("BACKUP");
        lv_obj_t* bk = row(box_);
        lv_obj_set_flex_flow(bk, LV_FLEX_FLOW_ROW_WRAP);
        button(bk, "Export to SD", Kind::Secondary, [this] {
            confirm("Export settings", "Include API key and WiFi password in /n8n-remote.json?", "Include secrets", false, [this] { exportSd(true); });
        });
        button(bk, "Import from SD", Kind::Secondary, [this] { importSd(); });

        sect("ABOUT");
        lv_obj_t* ab = card(box_);
        kv(ab, "Firmware", std::string("n8n Remote ") + FW_VERSION);
        kv(ab, "Device", plat::info().name);
        kv(ab, "Device ID", plat::deviceId());
        kv(ab, "Display", std::to_string(plat::info().w) + "x" + std::to_string(plat::info().h));
        kv(ab, "Free heap", std::to_string(plat::freeHeap() / 1024) + " KB");
        kv(ab, "Free PSRAM", std::to_string(plat::freePsram() / 1024) + " KB");
        kv(ab, "SD card", plat::sdAvailable() ? "present" : "not found");
        lv_obj_t* sr = row(box_);
        lv_obj_set_flex_flow(sr, LV_FLEX_FLOW_ROW_WRAP);
        button(sr, "Restart", Kind::Secondary, [] { plat::restart(); });
        button(sr, "Factory reset", Kind::Danger, [] {
            confirm("Factory reset", "Erase all settings, WiFi and the API key from this device?", "Erase", true, [] {
                cache::clear();
                plat::eraseAll();
                plat::restart();
            });
        });
    }

    void exportSd(bool secrets) {
        if (plat::sdAvailable() && plat::sdWrite("/n8n-remote.json", cfg::exportJson(secrets))) toast("Saved /n8n-remote.json", Tone::Success);
        else toast("No SD card or write failed", Tone::Danger);
    }
    void importSd() {
        std::string t, err;
        if (!plat::sdRead("/n8n-remote.json", t)) {
            toast("/n8n-remote.json not found on SD", Tone::Danger);
            return;
        }
        if (!cfg::importJson(t, err)) {
            toast("Import failed: " + err, Tone::Danger);
            return;
        }
        cfg::save();
        toast("Settings imported", Tone::Success);
        plat::setBrightness(cfg::s().brightness);
        setDark(cfg::isDark());
        nav::rebuild();
    }

    // device-hosted setup page; PIN protects it on the LAN
    void portal() {
        if (plat::wifiState() != plat::WifiState::Connected) {
            toast("Connect to WiFi first", Tone::Warning);
            return;
        }
        char pin[8];
        snprintf(pin, sizeof pin, "%06u", (unsigned)(plat::random32() % 1000000u));
        if (!plat::portalStart(pin)) {
            toast("Could not start portal", Tone::Danger);
            return;
        }
        std::string url = "http://" + plat::wifiIp() + "/";
        confirm("Setup from phone / PC", "On a device in the same network open\n" + url + "\nPIN: " + pin + "\n\nPress Done when finished.", "Done", false, [this] {
            plat::portalStop();
            setDark(cfg::isDark());
            nav::rebuild();
        }, [] { plat::portalStop(); });
    }
};

class DiagPage : public Page {
public:
    lv_obj_t* box_ = nullptr;
    bool probing = false;
    std::string report;
    void build() override {
        title = "Diagnostics";
        actions.push_back({LV_SYMBOL_REFRESH, [this] { render(); }});
        box_ = scroller(body);
        render();
    }
    void poll() override {
        if (!probing) return;
        std::string r;
        bool done = plat::probeDone(r);
        if (r != report || done) {
            report = r;
            if (done) probing = false;
            render();
        }
    }
    void run() {
        probing = true;
        report.clear();
        plat::probeStart(cfg::s().url, cfg::s().apiKey, cfg::s().tls, cfg::s().caPem);
        render();
    }
    void render() {
        lv_obj_clean(box_);
        auto& s = cfg::s();
        lv_obj_t* c = card(box_);
        heading(c, "Device");
        kv(c, "Last boot", plat::resetReason() + (plat::crashCount() ? "  (crashes: " + std::to_string(plat::crashCount()) + ")" : ""));
        if (plat::safeMode()) {
            lv_obj_set_width(label(c, "SAFE MODE: networking is off after repeated crashes.", M().sm, C().danger), LV_PCT(100));
            button(c, "Leave safe mode", Kind::Primary, [] { plat::clearCrashes(); plat::restart(); });
        }
        kv(c, "WiFi", plat::wifiState() == plat::WifiState::Connected ? plat::wifiSsid() + " " + std::to_string(plat::wifiRssi()) + "dBm" : "NOT connected");
        kv(c, "IP", plat::wifiIp());
        time_t n = plat::now();
        kv(c, "Clock", n ? util::fmtDateTime(n) : "not synced");
        kv(c, "Heap", std::to_string(plat::freeHeap() / 1024) + "K (largest " + std::to_string(plat::largestFreeBlock() / 1024) + "K)");
        kv(c, "PSRAM", std::to_string(plat::freePsram() / 1024) + "K");
        lv_obj_t* c2 = card(box_);
        heading(c2, "Connection");
        kv(c2, "URL", s.url.empty() ? "(not set)" : s.url);
        kv(c2, "Key", s.apiKey.empty() ? "(not set)" : std::to_string(s.apiKey.size()) + " chars");
        kv(c2, "TLS", s.tls == 0 ? "verify" : s.tls == 1 ? "custom CA" : "insecure");
        lv_obj_t* r = row(c2);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
        button(r, probing ? "Probing..." : "Run probe", Kind::Primary, [this] { if (!probing) run(); });
        if (!report.empty()) {
            lv_obj_t* rc = card(box_);
            heading(rc, "Probe result");
            lv_obj_set_width(text(rc, report, M().sm), LV_PCT(100));
        }
        lv_obj_t* hc = card(box_);
        heading(hc, "Recent requests");
        auto& h = api::history();
        if (h.empty()) subtext(hc, "none yet");
        for (size_t i = h.size(); i-- > 0;) {
            std::string line = h[i].method + " " + h[i].path + "  ->  " + std::to_string(h[i].status) + "  " + std::to_string(h[i].ms) + "ms" + (h[i].error.empty() ? "" : "\n" + h[i].error);
            lv_obj_t* l = label(hc, line, M().sm, h[i].status >= 200 && h[i].status < 300 ? C().success : C().danger);
            lv_obj_set_width(l, LV_PCT(100));
        }
    }
};
PagePtr makeDiagnostics() { return std::make_unique<DiagPage>(); }

PagePtr makeSettings() { return std::make_unique<SettingsPage>(); }
PagePtr makeSetup() { return std::make_unique<SettingsPage>(); }

}  // namespace ui
