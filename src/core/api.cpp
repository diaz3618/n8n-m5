#include <algorithm>
#include "api.h"

#include <condition_variable>
#include <deque>
#include <mutex>

#include "cache.h"
#include "config.h"
#include "plat.h"

namespace api {

uint32_t lastLatencyMs = 0;
int lastStatus = 0;

struct Job {
    Request req;
    Callback cb;
    Response res;
};

static std::mutex mu;
static std::condition_variable cv;
static std::deque<std::shared_ptr<Job>> todo, done;
static std::vector<Hist> hist;
const std::vector<Hist>& history() { return hist; }
static bool started = false;
static int inflight = 0;

static void worker(void*) {
    for (;;) {
        std::shared_ptr<Job> j;
        {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk, [] { return !todo.empty(); });
            j = todo.front();
            todo.pop_front();
        }
        uint32_t t0 = plat::millis();
        transport(j->req, j->res);
        // Older n8n versions reject query parameters they don't know (400 "Unknown query parameter 'x'"): drop it and retry once.
        if (j->res.status == 400 && !j->req.query.empty()) {
            std::string m = j->res.doc ? std::string((*j->res.doc)["message"] | "") : "";
            if (m.empty()) m = j->res.body;
            size_t p = m.find("query parameter");
            size_t q1 = p == std::string::npos ? p : m.find('\'', p);
            size_t q2 = q1 == std::string::npos ? q1 : m.find('\'', q1 + 1);
            if (q2 != std::string::npos) {
                std::string bad = m.substr(q1 + 1, q2 - q1 - 1), nq;
                size_t pos = 0;
                while (pos <= j->req.query.size()) {
                    size_t amp = j->req.query.find('&', pos);
                    std::string part = j->req.query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
                    if (part.substr(0, part.find('=')) != bad && !part.empty()) nq += (nq.empty() ? "" : "&") + part;
                    if (amp == std::string::npos) break;
                    pos = amp + 1;
                }
                if (nq != j->req.query) {
                    plat::log("[api] retry without query param '" + bad + "'");
                    j->req.query = nq;
                    j->res = Response();
                    transport(j->req, j->res);
                }
            }
        }
        j->res.ms = plat::millis() - t0;
        std::lock_guard<std::mutex> lk(mu);
        done.push_back(j);
    }
}

void finish(const Request& r, Response& out, int status, std::string&& body, bool truncated) {
    out.status = status;
    out.bytes = body.size();
    out.truncated = truncated;
    out.doc = plat::newDoc();
    bool ok = status >= 200 && status < 300;
    if (!ok) {  // error bodies are small; keep text + parsed message
        if (!truncated && body.size() < 8192) deserializeJson(*out.doc, body);
        out.body = std::move(body);
        return;
    }
    if (r.filter.empty()) {
        out.body = std::move(body);
        return;
    }
    if (truncated) {
        out.status = -3;
        out.error = "Response larger than the " + std::to_string(r.maxKb) + " KB limit (raise it in Settings or use smaller pages)";
        return;
    }
    DeserializationError e;
    if (r.filter == "*") {
        e = deserializeJson(*out.doc, body, DeserializationOption::NestingLimit(64));
    } else {
        JsonDocument f;
        if (!makeFilter(r.filter, f)) {
            out.status = -4;
            out.error = "internal: bad filter";
            return;
        }
        e = deserializeJson(*out.doc, body, DeserializationOption::Filter(f), DeserializationOption::NestingLimit(64));
    }
    setParseError(out, e);
}

bool makeFilter(const std::string& filterJson, JsonDocument& out) { return !deserializeJson(out, filterJson); }

void setParseError(Response& out, DeserializationError e) {
    if (!e) return;
    out.status = -2;
    out.error = e == DeserializationError::NoMemory ? "Out of memory parsing the response" : std::string("Invalid JSON from server: ") + e.c_str();
}

std::string Response::message() const {
    if (ok()) return "";
    if (status <= 0) return error.empty() ? "Network error" : error;
    std::string m = "HTTP " + std::to_string(status);
    if (doc) {
        const char* msg = (*doc)["message"] | (const char*)nullptr;
        if (msg) m += ": " + std::string(msg);
    }
    if (status == 401) m += " (check API key)";
    else if (status == 403) m += " (key lacks scope)";
    else if (status == 404 && m.size() == 7) m += " (not found)";
    return m;
}

