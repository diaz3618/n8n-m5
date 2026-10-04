#include "config.h"

#include <ArduinoJson.h>

#include "plat.h"
#include "util.h"

namespace cfg {

static Settings g;
Settings& s() { return g; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static std::string hooksToJson(const std::vector<Hook>& h) {
    JsonDocument d;
    JsonArray a = d.to<JsonArray>();
    for (auto& x : h) {
        JsonObject o = a.add<JsonObject>();
        o["name"] = x.name;
        o["path"] = x.path;
        o["method"] = x.method;
        o["body"] = x.body;
    }
    std::string o;
    serializeJson(d, o);
    return o;
}

static void hooksFromJson(JsonVariantConst v, std::vector<Hook>& out) {
    out.clear();
    for (JsonObjectConst o : v.as<JsonArrayConst>()) {
        if (out.size() >= 12) break;
        Hook h;
        h.name = o["name"] | "";
        h.path = o["path"] | "";
        h.method = o["method"] | "POST";
        h.body = o["body"] | "";
        if (!h.path.empty()) out.push_back(h);
    }
}

void load() {
    g.url = plat::getStr("url");
    g.apiKey = plat::getStr("key");
    g.ssid = plat::getStr("ssid");
    g.wifiPass = plat::getStr("wpass");
    g.tls = plat::getInt("tls", 0);
    g.caPem = plat::getStr("ca");
    g.theme = plat::getInt("theme", 2);
    g.brightness = clampi(plat::getInt("bright", 80), 5, 100);
    g.pageSize = clampi(plat::getInt("page", plat::info().compact ? 12 : 25), 5, 100);
    g.timeoutSec = clampi(plat::getInt("tmo", 15), 3, 120);
    g.refreshSec = plat::getInt("refresh", 0);
    g.sleepSec = plat::getInt("sleep", 120);
    g.tzMinutes = clampi(plat::getInt("tz", 0), -720, 840);
    g.ntp = plat::getStr("ntp", "pool.ntp.org");
    g.maxKb = clampi(plat::getInt("maxkb", plat::info().compact ? 512 : 2048), 64, plat::info().compact ? 2048 : 8192);
    g.confirmDestructive = plat::getInt("confirm", 1);
    g.flip = plat::getInt("flip", 0) ? 1 : 0;
    g.sdCache = plat::getInt("sdc", 1) ? 1 : 0;
    g.setupDone = plat::getInt("setup", 0) != 0;
    JsonDocument d;
    if (deserializeJson(d, plat::getStr("hooks", "[]")) == DeserializationError::Ok) hooksFromJson(d.as<JsonVariantConst>(), g.hooks);
}

void save() {
    plat::putStr("url", g.url);
    plat::putStr("key", g.apiKey);
    plat::putStr("ssid", g.ssid);
    plat::putStr("wpass", g.wifiPass);
    plat::putInt("tls", g.tls);
    plat::putStr("ca", g.caPem);
    plat::putInt("theme", g.theme);
    plat::putInt("bright", g.brightness);
    plat::putInt("page", g.pageSize);
    plat::putInt("tmo", g.timeoutSec);
    plat::putInt("refresh", g.refreshSec);
    plat::putInt("sleep", g.sleepSec);
    plat::putInt("tz", g.tzMinutes);
    plat::putStr("ntp", g.ntp);
    plat::putInt("maxkb", g.maxKb);
    plat::putInt("confirm", g.confirmDestructive);
    plat::putInt("flip", g.flip);
    plat::putInt("sdc", g.sdCache);
    plat::putInt("setup", g.setupDone ? 1 : 0);
    plat::putStr("hooks", hooksToJson(g.hooks));
}

bool configured() { return !g.url.empty() && !g.apiKey.empty(); }

std::string exportJson(bool secrets) {
    JsonDocument d;
    d["url"] = g.url;
    if (secrets) {
        d["apiKey"] = g.apiKey;
        d["ssid"] = g.ssid;
        d["wifiPassword"] = g.wifiPass;
        d["caPem"] = g.caPem;
    }
    d["tls"] = g.tls;
    d["theme"] = g.theme;
    d["brightness"] = g.brightness;
    d["pageSize"] = g.pageSize;
    d["timeoutSec"] = g.timeoutSec;
    d["refreshSec"] = g.refreshSec;
    d["sleepSec"] = g.sleepSec;
    d["tzMinutes"] = g.tzMinutes;
    d["ntp"] = g.ntp;
    d["maxKb"] = g.maxKb;
    d["confirmDestructive"] = g.confirmDestructive;
    d["flip"] = g.flip;
    d["sdCache"] = g.sdCache;
    JsonDocument h;
    deserializeJson(h, hooksToJson(g.hooks));
    d["webhooks"] = h;
    std::string o;
    serializeJsonPretty(d, o);
    return o;
}

bool importJson(const std::string& js, std::string& err) {
    JsonDocument d;
    DeserializationError e = deserializeJson(d, js);
    if (e) {
        err = e.c_str();
        return false;
    }
    if (!d.is<JsonObject>()) {
        err = "not a JSON object";
        return false;
    }
    auto S = [&](const char* k, std::string& dst) {
        if (d[k].is<const char*>()) dst = d[k].as<const char*>();
    };
    auto I = [&](const char* k, int& dst, int lo, int hi) {
        if (d[k].is<int>()) dst = clampi(d[k].as<int>(), lo, hi);
    };
    if (d["url"].is<const char*>()) g.url = util::normalizeUrl(d["url"].as<const char*>());
    S("apiKey", g.apiKey);
    S("ssid", g.ssid);
    S("wifiPassword", g.wifiPass);
    S("caPem", g.caPem);
    S("ntp", g.ntp);
    I("tls", g.tls, 0, 2);
    I("theme", g.theme, 0, 2);
    I("brightness", g.brightness, 5, 100);
    I("pageSize", g.pageSize, 5, 100);
    I("timeoutSec", g.timeoutSec, 3, 120);
    I("refreshSec", g.refreshSec, 0, 3600);
    I("sleepSec", g.sleepSec, 0, 36000);
    I("tzMinutes", g.tzMinutes, -720, 840);
    I("maxKb", g.maxKb, 64, plat::info().compact ? 2048 : 8192);
    I("confirmDestructive", g.confirmDestructive, 0, 1);
    I("flip", g.flip, 0, 1);
    I("sdCache", g.sdCache, 0, 1);
    if (d["webhooks"].is<JsonArray>()) hooksFromJson(d["webhooks"], g.hooks);
    return true;
}

bool isDark() {
    if (g.theme == 0) return false;
    if (g.theme == 1) return true;
    time_t n = plat::now();
    if (n < 1600000000) return true;
    time_t local = n + g.tzMinutes * 60;
    struct tm t;
    gmtime_r(&local, &t);
    return t.tm_hour >= 19 || t.tm_hour < 7;
}

}  // namespace cfg
