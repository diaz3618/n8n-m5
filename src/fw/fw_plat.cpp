// Device implementation of plat:: (NVS store, WiFi, SD, time, worker threads, setup portal).
#include <Arduino.h>
#include <memory>
#include <FS.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_system.h>
#include <time.h>

#include "../core/config.h"
#include "../core/plat.h"
#include "../core/util.h"
#include "fw.h"

RTC_NOINIT_ATTR static uint32_t rtcMagic, rtcCrashes;   // survive software/watchdog resets, not power loss

namespace fw {
void crashInit() {
    if (rtcMagic != 0xC0DE5AFE) {
        rtcMagic = 0xC0DE5AFE;
        rtcCrashes = 0;
    }
    esp_reset_reason_t r = esp_reset_reason();
    if (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT) rtcCrashes++;
    else if (r == ESP_RST_POWERON) rtcCrashes = 0;
    Serial.printf("boot: reason=%s crashes=%u\n", plat::resetReason().c_str(), (unsigned)rtcCrashes);
}
}  // namespace fw

namespace {
Preferences prefs;
bool sdOk = false, sdTried = false;
bool wifiConnecting = false, wifiFailed = false;
uint32_t wifiStart = 0;
WebServer* portal = nullptr;
std::string portalPin;
int portalFails = 0;
uint32_t portalLockUntil = 0;

struct PsramAlloc : ArduinoJson::Allocator {
    void* allocate(size_t n) override {
        void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : malloc(n);
    }
    void deallocate(void* p) override { free(p); }
    void* reallocate(void* p, size_t n) override {
        void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return q ? q : realloc(p, n);
    }
} psramAlloc;
}  // namespace

namespace fw {
void platInit() {
    prefs.begin("n8nremote", false);
#if defined(DEVICE_TAB5)
    // Tab5: WiFi is an ESP32-C6 behind SDIO (esp_hosted); the pins must be set before the WiFi stack starts (before mode())
    if (!WiFi.setPins(12, 13, 11, 10, 9, 8, 15)) Serial.println("WiFi.setPins failed");
#endif
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
}
}  // namespace fw

