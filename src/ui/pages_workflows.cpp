// Workflows (list + detail) and Executions.
#include <algorithm>
#include <map>

#include "../core/config.h"
#include "../core/plat.h"
#include "../core/util.h"
#include "res.h"

namespace ui {

static std::string S(JsonObjectConst o, const char* k) { return util::str(o[k]); }

struct WfInfo {
    std::string name;
    bool active;
};
static std::map<std::string, WfInfo> wfCache;
static uint32_t wfCacheAt = 0;
static bool wfCacheMore = false;
static std::string wfCacheErr;
std::string workflowCacheError() { return wfCacheErr; }

std::string workflowName(const std::string& id) {
    auto it = wfCache.find(id);
    return it == wfCache.end() ? "" : it->second.name;
}
int workflowCount(int* active, bool* more) {
    int a = 0;
    for (auto& kv : wfCache) a += kv.second.active;
    if (active) *active = a;
    if (more) *more = wfCacheMore;
    return (int)wfCache.size();
}

void resolveWorkflowNames(Page& owner, const std::vector<std::string>& ids, std::function<void()> done) {
    std::vector<std::string> todo;
    for (auto& id : ids)
        if (!id.empty() && !wfCache.count(id) && std::find(todo.begin(), todo.end(), id) == todo.end() && todo.size() < 8) todo.push_back(id);
    if (todo.empty()) return;
    auto step = std::make_shared<std::function<void(size_t)>>();
    std::shared_ptr<bool> life = owner.life;
    Page* po = &owner;
    *step = [=](size_t i) {
        if (!*life) return;
        if (i >= todo.size()) {
            done();
            return;
        }
        po->get("/workflows/" + util::urlEncode(todo[i]), "", "{\"name\":true,\"active\":true}", [=](api::Response& r) {
            if (r.ok()) wfCache[todo[i]] = {S(r.json().as<JsonObjectConst>(), "name"), r.json()["active"] | false};
            else if (r.status == 404) wfCache[todo[i]] = {"(deleted workflow)", false};
            (*step)(i + 1);
        });
    };
    (*step)(0);
}

void ensureWorkflowNames(Page& owner, std::function<void()> done, bool force) {
    if (!force && wfCacheAt && plat::millis() - wfCacheAt < 60000) {
        done();
        return;
    }
    owner.get("/workflows", "limit=100", "{\"data\":[{\"id\":true,\"name\":true,\"active\":true}],\"nextCursor\":true}",
              [done](api::Response& r) {
                  wfCacheErr = r.ok() ? "" : r.message();
                  if (r.ok()) {
                      wfCache.clear();
                      for (JsonObjectConst o : r.json()["data"].as<JsonArrayConst>()) wfCache[S(o, "id")] = {S(o, "name"), o["active"] | false};
                      wfCacheMore = !r.json()["nextCursor"].isNull();
                      wfCacheAt = plat::millis();
                  }
                  done();
              });
}

static void setActive(Page& p, const std::string& id, bool on, std::function<void(bool)> done) {
    std::string base = "/workflows/" + util::urlEncode(id);
    std::shared_ptr<bool> l = p.life;
    Page* pp = &p;
    p.call("POST", base + (on ? "/publish" : "/unpublish"), "{}", [pp, l, base, on, done](api::Response& r) {
        if (r.ok()) {
            toast(on ? "Published" : "Unpublished", Tone::Success);
            done(true);
        } else if (r.status == 404 && *l) {
            pp->call("POST", base + (on ? "/activate" : "/deactivate"), "", [done, on](api::Response& r2) {
                if (r2.ok()) toast(on ? "Activated" : "Deactivated", Tone::Success);
                else toast(r2.message(), Tone::Danger);
                done(r2.ok());
            });
        } else {
            toast(r.message(), Tone::Danger);
            done(false);
        }
    });
}
void setWorkflowActive(Page& p, const std::string& id, bool on, std::function<void(bool)> done) { setActive(p, id, on, std::move(done)); }

class TagPicker : public Page {
public:
    std::string wid;
    std::vector<std::string> ids, names;
    std::vector<bool> sel;
    std::function<void()> done;
    lv_obj_t* list = nullptr;
    void build() override {
        title = "Workflow tags";
        list = scroller(body);
        loading(list, "Loading tags...");
        get("/workflows/" + util::urlEncode(wid) + "/tags", "", "*", [this](api::Response& cur) {
            std::vector<std::string> have;
            if (cur.ok()) {
                JsonVariantConst v = cur.json();
                if (!v.is<JsonArrayConst>()) v = v["data"];
                for (JsonObjectConst o : v.as<JsonArrayConst>()) have.push_back(S(o, "id"));
            }
            get("/tags", "limit=250", "{\"data\":[{\"id\":true,\"name\":true}]}", [this, have](api::Response& r) {
                lv_obj_clean(list);
                if (!r.ok()) {
                    empty(list, LV_SYMBOL_WARNING, "Could not load tags", r.message());
                    return;
                }
                for (JsonObjectConst o : r.json()["data"].as<JsonArrayConst>()) {
                    ids.push_back(S(o, "id"));
                    names.push_back(S(o, "name"));
                    sel.push_back(std::find(have.begin(), have.end(), S(o, "id")) != have.end());
                }
                if (ids.empty()) empty(list, LV_SYMBOL_BARS, "No tags exist yet", "Create tags under Manage > Tags");
                for (size_t i = 0; i < ids.size(); i++) {
                    lv_obj_t* c = card(list);
                    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                    lv_obj_t* t = text(c, names[i]);
                    lv_obj_set_flex_grow(t, 1);
                    toggle(c, sel[i], [this, i](bool on) { sel[i] = on; });
                }
                lv_obj_t* r2 = row(list);
                lv_obj_set_flex_align(r2, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                button(r2, "Save tags", Kind::Primary, [this] { save(); });
            });
        });
    }
    void save() {
        JsonDocument d;
        JsonArray a = d.to<JsonArray>();
        for (size_t i = 0; i < ids.size(); i++)
            if (sel[i]) a.add<JsonObject>()["id"] = ids[i];
        std::string b;
        serializeJson(d, b);
        auto dn = done;
        call("PUT", "/workflows/" + util::urlEncode(wid) + "/tags", b, [this, dn](api::Response& r) {
            if (r.ok()) {
                toast("Tags saved", Tone::Success);
                nav::popPage(this);
                if (dn) dn();
            } else toast(r.message(), Tone::Danger);
        });
    }
};

class WorkflowDetail : public Page {
public:
    std::string id, name;
    std::function<void()> changed;
    lv_obj_t* box_ = nullptr;
    std::shared_ptr<JsonDocument> doc;
    std::vector<std::pair<std::string, std::string>> hooks;  // (method, path)

