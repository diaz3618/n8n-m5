// Developer console over USB serial (only compiled with -DDEV_CONSOLE=1, i.e. the *-dev environments).
// Gives a developer (or an AI agent) exact insight and control: status, UI widget tree, screenshots, taps, config, API calls.
// Every command's output ends with a line "<<END>>". NOT for release builds: it can read/change the API key.
#ifdef DEV_CONSOLE
#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <lvgl.h>
#include <mbedtls/base64.h>

#include "../core/api.h"
#include "../core/config.h"
#include "../core/plat.h"
#include "../core/util.h"
#include "../ui/nav.h"
#include "../ui/res.h"
#include "fw.h"

namespace {
uint16_t* shadow = nullptr;   // copy of what is on the panel (RGB565), updated by every LVGL flush
int sw = 0, sh = 0;
struct {
    bool active = false;
    int x = 0, y = 0;
    uint32_t until = 0;
} inj;
std::string lineBuf;
bool busy = false;       // an async command is still running (its callback prints <<END>>)
bool probing = false;

void end() { Serial.print("<<END>>\n"); }

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

const char* typeName(lv_obj_t* o) {
    if (lv_obj_check_type(o, &lv_label_class)) return "label";
    if (lv_obj_check_type(o, &lv_button_class)) return "button";
    if (lv_obj_check_type(o, &lv_switch_class)) return "switch";
    if (lv_obj_check_type(o, &lv_textarea_class)) return "textarea";
    if (lv_obj_check_type(o, &lv_keyboard_class)) return "keyboard";
    if (lv_obj_check_type(o, &lv_slider_class)) return "slider";
    if (lv_obj_check_type(o, &lv_spinner_class)) return "spinner";
    if (lv_obj_check_type(o, &lv_line_class)) return "line";
    return "obj";
}

std::string textOf(lv_obj_t* o) {
    if (lv_obj_check_type(o, &lv_label_class)) return lv_label_get_text(o);
    if (lv_obj_check_type(o, &lv_textarea_class)) return std::string("[") + lv_textarea_get_text(o) + "]";
    if (lv_obj_check_type(o, &lv_switch_class)) return lv_obj_has_state(o, LV_STATE_CHECKED) ? "ON" : "OFF";
    return "";
}

void dump(lv_obj_t* o, int depth, bool all) {
    bool hidden = lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (hidden && !all) return;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    std::string t = textOf(o);
    bool clickable = lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE);
    // skip pure layout containers unless they matter (clickable) to keep the output readable
    const char* tn = typeName(o);
    if (all || t.size() || clickable || strcmp(tn, "obj") != 0 || depth <= 1) {
        Serial.printf("%*s%s (%d,%d)-(%d,%d)%s%s%s\n", depth * 2, "", tn, a.x1, a.y1, a.x2, a.y2, clickable ? " CLICK" : "", t.empty() ? "" : " \"", t.c_str());
        if (!t.empty()) Serial.print("\"\n");
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) dump(lv_obj_get_child(o, i), depth + 1, all);
}

// Best label for a click: exact text match inside a clickable widget > exact match > contains inside clickable > contains.
static void collectLabels(lv_obj_t* o, const std::string& needle, std::vector<lv_obj_t*>& out) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(o, &lv_label_class) && lower(lv_label_get_text(o)).find(needle) != std::string::npos) out.push_back(o);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) collectLabels(lv_obj_get_child(o, i), needle, out);
}
// nearest clickable ancestor that is a real widget (not a screen-sized backdrop/container), else null
static lv_obj_t* clickTarget(lv_obj_t* l) {
    for (lv_obj_t* c = lv_obj_get_parent(l); c; c = lv_obj_get_parent(c)) {
        if (!lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE) || !lv_obj_get_parent(c)) continue;
        lv_area_t a;
        lv_obj_get_coords(c, &a);
        if ((long)(a.x2 - a.x1 + 1) * (a.y2 - a.y1 + 1) * 2 > (long)sw * sh) continue;   // bigger than half the screen: not a button
        return c;
    }
    return nullptr;
}
lv_obj_t* findLabel(lv_obj_t* root, const std::string& needle) {
    std::vector<lv_obj_t*> c;
    collectLabels(root, needle, c);
    lv_obj_t* best = nullptr;
    int bestScore = -1;
    for (auto* l : c) {
        std::string t = lower(lv_label_get_text(l));
        while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
        int score = (t == needle ? 2 : 0) + (clickTarget(l) ? 1 : 0);
        if (score > bestScore) {
            best = l;
            bestScore = score;
        }
    }
    return best;
}