namespace plat {

uint32_t millis() { return ::millis(); }
uint32_t random32() { return esp_random(); }
time_t now() {
    time_t t = time(nullptr);
    return t > 1600000000 ? t : 0;
}
void setTz(int, const char* ntp) { configTime(0, 0, ntp && *ntp ? ntp : "pool.ntp.org"); }  // UTC; UI applies the offset
std::string deviceId() {
    char b[20];
    snprintf(b, sizeof b, "%012llX", (unsigned long long)ESP.getEfuseMac());
    return b;
}
size_t freeHeap() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
std::string resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_SW: return "software restart";
        case ESP_RST_PANIC: return "PANIC (exception/abort)";
        case ESP_RST_INT_WDT: return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "TASK WATCHDOG (a task hung)";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_BROWNOUT: return "BROWNOUT (power dip)";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_EXT: return "reset button";
        default: return "other (" + std::to_string((int)esp_reset_reason()) + ")";
    }
}
int crashCount() { return (int)rtcCrashes; }
bool safeMode() { return rtcCrashes >= 3; }
void clearCrashes() { rtcCrashes = 0; }
size_t largestFreeBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL); }
void log(const std::string& line) { Serial.println(line.c_str()); }
size_t freePsram() { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
void restart() { ESP.restart(); }

std::string getStr(const char* k, const char* d) { return prefs.getString(k, d).c_str(); }
void putStr(const char* k, const std::string& v) {
    if (prefs.getString(k, "\x01") != String(v.c_str())) prefs.putString(k, v.c_str());  // skip identical writes (flash wear)
}
int getInt(const char* k, int d) { return prefs.getInt(k, d); }
void putInt(const char* k, int v) {
    if (prefs.getInt(k, v + 1) != v) prefs.putInt(k, v);
}
void eraseAll() {
    prefs.clear();
    WiFi.disconnect(true, true);
}

static bool mountSd() {
    static uint32_t lastTry = 0;
    if (sdOk) return true;
    if (sdTried && ::millis() - lastTry < 5000) return false;  // retry a missing card every 5 s
    sdTried = true;
    lastTry = ::millis();
    static bool spiBegun = false;  // re-running SPI.begin() on the bus shared with the LCD can upset the display
#if defined(DEVICE_TAB5)
    if (!spiBegun) SPI.begin(43, 39, 44, 42);  // SCK, MISO, MOSI, CS
    spiBegun = true;
    sdOk = SD.begin(42, SPI, 20000000);
#else
    if (!spiBegun) SPI.begin(36, 35, 37, 4);
    spiBegun = true;
    sdOk = SD.begin(4, SPI, 20000000);
#endif
    return sdOk;
}
bool sdAvailable() { return mountSd(); }
bool sdRead(const char* path, std::string& out) {
    if (!mountSd()) return false;
    File f = SD.open(path, FILE_READ);
    if (!f || f.size() > 768 * 1024) return false;
    out.resize(f.size());
    out.resize(f.read((uint8_t*)&out[0], out.size()));
    f.close();
    return true;
}
bool sdWrite(const char* path, const std::string& data) {
    if (!mountSd()) return false;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    size_t n = f.write((const uint8_t*)data.data(), data.size());
    f.close();
    return n == data.size();
}

bool sdMkdir(const char* path) { return mountSd() && (SD.exists(path) || SD.mkdir(path)); }
bool sdRemove(const char* path) { return mountSd() && SD.remove(path); }
bool sdList(const char* dir, std::vector<SdEntry>& out) {
    out.clear();
    if (!mountSd()) return false;
    File d = SD.open(dir);
    if (!d || !d.isDirectory()) return false;
    for (File f = d.openNextFile(); f; f = d.openNextFile())
        if (!f.isDirectory()) out.push_back({f.name(), (time_t)f.getLastWrite()});
    return true;
}

void wifiScanStart() { WiFi.scanNetworks(true, false); }
bool wifiScanDone(std::vector<Ap>& out) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return false;
    out.clear();
    for (int i = 0; i < n; i++) {
        Ap a;
        a.ssid = WiFi.SSID(i).c_str();
        a.rssi = WiFi.RSSI(i);
        a.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        if (a.ssid.empty()) continue;
        bool dup = false;
        for (auto& e : out) if (e.ssid == a.ssid) dup = true;
        if (!dup) out.push_back(a);
    }
    WiFi.scanDelete();
    return true;
}
void wifiConnect(const std::string& ssid, const std::string& pass) {
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), pass.empty() ? nullptr : pass.c_str());
    wifiConnecting = true;
    wifiFailed = false;
    wifiStart = ::millis();
}
void wifiDisconnect() { WiFi.disconnect(); }
WifiState wifiState() {
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnecting = false;
        return WifiState::Connected;
    }
    if (wifiConnecting) {
        if (WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_NO_SSID_AVAIL || ::millis() - wifiStart > 20000) {
            wifiConnecting = false;
            wifiFailed = true;
            return WifiState::Failed;
        }
        return WifiState::Connecting;
    }
    return wifiFailed ? WifiState::Failed : WifiState::Off;
}
std::string wifiSsid() { return WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : ""; }
std::string wifiIp() { return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "-"; }
int wifiRssi() { return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0; }

void spawn(void (*fn)(void*), void* arg, const char* name, int stackBytes) {
    xTaskCreatePinnedToCore(fn, name, stackBytes, arg, 1, nullptr, tskNO_AFFINITY);
}
std::shared_ptr<JsonDocument> newDoc() { return std::make_shared<JsonDocument>(&psramAlloc); }

static String esc(const String& s) {
    String o;
    for (char c : std::string(s.c_str())) {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else if (c == '"') o += "&quot;";
        else if (c == '\'') o += "&#39;";
        else o += c;
    }
    return o;
}
static const char* kPage =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>n8n Remote setup</title>"
    "<style>body{font:16px system-ui;margin:0;background:#f6f7fa;color:#101328}main{max-width:560px;margin:auto;padding:20px}"
    "h1{font-size:22px}h1 b{color:#ff6d5a}label{display:block;margin:14px 0 4px;font-weight:600}"
    "input,textarea,select{width:100%;box-sizing:border-box;padding:10px;border:1px solid #dbdfe7;border-radius:8px;font:inherit}"
    "button{margin-top:18px;background:#ff6d5a;color:#fff;border:0;border-radius:8px;padding:12px 18px;font:inherit;font-weight:600}"
    "small{color:#8b8fa0}</style><main><h1><b>n8n</b> Remote setup</h1>%MSG%"
    "<form method=post action=/save><label>PIN shown on the device</label><input name=pin inputmode=numeric autocomplete=off>"
    "<label>n8n server URL</label><input name=url value='%URL%' placeholder='https://n8n.example.com'>"
    "<label>API key</label><textarea name=key rows=4 placeholder='leave empty to keep the current key'></textarea>"
    "<label>TLS</label><select name=tls><option value=0>Verify certificate</option><option value=2>Insecure (skip verify)</option></select>"
    "<small>Traffic to this page is not encrypted: only use it on a trusted network.</small><br><button>Save</button></form></main>";

