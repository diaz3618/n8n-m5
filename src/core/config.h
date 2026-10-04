#pragma once
#include <string>
#include <vector>

namespace cfg {

struct Hook {  // saved webhook favourite
    std::string name, path, method, body;
};

struct Settings {
    std::string url, apiKey;
    std::string ssid, wifiPass;
    int tls = 0;             // 0 verify (CA bundle), 1 custom CA, 2 insecure
    std::string caPem;       // used when tls==1
    int theme = 2;           // 0 light, 1 dark, 2 auto (dark 19:00-07:00)
    int brightness = 80;     // %
    int pageSize = 20;       // list page size
    int timeoutSec = 15;     // HTTP timeout
    int refreshSec = 0;      // auto refresh lists, 0 = off
    int sleepSec = 120;      // dim/off after idle, 0 = never
    int tzMinutes = 0;       // offset from UTC
    std::string ntp = "pool.ntp.org";
    int maxKb = 1024;        // max response body held in memory
    int confirmDestructive = 1;
    int sdCache = 1;         // keep API responses on the SD card (validated with ETag)
    int flip = 0;            // rotate the screen 180 degrees
    bool setupDone = false;
    std::vector<Hook> hooks;
};

Settings& s();
void load();
void save();
bool configured();  // url + key present
// JSON backup/restore (for SD card + web portal)
std::string exportJson(bool secrets);
bool importJson(const std::string& js, std::string& err);
bool isDark();      // resolves theme==auto using the clock
}  // namespace cfg
