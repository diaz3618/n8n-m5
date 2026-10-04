#pragma once
#include <ArduinoJson.h>
#include <string>
#include <vector>

namespace util {
std::string urlEncode(const std::string& s);
std::string encodePath(const std::string& s);   // keeps / : ? & = % etc., encodes spaces/control/unsafe chars
// replaces {field} with urlEncode(item[field]) ; missing field -> empty
std::string subst(const std::string& tpl, JsonVariantConst item, bool encode = true);
std::string ago(time_t t);               // "5m ago"
std::string isoToEpochAgo(const char* iso);  // "5m ago" from ISO-8601 string
time_t parseIso(const char* iso);        // 0 on failure
std::string duration(time_t a, time_t b);
std::string fmtDateTime(time_t t);
std::string trunc(const std::string& s, size_t n);
std::string mask(const std::string& s);  // "abcd••••wxyz"
std::string pretty(const std::string& raw, size_t maxBytes);  // pretty JSON or raw
std::string str(JsonVariantConst v, const char* def = "");  // number/bool/string to text
bool startsWith(const std::string& s, const char* p);
std::string trim(const std::string& s);
std::string normalizeUrl(std::string u);  // adds scheme, strips trailing / and /api/v1
}  // namespace util