void tap(int x, int y, uint32_t holdMs = 140) {
    inj.x = x;
    inj.y = y;
    inj.until = millis() + holdMs;
    inj.active = true;
    plat::sleepDisplay(false);
}

bool clickText(const std::string& needle) {
    lv_obj_t* l = findLabel(lv_layer_top(), lower(needle));   // overlays (menus, dialogs, prompts) win over the page
    if (!l) l = findLabel(lv_screen_active(), lower(needle));
    if (!l) return false;
    lv_obj_t* c = clickTarget(l);
    if (!c) c = l;   // not inside a button: tap the label itself
    lv_obj_scroll_to_view_recursive(c, LV_ANIM_OFF);   // rows below the fold: bring them on screen first
    lv_obj_update_layout(c);
    lv_area_t a;
    lv_obj_get_coords(c, &a);
    int x = (a.x1 + a.x2) / 2, y = (a.y1 + a.y2) / 2;
    if (y < 0 || y >= sh || x < 0 || x >= sw) {   // never tap something that is not visible (it would hit another widget)
        Serial.printf("target at %d,%d is off-screen\n", x, y);
        return false;
    }
    Serial.printf("click \"%s\" -> %s at %d,%d\n", needle.c_str(), typeName(c), x, y);
    tap(x, y);
    return true;
}

lv_obj_t* findTextarea(lv_obj_t* o) {
    if (lv_obj_check_type(o, &lv_textarea_class)) return o;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        if (lv_obj_t* r = findTextarea(lv_obj_get_child(o, i))) return r;
    return nullptr;
}

std::string wifiStateName() {
    switch (plat::wifiState()) {
        case plat::WifiState::Off: return "off";
        case plat::WifiState::Connecting: return "connecting";
        case plat::WifiState::Connected: return "connected";
        default: return "failed";
    }
}

void status() {
    JsonDocument d;
    d["fw"] = "n8n-remote dev";
    d["device"] = plat::info().name;
    d["uptime_s"] = millis() / 1000;
    d["reset"] = plat::resetReason();
    d["crashes"] = plat::crashCount();
    d["heap_free"] = plat::freeHeap();
    d["heap_largest"] = plat::largestFreeBlock();
    d["psram_free"] = plat::freePsram();
    JsonObject w = d["wifi"].to<JsonObject>();
    w["state"] = wifiStateName();
    w["ssid"] = plat::wifiSsid();
    w["ip"] = plat::wifiIp();
    w["rssi"] = plat::wifiRssi();
    d["time"] = (long)plat::now();
    auto& c = cfg::s();
    JsonObject cf = d["cfg"].to<JsonObject>();
    cf["url"] = c.url;
    cf["key_len"] = c.apiKey.size();
    cf["tls"] = c.tls;
    cf["theme"] = c.theme;
    cf["brightness"] = c.brightness;
    cf["page"] = c.pageSize;
    cf["timeout"] = c.timeoutSec;
    cf["maxKb"] = c.maxKb;
    JsonObject u = d["ui"].to<JsonObject>();
    u["section"] = ui::nav::currentSection();
    u["depth"] = ui::nav::depth();
    u["title"] = ui::nav::top() ? ui::nav::top()->title : "";
    u["overlay"] = ui::overlayOpen();
    JsonObject a = d["api"].to<JsonObject>();
    a["pending"] = api::pending();
    a["last_status"] = api::lastStatus;
    JsonArray h = a["history"].to<JsonArray>();
    auto& hist = api::history();
    for (size_t i = hist.size() > 6 ? hist.size() - 6 : 0; i < hist.size(); i++) {
        JsonObject e = h.add<JsonObject>();
        e["req"] = hist[i].method + " " + hist[i].path;
        e["status"] = hist[i].status;
        e["ms"] = hist[i].ms;
        if (!hist[i].error.empty()) e["error"] = hist[i].error;
    }
    serializeJsonPretty(d, Serial);
    Serial.println();
}

void shot(int scale) {
    if (!shadow) {
        Serial.println("no framebuffer");
        return;
    }
    if (scale < 1) scale = 1;
    int w = sw / scale, h = sh / scale;
    Serial.printf("SHOT %d %d rgb565le\n", w, h);
    std::vector<uint8_t> row(w * 2);
    unsigned char out[w * 2 / 3 * 4 + 16];
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t p = shadow[(y * scale) * sw + x * scale];
            row[x * 2] = p & 0xFF;
            row[x * 2 + 1] = p >> 8;
        }
        size_t olen = 0;
        mbedtls_base64_encode(out, sizeof out, &olen, row.data(), row.size());
        Serial.write(out, olen);
        Serial.print("\n");
    }
    Serial.print("SHOT_END\n");
}