    void build() override {
        title = name.empty() ? "Workflow" : name;
        actions.push_back({LV_SYMBOL_REFRESH, [this] { refresh(); }});
        box_ = scroller(body);
        refresh();
    }

    void refresh() override {
        lv_obj_clean(box_);
        loading(box_, "Loading workflow...");
        get("/workflows/" + util::urlEncode(id), "",
            "{\"id\":true,\"name\":true,\"active\":true,\"isArchived\":true,\"createdAt\":true,\"updatedAt\":true,\"versionId\":true,"
            "\"triggerCount\":true,\"tags\":[{\"id\":true,\"name\":true}],\"nodes\":[{\"name\":true,\"type\":true,\"disabled\":true,"
            "\"parameters\":{\"path\":true,\"httpMethod\":true}}]}",
            [this](api::Response& r) {
                lv_obj_clean(box_);
                if (!r.ok()) {
                    empty(box_, LV_SYMBOL_WARNING, "Could not load", r.message());
                    return;
                }
                doc = r.doc;
                render();
            });
    }

    void render() {
        JsonObjectConst o = doc->as<JsonObjectConst>();
        name = S(o, "name");
        title = name;
        nav::refreshTopBar();
        bool active = o["active"] | false, arch = o["isArchived"] | false;
        lv_obj_t* c = card(box_);
        lv_obj_t* h = row(c);
        lv_obj_t* t = text(h, name, M().lg);
        lv_obj_set_flex_grow(t, 1);
        badge(h, arch ? "archived" : (active ? "active" : "inactive"), arch ? Tone::Neutral : (active ? Tone::Success : Tone::Warning));
        divider(c);
        kv(c, "ID", id);
        kv(c, "Updated", util::isoToEpochAgo(S(o, "updatedAt").c_str()));
        kv(c, "Created", util::fmtDateTime(util::parseIso(S(o, "createdAt").c_str())));
        JsonArrayConst nodes = o["nodes"].as<JsonArrayConst>();
        kv(c, "Nodes", std::to_string(nodes.size()) + "  (" + S(o, "triggerCount") + " trigger)");
        std::string tags;
        for (JsonObjectConst tg : o["tags"].as<JsonArrayConst>()) tags += (tags.empty() ? "" : ", ") + S(tg, "name");
        kv(c, "Tags", tags.empty() ? "-" : tags);
        hooks.clear();
        for (JsonObjectConst n : nodes) {
            std::string type = S(n, "type");
            if (type.find("webhook") != std::string::npos && !(n["disabled"] | false)) {
                std::string path = S(n["parameters"], "path");
                std::string m = S(n["parameters"], "httpMethod");
                if (!path.empty()) hooks.push_back({m.empty() ? "GET" : m, path});
            }
        }
        lv_obj_t* a = row(box_);
        lv_obj_set_flex_flow(a, LV_FLEX_FLOW_ROW_WRAP);
        button(a, active ? "Unpublish" : "Publish", active ? Kind::Secondary : Kind::Primary, [this, active] {
            setActive(*this, id, !active, [this](bool ok) {
                if (ok) {
                    if (changed) changed();
                    refresh();
                }
            });
        });
        button(a, LV_SYMBOL_SHUFFLE "  Canvas", Kind::Primary, [this] { nav::push(makeFlow(id, name)); });
        button(a, "Executions", Kind::Secondary, [this] { nav::push(makeExecutions(id, name)); });
        button(a, "Tags", Kind::Secondary, [this] {
            auto p = std::make_unique<TagPicker>();
            p->wid = id;
            p->done = [this] {
                if (changed) changed();
                refresh();
            };
            nav::push(std::move(p));
        });
        button(a, "More...", Kind::Secondary, [this, arch] { more(arch); });
        for (auto& h2 : hooks) {
            std::string m = h2.first, p = h2.second;
            button(box_, LV_SYMBOL_PLAY "  Run webhook " + m + " /" + util::trunc(p, 24), Kind::Primary, [this, m, p] { runHook(m, p); });
        }
    }

