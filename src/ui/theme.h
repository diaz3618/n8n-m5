// n8n look & feel: coral primary, clean cards, light + dark palettes. Metrics adapt to CoreS3 vs Tab5.
#pragma once
#include <lvgl.h>

namespace ui {

struct Palette {
    lv_color_t bg, surface, surface2, border, text, text2, muted;
    lv_color_t primary, primaryHover, onPrimary;
    lv_color_t success, warning, danger, info;
    lv_color_t scrim;
    bool dark;
};

struct Metrics {
    int pad, gap, radius, rowH, btnH, topH, navW, navH, icon, kbH;
    const lv_font_t *sm, *md, *lg, *xl, *title;
    bool compact;
};

const Palette& C();
const Metrics& M();
void setDark(bool dark);   // swap palette (caller rebuilds the UI)
void initMetrics();        // picks CoreS3 vs Tab5 metrics from plat::info()

}  // namespace ui
