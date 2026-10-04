// Tab5 keyboard module (SKU A164, STM32F030 on Ext.Port1: I2C 0x6D, SDA=G0 SCL=G1, INT=G50) -> LVGL keypad.
// Typed characters go to the focused text field; Esc closes the top dialog / goes back; arrows scroll the page and switch sections.
// Protocol notes (M5Unit-KEYBOARD, as used by M5's demo BSP and bmorcelli's Launcher): Normal mode reports matrix press/release,
// the library maps them to HID codes / ASCII with the Sym and Aa (shift) layers applied.
#include <Arduino.h>
#include <lvgl.h>

#include "../core/plat.h"
#include "../ui/nav.h"
#include "fw.h"

#if defined(DEVICE_TAB5)
#include <M5UnitUnified.h>
#include <M5UnitUnifiedKEYBOARD.h>
#include <Wire.h>

namespace kb = m5::unit::tab5_keyboard;

namespace {
constexpr int8_t KB_SDA = 0, KB_SCL = 1, KB_INT = 50;
constexpr uint32_t RETRY_MS = 4000;  // keyboard absent at boot / plugged in later

m5::unit::UnitUnified units;
m5::unit::UnitTab5Keyboard dev;
bool added = false, ready = false;
uint32_t lastTry = 0;
lv_group_t* grp = nullptr;
lv_indev_t* indev = nullptr;

constexpr size_t QN = 32;
uint32_t q[QN];
size_t qh = 0, qt = 0;
bool releasePending = false;
uint32_t lastKey = 0;

void push(uint32_t k) {
    size_t n = (qt + 1) % QN;
    if (n != qh) {
        q[qt] = k;
        qt = n;
    }
}

void readCb(lv_indev_t*, lv_indev_data_t* d) {
    if (releasePending) {
        releasePending = false;
        d->state = LV_INDEV_STATE_RELEASED;
        d->key = lastKey;
    } else if (qh != qt) {
        lastKey = q[qh];
        qh = (qh + 1) % QN;
        d->key = lastKey;
        d->state = LV_INDEV_STATE_PRESSED;
        releasePending = true;
    } else {
        d->state = LV_INDEV_STATE_RELEASED;
    }
    d->continue_reading = releasePending || qh != qt;
}

bool begin() {
    if (!added) {
        auto cfg = dev.config();
        cfg.mode = kb::Mode::Normal;
        cfg.irq_pin = KB_INT;
        cfg.software_repeat = true;  // hold a key to repeat (backspace, arrows)
        dev.config(cfg);
        Wire.end();
        Wire.begin(KB_SDA, KB_SCL, dev.component_config().clock);
        if (!units.add(dev, Wire)) return false;
        added = true;
    }
    return units.begin();
}

bool inTextField() {
    lv_obj_t* f = grp ? lv_group_get_focused(grp) : nullptr;
    return f && lv_obj_check_type(f, &lv_textarea_class);
}

// first descendant of the current page that can scroll
lv_obj_t* scrollable(lv_obj_t* o) {
    if (!o) return nullptr;
    if (lv_obj_get_scroll_bottom(o) > 0 || lv_obj_get_scroll_top(o) > 0) return o;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        if (lv_obj_t* r = scrollable(lv_obj_get_child(o, i))) return r;
    return nullptr;
}

void handle(uint8_t kidx, bool sym, bool shift, bool ctrl, bool alt) {
    (void)ctrl;
    const uint8_t row = kidx / kb::KEY_COL_COUNT, col = kidx % kb::KEY_COL_COUNT;
    const kb::HidMapping m = sym ? kb::keyMatrixToHidSym(row, col) : kb::keyMatrixToHidBase(row, col);
    if (m.keycode == 0) return;  // Sym / Aa / Ctrl / Alt themselves
    bool edit = inTextField();
#ifdef DEV_CONSOLE
    Serial.printf("[kbd] key idx=%u hid=0x%02X ch=%d sym=%d shift=%d ctrl=%d alt=%d edit=%d\n", kidx, m.keycode, (int)dev.keyMatrixToChar(kidx), sym, shift, ctrl, alt, edit);
#endif
    switch (m.keycode) {
        case 0x29:  // Esc
            if (ui::overlayOpen()) ui::closeTopOverlay();
            else if (ui::nav::depth() > 1) ui::nav::pop();
            return;
        case 0x28: if (edit) push(LV_KEY_ENTER); return;
        case 0x2A: if (edit) push(LV_KEY_BACKSPACE); return;
        case 0x4C: if (edit) push(LV_KEY_DEL); return;
        case 0x2B: push(shift ? LV_KEY_PREV : LV_KEY_NEXT); return;
        case 0x4F: case 0x50: case 0x51: case 0x52: {
            const bool right = m.keycode == 0x4F, left = m.keycode == 0x50, down = m.keycode == 0x51;
            if (edit) { push(right ? LV_KEY_RIGHT : left ? LV_KEY_LEFT : down ? LV_KEY_DOWN : LV_KEY_UP); return; }
            if (ui::overlayOpen()) return;
            if (left || right) {  // Left/Right: previous/next section
                int s = ui::nav::currentSection() + (right ? 1 : -1);
                if (s >= 0 && s < ui::nav::SectionCount) ui::nav::section(s);
            } else if (lv_obj_t* sc = scrollable(ui::nav::top() ? ui::nav::top()->body : nullptr)) {
                lv_obj_scroll_by(sc, 0, down ? -120 : 120, LV_ANIM_ON);
            }
            return;
        }
        default: break;
    }
    if (alt && m.keycode >= 0x1E && m.keycode <= 0x24) {  // Alt+1..7: jump to a section
        ui::nav::section(m.keycode - 0x1E);
        return;
    }
    if (!edit) return;
    char ch = dev.keyMatrixToChar(kidx);
    if (ch >= 0x20 && ch < 0x7F) push((uint8_t)ch);
}

void poll() {
    units.update();
    if (!dev.wasPressed() && !dev.isRepeating()) return;
    if (fw::wakeIfAsleep()) return;  // the first key only wakes the screen
    lv_display_trigger_activity(nullptr);
    const bool sym = dev.isSym(), shift = dev.isAa(), ctrl = dev.isCtrl(), alt = dev.isAlt();
    for (uint8_t i = 0; i < kb::KEY_COUNT; i++)
        if (dev.wasPressed(i) || dev.repeatingBits().test(i)) handle(i, sym, shift, ctrl, alt);
}
}  // namespace

namespace fw {
void kbdInit() {
    grp = lv_group_create();
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, readCb);
    lv_indev_set_group(indev, grp);
    ready = begin();
    lastTry = millis();
    Serial.println(ready ? "[kbd] Tab5 keyboard ready" : "[kbd] Tab5 keyboard not found (will keep looking)");
}
void kbdTick() {
    if (!ready) {
        if (millis() - lastTry < RETRY_MS) return;
        lastTry = millis();
        ready = begin();
        if (ready) Serial.println("[kbd] Tab5 keyboard connected");
        return;
    }
    poll();
}
}  // namespace fw

namespace plat {
bool hasKeyboard() { return ready; }
void keyboardFocus(void* ta) {
    if (!grp || !ta) return;
    lv_group_add_obj(grp, static_cast<lv_obj_t*>(ta));
    lv_group_focus_obj(static_cast<lv_obj_t*>(ta));
    lv_group_set_editing(grp, true);
}
}  // namespace plat

#else  // CoreS3: no keyboard module
namespace fw {
void kbdInit() {}
void kbdTick() {}
}  // namespace fw
namespace plat {
bool hasKeyboard() { return false; }
void keyboardFocus(void*) {}
}  // namespace plat
#endif
