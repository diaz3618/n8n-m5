// Generic REST resource screens: paged list + row actions + create/edit forms.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "nav.h"

namespace ui {

struct FormField {
    enum Kind { Text, Int, Bool, Json, Choice, Secret, Multiline };
    std::string key, label;
    Kind kind = Text;
    std::string def;
    std::vector<std::string> choices;
    bool required = false;
    std::string hint;
    bool mask = false;   // never show the value on screen (credential data)
};

struct Form {
    std::string title, method, path;  // path may contain {field} placeholders resolved from the prefill object
    std::string submit = "Save";
    std::vector<FormField> fields;
    bool wrapArray = false;           // body = [ {...} ]
};

struct RowView {
    std::string title, sub;
    std::vector<std::pair<std::string, Tone>> badges;
    int sw = -1;  // -1 no switch, else 0/1
    bool hasDot = false;
    Tone dot = Tone::Neutral;
};

inline std::shared_ptr<const Form> ref(const Form& f) { return std::shared_ptr<const Form>(&f, [](const Form*) {}); }  // for static forms

struct RowAction {
    std::string label;
    Tone tone = Tone::Neutral;
    std::string method, path, body;                  // templates, {field} filled from the row
    std::string confirm;                             // non-empty: confirm dialog text
    std::shared_ptr<const Form> form;                // open form prefilled from the row instead of sending directly
    std::function<bool(JsonObjectConst)> when;       // visibility predicate
    std::function<void(Page&, JsonObjectConst)> custom;
};

struct ListSpec {
    std::string title, path, query, dataKey = "data", filter, detailPath;
    bool paged = true;
    std::function<RowView(JsonObjectConst)> row;
    std::function<void(Page&, JsonObjectConst, bool, std::function<void(bool)>)> onSwitch;  // (page,item,newState,done(ok))
    std::function<void(Page&, JsonObjectConst)> onOpen;  // replaces the action menu
    std::vector<RowAction> actions;
    std::vector<Form> creates;
    std::string emptyTitle = "Nothing here yet", emptySub;
    bool search = true;
    std::vector<std::pair<std::string, std::string>> chips;   // (label, extra query) filter pills
    std::function<void(Page&, std::function<void()>)> prefetch; // run once before first load
    std::function<void(Page&, const std::vector<JsonObjectConst>&, std::function<void()>)> afterLoad;  // after rows are shown (e.g. resolve names)
    std::vector<Action> extraActions;                           // extra top-bar buttons
    std::function<std::string()> dynQuery;                      // extra query built from live filter state (part of the cache key)
    std::function<bool(JsonObjectConst)> keep;                  // client-side filter, applied before rows are drawn
    std::function<void(Page&, lv_obj_t*, std::function<void(bool)>)> filterBar;  // builds filter controls; apply(true) reloads, apply(false) only redraws
    bool autoFill = false;                                      // keep fetching pages until a page worth of rows survives keep()
};

PagePtr makeList(ListSpec spec);
void openForm(const Form& f, JsonVariantConst prefill, std::function<void()> onDone);
void showJson(Page& owner, const std::string& title, const std::string& path);   // GET + viewer
void showJsonText(const std::string& title, std::string raw, bool truncated = false);             // viewer for already-fetched JSON
void runCall(Page& owner, const std::string& method, const std::string& path, const std::string& body,
             const std::string& okMsg, std::function<void()> done);

void setWorkflowActive(Page& p, const std::string& id, bool on, std::function<void(bool)> done);   // publish / unpublish
void runWebhook(Page& owner, const std::string& method, const std::string& path, const std::string& body);
void ensureWorkflowNames(Page& owner, std::function<void()> done, bool force = false);   // heavy: whole workflow list (dashboard only)
void resolveWorkflowNames(Page& owner, const std::vector<std::string>& ids, std::function<void()> done);  // light: one GET per unknown id
std::string workflowCacheError();   // "" when the last refresh worked
int workflowCount(int* active, bool* more);   // from the cache
std::string workflowName(const std::string& id);

// resource catalog (pages_manage.cpp)
PagePtr makeResource(const std::string& id);

}  // namespace ui
