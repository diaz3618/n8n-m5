// Platform abstraction. Implemented by src/fw (device) and sim/ (host screenshots).
#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace plat {

struct Info {
    int w, h;
    bool compact;      // true on the 320x240 CoreS3
    const char* name;  // "CoreS3" / "Tab5"
};
const Info& info();

uint32_t millis();
uint32_t random32();                       // hardware RNG
time_t now();                              // epoch seconds, 0 if clock not set
void setTz(int minutes, const char* ntp);  // starts/refreshes NTP
std::string deviceId();
size_t freeHeap();
size_t freePsram();
void restart();
void setBrightness(int pct);               // 0..100
void sleepDisplay(bool off);
bool hasKeyboard();                       // a physical keyboard is attached (Tab5 keyboard module)
void keyboardFocus(void* lvTextarea);     // route typed keys to this lv_textarea (no-op without a keyboard)
void setFlip(bool flipped);                // rotate display+touch 180 degrees

// key/value store (NVS on device)
std::string getStr(const char* k, const char* def = "");
void putStr(const char* k, const std::string& v);
int getInt(const char* k, int def);
void putInt(const char* k, int v);
void eraseAll();

// SD card (optional)
bool sdAvailable();
bool sdRead(const char* path, std::string& out);
bool sdWrite(const char* path, const std::string& data);
struct SdEntry {
    std::string name;
    time_t mtime;
};
bool sdMkdir(const char* path);
bool sdRemove(const char* path);
bool sdList(const char* dir, std::vector<SdEntry>& out);

// WiFi
struct Ap {
    std::string ssid;
    int rssi;
    bool secure;
};
enum class WifiState { Off, Connecting, Connected, Failed };
void wifiScanStart();
bool wifiScanDone(std::vector<Ap>& out);  // true once results are in
void wifiConnect(const std::string& ssid, const std::string& pass);
void wifiDisconnect();
WifiState wifiState();
std::string wifiSsid();
std::string wifiIp();
int wifiRssi();

// Phone/PC setup portal (device-hosted web form protected by a PIN)
bool portalStart(const std::string& pin);
void portalStop();
bool portalRunning();

// diagnostics
void log(const std::string& line);                       // serial on device, stderr in the sim
void probeStart(const std::string& baseUrl, const std::string& apiKey, int tlsMode, const std::string& caPem);  // async DNS/TCP/TLS/HTTP check
bool probeDone(std::string& report);                      // true once finished (report filled)
size_t largestFreeBlock();
std::string resetReason();   // why the last boot happened (power-on, panic, watchdog, brownout, ...)
int crashCount();            // consecutive crash resets (RTC memory)
bool safeMode();             // true after 3 consecutive crashes: networking is disabled so the UI stays reachable
void clearCrashes();

// background worker thread (stack in bytes)
void spawn(void (*fn)(void*), void* arg, const char* name, int stackBytes);

// JSON documents (PSRAM-backed on device)
std::shared_ptr<JsonDocument> newDoc();

}  // namespace plat