    void runHook(const std::string& m, const std::string& path) {
        menu("Webhook environment", {{"Production  /webhook/"}, {"Test  /webhook-test/ (click Listen in n8n first)"}}, [this, m, path](int i) {
            std::string full = std::string(i == 0 ? "/webhook/" : "/webhook-test/") + path;  // encoded in runWebhook
            if (m == "GET" || m == "HEAD" || m == "DELETE") {
                runWebhook(*this, m, full, "");
            } else {
                prompt("Request body (JSON)", "{}", "{}", false, true, [this, m, full](const std::string& b) { runWebhook(*this, m, full, b); });
            }
        });
    }

    void more(bool arch) {
        std::vector<MenuItem> items = {{"Version history"}, {"Rename"}, {"Transfer to project"}, {"View JSON"}, {"Export JSON to SD"},
                                       {arch ? "Unarchive" : "Archive"}, {"Delete workflow", Tone::Danger}};
        menu(name, items, [this, arch](int i) {
            std::string base = "/workflows/" + util::urlEncode(id);
            switch (i) {
                case 0: history(); break;
                case 1:
                    prompt("Rename workflow", name, "name", false, false, [this, base](const std::string& v) { rename(v); });
                    break;
                case 2:
                    prompt("Destination project ID", "", "project id", false, false, [this, base](const std::string& v) {
                        JsonDocument d;
                        d["destinationProjectId"] = v;
                        std::string b;
                        serializeJson(d, b);
                        runCall(*this, "PUT", base + "/transfer", b, "Transferred", [this] { if (changed) changed(); });
                    });
                    break;
                case 3: showJson(*this, name, base); break;
                case 4: exportSd(); break;
                case 5:
                    runCall(*this, "POST", base + (arch ? "/unarchive" : "/archive"), "", arch ? "Unarchived" : "Archived", [this] {
                        if (changed) changed();
                        refresh();
                    });
                    break;
                case 6:
                    confirmDanger("Delete workflow", "Permanently delete \"" + name + "\"?", "Delete", [this, base] {
                        runCall(*this, "DELETE", base, "", "Deleted", [this] {
                            if (changed) changed();
                            nav::popPage(this);
                        });
                    });
                    break;
            }
        });
    }

