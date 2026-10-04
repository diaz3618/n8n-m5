#include "kit.h"

#include <cstring>

#include "../core/config.h"
#include "logo_data.h"
#include "../core/plat.h"

namespace ui {

lv_color_t toneColor(Tone t) {
    const Palette& c = C();
    switch (t) {
        case Tone::Success: return c.success;
        case Tone::Warning: return c.warning;
        case Tone::Danger: return c.danger;
        case Tone::Info: return c.info;
        case Tone::Primary: return c.primary;
        default: return c.muted;
    }
}

static void fnCb(lv_event_t* e) {
    auto* f = (Fn*)lv_event_get_user_data(e);
    if (f && *f) {
        Fn copy = *f;  // the handler may delete the widget that owns *f
        copy();
    }
}
static void fnDel(lv_event_t* e) { delete (Fn*)lv_event_get_user_data(e); }

static void bind(lv_obj_t* o, lv_event_code_t code, Fn fn) {
    auto* p = new Fn(std::move(fn));
    lv_obj_add_event_cb(o, fnCb, code, p);
    lv_obj_add_event_cb(o, fnDel, LV_EVENT_DELETE, p);
}
void onClick(lv_obj_t* o, Fn fn) { bind(o, LV_EVENT_CLICKED, std::move(fn)); }
void onChange(lv_obj_t* o, std::function<void()> fn) { bind(o, LV_EVENT_VALUE_CHANGED, std::move(fn)); }

static void plain(lv_obj_t* o) {
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
}

lv_obj_t* box(lv_obj_t* p) {
    lv_obj_t* o = lv_obj_create(p);
    plain(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);  // layout only: taps must reach the clickable ancestor
    return o;
}

lv_obj_t* row(lv_obj_t* p) {
    lv_obj_t* o = box(p);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, M().gap, 0);
    return o;
}

lv_obj_t* col(lv_obj_t* p) {
    lv_obj_t* o = box(p);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o, M().gap, 0);
    return o;
}

lv_obj_t* card(lv_obj_t* p) {
    lv_obj_t* o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(o, C().surface, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, C().border, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, M().radius, 0);
    lv_obj_set_style_pad_all(o, M().pad, 0);
    lv_obj_set_style_pad_row(o, M().gap, 0);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    return o;
}

