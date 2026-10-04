// Manage hub + curated resource screens built on the generic list/form engine.
#include <algorithm>

#include "../core/config.h"
#include "../core/util.h"
#include "res.h"

namespace ui {

static std::string S(JsonObjectConst o, const char* k) { return util::str(o[k]); }
static const char* kPage = "\"nextCursor\":true";

static std::string pg(const char* itemFilter) { return std::string("{\"data\":[") + itemFilter + "],\"nextCursor\":true}"; }

static void callView(Page& p, const char* method, const std::string& path, const std::string& body, const std::string& title) {
    toast("Working...");
    api::Request r;
    r.method = method;
    r.path = path;
    r.body = body;
    p.send(r, [title](api::Response& res) {
        if (!res.ok()) {
            toast(res.message(), Tone::Danger);
            return;
        }
        showJsonText(title, res.body.empty() ? std::string("{}") : res.body);
    });
}

class AdminPage : public Page {
public:
    void build() override {
        title = "Admin tools";
        lv_obj_t* l = scroller(body);
        auto item = [&](const char* ic, const char* name, const char* desc, Fn fn) {
            lv_obj_t* c = card(l);
            lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(c, C().surface2, LV_STATE_PRESSED);
            lv_obj_set_style_pad_column(c, M().pad, 0);
            label(c, ic, M().lg, C().primary);
            lv_obj_t* t = col(c);
            lv_obj_set_width(t, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(t, 1);
            lv_obj_set_style_pad_row(t, 2, 0);
            text(t, name);
            subtext(t, desc);
            onClick(c, std::move(fn));
        };
        item(LV_SYMBOL_EYE_OPEN, "Insights summary", "GET /insights/summary", [this] { callView(*this, "GET", "/insights/summary", "", "Insights"); });
        item(LV_SYMBOL_WARNING, "Security audit", "POST /audit (may take a while)", [this] {
            callView(*this, "POST", "/audit", "{}", "Security audit");
        });
        item(LV_SYMBOL_DIRECTORY, "Source control status", "GET /source-control/status", [this] {
            callView(*this, "GET", "/source-control/status", "", "Source control");
        });
        item(LV_SYMBOL_DOWNLOAD, "Source control pull", "POST /source-control/pull", [this] {
            confirm("Pull from Git", "Pull remote changes into this n8n instance (force)?", "Pull", false, [this] {
                callView(*this, "POST", "/source-control/pull", "{\"force\":true}", "Pull result");
            });
        });
        item(LV_SYMBOL_UPLOAD, "Source control push", "POST /source-control/push", [this] {
            prompt("Commit message", "Update from device", "message", false, false, [this](const std::string& m) {
                JsonDocument d;
                d["message"] = m;
                std::string b;
                serializeJson(d, b);
                callView(*this, "POST", "/source-control/push", b, "Push result");
            });
        });
        item(LV_SYMBOL_GPS, "Discover API capabilities", "GET /discover", [this] { callView(*this, "GET", "/discover", "", "Discover"); });
    }
};

static std::shared_ptr<Form> mkForm(Form f) { return std::make_shared<Form>(std::move(f)); }

static PagePtr projectUsers(const std::string& pid, const std::string& pname) {
    ListSpec s;
    s.title = pname + " - users";
    s.path = "/projects/" + util::urlEncode(pid) + "/users";
    s.filter = "*";
    s.row = [](JsonObjectConst o) {
        RowView r;
        r.title = S(o, "email");
        if (r.title.empty()) r.title = S(o, "id");
        r.sub = S(o, "firstName") + " " + S(o, "lastName");
        r.badges.push_back({S(o, "role"), Tone::Info});
        return r;
    };
    auto role = mkForm({"Change role", "PATCH", "/projects/" + pid + "/users/{id}", "Save",
                        {{"role", "Role", FormField::Choice, "project:editor", {"project:admin", "project:editor", "project:viewer"}, true}}, false});
    RowAction chg;
    chg.label = "Change role";
    chg.form = role;
    s.actions.push_back(chg);
    RowAction rm;
    rm.label = "Remove from project";
    rm.tone = Tone::Danger;
    rm.method = "DELETE";
    rm.path = "/projects/" + pid + "/users/{id}";
    rm.confirm = "Remove {email} from the project?";
    s.actions.push_back(rm);
    s.creates.push_back(*mkForm({"Add users", "POST", "/projects/" + pid + "/users", "Add",
                                 {{"relations", "Relations JSON", FormField::Json, "[{\"userId\":\"\",\"role\":\"project:editor\"}]", {}, true}}, false}));
    return makeList(std::move(s));
}

static PagePtr folders(const std::string& pid, const std::string& pname) {
    ListSpec s;
    s.title = pname + " - folders";
    s.path = "/projects/" + util::urlEncode(pid) + "/folders";
    s.filter = "*";
    s.row = [](JsonObjectConst o) {
        RowView r;
        r.title = S(o, "name");
        r.sub = S(o, "id");
        return r;
    };
    RowAction ren;
    ren.label = "Rename";
    ren.form = mkForm({"Rename folder", "PATCH", "/projects/" + pid + "/folders/{id}", "Save", {{"name", "Name", FormField::Text, "", {}, true}}, false});
    s.actions.push_back(ren);
    RowAction del;
    del.label = "Delete";
    del.tone = Tone::Danger;
    del.method = "DELETE";
    del.path = "/projects/" + pid + "/folders/{id}";
    del.confirm = "Delete folder {name}?";
    s.actions.push_back(del);
    s.creates.push_back(*mkForm({"New folder", "POST", "/projects/" + pid + "/folders", "Create",
                                 {{"name", "Name", FormField::Text, "", {}, true}, {"parentFolderId", "Parent folder ID (optional)", FormField::Text, "", {}, false}}, false}));
    return makeList(std::move(s));
}

static PagePtr tableRows(const std::string& tid, const std::string& tname) {
    ListSpec s;
    s.title = tname + " - rows";
    s.path = "/data-tables/" + util::urlEncode(tid) + "/rows";
    s.filter = "*";
    s.row = [](JsonObjectConst o) {
        RowView r;
        std::string t;
        serializeJson(o, t);
        r.title = util::trunc(t, 90);
        r.sub = "id " + S(o, "id");
        return r;
    };
    s.creates.push_back(*mkForm({"Add rows", "POST", "/data-tables/" + tid + "/rows", "Add",
                                 {{"data", "Rows JSON", FormField::Json, "[{}]", {}, true},
                                  {"returnType", "Return", FormField::Choice, "count", {"count", "id", "all"}, false}}, false}));
    RowAction del;
    del.label = "Delete row";
    del.tone = Tone::Danger;
    del.method = "DELETE";
    // filter={"type":"and","filters":[{"columnName":"id","condition":"eq","value":<id>}]}
    del.path = "/data-tables/" + tid + "/rows/delete?filter=%7B%22type%22%3A%22and%22%2C%22filters%22%3A%5B%7B%22columnName%22%3A%22id%22%2C%22condition%22%3A%22eq%22%2C%22value%22%3A{id}%7D%5D%7D";
    del.confirm = "Delete row {id}?";
    s.actions.push_back(del);
    return makeList(std::move(s));
}

PagePtr makeResource(const std::string& id) {
    ListSpec s;
    if (id == "credentials") {
        s.title = "Credentials";
        s.path = "/credentials";
        s.filter = pg("{\"id\":true,\"name\":true,\"type\":true,\"updatedAt\":true,\"isManaged\":true}");
        s.row = [](JsonObjectConst o) {
            RowView r;
            r.title = S(o, "name");
            r.sub = S(o, "type") + "  -  " + util::isoToEpochAgo(o["updatedAt"] | "");
            return r;
        };
        static Form mk, ren, move;
        mk = Form{"New credential", "POST", "/credentials", "Create",
                  {{"name", "Name", FormField::Text, "", {}, true}, {"type", "Type (e.g. httpHeaderAuth)", FormField::Text, "", {}, true},
                   {"data", "Data (JSON)", FormField::Json, "{}", {}, true, "", true}}};
        ren = Form{"Edit credential", "PATCH", "/credentials/{id}", "Save",
                   {{"name", "Name", FormField::Text, "", {}, false}, {"data", "Data (JSON, optional)", FormField::Json, "", {}, false, "", true}}};
        move = Form{"Transfer credential", "PUT", "/credentials/{id}/transfer", "Transfer",
                    {{"destinationProjectId", "Destination project ID", FormField::Text, "", {}, true}}};
        s.creates.push_back(mk);
        s.actions.push_back({"Test connection", Tone::Neutral, "POST", "/credentials/{id}/test", "{}", "", nullptr, nullptr, nullptr});
        s.actions.push_back({"Edit", Tone::Neutral, "", "", "", "", ref(ren), nullptr, nullptr});
        s.actions.push_back({"Transfer to project", Tone::Neutral, "", "", "", "", ref(move), nullptr, nullptr});
        s.actions.push_back({"View type schema", Tone::Neutral, "", "", "", "", nullptr, nullptr,
                             [](Page& p, JsonObjectConst o) { showJson(p, "Schema " + S(o, "type"), "/credentials/schema/" + util::urlEncode(S(o, "type"))); }});
        s.actions.push_back({"Delete", Tone::Danger, "DELETE", "/credentials/{id}", "", "Delete credential {name}? Workflows using it will break.", nullptr, nullptr, nullptr});
        s.detailPath = "";
        s.emptyTitle = "No credentials";
    } else if (id == "tags") {
        s.title = "Tags";
        s.path = "/tags";
        s.filter = pg("{\"id\":true,\"name\":true,\"updatedAt\":true}");
        s.row = [](JsonObjectConst o) {
            RowView r;
            r.title = S(o, "name");
            r.sub = S(o, "id");
            return r;
        };
        static Form mk, ren;
        mk = Form{"New tag", "POST", "/tags", "Create", {{"name", "Name", FormField::Text, "", {}, true}}};
        ren = Form{"Rename tag", "PUT", "/tags/{id}", "Save", {{"name", "Name", FormField::Text, "", {}, true}}};
        s.creates.push_back(mk);
        s.actions.push_back({"Rename", Tone::Neutral, "", "", "", "", ref(ren), nullptr, nullptr});
        s.actions.push_back({"Delete", Tone::Danger, "DELETE", "/tags/{id}", "", "Delete tag {name}?", nullptr, nullptr, nullptr});
        s.emptyTitle = "No tags";
    } else if (id == "variables") {
        s.title = "Variables";
        s.path = "/variables";
        s.filter = pg("{\"id\":true,\"key\":true,\"value\":true,\"type\":true}");
        s.row = [](JsonObjectConst o) {
            RowView r;
            r.title = S(o, "key");
            r.sub = util::trunc(S(o, "value"), 60);
            return r;
        };
        static Form mk, ed;
        mk = Form{"New variable", "POST", "/variables", "Create",
                  {{"key", "Key", FormField::Text, "", {}, true}, {"value", "Value", FormField::Multiline, "", {}, true}}};
        ed = Form{"Edit variable", "PUT", "/variables/{id}", "Save",
                  {{"key", "Key", FormField::Text, "", {}, true}, {"value", "Value", FormField::Multiline, "", {}, true}}};
        s.creates.push_back(mk);
        s.actions.push_back({"Edit", Tone::Neutral, "", "", "", "", ref(ed), nullptr, nullptr});
        s.actions.push_back({"Delete", Tone::Danger, "DELETE", "/variables/{id}", "", "Delete variable {key}?", nullptr, nullptr, nullptr});
        s.emptyTitle = "No variables";
    } else if (id == "users") {
        s.title = "Users";
        s.path = "/users";
        s.query = "includeRole=true";
        s.filter = pg("{\"id\":true,\"email\":true,\"firstName\":true,\"lastName\":true,\"isPending\":true,\"role\":true}");
        s.row = [](JsonObjectConst o) {
            RowView r;
            r.title = S(o, "email");
            r.sub = S(o, "firstName") + " " + S(o, "lastName");
            r.badges.push_back({S(o, "role"), Tone::Info});
            if (o["isPending"] | false) r.badges.push_back({"pending", Tone::Warning});
            return r;
        };
        static Form mk, role;
        mk = Form{"Invite user", "POST", "/users", "Invite",
                  {{"email", "Email", FormField::Text, "", {}, true}, {"role", "Role", FormField::Choice, "global:member", {"global:member", "global:admin"}, true}},
                  true};
        role = Form{"Change role", "PATCH", "/users/{id}/role", "Save",
                    {{"newRoleName", "Role", FormField::Choice, "global:member", {"global:member", "global:admin"}, true}}};
        s.creates.push_back(mk);
        s.actions.push_back({"Change role", Tone::Neutral, "", "", "", "", ref(role), nullptr, nullptr});
        s.actions.push_back({"Delete user", Tone::Danger, "DELETE", "/users/{id}", "", "Delete {email}?", nullptr, nullptr, nullptr});
        s.emptyTitle = "No users";
    } else if (id == "projects") {
        s.title = "Projects";
        s.path = "/projects";
        s.filter = pg("{\"id\":true,\"name\":true,\"type\":true}");
        s.row = [](JsonObjectConst o) {
            RowView r;
            r.title = S(o, "name");
            r.sub = S(o, "id");
            r.badges.push_back({S(o, "type"), Tone::Neutral});
            return r;
        };
        static Form mk, ren;
        mk = Form{"New project", "POST", "/projects", "Create", {{"name", "Name", FormField::Text, "", {}, true}}};
        ren = Form{"Rename project", "PUT", "/projects/{id}", "Save", {{"name", "Name", FormField::Text, "", {}, true}}};
        s.creates.push_back(mk);
        s.actions.push_back({"Members", Tone::Neutral, "", "", "", "", nullptr, nullptr,
                             [](Page&, JsonObjectConst o) { nav::push(projectUsers(S(o, "id"), S(o, "name"))); }});
        s.actions.push_back({"Folders", Tone::Neutral, "", "", "", "", nullptr, nullptr,
                             [](Page&, JsonObjectConst o) { nav::push(folders(S(o, "id"), S(o, "name"))); }});
        s.actions.push_back({"Rename", Tone::Neutral, "", "", "", "", ref(ren), nullptr, nullptr});
        s.actions.push_back({"Delete", Tone::Danger, "DELETE", "/projects/{id}", "", "Delete project {name}?", nullptr, nullptr, nullptr});
        s.emptyTitle = "No projects";
    } else if (id == "datatables") {
        s.title = "Data tables";
        s.path = "/data-tables";
        s.filter = "*";
        s.row = [](JsonObjectConst o) {
            RowView r;
            r.title = S(o, "name");
            r.sub = S(o, "id");
            return r;
        };
        static Form mk, ren, col;
        mk = Form{"New data table", "POST", "/data-tables", "Create",
                  {{"name", "Name", FormField::Text, "", {}, true},
                   {"columns", "Columns JSON", FormField::Json, "[{\"name\":\"title\",\"type\":\"string\"}]", {}, true}}};
        ren = Form{"Rename table", "PATCH", "/data-tables/{id}", "Save", {{"name", "Name", FormField::Text, "", {}, true}}};
        col = Form{"Add column", "POST", "/data-tables/{id}/columns", "Add",
                   {{"name", "Name", FormField::Text, "", {}, true},
                    {"type", "Type", FormField::Choice, "string", {"string", "number", "boolean", "date"}, true}}};
        s.creates.push_back(mk);
        s.actions.push_back({"Rows", Tone::Neutral, "", "", "", "", nullptr, nullptr,
                             [](Page&, JsonObjectConst o) { nav::push(tableRows(S(o, "id"), S(o, "name"))); }});
        s.actions.push_back({"Columns (JSON)", Tone::Neutral, "", "", "", "", nullptr, nullptr,
                             [](Page& p, JsonObjectConst o) { showJson(p, "Columns", "/data-tables/" + util::urlEncode(S(o, "id")) + "/columns"); }});
        s.actions.push_back({"Add column", Tone::Neutral, "", "", "", "", ref(col), nullptr, nullptr});
        s.actions.push_back({"Rename", Tone::Neutral, "", "", "", "", ref(ren), nullptr, nullptr});
        s.actions.push_back({"Clear all rows", Tone::Danger, "DELETE", "/data-tables/{id}/rows/clear", "", "Delete ALL rows of {name}?", nullptr, nullptr, nullptr});
        s.actions.push_back({"Delete table", Tone::Danger, "DELETE", "/data-tables/{id}", "", "Delete table {name}?", nullptr, nullptr, nullptr});
        s.emptyTitle = "No data tables";
    } else if (id == "roles") {
        s.title = "Roles";
        s.path = "/roles";
        s.paged = false;
        s.filter = "*";
        s.row = [](JsonObjectConst o) {
            RowView r;
            r.title = S(o, "displayName");
            r.sub = S(o, "slug");
            r.badges.push_back({S(o, "roleType"), Tone::Neutral});
            if (o["systemRole"] | false) r.badges.push_back({"system", Tone::Info});
            return r;
        };
        s.actions.push_back({"Delete role", Tone::Danger, "DELETE", "/roles/{slug}", "", "Delete role {displayName}?",
                             nullptr, [](JsonObjectConst o) { return !(o["systemRole"] | false); }, nullptr});
        s.detailPath = "/roles/{slug}";
    } else if (id == "packages") {
        s.title = "Community packages";
        s.path = "/community-packages";
        s.paged = false;
        s.dataKey = "";
        s.filter = "*";
        s.row = [](JsonObjectConst o) {
            RowView r;
            r.title = S(o, "packageName");
            if (r.title.empty()) r.title = S(o, "name");
            r.sub = "v" + S(o, "installedVersion");
            return r;
        };
        static Form mk;
        mk = Form{"Install package", "POST", "/community-packages", "Install", {{"name", "npm package name", FormField::Text, "", {}, true}}};
        s.creates.push_back(mk);
        s.actions.push_back({"Update", Tone::Neutral, "PATCH", "/community-packages/{packageName}", "{}", "", nullptr, nullptr, nullptr});
        s.actions.push_back({"Uninstall", Tone::Danger, "DELETE", "/community-packages/{packageName}", "", "Uninstall {packageName}?", nullptr, nullptr, nullptr});
    } else if (id == "admin") {
        return std::make_unique<AdminPage>();
    } else {
        return makeList(ListSpec());
    }
    return makeList(std::move(s));
}

class ManagePage : public Page {
public:
    void build() override {
        title = "Manage";
        lv_obj_t* l = scroller(body);
        lv_obj_set_flex_flow(l, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(l, M().gap, 0);
        lv_obj_set_style_pad_row(l, M().gap, 0);
        struct T {
            const char* ic;
            const char* name;
            const char* desc;
            const char* id;
        };
        static const T tiles[] = {
            {LV_SYMBOL_EYE_CLOSE, "Credentials", "Create, test, transfer, delete", "credentials"},
            {LV_SYMBOL_BARS, "Tags", "Organise workflows", "tags"},
            {LV_SYMBOL_EDIT, "Variables", "Instance variables", "variables"},
            {LV_SYMBOL_ENVELOPE, "Users", "Invite, roles, remove", "users"},
            {LV_SYMBOL_DIRECTORY, "Projects", "Members and folders", "projects"},
            {LV_SYMBOL_LIST, "Data tables", "Tables, columns, rows", "datatables"},
            {LV_SYMBOL_SETTINGS, "Roles", "Custom and system roles", "roles"},
            {LV_SYMBOL_DOWNLOAD, "Packages", "Community nodes", "packages"},
            {LV_SYMBOL_WARNING, "Admin tools", "Audit, Git, insights", "admin"},
        };
        int w = lv_display_get_horizontal_resolution(nullptr);
        int avail = M().compact ? w - 2 * M().pad : w - M().navW - 2 * M().pad;
        int cols = M().compact ? 2 : 3;
        int tw = (avail - (cols - 1) * M().gap) / cols;
        for (auto& t : tiles) {
            lv_obj_t* c = card(l);
            lv_obj_set_width(c, tw);
            lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(c, C().surface2, LV_STATE_PRESSED);
            label(c, t.ic, M().xl, C().primary);
            text(c, t.name, M().lg);
            subtext(c, t.desc);
            std::string rid = t.id;
            onClick(c, [rid] { nav::push(makeResource(rid)); });
        }
        if (M().compact) {
            struct X { const char* ic; const char* name; const char* desc; int sec; };
            static const X more[] = {{LV_SYMBOL_UPLOAD, "Webhooks", "Call workflows", nav::Webhooks}, {LV_SYMBOL_CHARGE, "AI & Agents", "Chat with AI workflows", nav::Agents}, {LV_SYMBOL_EDIT, "API Explorer", "Every endpoint", nav::Explorer}};
            for (auto& t : more) {
                lv_obj_t* c = card(l);
                lv_obj_set_width(c, tw);
                lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_style_bg_color(c, C().surface2, LV_STATE_PRESSED);
                label(c, t.ic, M().xl, C().primary);
                text(c, t.name, M().lg);
                subtext(c, t.desc);
                int sec = t.sec;
                onClick(c, [sec] { nav::section(sec); });
            }
        }
    }
};
PagePtr makeManage() { return std::make_unique<ManagePage>(); }

}  // namespace ui
