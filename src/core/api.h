// Async n8n REST client. Callbacks always run on the UI thread (inside api::pump()).
#pragma once
#include <ArduinoJson.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace api {

struct Request {
    std::string method = "GET";
    std::string path;    // "/workflows" (relative to /api/v1) or, if raw, to the server root
    std::string query;   // "limit=20&cursor=x" (already encoded)
    std::string body;    // JSON text
    std::string filter;  // "" = keep raw body text only; "*" = parse everything; else ArduinoJson filter (JSON text)
    bool raw = false;    // webhook-style call: server root, no API key
    std::vector<std::pair<std::string, std::string>> headers;   // extra request headers (webhook auth, custom)
    std::string contentType;   // body type; empty = application/json
    int minTimeoutSec = 0;     // long-running calls (AI answers): wait at least this long, whatever the setting
    // connection snapshot, filled by send() on the UI thread so the worker never touches live settings
    std::string baseUrl, apiKey, caPem;
    int tls = 0, timeoutSec = 15, maxKb = 1024;
    // response cache (filled by send() from cache::lookup): conditional GET
    std::string cacheKey, ifNoneMatch, cachedJson;
};

struct Response {
    int status = 0;  // HTTP status, or <=0 for transport errors
    std::string error;
    std::string body;                      // raw text (empty when a filter parsed it)
    std::shared_ptr<JsonDocument> doc;     // parsed/filtered JSON, may be empty doc
    uint32_t ms = 0;
    std::string contentType, location;   // response headers worth showing (webhook results)
    size_t bytes = 0;
    bool truncated = false;
    bool notModified = false;   // server said 304: doc is the cached copy, identical to what the caller may already show
    std::string etag, cacheBlob; // set by the transport on a fresh 200 (cacheBlob = compact filtered JSON)
    bool ok() const { return status >= 200 && status < 300; }
    JsonVariantConst json() const { return doc ? doc->as<JsonVariantConst>() : JsonVariantConst(); }
    std::string message() const;  // human readable error ("" when ok)
};

using Callback = std::function<void(Response&)>;

void send(const Request& r, Callback cb);
void get(const std::string& path, const std::string& query, const std::string& filter, Callback cb);
void call(const char* method, const std::string& path, const std::string& body, Callback cb);
void pump();            // call from the UI loop
int pending();          // queued + running requests
std::shared_ptr<JsonDocument> peek(const Request& r);   // cached answer for this request (RAM/SD), or null; UI thread
struct Hist {
    std::string method, path, error;
    int status;
    uint32_t ms;
};
const std::vector<Hist>& history();   // last ~12 completed requests, newest last (UI thread)
extern uint32_t lastLatencyMs;
extern int lastStatus;   // status of the most recent completed request (0 = none yet)

// implemented per platform: blocking HTTP round trip (must call finish() with the received status/body)
void transport(const Request& r, Response& out);
// helpers for streaming transports (parse straight from the socket instead of buffering the body)
bool makeFilter(const std::string& filterJson, JsonDocument& out);        // false if the filter text is invalid
void setParseError(Response& out, DeserializationError e);                 // maps an ArduinoJson error onto out.status/error
// shared post-processing: size cap, error-body parsing, ArduinoJson filtering
void finish(const Request& r, Response& out, int status, std::string&& body, bool truncated);

}  // namespace api