void setCfg(const std::string& k, const std::string& v) {
    auto& c = cfg::s();
    if (k == "url") c.url = util::normalizeUrl(v);
    else if (k == "key") c.apiKey = util::trim(v);
    else if (k == "tls") c.tls = atoi(v.c_str());
    else if (k == "theme") c.theme = atoi(v.c_str());
    else if (k == "brightness") { c.brightness = atoi(v.c_str()); plat::setBrightness(c.brightness); }
    else if (k == "page") c.pageSize = atoi(v.c_str());
    else if (k == "timeout") c.timeoutSec = atoi(v.c_str());
    else if (k == "maxkb") c.maxKb = atoi(v.c_str());
    else if (k == "sleep") c.sleepSec = atoi(v.c_str());
    else { Serial.printf("unknown key '%s' (url key tls theme brightness page timeout maxkb sleep)\n", k.c_str()); return; }
    cfg::save();
    Serial.printf("ok %s saved\n", k.c_str());
}

void handle(const std::string& line) {
    std::string cmd = line.substr(0, line.find(' '));
    std::string rest = line.size() > cmd.size() ? util::trim(line.substr(cmd.size())) : "";
    if (cmd.empty()) { end(); return; }
    if (cmd == "help") {
        Serial.print(
            "status | heap | tree [all] | shot [scale] | tap X Y | click TEXT | type TEXT | scroll DY | nav N | back | push NAME\n"
            "set K V | get cfg | wifi SSID PASS | wifi | req METHOD /path?q [json] | reqraw METHOD /path | probe | log TEXT | reboot | factory | safe off\n"
            "nav: 0 overview 1 workflows 2 executions 3 manage 4 webhooks 5 explorer 6 settings; push: diag credentials tags variables users projects datatables roles packages admin\n");
    } else if (cmd == "status") status();
    else if (cmd == "heap") Serial.printf("heap %u largest %u psram %u\n", (unsigned)plat::freeHeap(), (unsigned)plat::largestFreeBlock(), (unsigned)plat::freePsram());
    else if (cmd == "tree") { dump(lv_screen_active(), 0, rest == "all"); Serial.println("--- overlay layer"); dump(lv_layer_top(), 0, rest == "all"); }
    else if (cmd == "shot") shot(rest.empty() ? (plat::info().compact ? 1 : 2) : atoi(rest.c_str()));
    else if (cmd == "tap") { int x = 0, y = 0; if (sscanf(rest.c_str(), "%d %d", &x, &y) == 2) { tap(x, y); Serial.printf("tap %d,%d\n", x, y); } else Serial.println("usage: tap X Y"); }
    else if (cmd == "click") { if (!clickText(rest)) Serial.printf("no visible label containing \"%s\"\n", rest.c_str()); }
    else if (cmd == "scroll") {
        ui::Page* p = ui::nav::top();
        if (p && p->body)
            for (uint32_t k = 0; k < lv_obj_get_child_count(p->body); k++) {
                lv_obj_t* c = lv_obj_get_child(p->body, k);
                if (lv_obj_has_flag(c, LV_OBJ_FLAG_SCROLLABLE)) { lv_obj_scroll_by(c, 0, -atoi(rest.c_str()), LV_ANIM_OFF); Serial.println("scrolled"); break; }
            }
    }
    else if (cmd == "type") {  // put text into the textarea of the open prompt (like typing it)
        lv_obj_t* ta = findTextarea(lv_layer_top());
        if (!ta) Serial.println("no textarea open");
        else { lv_textarea_set_text(ta, rest.c_str()); Serial.printf("typed %u chars\n", (unsigned)rest.size()); }
    }
    else if (cmd == "nav") { ui::nav::section(atoi(rest.c_str())); Serial.println("ok"); }
    else if (cmd == "back") { ui::nav::pop(); Serial.println("ok"); }
    else if (cmd == "push") { if (rest == "diag") ui::nav::push(ui::makeDiagnostics()); else { auto pg = ui::makeResource(rest); if (pg) ui::nav::push(std::move(pg)); } Serial.println("ok"); }
    else if (cmd == "set") { size_t sp = rest.find(' '); setCfg(rest.substr(0, sp), sp == std::string::npos ? "" : rest.substr(sp + 1)); }
    else if (cmd == "get") { Serial.printf("url=%s key_len=%u tls=%d theme=%d\n", cfg::s().url.c_str(), (unsigned)cfg::s().apiKey.size(), cfg::s().tls, cfg::s().theme); }
    else if (cmd == "wifi") {
        if (rest.empty()) Serial.printf("wifi %s ssid=%s ip=%s rssi=%d\n", wifiStateName().c_str(), plat::wifiSsid().c_str(), plat::wifiIp().c_str(), plat::wifiRssi());
        else {
            size_t sp = rest.find(' ');
            cfg::s().ssid = rest.substr(0, sp);
            cfg::s().wifiPass = sp == std::string::npos ? "" : rest.substr(sp + 1);
            cfg::save();
            plat::wifiConnect(cfg::s().ssid, cfg::s().wifiPass);
            Serial.println("connecting");
        }
    }
    else if (cmd == "scan") {   // blocking scan: proves the radio (Tab5: the C6 behind SDIO) works without credentials
        Serial.printf("mode=%d status=%d\n", (int)WiFi.getMode(), (int)WiFi.status());
        WiFi.mode(WIFI_STA);
        int n = WiFi.scanNetworks();
        Serial.printf("scan %d mac=%s\n", n, WiFi.macAddress().c_str());
        for (int i = 0; i < n && i < 12; i++) Serial.printf("  %s %ddBm ch%d\n", WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i), (int)WiFi.channel(i));
        WiFi.scanDelete();
    }
    else if (cmd == "req" || cmd == "reqraw") {   // reqraw: path is relative to the server root, no API key (like a webhook)
        size_t s1 = rest.find(' ');
        std::string method = rest.substr(0, s1), r2 = s1 == std::string::npos ? "" : util::trim(rest.substr(s1));
        size_t s2 = r2.find(' ');
        std::string pq = r2.substr(0, s2), body = s2 == std::string::npos ? "" : util::trim(r2.substr(s2));
        if (method.empty() || pq.empty()) { Serial.println("usage: req METHOD /path?query [json]"); end(); return; }
        api::Request rq;
        rq.method = method;
        rq.raw = cmd == "reqraw";
        size_t q = pq.find('?');
        rq.path = pq.substr(0, q);
        if (q != std::string::npos) rq.query = pq.substr(q + 1);
        rq.body = body;
        busy = true;
        api::send(rq, [](api::Response& r) {
            Serial.printf("status=%d ms=%u bytes=%u truncated=%d\n", r.status, (unsigned)r.ms, (unsigned)r.bytes, (int)r.truncated);
            if (!r.error.empty()) Serial.printf("error=%s\n", r.error.c_str());
            std::string b = r.body;
            if (b.empty() && r.doc) serializeJson(*r.doc, b);
            Serial.printf("body=%s\n", util::trunc(b, 3000).c_str());
            busy = false;
            end();
        });
        return;  // END printed by the callback
    }
    else if (cmd == "probe") {
        plat::probeStart(cfg::s().url, cfg::s().apiKey, cfg::s().tls, cfg::s().caPem);
        busy = probing = true;
        return;
    }
    else if (cmd == "log") plat::log("[dev] " + rest);
    else if (cmd == "reboot") { Serial.println("rebooting"); end(); delay(100); ESP.restart(); }
    else if (cmd == "factory") { plat::eraseAll(); Serial.println("erased"); end(); delay(100); ESP.restart(); }
    else if (cmd == "safe") { plat::clearCrashes(); Serial.println("crash counter cleared"); }
    else Serial.printf("unknown command '%s' (try help)\n", cmd.c_str());
    end();
}
}  // namespace

namespace fw {
void devInit(int w, int h) {
    sw = w;
    sh = h;
    shadow = (uint16_t*)heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    Serial.println("[dev] console ready (type help). Output ends with <<END>>");
}
void devFrame(int x, int y, int w, int h, const uint16_t* px) {
    if (!shadow) return;
    for (int r = 0; r < h; r++) memcpy(&shadow[(y + r) * sw + x], px + r * w, w * 2);
}
bool devTouch(int& x, int& y) {
    if (!inj.active) return false;
    if ((int32_t)(millis() - inj.until) > 0) { inj.active = false; return false; }
    x = inj.x;
    y = inj.y;
    return true;
}
void devTick() {
    if (probing) {
        std::string rep;
        if (plat::probeDone(rep)) {
            Serial.print(rep.c_str());
            probing = busy = false;
            end();
        }
        return;
    }
    while (Serial.available() && !busy) {
        int c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            std::string l = lineBuf;
            lineBuf.clear();
            handle(util::trim(l));
            continue;
        }
        if (lineBuf.size() < 4096) lineBuf += (char)c;
    }
}
}  // namespace fw
#endif