    // n8n PUT needs the full workflow body: fetch, change the name, send back (settings trimmed to what the API accepts).
    void rename(const std::string& v) {
        std::string base = "/workflows/" + util::urlEncode(id);
        get(base, "",
            "{\"nodes\":true,\"connections\":true,\"settings\":{\"executionOrder\":true,\"timezone\":true,\"saveManualExecutions\":true,"
            "\"saveDataErrorExecution\":true,\"saveDataSuccessExecution\":true,\"executionTimeout\":true,\"errorWorkflow\":true,"
            "\"callerPolicy\":true,\"saveExecutionProgress\":true}}",
            [this, v, base](api::Response& r) {
                if (!r.ok()) {
                    toast(r.message(), Tone::Danger);
                    return;
                }
                JsonDocument d;
                d["name"] = v;
                d["nodes"] = r.json()["nodes"];
                d["connections"] = r.json()["connections"];
                if (r.json()["settings"].isNull()) d["settings"].to<JsonObject>();
                else d["settings"] = r.json()["settings"];
                std::string b;
                serializeJson(d, b);
                runCall(*this, "PUT", base, b, "Renamed", [this] {
                    if (changed) changed();
                    refresh();
                });
            });
    }

    void exportSd() {
        api::Request r;
        r.path = "/workflows/" + util::urlEncode(id);
        send(r, [this](api::Response& res) {
            if (!res.ok()) {
                toast(res.message(), Tone::Danger);
                return;
            }
            std::string safe;
            for (char c : id) if (isalnum((unsigned char)c) || c == '-' || c == '_') safe += c;  // server-supplied id -> safe file name
            std::string fn = "/workflow-" + safe + ".json";
            if (res.truncated) toast("Workflow larger than the size limit: not saved", Tone::Warning);
            else if (plat::sdAvailable() && plat::sdWrite(fn.c_str(), res.body)) toast("Saved " + fn, Tone::Success);
            else toast("No SD card or write failed", Tone::Danger);
        });
    }

