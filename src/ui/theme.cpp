#include "theme.h"

#include "../core/plat.h"

namespace ui {

static Palette light = {
    /*bg*/ lv_color_hex(0xF6F7FA), /*surface*/ lv_color_hex(0xFFFFFF), /*surface2*/ lv_color_hex(0xEFF1F6),
    /*border*/ lv_color_hex(0xDBDFE7), /*text*/ lv_color_hex(0x101328), /*text2*/ lv_color_hex(0x4A4F63),
    /*muted*/ lv_color_hex(0x8B8FA0), /*primary*/ lv_color_hex(0xFF6D5A), /*hover*/ lv_color_hex(0xE9533F),
    /*onPrimary*/ lv_color_hex(0xFFFFFF), /*success*/ lv_color_hex(0x1F9D61), /*warning*/ lv_color_hex(0xD98A0B),
    /*danger*/ lv_color_hex(0xE5484D), /*info*/ lv_color_hex(0x5B6CF0), /*scrim*/ lv_color_hex(0x101328), false};

static Palette dark = {
    /*bg*/ lv_color_hex(0x131316), /*surface*/ lv_color_hex(0x1D1D21), /*surface2*/ lv_color_hex(0x27272C),
    /*border*/ lv_color_hex(0x37373E), /*text*/ lv_color_hex(0xF4F4F6), /*text2*/ lv_color_hex(0xB6B8C2),
    /*muted*/ lv_color_hex(0x7D808C), /*primary*/ lv_color_hex(0xFF6D5A), /*hover*/ lv_color_hex(0xFF8576),
    /*onPrimary*/ lv_color_hex(0xFFFFFF), /*success*/ lv_color_hex(0x3DD68C), /*warning*/ lv_color_hex(0xFFC15A),
    /*danger*/ lv_color_hex(0xFF6B72), /*info*/ lv_color_hex(0x8C9BFF), /*scrim*/ lv_color_hex(0x000000), true};

static const Palette* cur = &dark;
static Metrics m;

const Palette& C() { return *cur; }
const Metrics& M() { return m; }
void setDark(bool d) { cur = d ? &dark : &light; }

void initMetrics() {
    if (plat::info().compact) {
        m = {8, 6, 8, 46, 36, 36, 0, 46, 16, 120, &lv_font_montserrat_12, &lv_font_montserrat_14, &lv_font_montserrat_16,
             &lv_font_montserrat_20, &lv_font_montserrat_16, true};
    } else {
        m = {20, 14, 14, 88, 64, 84, 264, 0, 28, 330, &lv_font_montserrat_18, &lv_font_montserrat_20, &lv_font_montserrat_24,
             &lv_font_montserrat_32, &lv_font_montserrat_28, false};
    }
}

}  // namespace ui
