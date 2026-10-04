#pragma once
// Device-only glue shared between fw_*.cpp files.
#include <WiFiClientSecure.h>
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
namespace fw {
// Arduino core 3.3 has useBuiltinCACertBundle(); 3.2 (the Tab5 build, see platformio.ini) needs the bundle handed over
template <class T> auto useBundle(T& s, int) -> decltype(s.useBuiltinCACertBundle(), void()) { s.useBuiltinCACertBundle(); }
template <class T> void useBundle(T& s, long) { s.setCACertBundle(rootca_crt_bundle_start, rootca_crt_bundle_end - rootca_crt_bundle_start); }
void crashInit();    // first thing in setup(): counts consecutive crash resets
void platInit();     // NVS, WiFi pins
void portalTick();
void kbdInit();         // Tab5 keyboard module -> LVGL keypad (no-op on CoreS3)
void kbdTick();
bool wakeIfAsleep();  // true if the screen was off and a key just woke it
#ifdef DEV_CONSOLE
void devInit(int w, int h);
void devTick();
void devFrame(int x, int y, int w, int h, const uint16_t* px);
bool devTouch(int& x, int& y);   // injected touch (true while a tap is being held)
#endif
}
