// Small widget kit on top of LVGL: themed cards, buttons, badges, modals, toasts, keyboard prompts.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "theme.h"

namespace ui {

enum class Kind { Primary, Secondary, Danger, Ghost };
enum class Tone { Neutral, Success, Warning, Danger, Info, Primary };
using Fn = std::function<void()>;

lv_color_t toneColor(Tone t);

// events
void onClick(lv_obj_t* o, Fn fn);
void onChange(lv_obj_t* o, std::function<void()> fn);  // VALUE_CHANGED

// layout primitives
lv_obj_t* box(lv_obj_t* p);      // transparent container (no padding/border)
lv_obj_t* row(lv_obj_t* p);      // horizontal flex, centered cross axis, width 100%
lv_obj_t* col(lv_obj_t* p);      // vertical flex, width 100%
lv_obj_t* card(lv_obj_t* p);     // surface card (vertical flex)
lv_obj_t* scroller(lv_obj_t* p); // vertical scrolling column filling remaining space
lv_obj_t* divider(lv_obj_t* p);
lv_obj_t* spacer(lv_obj_t* p);   // flex-grow filler

// content
lv_obj_t* label(lv_obj_t* p, const std::string& text, const lv_font_t* f, lv_color_t c);
lv_obj_t* text(lv_obj_t* p, const std::string& t, const lv_font_t* f = nullptr);        // primary text color
lv_obj_t* subtext(lv_obj_t* p, const std::string& t, const lv_font_t* f = nullptr);    // muted
lv_obj_t* heading(lv_obj_t* p, const std::string& t);
lv_obj_t* button(lv_obj_t* p, const std::string& t, Kind k, Fn fn);
lv_obj_t* iconButton(lv_obj_t* p, const char* symbol, Fn fn, bool filled = false);
lv_obj_t* badge(lv_obj_t* p, const std::string& t, Tone tone);
lv_obj_t* dot(lv_obj_t* p, Tone tone, int size = 0);
lv_obj_t* toggle(lv_obj_t* p, bool on, std::function<void(bool)> fn);
lv_obj_t* kv(lv_obj_t* p, const std::string& k, const std::string& v);   // detail row
lv_obj_t* field(lv_obj_t* p, const std::string& k, const std::string& v, Fn onTap);  // tappable setting row
lv_obj_t* empty(lv_obj_t* p, const char* symbol, const std::string& title, const std::string& sub);
lv_obj_t* loading(lv_obj_t* p, const std::string& text);
lv_obj_t* chip(lv_obj_t* p, const std::string& t, bool active, Fn fn);
lv_obj_t* logo(lv_obj_t* p, int size);
void setText(lv_obj_t* l, const std::string& t);
void ellipsis(lv_obj_t* l);   // single line with "..." (fixed one-line height)

// overlays (modals live on lv_layer_top)
void toast(const std::string& msg, Tone tone = Tone::Neutral);
void confirm(const std::string& title, const std::string& msg, const std::string& yes, bool danger, Fn onYes, Fn onCancel = nullptr);
void confirmDanger(const std::string& title, const std::string& msg, const std::string& yes, Fn onYes);  // honours setting
struct MenuItem {
    std::string label;
    Tone tone = Tone::Neutral;
};
void menu(const std::string& title, const std::vector<MenuItem>& items, std::function<void(int)> onPick);
void prompt(const std::string& title, const std::string& initial, const std::string& hint, bool password, bool multiline,
            std::function<void(const std::string&)> onOk);
void viewer(const std::string& title, const std::string& text, std::function<void()> onSave = nullptr);
void closeOverlays();   // removes everything on the top layer
bool overlayOpen();
void closeTopOverlay();  // Esc key: closes the topmost dialog
void initOverlays();    // call after theme/rebuild so stale pointers are dropped

}  // namespace ui