void send(const Request& r, Callback cb) {
    if (plat::safeMode()) {  // repeated crashes: don't touch the network until the user clears safe mode
        auto j = std::make_shared<Job>();
        j->cb = std::move(cb);
        j->res.doc = plat::newDoc();
        j->res.status = -9;
        j->res.error = "Safe mode after repeated crashes (Settings > Diagnose > Leave safe mode)";
        std::lock_guard<std::mutex> lk(mu);
        done.push_back(j);
        inflight++;
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mu);
        if (!started) {
            started = true;
            plat::spawn(worker, nullptr, "n8n-http", 20480);
        }
        auto j = std::make_shared<Job>();
        j->req = r;
        const auto& c = cfg::s();
        j->req.baseUrl = c.url;
        j->req.apiKey = c.apiKey;
        j->req.caPem = c.caPem;
        j->req.tls = c.tls;
        j->req.timeoutSec = std::max(c.timeoutSec, r.minTimeoutSec);
        j->req.maxKb = c.maxKb;
        j->req.cacheKey = cache::keyFor(j->req);
        cache::Entry ce;
        if (!j->req.cacheKey.empty() && cache::lookup(j->req.cacheKey, ce)) {
            j->req.ifNoneMatch = ce.etag;
            j->req.cachedJson = std::move(ce.json);
        }
        j->cb = std::move(cb);
        todo.push_back(j);
        inflight++;
    }
    cv.notify_one();
}

void get(const std::string& path, const std::string& query, const std::string& filter, Callback cb) {
    Request r;
    r.path = path;
    r.query = query;
    r.filter = filter;
    send(r, std::move(cb));
}

void call(const char* method, const std::string& path, const std::string& body, Callback cb) {
    Request r;
    r.method = method;
    r.path = path;
    r.body = body;
    send(r, std::move(cb));
}

void pump() {
    std::deque<std::shared_ptr<Job>> batch;
    {
        std::lock_guard<std::mutex> lk(mu);
        batch.swap(done);
        inflight -= (int)batch.size();
    }
    for (auto& j : batch) {
        lastLatencyMs = j->res.ms;
        if (!j->req.cacheKey.empty() && j->res.ok() && !j->res.etag.empty() && !j->res.cacheBlob.empty() && !j->res.notModified)
            cache::store(j->req.cacheKey, j->res.etag, std::move(j->res.cacheBlob));
        else if (j->res.notModified && !j->req.cacheKey.empty()) {
            cache::Entry touch;
            cache::lookup(j->req.cacheKey, touch);   // refresh LRU stamp
        }
        lastStatus = j->res.status;
        hist.push_back({j->req.method, j->req.path + (j->req.query.empty() ? "" : "?" + j->req.query), j->res.error, j->res.status, j->res.ms});
        if (hist.size() > 12) hist.erase(hist.begin());
        plat::log("[api] " + j->req.method + " " + j->req.baseUrl + j->req.path + " -> " + std::to_string(j->res.status) + " " + std::to_string(j->res.ms) + "ms " +
                  std::to_string(j->res.bytes) + "B" + (j->res.error.empty() ? "" : " err=" + j->res.error) + " heap=" + std::to_string(plat::freeHeap() / 1024) + "K");
        if (j->cb) j->cb(j->res);
    }
}

std::shared_ptr<JsonDocument> peek(const Request& r0) {
    Request r = r0;
    const auto& c = cfg::s();
    r.baseUrl = c.url;
    r.apiKey = c.apiKey;
    std::string key = cache::keyFor(r);
    cache::Entry e;
    if (key.empty() || !cache::lookup(key, e)) return nullptr;
    auto d = plat::newDoc();
    if (deserializeJson(*d, e.json, DeserializationOption::NestingLimit(64))) return nullptr;
    return d;
}

int pending() {
    std::lock_guard<std::mutex> lk(mu);
    return inflight;
}

}  // namespace api