static bool pinOk(const String& p) {
    if (p.length() != portalPin.size()) return false;
    uint8_t d = 0;
    for (size_t i = 0; i < portalPin.size(); i++) d |= (uint8_t)(p[i] ^ portalPin[i]);  // constant time
    return d == 0;
}

bool portalStart(const std::string& pin) {
    portalPin = pin;  // (also when already running: a re-open shows a new PIN)
    portalFails = 0;
    portalLockUntil = 0;
    if (portal) return true;
    portal = new WebServer(80);
    portal->on("/", HTTP_GET, [] {
        String p = kPage;
        p.replace("%MSG%", "");
        p.replace("%URL%", esc(cfg::s().url.c_str()));
        portal->send(200, "text/html", p);
    });
    portal->on("/save", HTTP_POST, [] {
        if ((int32_t)(::millis() - portalLockUntil) < 0) {
            portal->send(429, "text/plain", "Too many attempts, wait a minute");
            return;
        }
        if (!pinOk(portal->arg("pin"))) {
            if (++portalFails >= 5) {  // back off instead of a permanent lockout (LAN hosts could otherwise lock the owner out)
                portalFails = 0;
                portalLockUntil = ::millis() + 60000;
            }
            portal->send(403, "text/plain", "Wrong PIN");
            return;
        }
        String url = portal->arg("url"), key = portal->arg("key");
        key.trim();
        for (size_t i = 0; i < key.length(); i++)
            if ((uint8_t)key[i] < 0x20 || (uint8_t)key[i] == 0x7f) {
                portal->send(400, "text/plain", "API key contains control characters");
                return;
            }
        if (url.length()) cfg::s().url = util::normalizeUrl(url.c_str());
        if (key.length()) cfg::s().apiKey = key.c_str();
        int tls = portal->arg("tls").toInt();
        cfg::s().tls = tls == 2 ? 2 : (cfg::s().tls == 1 ? 1 : 0);
        cfg::save();
        String p = kPage;
        p.replace("%MSG%", "<p><b>Saved.</b> You can close this page and press Done on the device.</p>");
        p.replace("%URL%", esc(cfg::s().url.c_str()));
        portal->send(200, "text/html", p);
    });
    portal->begin();
    return true;
}
void portalStop() {
    if (!portal) return;
    portal->stop();
    delete portal;
    portal = nullptr;
    portalPin.clear();
}
bool portalRunning() { return portal != nullptr; }

}  // namespace plat

namespace fw {
void portalTick() {
    if (portal) portal->handleClient();
}
}  // namespace fw

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mutex>

