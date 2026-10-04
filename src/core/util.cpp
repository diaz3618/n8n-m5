#include "util.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "plat.h"

namespace util {

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            o += (char)c;
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}

std::string encodePath(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || strchr("-._~/:@!$&'()*+,;=%?#", c)) o += (char)c;
        else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}

std::string str(JsonVariantConst v, const char* def) {
    if (v.isNull()) return def;
    if (v.is<const char*>()) return v.as<const char*>();
    if (v.is<bool>()) return v.as<bool>() ? "true" : "false";
    if (v.is<long long>()) return std::to_string(v.as<long long>());
    if (v.is<double>()) {
        char b[32];
        snprintf(b, sizeof b, "%g", v.as<double>());
        return b;
    }
    std::string o;
    serializeJson(v, o);
    return o;
}

std::string subst(const std::string& tpl, JsonVariantConst item, bool encode) {
    std::string o;
    for (size_t i = 0; i < tpl.size(); i++) {
        if (tpl[i] == '{') {
            size_t e = tpl.find('}', i);
            bool ident = e != std::string::npos && e > i + 1;
            for (size_t j = i + 1; ident && j < e; j++) ident = isalnum((unsigned char)tpl[j]) || tpl[j] == '_';
            if (ident) {  // only {identifier} is a placeholder; literal JSON like {"a":1} is left alone
                std::string k = tpl.substr(i + 1, e - i - 1);
                std::string v = str(item[k.c_str()]);
                o += encode ? urlEncode(v) : v;
                i = e;
                continue;
            }
        }
        o += tpl[i];
    }
    return o;
}

time_t parseIso(const char* iso) {
    if (!iso || !*iso) return 0;
    int Y, M, D, h = 0, m = 0;
    float s = 0;
    if (sscanf(iso, "%d-%d-%dT%d:%d:%f", &Y, &M, &D, &h, &m, &s) < 3) return 0;
    // days from civil (UTC)
    Y -= M <= 2;
    long era = (Y >= 0 ? Y : Y - 399) / 400;
    unsigned yoe = (unsigned)(Y - era * 400);
    unsigned doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097 + (long)doe - 719468;
    return (time_t)(days * 86400 + h * 3600 + m * 60 + (int)s);
}

std::string ago(time_t t) {
    time_t n = plat::now();
    if (!t) return "-";
    if (!n || n < 1600000000) return fmtDateTime(t);
    long d = (long)(n - t);
    char b[24];
    if (d < 0) d = 0;
    if (d < 60) snprintf(b, sizeof b, "%lds ago", d);
    else if (d < 3600) snprintf(b, sizeof b, "%ldm ago", d / 60);
    else if (d < 86400) snprintf(b, sizeof b, "%ldh ago", d / 3600);
    else snprintf(b, sizeof b, "%ldd ago", d / 86400);
    return b;
}

std::string isoToEpochAgo(const char* iso) { return ago(parseIso(iso)); }

std::string duration(time_t a, time_t b) {
    if (!a || !b || b < a) return "-";
    long d = (long)(b - a);
    char s[24];
    if (d < 60) snprintf(s, sizeof s, "%lds", d);
    else if (d < 3600) snprintf(s, sizeof s, "%ldm %lds", d / 60, d % 60);
    else snprintf(s, sizeof s, "%ldh %ldm", d / 3600, (d % 3600) / 60);
    return s;
}

std::string fmtDateTime(time_t t) {
    if (!t) return "-";
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char b[32];
    strftime(b, sizeof b, "%Y-%m-%d %H:%M", &tmv);
    return std::string(b) + " UTC";
}

std::string trunc(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    size_t cut = n;
    while (cut > 0 && (s[cut] & 0xC0) == 0x80) cut--;  // don't split UTF-8
    return s.substr(0, cut) + "...";
}

std::string mask(const std::string& s) {
    if (s.empty()) return "(not set)";
    if (s.size() <= 10) return std::string(s.size(), '*');
    return s.substr(0, 4) + "..." + s.substr(s.size() - 4);
}

std::string pretty(const std::string& raw, size_t maxBytes) {
    if (raw.size() > 256 * 1024) return trunc(raw, maxBytes);  // too big to re-parse on device
    JsonDocument d;
    if (deserializeJson(d, raw, DeserializationOption::NestingLimit(64)) == DeserializationError::Ok) {
        std::string o;
        serializeJsonPretty(d, o);
        return trunc(o, maxBytes);
    }
    return trunc(raw, maxBytes);
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string normalizeUrl(std::string u) {
    u = trim(u);
    if (u.empty()) return u;
    if (!startsWith(u, "http://") && !startsWith(u, "https://")) {
        // no scheme typed: LAN-style hosts (IP, host:port, .local, single label) are almost always plain HTTP
        std::string h = u.substr(0, u.find('/'));
        bool lan = h.find(':') != std::string::npos || h.find('.') == std::string::npos || (h.size() > 6 && h.compare(h.size() - 6, 6, ".local") == 0);
        bool ip = !h.empty();
        for (char c : h.substr(0, h.find(':'))) if (!isdigit((unsigned char)c) && c != '.') ip = false;
        u = ((lan || ip) ? "http://" : "https://") + u;
    }
    while (!u.empty() && u.back() == '/') u.pop_back();
    const char* suf = "/api/v1";
    size_t n = strlen(suf);
    if (u.size() >= n && u.compare(u.size() - n, n, suf) == 0) u.erase(u.size() - n);
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

}  // namespace util
