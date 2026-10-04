// Host simulator platform: in-memory store, fake WiFi, canned n8n API answers. Used for screenshots/UI review.
#include <chrono>
#include <cstdio>
#include <map>
#include <thread>
#include <unistd.h>

#include "../src/core/api.h"
#include "../src/core/config.h"
#include "../src/core/plat.h"

namespace plat {
static Info inf = {320, 240, true, "CoreS3"};
Info& infoRw() { return inf; }
const Info& info() { return inf; }
static auto t0 = std::chrono::steady_clock::now();
uint32_t millis() { return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); }
uint32_t random32() { return 123456; }
time_t now() { return 1791028800 + millis() / 1000; }  // 2026-10-03 12:00:00 UTC
void setTz(int, const char*) {}
std::string deviceId() { return "SIM-0001"; }
size_t freeHeap() { return 180 * 1024; }
size_t freePsram() { return 6 * 1024 * 1024; }
void restart() {}
void setBrightness(int) {}
void sleepDisplay(bool) {}
bool hasKeyboard() { return false; }
void keyboardFocus(void*) {}
void setFlip(bool) {}
static std::map<std::string, std::string> kv;
std::string getStr(const char* k, const char* d) { auto i = kv.find(k); return i == kv.end() ? d : i->second; }
void putStr(const char* k, const std::string& v) { kv[k] = v; }
int getInt(const char* k, int d) { auto i = kv.find(k); return i == kv.end() ? d : atoi(i->second.c_str()); }
void putInt(const char* k, int v) { kv[k] = std::to_string(v); }
void eraseAll() { kv.clear(); }
bool sdAvailable() { return true; }
bool sdRead(const char*, std::string&) { return false; }
bool sdWrite(const char*, const std::string&) { return true; }
bool sdMkdir(const char*) { return true; }
bool sdRemove(const char*) { return true; }
bool sdList(const char*, std::vector<SdEntry>&) { return false; }
void wifiScanStart() {}
bool wifiScanDone(std::vector<Ap>& o) {
    o = {{"HomeLab-5G", -48, true}, {"Office-Guest", -63, true}, {"CoffeeShop", -78, false}, {"n8n-iot", -85, true}};
    return true;
}
void wifiConnect(const std::string&, const std::string&) {}
void wifiDisconnect() {}
WifiState wifiState() { return WifiState::Connected; }
std::string wifiSsid() { return "HomeLab-5G"; }
std::string wifiIp() { return "192.168.1.42"; }
int wifiRssi() { return -52; }
bool portalStart(const std::string&) { return true; }
void portalStop() {}
bool portalRunning() { return false; }
void log(const std::string& l) { if (getenv("SIM_LOG")) fprintf(stderr, "%s\n", l.c_str()); }
void probeStart(const std::string&, const std::string&, int, const std::string&) {}
bool probeDone(std::string& r) { r = "DNS    ok  192.168.1.10\nTCP    ok  :5678\nTLS    skipped (http)\nHEALTH HTTP 200\nAPI    HTTP 200 (sim)"; return true; }
std::string resetReason() { return "power-on"; }
int crashCount() { return 0; }
bool safeMode() { return false; }
void clearCrashes() {}
size_t largestFreeBlock() { return 100 * 1024; }
void spawn(void (*fn)(void*), void* arg, const char*, int) { std::thread(fn, arg).detach(); }
std::shared_ptr<JsonDocument> newDoc() { return std::make_shared<JsonDocument>(); }
}  // namespace plat