    void history() {
        ListSpec s;
        s.title = "History";
        s.path = "/workflows/" + util::urlEncode(id) + "/history";
        s.paged = false;
        s.filter = "*";
        std::string wid = id;
        s.row = [](JsonObjectConst o) {
            RowView r;
            std::string vid = S(o, "versionId");
            r.title = vid;
            r.sub = util::isoToEpochAgo(S(o, "createdAt").c_str());
            if (!S(o, "name").empty()) r.sub = S(o, "name") + "  -  " + r.sub;
            return r;
        };
        s.onOpen = [wid](Page& p, JsonObjectConst o) {
            showJson(p, "Version", "/workflows/" + util::urlEncode(wid) + "/versions/" + util::urlEncode(S(o, "versionId")));
        };
        s.emptyTitle = "No history";
        nav::push(makeList(std::move(s)));
    }
};

PagePtr makeWorkflowDetail(const std::string& id, const std::string& name) {
    auto d = std::make_unique<WorkflowDetail>();
    d->id = id;
    d->name = name;
    d->changed = [] {};
    return d;
}

// Same filters as n8n's own list: published / unpublished, tags (match all, like the public API), archived. Defaults: both
// states visible, no tags, archived hidden.
struct WfFilter {
    bool pub = true, unpub = true, archived = false;
    std::vector<std::string> tags;
    bool isDefault() const { return pub && unpub && !archived && tags.empty(); }
};
static WfFilter wfFilter;

class TagFilterPicker : public Page {
public:
    std::vector<std::string> names;
    std::vector<bool> sel;
    std::function<void()> done;
    lv_obj_t* list = nullptr;
    void build() override {
        title = "Filter by tags";
        actions.push_back({LV_SYMBOL_PLUS, [this] { create(); }});
        list = scroller(body);
        for (auto& t : wfFilter.tags) {   // keep a selected tag visible even if it was deleted meanwhile
            names.push_back(t);
            sel.push_back(true);
        }
        load();
    }
    void load() {
        loading(list, "Loading tags...");
        get("/tags", "limit=250", "{\"data\":[{\"name\":true}]}", [this](api::Response& r) {
            if (!r.ok()) {
                lv_obj_clean(list);
                empty(list, LV_SYMBOL_WARNING, "Could not load tags", r.message());
                return;
            }
            for (JsonObjectConst o : r.json()["data"].as<JsonArrayConst>()) {
                std::string n = S(o, "name");
                if (std::find(names.begin(), names.end(), n) == names.end()) {
                    names.push_back(n);
                    sel.push_back(false);
                }
            }
            draw();
        });
    }
    void draw() {
        lv_obj_clean(list);
        if (names.empty()) empty(list, LV_SYMBOL_BARS, "No tags yet", "Tap + to create the first one.");
        for (size_t i = 0; i < names.size(); i++) {
            lv_obj_t* c = card(list);
            lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t* t = text(c, names[i]);
            lv_obj_set_flex_grow(t, 1);
            toggle(c, sel[i], [this, i](bool on) { sel[i] = on; });
        }
        lv_obj_t* r2 = row(list);
        lv_obj_set_flex_align(r2, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(r2, "Clear", Kind::Secondary, [this] {
            std::fill(sel.begin(), sel.end(), false);
            draw();
        });
        button(r2, "Apply", Kind::Primary, [this] {
            wfFilter.tags.clear();
            for (size_t i = 0; i < names.size(); i++)
                if (sel[i]) wfFilter.tags.push_back(names[i]);
            auto dn = done;
            nav::popPage(this);
            if (dn) dn();
        });
    }
    void create() {
        prompt("New tag", "", "Tag name", false, false, [this](const std::string& v) {
            if (v.empty()) return;
            JsonDocument d;
            d["name"] = v;
            std::string b;
            serializeJson(d, b);
            call("POST", "/tags", b, [this, v](api::Response& r) {
                if (!r.ok()) {
                    toast(r.message(), Tone::Danger);
                    return;
                }
                toast("Tag created", Tone::Success);
                if (std::find(names.begin(), names.end(), v) == names.end()) {
                    names.push_back(v);
                    sel.push_back(true);   // selected right away: that is why one creates it here
                }
                draw();
            });
        });
    }
};

static void workflowFilterBar(Page& owner, lv_obj_t* bar, std::function<void(bool)> apply) {
    Page* pg = &owner;
    // rebuild the controls after any change (the pointer stays valid: the bar lives as long as the page)
    auto build = std::make_shared<std::function<void()>>();
    *build = [pg, bar, apply, build] {
        lv_obj_clean(bar);
        chip(bar, "Published", wfFilter.pub, [build, apply] {
            wfFilter.pub = !wfFilter.pub;
            (*build)();
            apply(true);
        });
        chip(bar, "Unpublished", wfFilter.unpub, [build, apply] {
            wfFilter.unpub = !wfFilter.unpub;
            (*build)();
            apply(true);
        });
        std::string tl = wfFilter.tags.empty() ? "Tags" : "Tags (" + std::to_string(wfFilter.tags.size()) + ")";
        chip(bar, std::string(LV_SYMBOL_BARS) + " " + tl, !wfFilter.tags.empty(), [pg, build, apply] {
            auto tp = std::make_unique<TagFilterPicker>();
            tp->done = [build, apply] {
                (*build)();
                apply(true);
            };
            (void)pg;
            nav::push(std::move(tp));
        });
        lv_obj_t* cb = lv_checkbox_create(bar);
        lv_checkbox_set_text(cb, "Show archived");
        if (wfFilter.archived) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(cb, C().text, 0);
        lv_obj_set_style_text_font(cb, M().sm, 0);
        lv_obj_set_style_pad_left(cb, M().gap, 0);
        lv_obj_set_style_bg_color(cb, C().primary, (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(cb, C().border, LV_PART_INDICATOR);
        lv_obj_add_event_cb(
            cb,
            [](lv_event_t* e) {
                auto* f = static_cast<std::function<void()>*>(lv_event_get_user_data(e));
                if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) (*f)();
                else if (lv_event_get_code(e) == LV_EVENT_DELETE) delete f;
            },
            LV_EVENT_ALL,
            new std::function<void()>([cb, apply] {
                wfFilter.archived = lv_obj_has_state(cb, LV_STATE_CHECKED);
                apply(false);
            }));
        if (!wfFilter.isDefault())
            chip(bar, "Reset", false, [build, apply] {
                wfFilter = WfFilter();
                (*build)();
                apply(true);
            });
    };
    (*build)();
}

PagePtr makeWorkflows() {
    ListSpec s;
    s.title = "Workflows";
    s.path = "/workflows";
    s.query = "";
    s.autoFill = true;
    s.filterBar = workflowFilterBar;
    s.dynQuery = [] {
        std::string q;
        if (wfFilter.pub != wfFilter.unpub) q = wfFilter.pub ? "active=true" : "active=false";
        if (!wfFilter.tags.empty()) {
            std::string t;
            for (auto& x : wfFilter.tags) t += (t.empty() ? "" : ",") + x;
            q += std::string(q.empty() ? "" : "&") + "tags=" + util::urlEncode(t);
        }
        return q;
    };
    s.keep = [](JsonObjectConst o) {
        if (!wfFilter.pub && !wfFilter.unpub) return false;
        if (!wfFilter.archived && (o["isArchived"] | false)) return false;
        return true;
    };
    s.filter =
        "{\"data\":[{\"id\":true,\"name\":true,\"active\":true,\"isArchived\":true,\"updatedAt\":true,\"triggerCount\":true,"
        "\"tags\":[{\"name\":true}]}],\"nextCursor\":true}";
    s.row = [](JsonObjectConst o) {
        RowView r;
        r.title = S(o, "name");
        r.sub = "Updated " + util::isoToEpochAgo(S(o, "updatedAt").c_str());
        r.sw = (o["active"] | false) ? 1 : 0;
        int n = 0;
        for (JsonObjectConst t : o["tags"].as<JsonArrayConst>()) {
            if (n++ >= (M().compact ? 1 : 3)) break;
            r.badges.push_back({S(t, "name"), Tone::Neutral});
        }
        if (o["isArchived"] | false) {
            r.badges.clear();
            r.badges.push_back({"archived", Tone::Neutral});
        }
        return r;
    };
    s.onSwitch = [](Page& p, JsonObjectConst o, bool on, std::function<void(bool)> done) { setActive(p, S(o, "id"), on, done); };
    s.onOpen = [](Page& p, JsonObjectConst o) {
        auto d = std::make_unique<WorkflowDetail>();
        d->id = S(o, "id");
        d->name = S(o, "name");
        d->changed = [] {};
        nav::push(std::move(d));
    };
    s.emptyTitle = "No workflows";
    s.emptySub = "Create workflows in the n8n editor; manage and run them from here.";
    RowAction dummy;
    s.actions.push_back(dummy);  // keeps chevron visible
    static Form mk;
    mk = Form{"New empty workflow", "POST", "/workflows", "Create", {{"name", "Name", FormField::Text, "", {}, true}}};
    s.creates.push_back(mk);
    // creating requires nodes/connections/settings: wrap via custom form body template
    s.creates[0].fields.push_back({"nodes", "Nodes JSON", FormField::Json, "[]", {}, false});
    s.creates[0].fields.push_back({"connections", "Connections JSON", FormField::Json, "{}", {}, false});
    s.creates[0].fields.push_back({"settings", "Settings JSON", FormField::Json, "{}", {}, false});
    return makeList(std::move(s));
}

static Tone statusTone(const std::string& st) {
    if (st == "success") return Tone::Success;
    if (st == "error" || st == "crashed" || st == "failed") return Tone::Danger;
    if (st == "running" || st == "new") return Tone::Info;
    if (st == "waiting") return Tone::Warning;
    return Tone::Neutral;
}

PagePtr makeExecutions(const std::string& workflowId, const std::string& workflowName_) {
    ListSpec s;
    s.title = workflowId.empty() ? "Executions" : "Runs: " + workflowName_;
    s.path = "/executions";
    s.query = workflowId.empty() ? "" : "workflowId=" + util::urlEncode(workflowId);
    s.filter =
        "{\"data\":[{\"id\":true,\"workflowId\":true,\"status\":true,\"mode\":true,\"startedAt\":true,\"stoppedAt\":true,\"finished\":true,"
        "\"retryOf\":true}],\"nextCursor\":true}";
    s.chips = {{"All", ""}, {"Success", "status=success"}, {"Error", "status=error"}, {"Running", "status=running"}, {"Waiting", "status=waiting"}, {"Canceled", "status=canceled"}};
    s.afterLoad = [](Page& p, const std::vector<JsonObjectConst>& items, std::function<void()> redraw) {
        std::vector<std::string> ids;
        for (auto& o : items) ids.push_back(S(o, "workflowId"));
        resolveWorkflowNames(p, ids, redraw);
    };
    s.row = [](JsonObjectConst o) {
        RowView r;
        std::string st = S(o, "status");
        std::string wn = workflowName(S(o, "workflowId"));
        r.title = wn.empty() ? "Workflow " + S(o, "workflowId") : wn;
        time_t a = util::parseIso(S(o, "startedAt").c_str()), b = util::parseIso(S(o, "stoppedAt").c_str());
        r.sub = "#" + S(o, "id") + "  " + S(o, "mode") + "  " + util::ago(a) + (b ? "  " + util::duration(a, b) : "");
        r.hasDot = true;
        r.dot = statusTone(st);
        r.badges.push_back({st, statusTone(st)});
        return r;
    };
    s.detailPath = "/executions/{id}?includeData=true";
    auto when = [](const char* a, const char* b) {
        return [a, b](JsonObjectConst o) {
            std::string st = S(o, "status");
            return st == a || st == b;
        };
    };
    RowAction retry;
    retry.label = "Retry";
    retry.method = "POST";
    retry.path = "/executions/{id}/retry";
    retry.body = "{\"loadWorkflow\":false}";
    retry.when = [](JsonObjectConst o) { return S(o, "status") == "error" || S(o, "status") == "crashed"; };
    s.actions.push_back(retry);
    RowAction retry2 = retry;
    retry2.label = "Retry with latest workflow";
    retry2.body = "{\"loadWorkflow\":true}";
    s.actions.push_back(retry2);
    RowAction stop;
    stop.label = "Stop";
    stop.tone = Tone::Warning;
    stop.method = "POST";
    stop.path = "/executions/{id}/stop";
    stop.when = when("running", "waiting");
    s.actions.push_back(stop);
    RowAction del;
    del.label = "Delete";
    del.tone = Tone::Danger;
    del.method = "DELETE";
    del.path = "/executions/{id}";
    del.confirm = "Delete execution #{id}?";
    s.actions.push_back(del);
    RowAction open;
    open.label = "Open workflow";
    open.custom = [](Page&, JsonObjectConst o) {
        auto d = std::make_unique<WorkflowDetail>();
        d->id = S(o, "workflowId");
        d->name = workflowName(d->id);
        nav::push(std::move(d));
    };
    s.actions.push_back(open);
    if (workflowId.empty()) {
        Action stopAll{LV_SYMBOL_STOP, [] {
                           confirmDanger("Stop executions", "Stop all running and waiting executions?", "Stop all", [] {
                               api::call("POST", "/executions/stop", "{\"status\":[\"running\",\"waiting\"]}", [](api::Response& r) {
                                   toast(r.ok() ? "Stop requested" : r.message(), r.ok() ? Tone::Success : Tone::Danger);
                               });
                           });
                       }};
        s.extraActions.push_back(stopAll);
    }
    s.emptyTitle = "No executions";
    return makeList(std::move(s));
}

}  // namespace ui
