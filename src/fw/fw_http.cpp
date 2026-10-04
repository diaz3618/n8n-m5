// Blocking HTTPS round trip to the n8n server (runs on the api worker thread).
// Bodies are read through BodyReader (handles Content-Length / chunked) and, when a filter is given,
// parsed straight from the socket so a multi-MB workflow list never has to fit in RAM.
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../core/api.h"
#include "../core/plat.h"
#include "../core/util.h"
#include "fw.h"

#ifdef DEV_CONSOLE
#define NET_TRACE(...) Serial.printf("[net] " __VA_ARGS__)
#else
#define NET_TRACE(...) ((void)0)
#endif

namespace {
class BodyReader : public Stream {
public:
    BodyReader(NetworkClient* c, int len, bool chunked, size_t cap, uint32_t budgetMs, uint32_t idleMs)
        : c_(c), remaining_(len), chunked_(chunked), cap_(cap), deadline_(millis() + budgetMs), idleMs_(idleMs) {}
    bool truncated = false, timedOut = false, clean = false;   // clean: the body ended exactly where the framing said (connection reusable)
    size_t total() const { return total_; }

    int read() override {
        if (pos_ >= fill_ && !refill()) return -1;
        total_++;
        return buf_[pos_++];
    }
    int peek() override { return (pos_ >= fill_ && !refill()) ? -1 : buf_[pos_]; }
    int available() override { return (int)(fill_ - pos_); }
    size_t write(uint8_t) override { return 0; }
    size_t readBytes(char* b, size_t n) override {
        size_t i = 0;
        for (int c; i < n && (c = read()) >= 0;) b[i++] = (char)c;
        return i;
    }

private:
    // NetworkClientSecure::readBytes() is NON-blocking (returns what has already arrived), unlike the plain client.
    // So wait here until bytes arrive, the peer closes, or nothing happens for idleMs_.
    size_t readSome(uint8_t* dst, size_t want) {
        uint32_t t0 = millis();
        for (;;) {
            if (c_->available() > 0) {
                int n = c_->read(dst, want);
                if (n > 0) return (size_t)n;
            } else if (!c_->connected()) {
                return 0;                                   // closed and drained
            }
            if (millis() - t0 > idleMs_ || (int32_t)(millis() - deadline_) > 0) return 0;
            delay(2);
        }
    }
    int readLine(char* out, size_t cap) {  // bounded: a never-ending line can't eat memory
        size_t n = 0;
        for (;;) {
            char c;
            if (readSome((uint8_t*)&c, 1) != 1) return -1;
            if (c == '\n') break;
            if (c != '\r' && n + 1 < cap) out[n++] = c;
        }
        out[n] = 0;
        return (int)n;
    }
    bool refill() {
        pos_ = fill_ = 0;
        if (done_) return false;
        if ((int32_t)(millis() - deadline_) > 0) {  // overall budget: a slow-drip server can't hold the worker forever
            timedOut = done_ = true;
            return false;
        }
        if (total_ >= cap_) {
            truncated = true;
            done_ = true;
            return false;
        }
        size_t want = sizeof buf_;
        if (chunked_) {
            if (chunkLeft_ == 0) {
                char line[40];
                int n = readLine(line, sizeof line);
                if (n == 0) n = readLine(line, sizeof line);  // CRLF that ends the previous chunk
                if (n <= 0 || !isxdigit((unsigned char)line[0])) {   // timeout / closed / garbage: truncated, never reusable
                    timedOut = done_ = true;
                    return false;
                }
                chunkLeft_ = strtoul(line, nullptr, 16);
                NET_TRACE("chunk line n=%d '%s' -> %u\n", n, n > 0 ? line : "", (unsigned)chunkLeft_);
                if (chunkLeft_ == 0) {
                    char trailer[8];
                    clean = readLine(trailer, sizeof trailer) == 0;   // the CRLF after the last chunk
                    done_ = true;
                    return false;
                }
            }
            if (want > chunkLeft_) want = chunkLeft_;
        } else if (remaining_ >= 0) {
            if (remaining_ == 0) {
                clean = done_ = true;
                return false;
            }
            if (want > (size_t)remaining_) want = remaining_;
        }
        size_t n = readSome(buf_, want);
        NET_TRACE("refill chunked=%d chunkLeft=%u remaining=%d want=%u got=%u total=%u\n", (int)chunked_, (unsigned)chunkLeft_, remaining_, (unsigned)want, (unsigned)n, (unsigned)total_);
        if (n == 0) {
            done_ = true;
            return false;
        }
        fill_ = n;
        if (chunked_) chunkLeft_ -= n;
        else if (remaining_ > 0) remaining_ -= (int)n;
        return true;
    }
    NetworkClient* c_;
    int remaining_;
    bool chunked_, done_ = false;
    size_t chunkLeft_ = 0, cap_, total_ = 0, pos_ = 0, fill_ = 0;
    uint32_t deadline_, idleMs_;
    uint8_t buf_[1024];
};
}  // namespace

