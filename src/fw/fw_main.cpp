// Arduino entry point: M5Unified display/touch -> LVGL, then the portable UI.
#include <Arduino.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "../core/api.h"
#include "../core/config.h"
#include "../core/plat.h"
#include "../ui/nav.h"
#include "fw.h"
#ifdef HAS_TLS13
#include <psa/crypto.h>
#endif

// LVGL software rendering + string handling needs more than the default 8 KB loop stack
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static plat::Info inf;
const plat::Info& plat::info() { return inf; }

static bool asleep = false, swallow = false;

namespace fw {
bool wakeIfAsleep() {
    if (!asleep) return false;
    plat::sleepDisplay(false);
    return true;
}
}  // namespace fw

static void flushCb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
    M5.Display.startWrite();
    M5.Display.pushImage(a->x1, a->y1, w, h, (const lgfx::rgb565_t*)px);
    M5.Display.endWrite();
#ifdef DEV_CONSOLE
    fw::devFrame(a->x1, a->y1, w, h, (const uint16_t*)px);
#endif
    lv_display_flush_ready(d);
}

static void touchCb(lv_indev_t*, lv_indev_data_t* d) {
    lgfx::touch_point_t tp;
#ifdef DEV_CONSOLE
    int ix, iy;
    if (fw::devTouch(ix, iy)) {  // injected by the dev console
        d->point.x = ix;
        d->point.y = iy;
        d->state = LV_INDEV_STATE_PRESSED;
        return;
    }
#endif
    if (M5.Display.getTouch(&tp)) {
        if (asleep) {  // first touch only wakes the screen
            plat::sleepDisplay(false);
            swallow = true;
        }
        d->point.x = tp.x;
        d->point.y = tp.y;
        d->state = swallow ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;
    } else {
        swallow = false;
        d->state = LV_INDEV_STATE_RELEASED;
    }
}

void setup() {
    Serial.begin(115200);
    fw::crashInit();
    auto mc = M5.config();
    M5.begin(mc);
    plat::setFlip(false);  // landscape; width/height are the same either way
    inf.w = M5.Display.width();
    inf.h = M5.Display.height();
    inf.compact = inf.w < 600;
#if defined(DEVICE_TAB5)
    inf.name = "Tab5";
#else
    inf.name = "CoreS3";
#endif
    fw::platInit();
#ifdef HAS_TLS13
    if (psa_crypto_init() != PSA_SUCCESS) Serial.println("psa_crypto_init failed");   // mbedTLS 3.6 TLS 1.3 needs PSA
#endif
    cfg::load();  // needs inf (page-size defaults) and the NVS opened by platInit
    plat::setFlip(cfg::s().flip);
    plat::setBrightness(cfg::s().brightness);
    M5.Display.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    lv_display_t* disp = lv_display_create(inf.w, inf.h);
    size_t lines = inf.compact ? 40 : 90;
    size_t bytes = inf.w * lines * sizeof(uint16_t);
    void* b1 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void* b2 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b1 || !b2) {  // fall back to a small internal buffer
        free(b1);
        free(b2);
        bytes = inf.w * 10 * sizeof(uint16_t);
        b1 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL);
        b2 = nullptr;
        if (!b1) {
            Serial.println("FATAL: no display buffer");
            delay(2000);
            ESP.restart();
        }
    }
    lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flushCb);
    lv_indev_t* in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, touchCb);
    fw::kbdInit();

    ui::initMetrics();
    ui::setDark(cfg::isDark());
    lv_display_set_theme(disp, lv_theme_default_init(disp, ui::C().primary, ui::C().info, ui::C().dark, ui::M().md));
    ui::nav::build(lv_screen_active());
#ifdef DEV_CONSOLE
    fw::devInit(inf.w, inf.h);
#endif

    if (!cfg::s().ssid.empty() && !plat::safeMode()) plat::wifiConnect(cfg::s().ssid, cfg::s().wifiPass);
    plat::setTz(cfg::s().tzMinutes, cfg::s().ntp.c_str());
}

void loop() {
    static uint32_t lastTheme = 0, lastWifi = 0;
    M5.update();
    api::pump();
    ui::nav::tick();
    fw::portalTick();
    fw::kbdTick();
#ifdef DEV_CONSOLE
    fw::devTick();
#endif
    uint32_t next = lv_timer_handler();
    uint32_t now = millis();

    int sl = cfg::s().sleepSec;
    if (sl > 0 && !asleep && lv_display_get_inactive_time(nullptr) > (uint32_t)sl * 1000 && !plat::portalRunning()) plat::sleepDisplay(true);

    if (now - lastTheme > 30000) {
        lastTheme = now;
        ui::applyTheme();
    }
    static bool stable = false;
    if (!stable && now > 60000) {  // survived a minute: the last crashes are history
        stable = true;
        plat::clearCrashes();
    }
    if (now - lastWifi > 15000 && !plat::safeMode()) {  // reconnect if the link dropped
        lastWifi = now;
        if (!cfg::s().ssid.empty() && plat::wifiState() != plat::WifiState::Connected && plat::wifiState() != plat::WifiState::Connecting)
            plat::wifiConnect(cfg::s().ssid, cfg::s().wifiPass);
    }
    delay(next > 20 ? 20 : (next < 2 ? 2 : next));
}

namespace plat {
void setBrightness(int pct) {
    if (pct < 1) pct = 1;
    M5.Display.setBrightness((uint8_t)(pct * 255 / 100));
}
void sleepDisplay(bool off) {
    asleep = off;
    if (off) M5.Display.setBrightness(0);
    else {
        setBrightness(cfg::s().brightness);
        lv_display_trigger_activity(nullptr);
    }
}
}  // namespace plat

namespace plat {
void setFlip(bool f) {
    // base landscape rotation: Tab5 panel is portrait-native (3 = landscape), CoreS3 is landscape-native (1)
#if defined(DEVICE_TAB5)
    int base = 3;
#else
    int base = 1;
#endif
    M5.Display.setRotation((base + (f ? 2 : 0)) % 4);
    if (lv_is_initialized()) lv_obj_invalidate(lv_screen_active());
}
}  // namespace plat
