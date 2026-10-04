// App shell (sidebar on Tab5, bottom bar on CoreS3) + page stack.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/api.h"
#include "kit.h"

namespace ui {

struct Action {
    std::string icon;  // LV_SYMBOL_*
    Fn fn;
};

class Page {
public:
    virtual ~Page() { *life = false; }
    std::string title;
    std::vector<Action> actions;  // top-bar buttons (right side)
    lv_obj_t* body = nullptr;     // set by nav before build()
    virtual void build() = 0;
    virtual void refresh() {}
    virtual bool autoRefresh() { return false; }
    virtual void poll() {}  // ~3x per second while this page is on top

    // async helpers: the callback is dropped if the page was closed meanwhile
    void send(const api::Request& r, api::Callback cb);
    void get(const std::string& path, const std::string& query, const std::string& filter, api::Callback cb);
    void call(const char* method, const std::string& path, const std::string& body, api::Callback cb);
    std::shared_ptr<bool> life = std::make_shared<bool>(true);
};
using PagePtr = std::unique_ptr<Page>;

namespace nav {
enum Section { Dashboard, Workflows, Executions, Manage, Webhooks, Agents, Explorer, Settings, SectionCount };

void build(lv_obj_t* screen);  // (re)creates the whole shell
void section(int s);
int currentSection();
void push(PagePtr p);
void pop();
void popPage(Page* p);   // closes that page even if something was opened on top of it
int depth();
Page* top();
void refreshTopBar();  // after a page changed title/actions
void tick();           // call every loop: busy indicator, auto refresh, connection dot
void rebuild();        // after theme change: rebuilds shell and reopens current section
}  // namespace nav

// page factories (pages_*.cpp)
PagePtr makeDashboard();
PagePtr makeWorkflows();
PagePtr makeExecutions(const std::string& workflowId = "", const std::string& workflowName = "");
PagePtr makeManage();
PagePtr makeWebhooks();
PagePtr makeAgents();
PagePtr makeFlow(const std::string& id, const std::string& name);   // workflow canvas / editor
PagePtr makeWorkflowDetail(const std::string& id, const std::string& name);
PagePtr makeExplorer();
PagePtr makeSettings();
PagePtr makeSetup();
PagePtr makeDiagnostics();
void setupChecklist(Page& owner, lv_obj_t* parent, Fn onChanged);  // onboarding card (WiFi, URL, key, test)
void testConnection(Page& owner, std::function<void(bool, const std::string&)> done);
bool applyTheme();  // re-evaluate light/dark/auto; true if the whole UI was rebuilt (callers must not touch their page afterwards)

}  // namespace ui