namespace {
// One kept-alive connection to the n8n server (the worker thread is the only user). Reconnecting for every request is what
// made the app slow: a TLS handshake costs ~0.3-0.5 s on these chips.
struct Conn {
    std::unique_ptr<NetworkClient> plain;
    std::unique_ptr<NetworkClientSecure> secure;
    NetworkClient* cl = nullptr;
    std::unique_ptr<HTTPClient> httpp;   // must outlive the request (~HTTPClient() stops the socket), but die BEFORE the client it points to
    std::string key;
    uint32_t lastUse = 0;
    void drop() {
        if (cl) cl->stop();
        httpp.reset();   // its destructor runs while the client is still alive
        plain.reset();
        secure.reset();
        cl = nullptr;
        key.clear();
    }
} conn;
}  // namespace

namespace api {

void transport(const Request& s, Response& out) {
    out.doc = plat::newDoc();
    if (WiFi.status() != WL_CONNECTED) {
        out.status = -1;
        out.error = "WiFi not connected";
        return;
    }
    if (s.baseUrl.empty() || (!s.raw && s.apiKey.empty())) {
        out.status = -1;
        out.error = "Set the server URL and API key in Settings";
        return;
    }
    std::string url = s.baseUrl + (s.raw ? "" : "/api/v1") + s.path;
    if (!s.query.empty()) url += "?" + s.query;

    // reuse the open connection only when it is for the same server+TLS settings and was used a moment ago
    // (n8n/Express closes idle keep-alive sockets after ~5 s)
    bool https = util::startsWith(url, "https://");
    static bool clockWaited = false;
    if (https && !clockWaited) {   // wait (max 12 s, once) for NTP: Arduino's hostByName races the pending SNTP DNS lookup, and cert checks need the time
        for (int i = 0; i < 120 && !plat::now(); i++) delay(100);
        clockWaited = true;
    }
    std::string hostPart = url.substr(0, url.find('/', url.find("://") + 3));
    std::string key = hostPart + "|" + std::to_string(s.tls) + "|" + std::to_string(std::hash<std::string>()(s.caPem));
    // GETs may reuse a socket idle up to 45 s (a stale one just costs a failed attempt and one retry); anything that changes data
    // only reuses a socket used within 4 s, so a retry can never replay a mutation the server already processed.
    bool isGet = s.method == "GET";
    uint32_t maxIdle = isGet ? 45000 : 4000;
    bool reused = conn.cl && conn.key == key && millis() - conn.lastUse < maxIdle && conn.cl->connected();
    NET_TRACE("conn: reused=%d have=%d keymatch=%d idle=%ums connected=%d\n", (int)reused, (int)(conn.cl != nullptr), (int)(conn.key == key), (unsigned)(millis() - conn.lastUse), (int)(conn.cl && conn.cl->connected()));
    if (!reused) {
        conn.drop();
        if (https) {
            conn.secure.reset(new NetworkClientSecure());
            if (s.tls == 2) conn.secure->setInsecure();
            else if (s.tls == 1 && !s.caPem.empty()) conn.secure->setCACert(s.caPem.c_str());
            else fw::useBundle(*conn.secure, 0);
            conn.secure->setTimeout(s.timeoutSec);
            conn.cl = conn.secure.get();
        } else {
            conn.plain.reset(new NetworkClient());
            conn.plain->setTimeout(s.timeoutSec * 1000);
            conn.cl = conn.plain.get();
        }
        conn.key = key;
    }
    NetworkClient* client = conn.cl;
    NetworkClientSecure* secure = conn.secure.get();

    if (!conn.httpp) conn.httpp.reset(new HTTPClient());
    HTTPClient& http = *conn.httpp;
    http.setReuse(true);
    http.setTimeout(s.timeoutSec * 1000);
    http.setConnectTimeout(s.timeoutSec * 1000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);  // never forward the API key to another host
    if (!http.begin(*client, url.c_str())) {
        out.status = -1;
        out.error = "Bad URL";
        return;
    }
    const char* want[] = {"Transfer-Encoding", "ETag", "Connection", "Keep-Alive", "Content-Type", "Location"};
    http.collectHeaders(want, 6);
    http.addHeader("Accept", s.raw ? "application/json, text/plain, image/*, */*;q=0.5" : "application/json");
    http.addHeader("User-Agent", "n8n-remote-m5/1.0");
    if (!s.raw) http.addHeader("X-N8N-API-KEY", s.apiKey.c_str());
    for (auto& h : s.headers) http.addHeader(h.first.c_str(), h.second.c_str());
    if (!s.body.empty()) http.addHeader("Content-Type", s.contentType.empty() ? "application/json" : s.contentType.c_str());
    if (!s.ifNoneMatch.empty()) http.addHeader("If-None-Match", s.ifNoneMatch.c_str());

    int code = http.sendRequest(s.method.c_str(), (uint8_t*)s.body.data(), s.body.size());
    if (code <= 0 && reused && isGet) {  // the server had closed the idle socket: reconnect once
        http.end();
        conn.drop();
        api::Request again = s;
        transport(again, out);
        return;
    }
    if (code <= 0) {
        if (reused && !isGet) code = -1;
        out.status = code;
        char tls[100] = {0};
        if (secure) secure->lastError(tls, sizeof tls);
        out.error = HTTPClient::errorToString(code).c_str();
        if (tls[0]) out.error += std::string(" (") + tls + ")";
        if (util::startsWith(url, "https://")) out.error += " [https: use http:// if the server has no TLS]";
        if (s.tls == 0 && !plat::now()) out.error += " - clock not synced yet, retry in a moment";
        http.end();
        conn.drop();
        return;
    }

    String etagHdr = http.header("ETag");
    out.contentType = http.header("Content-Type").c_str();
    out.location = http.header("Location").c_str();
    if (code == 304 && !s.cachedJson.empty()) {   // unchanged on the server: use the cached copy, nothing was downloaded
        DeserializationError ce = deserializeJson(*out.doc, s.cachedJson, DeserializationOption::NestingLimit(64));
        http.end();
        conn.lastUse = millis();
        if (!ce) {
            out.status = 200;
            out.notModified = true;
            out.bytes = 0;
            return;
        }
        conn.drop();
        api::Request again = s;      // cache unreadable: fetch normally
        again.ifNoneMatch.clear();
        again.cachedJson.clear();
        again.cacheKey.clear();
        transport(again, out);
        return;
    }
    bool chunked = http.header("Transfer-Encoding").indexOf("chunked") >= 0;
    bool ok = code >= 200 && code < 300;
    bool stream = ok && !s.filter.empty() && s.filter != "*";
    // filtered parses don't buffer the body, so they may read far more than maxKb
    size_t cap = stream ? (size_t)16 * 1024 * 1024 : (size_t)s.maxKb * 1024;
    if (code == 204 || code == 205 || (code == 304 && s.cachedJson.empty())) {   // no body at all: nothing to read, socket stays clean
        http.end();
        conn.lastUse = millis();
        finish(s, out, code, std::string(), false);
        return;
    }
    NET_TRACE("response code=%d size=%d chunked=%d conn-hdr='%s' ka='%s' clientConnected=%d\n", code, http.getSize(), (int)chunked, http.header("Connection").c_str(), http.header("Keep-Alive").c_str(), (int)client->connected());
    BodyReader rd(client, http.getSize(), chunked, cap, 30000 + (uint32_t)s.timeoutSec * 4000, (uint32_t)s.timeoutSec * 1000);

    if (stream) {
        JsonDocument f;
        if (!makeFilter(s.filter, f)) {
            out.status = -4;
            out.error = "internal: bad filter";
            http.end();
            conn.drop();
            return;
        }
        DeserializationError e = deserializeJson(*out.doc, rd, DeserializationOption::Filter(f), DeserializationOption::NestingLimit(64));
        out.status = code;
        out.bytes = rd.total();
        setParseError(out, e);
        if (rd.truncated && out.status == -2) out.error = "Response too large (over 16 MB)";
        if (rd.timedOut && out.status == -2) out.error = "Server too slow (transfer timed out)";
        if (out.status == code && !s.cacheKey.empty()) {   // remember the fresh answer for next time
            if (etagHdr.length()) {
                out.etag = etagHdr.c_str();
                serializeJson(*out.doc, out.cacheBlob);
            }
        }
        if (!e) while (rd.read() >= 0) {}   // consume the rest of the body so the socket is clean for the next request
        conn.lastUse = millis();
        http.end();                 // (http holds a pointer to the client: end it before the client may be dropped)
        if (!rd.clean) conn.drop();
        return;
    }

    std::string body;
    if (http.getSize() > 0 && (size_t)http.getSize() <= cap) body.reserve(http.getSize());
    for (int c; (c = rd.read()) >= 0;) body += (char)c;
    conn.lastUse = millis();
    NET_TRACE("before end: clean=%d connected=%d\n", (int)rd.clean, (int)client->connected());
    http.end();
    NET_TRACE("after end: connected=%d\n", (int)client->connected());
    if (!rd.clean) conn.drop();   // truncated / timed out / read-until-close: never reuse
    if (code >= 300 && code < 400) {
        out.status = code;
        out.error = "Redirected: check the server URL (http vs https?)";
        out.body = std::move(body);
        return;
    }
    finish(s, out, code, std::move(body), rd.truncated);
    if (out.status == code && ok && !s.cacheKey.empty() && etagHdr.length() && out.doc && s.filter == "*") {
        out.etag = etagHdr.c_str();
        serializeJson(*out.doc, out.cacheBlob);
    }
}

}  // namespace api