namespace {
std::string iso(int minsAgo) {
    time_t t = plat::now() - minsAgo * 60;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char b[40];
    strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S.000Z", &tmv);
    return b;
}
const char* wfNames[] = {"Daily sales report", "Slack alerts to PagerDuty", "Sync Notion to Airtable", "Invoice OCR pipeline",
                         "Weekly newsletter", "GitHub issue triage", "Nightly database backup", "Lead enrichment", "Customer onboarding emails"};
std::string workflows() {
    std::string o = "{\"data\":[";
    for (int i = 0; i < 9; i++) {
        if (i) o += ",";
        o += "{\"id\":\"wf" + std::to_string(i + 1) + "\",\"name\":\"" + wfNames[i] + "\",\"active\":" + (i % 3 == 2 ? "false" : "true") +
             ",\"isArchived\":false,\"updatedAt\":\"" + iso(30 + i * 400) + "\",\"createdAt\":\"" + iso(90000) + "\",\"triggerCount\":1,\"versionId\":\"v" + std::to_string(i) +
             "\",\"nodes\":[{\"name\":\"Webhook\",\"type\":\"n8n-nodes-base.webhook\",\"parameters\":{\"path\":\"sales-" + std::to_string(i) +
             "\",\"httpMethod\":\"POST\"}},{\"name\":\"Set\",\"type\":\"n8n-nodes-base.set\"},{\"name\":\"HTTP\",\"type\":\"n8n-nodes-base.httpRequest\"}]" +
             ",\"tags\":[" + (i % 2 ? "{\"id\":\"t1\",\"name\":\"production\"}" : "{\"id\":\"t2\",\"name\":\"internal\"},{\"id\":\"t3\",\"name\":\"ops\"}") + "]}";
    }
    return o + "],\"nextCursor\":null}";
}
std::string executions() {
    const char* st[] = {"success", "success", "error", "success", "running", "success", "error", "waiting", "success", "canceled"};
    std::string o = "{\"data\":[";
    for (int i = 0; i < 10; i++) {
        if (i) o += ",";
        o += "{\"id\":" + std::to_string(2040 - i) + ",\"workflowId\":\"wf" + std::to_string(i % 9 + 1) + "\",\"status\":\"" + st[i] +
             "\",\"mode\":\"" + (i % 4 == 0 ? "manual" : "trigger") + "\",\"startedAt\":\"" + iso(3 + i * 17) + "\",\"stoppedAt\":\"" +
             (st[i][0] == 'r' || st[i][0] == 'w' ? "" : iso(3 + i * 17 - 1)) + "\",\"finished\":true}";
    }
    return o + "],\"nextCursor\":null}";
}
}  // namespace

namespace {
std::string shq(const std::string& v) {  // escape for a curl config-file quoted string
    std::string o;
    for (char c : v) {
        if (c == '"' || c == '\\') o += '\\';
        if (c == '\n' || c == '\r') continue;
        o += c;
    }
    return o;
}
// Live mode (SIM_LIVE=1): real HTTP via the curl binary. The key goes through a 0600 config file, never argv, never logged.
bool liveTransport(const api::Request& r, api::Response& out) {
    char cfgPath[] = "/tmp/n8nsim-XXXXXX";
    int fd = mkstemp(cfgPath);
    if (fd < 0) return false;
    std::string url = r.baseUrl + (r.raw ? "" : "/api/v1") + r.path + (r.query.empty() ? "" : "?" + r.query);
    std::string c = "url = \"" + shq(url) + "\"\nrequest = \"" + r.method + "\"\nsilent\nmax-time = 25\nwrite-out = \"\\n%{http_code}\"\n";
    if (!r.raw) c += "header = \"X-N8N-API-KEY: " + shq(r.apiKey) + "\"\n";
    c += "header = \"Accept: application/json\"\n";
    for (auto& h : r.headers) c += "header = \"" + shq(h.first) + ": " + shq(h.second) + "\"\n";
    if (r.tls == 2) c += "insecure\n";
    std::string bodyPath;
    if (!r.body.empty()) {
        bodyPath = std::string(cfgPath) + ".body";
        FILE* bf = fopen(bodyPath.c_str(), "wb");
        fwrite(r.body.data(), 1, r.body.size(), bf);
        fclose(bf);
        c += "header = \"Content-Type: " + (r.contentType.empty() ? std::string("application/json") : shq(r.contentType)) + "\"\ndata-binary = \"@" + bodyPath + "\"\n";
    }
    FILE* cf = fdopen(fd, "w");
    fputs(c.c_str(), cf);
    fclose(cf);
    std::string cmd = std::string("curl -K ") + cfgPath + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    std::string all;
    char buf[8192];
    size_t n;
    while (p && (n = fread(buf, 1, sizeof buf, p)) > 0) all.append(buf, n);
    if (p) pclose(p);
    remove(cfgPath);
    if (!bodyPath.empty()) remove(bodyPath.c_str());
    size_t nl = all.rfind('\n');
    int code = nl == std::string::npos ? 0 : atoi(all.c_str() + nl + 1);
    std::string body = nl == std::string::npos ? all : all.substr(0, nl);
    fprintf(stderr, "[live] %s %s%s%s -> %d (%zu bytes)\n", r.method.c_str(), r.path.c_str(), r.query.empty() ? "" : "?", r.query.c_str(), code, body.size());
    if (code == 0) {
        out.doc = plat::newDoc();
        out.status = -1;
        out.error = "curl failed (network/TLS)";
        return true;
    }
    bool trunc = body.size() > (size_t)r.maxKb * 1024;
    if (trunc) body.resize((size_t)r.maxKb * 1024);
    api::finish(r, out, code, std::move(body), trunc);
    return true;
}
}  // namespace