lv_obj_t* scroller(lv_obj_t* p) {
    lv_obj_t* o = lv_obj_create(p);
    plain(o);
    lv_obj_set_width(o, LV_PCT(100));
    lv_obj_set_flex_grow(o, 1);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(o, M().pad, 0);
    lv_obj_set_style_pad_row(o, M().gap, 0);
    lv_obj_set_style_pad_column(o, M().gap, 0);
    lv_obj_set_scroll_dir(o, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(o, C().border, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_SCROLLBAR);
    return o;
}

lv_obj_t* divider(lv_obj_t* p) {
    lv_obj_t* o = box(p);
    lv_obj_set_size(o, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(o, C().border, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

lv_obj_t* spacer(lv_obj_t* p) {
    lv_obj_t* o = box(p);
    lv_obj_set_size(o, 1, 1);
    lv_obj_set_flex_grow(o, 1);
    return o;
}

lv_obj_t* label(lv_obj_t* p, const std::string& t, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = lv_label_create(p);
    lv_label_set_text(l, t.c_str());
    lv_obj_set_style_text_font(l, f ? f : M().md, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return l;
}
lv_obj_t* text(lv_obj_t* p, const std::string& t, const lv_font_t* f) { return label(p, t, f, C().text); }
lv_obj_t* subtext(lv_obj_t* p, const std::string& t, const lv_font_t* f) { return label(p, t, f ? f : M().sm, C().muted); }
lv_obj_t* heading(lv_obj_t* p, const std::string& t) { return label(p, t, M().lg, C().text); }
void ellipsis(lv_obj_t* l) {
    const lv_font_t* f = lv_obj_get_style_text_font(l, LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(l, lv_font_get_line_height(f) + 2);
}

void setText(lv_obj_t* l, const std::string& t) {
    if (l) lv_label_set_text(l, t.c_str());
}

lv_obj_t* button(lv_obj_t* p, const std::string& t, Kind k, Fn fn) {
    const Palette& c = C();
    lv_obj_t* b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_height(b, M().btnH);
    lv_obj_set_style_radius(b, M().radius, 0);
    lv_obj_set_style_pad_hor(b, M().pad * 3 / 2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_color_t bg = c.primary, hover = c.primaryHover, fg = c.onPrimary;
    switch (k) {
        case Kind::Secondary:
            bg = c.surface; hover = c.surface2; fg = c.text;
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_color(b, c.border, 0);
            break;
        case Kind::Danger:
            bg = c.surface; hover = c.surface2; fg = c.danger;
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_color(b, c.danger, 0);
            break;
        case Kind::Ghost:
            bg = c.bg; hover = c.surface2; fg = c.text2;
            lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
            break;
        default: break;
    }
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_color(b, hover, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, t.c_str());
    lv_obj_set_style_text_font(l, M().md, 0);
    lv_obj_set_style_text_color(l, fg, 0);
    if (fn) onClick(b, std::move(fn));
    return b;
}

lv_obj_t* iconButton(lv_obj_t* p, const char* sym, Fn fn, bool filled) {
    const Palette& c = C();
    lv_obj_t* b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    int s = M().btnH;
    lv_obj_set_size(b, s, s);
    lv_obj_set_style_radius(b, M().radius, 0);
    lv_obj_set_style_bg_color(b, filled ? c.primary : c.surface2, 0);
    lv_obj_set_style_bg_opa(b, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(b, c.surface2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, M().lg, 0);
    lv_obj_set_style_text_color(l, filled ? c.onPrimary : c.text2, 0);
    lv_obj_center(l);
    if (fn) onClick(b, std::move(fn));
    return b;
}

lv_obj_t* badge(lv_obj_t* p, const std::string& t, Tone tone) {
    lv_color_t col = toneColor(tone);
    lv_obj_t* b = box(p);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(b, col, 0);
    lv_obj_set_style_bg_opa(b, C().dark ? LV_OPA_20 : LV_OPA_10, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(b, M().compact ? 6 : 12, 0);
    lv_obj_set_style_pad_ver(b, M().compact ? 1 : 3, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, t.c_str());
    lv_obj_set_style_text_font(l, M().sm, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return b;
}

lv_obj_t* dot(lv_obj_t* p, Tone tone, int size) {
    if (!size) size = M().compact ? 8 : 14;
    lv_obj_t* d = box(p);
    lv_obj_set_size(d, size, size);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, toneColor(tone), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    return d;
}

lv_obj_t* toggle(lv_obj_t* p, bool on, std::function<void(bool)> fn) {
    const Palette& c = C();
    lv_obj_t* s = lv_switch_create(p);
    if (M().compact) lv_obj_set_size(s, 40, 22);
    else lv_obj_set_size(s, 76, 42);
    lv_obj_set_style_bg_color(s, c.border, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, c.primary, (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s, lv_color_white(), LV_PART_KNOB);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    if (fn) {
        bind(s, LV_EVENT_VALUE_CHANGED, [s, fn]() { fn(lv_obj_has_state(s, LV_STATE_CHECKED)); });
    }
    return s;
}

lv_obj_t* kv(lv_obj_t* p, const std::string& k, const std::string& v) {
    lv_obj_t* r = row(p);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t* a = subtext(r, k);
    lv_obj_set_width(a, M().compact ? 84 : 150);
    lv_obj_t* b = text(r, v);
    lv_obj_set_flex_grow(b, 1);
    return r;
}

lv_obj_t* field(lv_obj_t* p, const std::string& k, const std::string& v, Fn onTap) {
    lv_obj_t* r = card(p);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(r, M().compact ? 6 : 14, 0);
    lv_obj_set_style_bg_color(r, C().surface2, LV_STATE_PRESSED);
    lv_obj_t* a = text(r, k);
    lv_obj_set_width(a, M().compact ? 112 : 320);
    lv_obj_t* b = label(r, v, M().md, C().text2);
    ellipsis(b);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_t* c = label(r, LV_SYMBOL_RIGHT, M().sm, C().muted);
    (void)c;
    if (onTap) {
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        onClick(r, std::move(onTap));
    }
    return r;
}

lv_obj_t* empty(lv_obj_t* p, const char* sym, const std::string& title, const std::string& sub) {
    lv_obj_t* c = col(p);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(c, M().pad * 3, 0);
    label(c, sym, M().xl, C().muted);
    lv_obj_t* t = text(c, title, M().lg);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    if (!sub.empty()) {
        lv_obj_t* s = subtext(c, sub);
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(s, LV_PCT(90));
    }
    return c;
}

lv_obj_t* loading(lv_obj_t* p, const std::string& t) {
    lv_obj_t* c = col(p);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(c, M().pad * 3, 0);
    lv_obj_t* sp = lv_spinner_create(c);
    int s = M().compact ? 28 : 56;
    lv_obj_set_size(sp, s, s);
    lv_spinner_set_anim_params(sp, 1000, 240);
    lv_obj_set_style_arc_width(sp, M().compact ? 3 : 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, M().compact ? 3 : 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sp, C().border, LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, C().primary, LV_PART_INDICATOR);
    subtext(c, t);
    return c;
}

lv_obj_t* chip(lv_obj_t* p, const std::string& t, bool active, Fn fn) {
    const Palette& c = C();
    lv_obj_t* b = box(p);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(b, M().compact ? 10 : 22, 0);
    lv_obj_set_style_pad_ver(b, M().compact ? 4 : 10, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, active ? c.primary : c.border, 0);
    lv_obj_set_style_bg_color(b, active ? c.primary : c.surface, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    label(b, t, M().sm, active ? c.onPrimary : c.text2);
    if (fn) onClick(b, std::move(fn));
    return b;
}

// n8n's mark (docs/n8n-logo.svg -> scripts/gen_logo.py), tinted with the brand colour; `w` is the width in px
lv_obj_t* logo(lv_obj_t* p, int w) {
    const lv_image_dsc_t* best = kLogos[0];
    for (const lv_image_dsc_t* d : kLogos)
        if (abs((int)d->header.w - w) < abs((int)best->header.w - w)) best = d;
    lv_obj_t* im = lv_image_create(p);
    lv_image_set_src(im, best);
    lv_obj_set_style_image_recolor(im, lv_color_hex(0xEA4B71), 0);
    lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
    lv_obj_remove_flag(im, LV_OBJ_FLAG_CLICKABLE);
    return im;
}

static std::vector<lv_obj_t*> stack;
static lv_obj_t* toastObj = nullptr;
static lv_timer_t* toastTimer = nullptr;

void initOverlays() {
    if (toastTimer) lv_timer_delete(toastTimer);
    if (toastObj) lv_obj_delete(toastObj);
    stack.clear();
    toastObj = nullptr;
    toastTimer = nullptr;
}
bool overlayOpen() { return !stack.empty(); }

static void closeTop() {
    if (stack.empty()) return;
    lv_obj_t* o = stack.back();
    stack.pop_back();
    lv_obj_delete_async(o);
}
void closeOverlays() {
    while (!stack.empty()) closeTop();
}

static lv_obj_t* backdrop(bool dim) {
    lv_obj_t* b = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(b, dim ? C().scrim : C().bg, 0);
    lv_obj_set_style_bg_opa(b, dim ? LV_OPA_60 : LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    stack.push_back(b);
    return b;
}

static lv_obj_t* sheet(lv_obj_t* bd, int width) {
    lv_obj_t* c = card(bd);
    lv_obj_set_width(c, width);
    lv_obj_set_style_max_height(c, LV_PCT(92), 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    lv_obj_center(c);
    lv_obj_set_style_shadow_width(c, M().compact ? 16 : 40, 0);
    lv_obj_set_style_shadow_opa(c, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(c, lv_color_black(), 0);
    return c;
}

static int sheetW() {
    int w = lv_display_get_horizontal_resolution(nullptr);
    return M().compact ? w - 24 : (w > 1000 ? 760 : w - 80);
}

void toast(const std::string& msg, Tone tone) {
    plat::log("[toast] " + msg);
    if (toastObj) {
        lv_obj_delete(toastObj);
        toastObj = nullptr;
    }
    if (toastTimer) {
        lv_timer_delete(toastTimer);
        toastTimer = nullptr;
    }
    const Palette& c = C();
    lv_obj_t* t = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(t);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(t, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(t, LV_PCT(92), 0);
    lv_obj_set_style_bg_color(t, c.dark ? c.surface2 : lv_color_hex(0x101328), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, M().radius, 0);
    lv_obj_set_style_pad_all(t, M().pad, 0);
    lv_obj_set_style_border_width(t, 2, 0);
    lv_obj_set_style_border_side(t, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(t, tone == Tone::Neutral ? c.primary : toneColor(tone), 0);
    lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, M().compact ? -52 : -40);
    lv_obj_t* l = lv_label_create(t);
    lv_label_set_text(l, msg.c_str());
    lv_obj_set_style_text_font(l, M().md, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_max_width(l, lv_display_get_horizontal_resolution(nullptr) * 80 / 100, 0);
    toastObj = t;
    toastTimer = lv_timer_create(
        [](lv_timer_t*) {
            if (toastObj) lv_obj_delete(toastObj);
            toastObj = nullptr;
            toastTimer = nullptr;
        },
        3200, nullptr);
    lv_timer_set_repeat_count(toastTimer, 1);
}

void confirm(const std::string& title, const std::string& msg, const std::string& yes, bool danger, Fn onYes, Fn onCancel) {
    lv_obj_t* bd = backdrop(true);
    lv_obj_t* c = sheet(bd, sheetW());
    heading(c, title);
    lv_obj_t* m = text(c, msg);
    lv_obj_set_width(m, LV_PCT(100));
    lv_obj_t* r = row(c);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    button(r, "Cancel", Kind::Secondary, [onCancel] {
        closeTop();
        if (onCancel) onCancel();
    });
    button(r, yes, danger ? Kind::Danger : Kind::Primary, [onYes] {
        closeTop();
        if (onYes) onYes();
    });
}

void confirmDanger(const std::string& title, const std::string& msg, const std::string& yes, Fn onYes) {
    if (!cfg::s().confirmDestructive) {
        if (onYes) onYes();
        return;
    }
    confirm(title, msg, yes, true, std::move(onYes));
}

void menu(const std::string& title, const std::vector<MenuItem>& items, std::function<void(int)> onPick) {
    lv_obj_t* bd = backdrop(true);
    lv_obj_add_flag(bd, LV_OBJ_FLAG_CLICKABLE);
    onClick(bd, [] { closeTop(); });
    lv_obj_t* c = sheet(bd, M().compact ? sheetW() : 640);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_pad_row(c, 0, 0);
    lv_obj_t* h = text(c, title, M().lg);
    lv_obj_set_style_pad_all(h, M().pad, 0);
    for (size_t i = 0; i < items.size(); i++) {
        divider(c);
        lv_obj_t* r = row(c);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_hor(r, M().pad, 0);
        lv_obj_set_style_pad_ver(r, M().compact ? 9 : 22, 0);
        lv_obj_set_style_bg_color(r, C().surface2, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
        label(r, items[i].label, M().md, items[i].tone == Tone::Danger ? C().danger : C().text);
        int idx = (int)i;
        onClick(r, [idx, onPick] {
            closeTop();
            if (onPick) onPick(idx);
        });
    }
    divider(c);
    lv_obj_t* r = row(c);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(r, M().pad, 0);
    lv_obj_set_style_pad_ver(r, M().compact ? 9 : 22, 0);
    label(r, "Cancel", M().md, C().muted);
    onClick(r, [] { closeTop(); });
}

// JSON-friendly symbol keyboard (LVGL's default lacks ':' and '_').
static const char* kSpec[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
                              "@", "#", "$", "%", "&", "-", "+", "=", "_", ":", ";", "\n",
                              "(", ")", "{", "}", "[", "]", "<", ">", "/", "\\", "|", "\n",
                              "abc", ",", " ", ".", "\"", "'", "!", "?", LV_SYMBOL_NEW_LINE, ""};
// widths; 0x80 marks "special" keys (no repeat, checked look)
static const uint8_t kSpecW[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0x82,
                                 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 0x82, 1, 4, 1, 1, 1, 1, 1, 0x82};
static_assert(sizeof(kSpecW) == 42, "one width per key in kSpec");
static lv_buttonmatrix_ctrl_t kSpecCtrl[sizeof(kSpecW)];

void prompt(const std::string& title, const std::string& initial, const std::string& hint, bool password, bool multiline,
            std::function<void(const std::string&)> onOk) {
    const Palette& c = C();
    lv_obj_t* bd = backdrop(false);
    lv_obj_set_flex_flow(bd, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(bd, 0, 0);
    lv_obj_set_style_pad_row(bd, 0, 0);

    lv_obj_t* hdr = row(bd);
    lv_obj_set_height(hdr, M().topH);
    lv_obj_set_style_pad_hor(hdr, M().pad, 0);
    lv_obj_set_style_bg_color(hdr, c.surface, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, c.border, 0);
    iconButton(hdr, LV_SYMBOL_CLOSE, [] { closeTop(); });
    lv_obj_t* t = text(hdr, title, M().lg);
    ellipsis(t);
    lv_obj_set_flex_grow(t, 1);
    lv_obj_t* ta = lv_textarea_create(bd);
    if (password) {
        iconButton(hdr, LV_SYMBOL_EYE_OPEN, [ta] { lv_textarea_set_password_mode(ta, !lv_textarea_get_password_mode(ta)); });
    }
    auto ok = [ta, onOk] {
        std::string v = lv_textarea_get_text(ta);
        closeTop();
        if (onOk) onOk(v);
    };
    iconButton(hdr, LV_SYMBOL_OK, ok, true);
    auto* okFn = new std::function<void()>(ok);  // Enter on a one-line field (physical or soft keyboard) = OK
    lv_obj_add_event_cb(
        ta,
        [](lv_event_t* e) {
            auto* f = static_cast<std::function<void()>*>(lv_event_get_user_data(e));
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_DELETE) delete f;
            else if (code == LV_EVENT_READY) (*f)();
        },
        LV_EVENT_ALL, okFn);

    lv_obj_set_width(ta, LV_PCT(100));
    if (multiline) lv_obj_set_flex_grow(ta, 1);
    else lv_obj_set_height(ta, M().btnH + M().pad);
    lv_textarea_set_one_line(ta, !multiline);
    lv_textarea_set_text(ta, initial.c_str());
    lv_textarea_set_placeholder_text(ta, hint.c_str());
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_max_length(ta, 8192);
    lv_obj_set_style_text_font(ta, M().md, 0);
    lv_obj_set_style_text_color(ta, c.text, 0);
    lv_obj_set_style_bg_color(ta, c.surface, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ta, c.border, 0);
    lv_obj_set_style_border_color(ta, c.primary, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_pad_all(ta, M().pad, 0);
    lv_obj_set_style_text_color(ta, c.muted, LV_PART_TEXTAREA_PLACEHOLDER);

    lv_obj_t* kb = lv_keyboard_create(bd);
    lv_obj_set_size(kb, LV_PCT(100), M().kbH);
    for (size_t i = 0; i < sizeof(kSpecW); i++)
        kSpecCtrl[i] = (lv_buttonmatrix_ctrl_t)((kSpecW[i] & 0x80 ? LV_KEYBOARD_CTRL_BUTTON_FLAGS : 0) | (kSpecW[i] & 0x0F));
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, kSpec, kSpecCtrl);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_bg_color(kb, c.surface2, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(kb, M().compact ? 3 : 8, 0);
    lv_obj_set_style_pad_gap(kb, M().compact ? 3 : 8, 0);
    lv_obj_set_style_bg_color(kb, c.surface, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, c.text, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, M().md, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, M().compact ? 4 : 8, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, c.surface2, (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, c.primary, (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        kb,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_READY) {  // Enter on a one-line field = OK
                lv_obj_t* k = lv_event_get_target_obj(e);
                lv_obj_t* ta2 = lv_keyboard_get_textarea(k);
                if (ta2 && lv_textarea_get_one_line(ta2)) lv_obj_send_event(ta2, LV_EVENT_READY, nullptr);
            }
        },
        LV_EVENT_READY, nullptr);
    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    if (plat::hasKeyboard()) {  // physical keyboard: type straight into the field; tap the field to bring the soft keyboard back
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        plat::keyboardFocus(ta);
        lv_obj_add_event_cb(
            ta, [](lv_event_t* e) { lv_obj_remove_flag(static_cast<lv_obj_t*>(lv_event_get_user_data(e)), LV_OBJ_FLAG_HIDDEN); },
            LV_EVENT_CLICKED, kb);
    }
}

void closeTopOverlay() { closeTop(); }

void viewer(const std::string& title, const std::string& txt, std::function<void()> onSave) {
    const Palette& c = C();
    lv_obj_t* bd = backdrop(false);
    lv_obj_set_flex_flow(bd, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* hdr = row(bd);
    lv_obj_set_height(hdr, M().topH);
    lv_obj_set_style_pad_hor(hdr, M().pad, 0);
    lv_obj_set_style_bg_color(hdr, c.surface, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    iconButton(hdr, LV_SYMBOL_LEFT, [] { closeTop(); });
    lv_obj_t* t = text(hdr, title, M().lg);
    ellipsis(t);
    lv_obj_set_flex_grow(t, 1);
    if (onSave) iconButton(hdr, LV_SYMBOL_SAVE, onSave);
    lv_obj_t* sc = scroller(bd);
    lv_obj_t* l = text(sc, txt, M().sm);
    lv_obj_set_width(l, LV_PCT(100));
}

}  // namespace ui