namespace {
struct Probe {
    std::string url, key, ca, report;
    int tls = 0;
    bool done = true;
};
Probe probe;
std::mutex probeMu;

void pline(const std::string& s) {
    std::lock_guard<std::mutex> lk(probeMu);
    probe.report += s + "\n";
}

void probeRun() {
    std::string url = probe.url;
    bool https = util::startsWith(url, "https://");
    std::string rest = url.substr(url.find("://") + 3);
    std::string host = rest.substr(0, rest.find('/'));
    int port = https ? 443 : 80;
    size_t c = host.find(':');
    if (c != std::string::npos) {
        port = atoi(host.c_str() + c + 1);
        host = host.substr(0, c);
    }
    pline("URL    " + url + "  (host " + host + ", port " + std::to_string(port) + ")");
    pline("WiFi   " + std::string(WiFi.status() == WL_CONNECTED ? "connected, rssi " + std::to_string(WiFi.RSSI()) : "NOT connected"));
    pline(std::string("Clock  ") + (plat::now() ? "synced" : "NOT synced (TLS verify will fail)"));
    IPAddress ip;
    if (!WiFi.hostByName(host.c_str(), ip)) {
        pline("DNS    FAILED for " + host);
        goto finish;
    }
    pline("DNS    ok " + std::string(ip.toString().c_str()));
    {
        NetworkClient t;
        t.setTimeout(6000);
        if (!t.connect(ip, port, 6000)) {
            pline("TCP    FAILED to " + std::string(ip.toString().c_str()) + ":" + std::to_string(port) + " (firewall/port/wrong scheme?)");
            goto finish;
        }
        pline("TCP    ok");
        t.stop();
    }
    if (https) {
        char err[100];
        {
            NetworkClientSecure s;
            s.setInsecure();
            s.setTimeout(8);
            bool ok = s.connect(host.c_str(), port);
            err[0] = 0;
            if (!ok) s.lastError(err, sizeof err);
            pline(std::string("TLS    handshake (no verify): ") + (ok ? "ok" : std::string("FAILED ") + err + " (server may be plain HTTP: try http://)"));
        }
        {
            NetworkClientSecure s;
            if (probe.tls == 1 && !probe.ca.empty()) s.setCACert(probe.ca.c_str());
            else fw::useBundle(s, 0);
            s.setTimeout(8);
            bool ok = s.connect(host.c_str(), port);
            err[0] = 0;
            if (!ok) s.lastError(err, sizeof err);
            pline(std::string("TLS    certificate check: ") + (ok ? "ok" : std::string("FAILED ") + err));
        }
    }
    {
        auto get = [&](const std::string& path, bool auth) {
            std::unique_ptr<NetworkClient> cl;
            if (https) {
                auto* s = new NetworkClientSecure();
                if (probe.tls == 2) s->setInsecure();
                else if (probe.tls == 1 && !probe.ca.empty()) s->setCACert(probe.ca.c_str());
                else fw::useBundle(*s, 0);
                s->setTimeout(8);
                cl.reset(s);
            } else {
                cl.reset(new NetworkClient());
                cl->setTimeout(8000);
            }
            HTTPClient h;
            h.setTimeout(8000);
            h.setConnectTimeout(8000);
            if (!h.begin(*cl, (url + path).c_str())) return std::string("bad url");
            if (auth) h.addHeader("X-N8N-API-KEY", probe.key.c_str());
            int code = h.GET();
            std::string r = code > 0 ? "HTTP " + std::to_string(code) : "FAILED " + std::string(HTTPClient::errorToString(code).c_str());
            if (code > 0 && code != 200) {
                String b = h.getString();
                if (b.length() > 120) b = b.substring(0, 120);
                r += "  " + std::string(b.c_str());
            }
            h.end();
            return r;
        };
        pline("HEALTH " + get("/healthz", false));
        pline("API    " + get("/api/v1/workflows?limit=1", true) + "   (key " + std::to_string(probe.key.size()) + " chars)");
    }
finish:
    pline("heap   " + std::to_string(plat::freeHeap() / 1024) + "K free, largest block " + std::to_string(plat::largestFreeBlock() / 1024) + "K");
}

void probeTask(void*) {
    probeRun();  // all locals/locks are released when this returns
    {
        std::lock_guard<std::mutex> lk(probeMu);
        probe.done = true;
    }
    vTaskDelete(nullptr);  // never returns: nothing with a destructor may be alive here
}
}  // namespace

namespace plat {
void probeStart(const std::string& baseUrl, const std::string& apiKey, int tlsMode, const std::string& caPem) {
    {
        std::lock_guard<std::mutex> lk(probeMu);
        if (!probe.done) return;
        probe.url = baseUrl;
        probe.key = apiKey;
        probe.tls = tlsMode;
        probe.ca = caPem;
        probe.report.clear();
        probe.done = false;
    }
    if (baseUrl.find("://") == std::string::npos) {
        pline("URL    missing http:// or https://");
        std::lock_guard<std::mutex> lk(probeMu);
        probe.done = true;
        return;
    }
    spawn(probeTask, nullptr, "n8n-probe", 24576);
}
bool probeDone(std::string& report) {
    std::lock_guard<std::mutex> lk(probeMu);
    report = probe.report;
    return probe.done;
}
}  // namespace plat