namespace api {
void transport(const Request& r, Response& out) {
    if (getenv("SIM_LIVE") && liveTransport(r, out)) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    std::string body;
    int st = 200;
    const std::string& p = r.path;
    if (p == "/workflows") body = workflows();
    else if (p.rfind("/workflows/", 0) == 0 && p.find('/', 11) == std::string::npos) {
        std::string w = workflows();
        JsonDocument d;
        deserializeJson(d, w);
        for (JsonObject o : d["data"].as<JsonArray>())
            if (p.substr(11) == o["id"].as<std::string>()) {
                serializeJson(o, body);
                break;
            }
    } else if (p == "/executions") body = executions();
    else if (p == "/tags") body = "{\"data\":[{\"id\":\"t1\",\"name\":\"production\"},{\"id\":\"t2\",\"name\":\"internal\"},{\"id\":\"t3\",\"name\":\"ops\"}],\"nextCursor\":null}";
    else if (p == "/variables") body = "{\"data\":[{\"id\":\"v1\",\"key\":\"API_BASE\",\"value\":\"https://api.example.com\"},{\"id\":\"v2\",\"key\":\"ENV\",\"value\":\"production\"}],\"nextCursor\":null}";
    else if (p == "/users") body = "{\"data\":[{\"id\":\"u1\",\"email\":\"owner@example.com\",\"firstName\":\"Ada\",\"lastName\":\"Lovelace\",\"role\":\"global:owner\"},{\"id\":\"u2\",\"email\":\"dev@example.com\",\"firstName\":\"Linus\",\"lastName\":\"T\",\"role\":\"global:member\",\"isPending\":true}],\"nextCursor\":null}";
    else if (p == "/projects") body = "{\"data\":[{\"id\":\"p1\",\"name\":\"Personal\",\"type\":\"personal\"},{\"id\":\"p2\",\"name\":\"Marketing\",\"type\":\"team\"}],\"nextCursor\":null}";
    else if (p == "/credentials") body = "{\"data\":[{\"id\":\"c1\",\"name\":\"Slack account\",\"type\":\"slackApi\",\"updatedAt\":\"" + iso(5000) + "\"},{\"id\":\"c2\",\"name\":\"Postgres prod\",\"type\":\"postgres\",\"updatedAt\":\"" + iso(900) + "\"}],\"nextCursor\":null}";
    else if (p == "/data-tables") body = "{\"data\":[{\"id\":\"d1\",\"name\":\"leads\"},{\"id\":\"d2\",\"name\":\"settings\"}],\"nextCursor\":null}";
    else if (p == "/roles") body = "{\"data\":[{\"slug\":\"global:owner\",\"displayName\":\"Owner\",\"roleType\":\"global\",\"systemRole\":true}]}";
    else if (r.raw) body = "{\"ok\":true,\"echo\":" + (r.body.empty() ? "null" : r.body) + "}";
    else { st = 404; body = "{\"message\":\"Not found\"}"; }
    finish(r, out, st, std::move(body), false);
}
}  // namespace api
