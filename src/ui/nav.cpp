#include "nav.h"

#include "../core/config.h"
#include "../core/plat.h"
#include "../core/util.h"

namespace ui {

void Page::send(const api::Request& r, api::Callback cb) {
    std::shared_ptr<bool> l = life;
    api::send(r, [l, cb](api::Response& res) {
        if (*l && cb) cb(res);
    });
}
void Page::get(const std::string& path, const std::string& query, const std::string& filter, api::Callback cb) {
    api::Request r;
    r.path = path;
    r.query = query;
    r.filter = filter;
    send(r, std::move(cb));
}
void Page::call(const char* method, const std::string& path, const std::string& body, api::Callback cb) {
    api::Request r;
    r.method = method;
    r.path = path;
    r.body = body;
    send(r, std::move(cb));
}

namespace nav {

struct Item {
    int sec;
    const char* icon;
    const char* name;
};
static const Item items[] = {
    {Dashboard, LV_SYMBOL_HOME, "Overview"},      {Workflows, LV_SYMBOL_SHUFFLE, "Workflows"},
    {Executions, LV_SYMBOL_PLAY, "Executions"},   {Manage, LV_SYMBOL_LIST, "Manage"},
    {Webhooks, LV_SYMBOL_UPLOAD, "Webhooks"},     {Agents, LV_SYMBOL_CHARGE, "AI & Agents"},
    {Explorer, LV_SYMBOL_EDIT, "API Explorer"},
    {Settings, LV_SYMBOL_SETTINGS, "Settings"},
};

static lv_obj_t *screen_, *content_, *topbar_, *nav_, *busy_;
static lv_obj_t* connDot_;
static lv_obj_t* connLbl_;
static std::vector<PagePtr> stack;
static std::vector<lv_obj_t*> navBtns;
static int sec_ = 0;
static uint32_t lastRefresh = 0, lastDot = 0, lastPoll = 0;

int currentSection() { return sec_; }
int depth() { return (int)stack.size(); }
Page* top() { return stack.empty() ? nullptr : stack.back().get(); }

static PagePtr makeSection(int s) {
    switch (s) {
        case Dashboard: return makeDashboard();
        case Workflows: return makeWorkflows();
        case Executions: return makeExecutions();
        case Manage: return makeManage();
        case Webhooks: return makeWebhooks();
        case Agents: return makeAgents();
        case Explorer: return makeExplorer();
        default: return makeSettings();
    }
}

static void highlight() {
    const Palette& c = C();
    for (size_t i = 0; i < navBtns.size(); i++) {
        int sec = (int)(intptr_t)lv_obj_get_user_data(navBtns[i]);
        bool act = (sec == sec_) || (M().compact && sec == Manage && (sec_ == Webhooks || sec_ == Agents || sec_ == Explorer));
        lv_obj_set_style_bg_opa(navBtns[i], act ? (c.dark ? LV_OPA_20 : LV_OPA_10) : LV_OPA_TRANSP, 0);
        for (uint32_t k = 0; k < lv_obj_get_child_count(navBtns[i]); k++) {
            lv_obj_t* ch = lv_obj_get_child(navBtns[i], k);
            lv_obj_set_style_text_color(ch, act ? c.primary : c.text2, 0);
        }
    }
}

static void renderTop() {
    if (!topbar_) return;
    lv_obj_clean(topbar_);
    busy_ = nullptr;
    Page* p = top();
    if (!p) return;
    if (stack.size() > 1) iconButton(topbar_, LV_SYMBOL_LEFT, [] { pop(); });
    lv_obj_t* t = text(topbar_, p->title, M().compact ? M().lg : M().xl);
    ellipsis(t);
    lv_obj_set_flex_grow(t, 1);
    busy_ = lv_spinner_create(topbar_);
    int s = M().compact ? 16 : 28;
    lv_obj_set_size(busy_, s, s);
    lv_spinner_set_anim_params(busy_, 900, 240);
    lv_obj_set_style_arc_width(busy_, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(busy_, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(busy_, C().border, LV_PART_MAIN);
    lv_obj_set_style_arc_color(busy_, C().primary, LV_PART_INDICATOR);
    lv_obj_add_flag(busy_, LV_OBJ_FLAG_HIDDEN);
    for (auto& a : p->actions) iconButton(topbar_, a.icon.c_str(), a.fn);
}

void refreshTopBar() { renderTop(); }

static void showTop() {
    for (size_t i = 0; i < stack.size(); i++) {
        if (stack[i]->body) {
            if (i + 1 == stack.size()) lv_obj_remove_flag(stack[i]->body, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(stack[i]->body, LV_OBJ_FLAG_HIDDEN);
        }
    }
    renderTop();
}

void push(PagePtr p) {
    if (!content_) return;
    lv_obj_t* b = col(content_);
    lv_obj_set_size(b, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_row(b, 0, 0);
    p->body = b;
    plat::log("[nav] push " + p->title);
    stack.push_back(std::move(p));
    stack.back()->build();
    showTop();
    lastRefresh = plat::millis();
}

void pop() {
    if (stack.size() <= 1) return;
    lv_obj_t* b = stack.back()->body;
    stack.pop_back();  // kills page life token first
    if (b) lv_obj_delete(b);
    showTop();
}

void popPage(Page* p) {
    for (size_t i = 0; i < stack.size(); i++) {
        if (stack[i].get() != p) continue;
        if (stack.size() == 1) return;
        lv_obj_t* b = stack[i]->body;
        stack.erase(stack.begin() + i);
        if (b) lv_obj_delete(b);
        showTop();
        return;
    }
}

static void clearStack() {
    while (!stack.empty()) {
        lv_obj_t* b = stack.back()->body;
        stack.pop_back();
        if (b) lv_obj_delete(b);
    }
}

void section(int s) {
    sec_ = s;
    plat::log("[nav] section " + std::to_string(s));
    closeOverlays();
    clearStack();
    push(makeSection(s));
    highlight();
}

static void buildSidebar(lv_obj_t* parent) {
    const Palette& c = C();
    nav_ = lv_obj_create(parent);
    lv_obj_remove_style_all(nav_);
    lv_obj_set_size(nav_, M().navW, LV_PCT(100));
    lv_obj_set_style_bg_color(nav_, c.surface, 0);
    lv_obj_set_style_bg_opa(nav_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nav_, 1, 0);
    lv_obj_set_style_border_side(nav_, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(nav_, c.border, 0);
    lv_obj_set_flex_flow(nav_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(nav_, M().pad, 0);
    lv_obj_set_style_pad_row(nav_, 6, 0);
    lv_obj_remove_flag(nav_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* head = row(nav_);
    lv_obj_set_style_pad_bottom(head, M().pad, 0);
    lv_obj_set_style_pad_column(head, M().gap + 2, 0);
    logo(head, 56);
    lv_obj_t* hl = col(head);
    lv_obj_set_width(hl, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(hl, 0, 0);
    text(hl, "n8n", M().xl);
    subtext(hl, "Remote");

    navBtns.clear();
    for (auto& it : items) {
        if (it.sec == Settings) continue;
        lv_obj_t* b = row(nav_);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(b, M().radius, 0);
        lv_obj_set_style_bg_color(b, c.primary, 0);
        lv_obj_set_style_pad_ver(b, 16, 0);
        lv_obj_set_style_pad_hor(b, 16, 0);
        lv_obj_set_style_pad_column(b, 16, 0);
        lv_obj_set_user_data(b, (void*)(intptr_t)it.sec);
        lv_obj_t* ic = label(b, it.icon, M().lg, c.text2);
        lv_obj_set_width(ic, 32);
        label(b, it.name, M().md, c.text2);
        int sec = it.sec;
        onClick(b, [sec] { section(sec); });
        navBtns.push_back(b);
    }
    spacer(nav_);
    lv_obj_t* st = row(nav_);
    connDot_ = dot(st, Tone::Neutral, 14);
    connLbl_ = subtext(st, "not connected");
    ellipsis(connLbl_);
    lv_obj_set_flex_grow(connLbl_, 1);
    for (auto& it : items) {
        if (it.sec != Settings) continue;
        lv_obj_t* b = row(nav_);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(b, M().radius, 0);
        lv_obj_set_style_bg_color(b, c.primary, 0);
        lv_obj_set_style_pad_ver(b, 16, 0);
        lv_obj_set_style_pad_hor(b, 16, 0);
        lv_obj_set_style_pad_column(b, 16, 0);
        lv_obj_set_user_data(b, (void*)(intptr_t)it.sec);
        lv_obj_t* ic = label(b, it.icon, M().lg, c.text2);
        lv_obj_set_width(ic, 32);
        label(b, it.name, M().md, c.text2);
        onClick(b, [] { section(Settings); });
        navBtns.push_back(b);
    }
}

static void buildBottomBar(lv_obj_t* parent) {
    const Palette& c = C();
    nav_ = lv_obj_create(parent);
    lv_obj_remove_style_all(nav_);
    lv_obj_set_size(nav_, LV_PCT(100), M().navH);
    lv_obj_set_style_bg_color(nav_, c.surface, 0);
    lv_obj_set_style_bg_opa(nav_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nav_, 1, 0);
    lv_obj_set_style_border_side(nav_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(nav_, c.border, 0);
    lv_obj_set_flex_flow(nav_, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(nav_, LV_OBJ_FLAG_SCROLLABLE);
    navBtns.clear();
    connDot_ = connLbl_ = nullptr;
    static const int show[] = {Dashboard, Workflows, Executions, Manage, Settings};
    for (int sec : show) {
        const Item* it = nullptr;
        for (auto& i : items) if (i.sec == sec) it = &i;
        lv_obj_t* b = lv_obj_create(nav_);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, LV_PCT(20), LV_PCT(100));
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(b, c.primary, 0);
        lv_obj_set_style_pad_row(b, 1, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(b, (void*)(intptr_t)sec);
        label(b, it->icon, M().lg, c.text2);
        label(b, sec == Dashboard ? "Home" : sec == Executions ? "Runs" : it->name, M().sm, c.text2);
        onClick(b, [sec] { section(sec); });
        navBtns.push_back(b);
    }
}

void build(lv_obj_t* scr) {
    screen_ = scr;
    initOverlays();
    stack.clear();
    const Palette& c = C();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, c.bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_gap(scr, 0, 0);
    lv_obj_set_flex_flow(scr, M().compact ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);

    lv_obj_t* main = scr;
    if (!M().compact) {
        buildSidebar(scr);
        main = lv_obj_create(scr);
        lv_obj_remove_style_all(main);
        lv_obj_set_height(main, LV_PCT(100));
        lv_obj_set_flex_grow(main, 1);
        lv_obj_set_flex_flow(main, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(main, LV_OBJ_FLAG_SCROLLABLE);
    } else {
        main = scr;
    }
    topbar_ = lv_obj_create(main);
    lv_obj_remove_style_all(topbar_);
    lv_obj_set_size(topbar_, LV_PCT(100), M().topH);
    lv_obj_set_style_bg_color(topbar_, c.surface, 0);
    lv_obj_set_style_bg_opa(topbar_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(topbar_, 1, 0);
    lv_obj_set_style_border_side(topbar_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(topbar_, c.border, 0);
    lv_obj_set_flex_flow(topbar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topbar_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(topbar_, M().pad, 0);
    lv_obj_set_style_pad_column(topbar_, M().gap, 0);
    lv_obj_remove_flag(topbar_, LV_OBJ_FLAG_SCROLLABLE);

    content_ = lv_obj_create(main);
    lv_obj_remove_style_all(content_);
    lv_obj_set_width(content_, LV_PCT(100));
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_remove_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    if (M().compact) buildBottomBar(scr);
    section(sec_);
}

void rebuild() {
    closeOverlays();
    clearStack();
    build(screen_);
}

void tick() {
    uint32_t now = plat::millis();
    if (busy_) {
        bool b = api::pending() > 0;
        bool hidden = lv_obj_has_flag(busy_, LV_OBJ_FLAG_HIDDEN);
        if (b && hidden) lv_obj_remove_flag(busy_, LV_OBJ_FLAG_HIDDEN);
        if (!b && !hidden) lv_obj_add_flag(busy_, LV_OBJ_FLAG_HIDDEN);
    }
    if (connDot_ && now - lastDot > 1000) {
        lastDot = now;
        bool wifi = plat::wifiState() == plat::WifiState::Connected;
        Tone t = !wifi ? Tone::Danger : (api::lastStatus == 0 ? Tone::Warning : (api::lastStatus >= 200 && api::lastStatus < 300 ? Tone::Success : Tone::Danger));
        lv_obj_set_style_bg_color(connDot_, toneColor(t), 0);
        std::string host = cfg::s().url;
        size_t p = host.find("://");
        if (p != std::string::npos) host = host.substr(p + 3);
        setText(connLbl_, !wifi ? "no WiFi" : (host.empty() ? "not configured" : host));
    }
    if (now - lastPoll > 300) {
        lastPoll = now;
        if (top()) top()->poll();
    }
    int rs = cfg::s().refreshSec;
    Page* p = top();
    if (rs > 0 && p && p->autoRefresh() && !overlayOpen() && now - lastRefresh > (uint32_t)rs * 1000) {
        lastRefresh = now;
        p->refresh();
    }
}

}  // namespace nav
}  // namespace ui
