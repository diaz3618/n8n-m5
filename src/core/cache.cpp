#include "cache.h"

#include <algorithm>
#include <map>

#include "api.h"
#include "config.h"
#include "plat.h"

namespace cache {

static std::map<std::string, Entry> ram;
static size_t ramBytes = 0;
static int writes = 0;
static const char* kDir = "/n8n-cache";

static uint64_t fnv(const std::string& s, uint64_t h = 1469598103934665603ULL) {
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string keyFor(const api::Request& r) {
    if (r.method != "GET" || r.raw || r.filter.empty() || r.baseUrl.empty()) return "";
    // the key fingerprint keeps users with different permissions apart without ever storing the key
    uint64_t h = fnv(r.baseUrl);
    h = fnv(r.apiKey, h);
    h = fnv(r.path + "?" + r.query, h);
    h = fnv(r.filter, h);
    char b[20];
    snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
    return b;
}

static std::string path(const std::string& key) { return std::string(kDir) + "/" + key + ".c"; }

static void evictRam() {
    size_t budget = plat::info().compact ? 384 * 1024 : 2048 * 1024;
    while (ramBytes > budget && !ram.empty()) {
        auto victim = ram.begin();
        for (auto it = ram.begin(); it != ram.end(); ++it)
            if (it->second.used < victim->second.used) victim = it;
        ramBytes -= victim->second.json.size() + victim->second.etag.size();
        ram.erase(victim);
    }
}

bool lookup(const std::string& key, Entry& out) {
    auto it = ram.find(key);
    if (it != ram.end()) {
        it->second.used = plat::millis();
        out = it->second;
        return true;
    }
    if (!cfg::s().sdCache || !plat::sdAvailable()) return false;
    std::string raw;
    if (!plat::sdRead(path(key).c_str(), raw)) return false;
    size_t nl = raw.find('\n');                // file = "<etag>\n<json>"
    if (nl == std::string::npos || nl == 0 || nl + 2 > raw.size()) return false;
    Entry e;
    e.etag = raw.substr(0, nl);
    e.json = raw.substr(nl + 1);
    e.used = plat::millis();
    ramBytes += e.json.size() + e.etag.size();
    out = e;
    ram[key] = std::move(e);
    evictRam();
    return true;
}

void store(const std::string& key, const std::string& etag, std::string&& json) {
    if (key.empty() || etag.empty() || json.empty()) return;
    auto it = ram.find(key);
    if (it != ram.end()) ramBytes -= it->second.json.size() + it->second.etag.size();
    bool changed = it == ram.end() || it->second.json != json || it->second.etag != etag;
    Entry e;
    e.etag = etag;
    e.json = std::move(json);
    e.used = plat::millis();
    ramBytes += e.json.size() + e.etag.size();
    std::string content = changed && cfg::s().sdCache ? etag + "\n" + e.json : "";
    ram[key] = std::move(e);
    evictRam();
    if (content.empty() || !plat::sdAvailable()) return;       // unchanged data is not rewritten (SD wear + SPI time)
    plat::sdMkdir(kDir);
    plat::sdWrite(path(key).c_str(), content);
    if (++writes % 25 == 0) {                                  // keep the folder bounded
        std::vector<plat::SdEntry> files;
        if (plat::sdList(kDir, files) && files.size() > 120) {
            std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return a.mtime < b.mtime; });
            for (size_t i = 0; i < 30 && i < files.size(); i++) plat::sdRemove((std::string(kDir) + "/" + files[i].name).c_str());
        }
    }
}

void clear() {
    ram.clear();
    ramBytes = 0;
    std::vector<plat::SdEntry> files;
    if (plat::sdAvailable() && plat::sdList(kDir, files))
        for (auto& f : files) plat::sdRemove((std::string(kDir) + "/" + f.name).c_str());
}

void stats(size_t& e, size_t& b, size_t& f) {
    e = ram.size();
    b = ramBytes;
    f = 0;
    std::vector<plat::SdEntry> files;
    if (plat::sdAvailable() && plat::sdList(kDir, files)) f = files.size();
}

}  // namespace cache
