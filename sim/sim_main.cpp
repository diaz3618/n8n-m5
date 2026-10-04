// Host simulator: renders the real UI into a framebuffer and writes a PPM screenshot.
// usage: sim <cores3|tab5> <light|dark> <section 0-6> [out.ppm] [--click x,y]... [--overlay menu|prompt|confirm|toast|viewer]
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <lvgl.h>

#include "../src/core/config.h"
#include "../src/core/plat.h"
#include "../src/core/util.h"
#include "../src/ui/nav.h"
#include "../src/ui/res.h"

namespace plat { Info& infoRw(); }

static std::vector<uint16_t> fb;
static int W, H;
static lv_indev_state_t ptState = LV_INDEV_STATE_RELEASED;
static int ptX, ptY;

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    uint16_t* src = (uint16_t*)px;
    int w = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; y++) memcpy(&fb[y * W + a->x1], src + (y - a->y1) * w, w * 2);
    lv_display_flush_ready(d);
}
static void readPt(lv_indev_t*, lv_indev_data_t* d) {
    d->state = ptState;
    d->point.x = ptX;
    d->point.y = ptY;
}

static void settle(int ms) {
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() < ms) {
        api::pump();
        ui::nav::tick();
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
static void click(int x, int y) {
    ptX = x; ptY = y; ptState = LV_INDEV_STATE_PRESSED;
    settle(80);
    ptState = LV_INDEV_STATE_RELEASED;
    settle(400);
}

// Reports visible widgets that stick out horizontally past the screen (clipped content).
static int overflows = 0;
static void checkFit(lv_obj_t* o, int depth) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    bool isLabel = lv_obj_check_type(o, &lv_label_class);
    if (isLabel && (a.x2 > W || a.x1 < 0) && a.x2 - a.x1 > 2) {
        const char* t = lv_label_get_text(o);
        printf("OVERFLOW x=%d..%d (screen %d): \"%.40s\"\n", a.x1, a.x2, W, t ? t : "");
        overflows++;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) checkFit(lv_obj_get_child(o, i), depth + 1);
}

int main(int argc, char** argv) {
    std::string dev = argc > 1 ? argv[1] : "cores3", th = argc > 2 ? argv[2] : "dark";
    int sec = argc > 3 ? atoi(argv[3]) : 0;
    std::string out = argc > 4 ? argv[4] : "shot.ppm";
    if (dev == "tab5") plat::infoRw() = {1280, 720, false, "Tab5"};
    W = plat::info().w; H = plat::info().h;
    fb.assign(W * H, 0);
    cfg::load();
    cfg::s().url = "https://n8n.example.com";
    cfg::s().apiKey = "n8n_api_abcdefghijklmnopqrstuvwxyz0123456789";
    if (getenv("SIM_LIVE") && getenv("SIM_N8N_URL") && getenv("SIM_N8N_KEY")) {  // live mode: real server from the environment
        cfg::s().url = util::normalizeUrl(getenv("SIM_N8N_URL"));
        cfg::s().apiKey = getenv("SIM_N8N_KEY");
        if (getenv("SIM_INSECURE")) cfg::s().tls = 2;
        cfg::s().maxKb = 16384;  // the device streams big lists; the sim buffers them
    }
    for (int i = 5; i < argc; i++) if (!strcmp(argv[i], "--unconfigured")) { cfg::s().url.clear(); cfg::s().apiKey.clear(); }
    cfg::s().theme = th == "light" ? 0 : 1;
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return plat::millis(); });
    lv_display_t* d = lv_display_create(W, H);
    static std::vector<uint16_t> buf(W * 60);
    lv_display_set_buffers(d, buf.data(), nullptr, buf.size() * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    lv_indev_t* in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, readPt);
    ui::initMetrics();
    ui::setDark(cfg::isDark());
    lv_theme_t* t = lv_theme_default_init(d, ui::C().primary, ui::C().info, ui::C().dark, ui::M().md);
    lv_display_set_theme(d, t);
    ui::nav::build(lv_screen_active());
    ui::nav::section(sec);
    settle(900);
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "--click") && i + 1 < argc) {
            int x, y;
            if (sscanf(argv[++i], "%d,%d", &x, &y) == 2) click(x, y);
        } else if (!strcmp(argv[i], "--scroll") && i + 1 < argc) {
            lv_obj_t* b = ui::nav::top()->body;
            for (uint32_t k = 0; k < lv_obj_get_child_count(b); k++) {
                lv_obj_t* c = lv_obj_get_child(b, k);
                if (lv_obj_has_flag(c, LV_OBJ_FLAG_SCROLLABLE)) lv_obj_scroll_by(c, 0, -atoi(argv[++i]), LV_ANIM_OFF);
            }
            settle(300);
        } else if (!strcmp(argv[i], "--push") && i + 1 < argc) {
            std::string n = argv[++i];
            if (n == "diag") ui::nav::push(ui::makeDiagnostics());
            else if (n.rfind("flow:", 0) == 0) ui::nav::push(ui::makeFlow(n.substr(5), "workflow"));
            else ui::nav::push(ui::makeResource(n));
            settle(700);
        } else if (!strcmp(argv[i], "--wait") && i + 1 < argc) {
            settle(atoi(argv[++i]));
        } else if (!strcmp(argv[i], "--overlay") && i + 1 < argc) {
            std::string o = argv[++i];
            if (o == "menu") ui::menu("Daily sales report", {{"View JSON"}, {"Retry"}, {"Delete", ui::Tone::Danger}}, nullptr);
            else if (o == "prompt") ui::prompt("n8n API key", "", "paste your key", true, true, nullptr);
            else if (o == "confirm") ui::confirm("Delete workflow", "Permanently delete \"Daily sales report\"?", "Delete", true, nullptr);
            else if (o == "wide") { lv_obj_t* l = lv_label_create(lv_layer_top()); lv_label_set_text(l, "this label is far too wide for it"); lv_obj_set_pos(l, 250, 10); }
            else if (o == "toast") ui::toast("Workflow published", ui::Tone::Success);
            else if (o == "viewer") ui::viewer("Response", "HTTP 200  123 ms\n\n{\n  \"data\": [\n    { \"id\": \"wf1\", \"name\": \"Daily sales report\" }\n  ]\n}");
            settle(500);
        }
    }
    settle(300);
    checkFit(lv_screen_active(), 0);
    checkFit(lv_layer_top(), 0);
    FILE* f = fopen(out.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t p = fb[i];
        unsigned char rgb[3] = {(unsigned char)(((p >> 11) & 31) * 255 / 31), (unsigned char)(((p >> 5) & 63) * 255 / 63), (unsigned char)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s (%dx%d)\n", out.c_str(), W, H);
    fflush(stdout);
    fflush(stderr);
    _exit(0);
}
