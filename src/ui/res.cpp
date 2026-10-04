#include "res.h"

#include <algorithm>
#include <map>

#include "../core/config.h"
#include "../core/plat.h"
#include "../core/util.h"

namespace ui {

void showJsonText(const std::string& title, std::string raw, bool truncated) {
    std::string txt = util::pretty(raw, 24000);
    auto keep = std::make_shared<std::string>(std::move(raw));  // shared: the save button must not copy multi-MB bodies
    viewer(title, txt, [keep, truncated] {
        std::string path = "/n8n-" + std::to_string(plat::millis()) + ".json";
        if (truncated) toast("Response was cut by the size limit: not saved", Tone::Warning);
        else if (plat::sdAvailable() && plat::sdWrite(path.c_str(), *keep)) toast("Saved " + path, Tone::Success);
        else toast("No SD card / write failed", Tone::Danger);
    });
}

void showJson(Page& owner, const std::string& title, const std::string& path) {
    toast("Loading...");
    api::Request r;
    r.path = path;
    r.filter = "";
    // keep raw text: use no filter, store body
    owner.send(r, [title](api::Response& res) {
        if (!res.ok()) {
            toast(res.message(), Tone::Danger);
            return;
        }
        std::string raw;
        if (!res.body.empty()) raw = std::move(res.body);
        else serializeJson(*res.doc, raw);
        showJsonText(title, std::move(raw), res.truncated);
    });
}

void runCall(Page& owner, const std::string& method, const std::string& path, const std::string& body,
             const std::string& okMsg, std::function<void()> done) {
    owner.call(method.c_str(), path, body, [okMsg, done](api::Response& res) {
        if (res.ok()) {
            toast(okMsg.empty() ? "Done" : okMsg, Tone::Success);
            if (done) done();
        } else {
            toast(res.message(), Tone::Danger);
        }
    });
}

class FormPage : public Page {
public:
    Form f;
    std::string resolvedPath;
    std::vector<std::string> vals;
    std::function<void()> onDone;
    lv_obj_t* list = nullptr;
    bool busy = false;

    void build() override {
        title = f.title;
        list = scroller(body);
        render();
    }

    void render() {
        lv_obj_clean(list);
        for (size_t i = 0; i < f.fields.size(); i++) {
            const FormField& ff = f.fields[i];
            std::string label = ff.label + (ff.required ? " *" : "");
            if (ff.kind == FormField::Bool) {
                lv_obj_t* c = card(list);
                lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                lv_obj_t* t = text(c, label);
                lv_obj_set_flex_grow(t, 1);
                toggle(c, vals[i] == "true", [this, i](bool on) { vals[i] = on ? "true" : "false"; });
                continue;
            }
            std::string shown = vals[i].empty() ? (ff.hint.empty() ? "tap to set" : ff.hint) : vals[i];
            if ((ff.kind == FormField::Secret || ff.mask) && !vals[i].empty()) shown = std::string(std::min<size_t>(vals[i].size(), 12), '*');
            if (ff.kind == FormField::Json || ff.kind == FormField::Multiline) shown = util::trunc(std::string(shown), 40);
            field(list, label, shown, [this, i] { edit(i); });
        }
        lv_obj_t* r = row(list);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_top(r, M().pad, 0);
        button(r, "Cancel", Kind::Secondary, [] { nav::pop(); });
        button(r, f.submit, Kind::Primary, [this] { submit(); });
    }

    void edit(size_t i) {
        const FormField& ff = f.fields[i];
        if (ff.kind == FormField::Choice) {
            std::vector<MenuItem> items;
            for (auto& c : ff.choices) items.push_back({c});
            std::shared_ptr<bool> l = life;
            menu(ff.label, items, [this, l, i](int k) {
                if (!*l) return;
                vals[i] = f.fields[i].choices[k];
                render();
            });
            return;
        }
        bool ml = ff.kind == FormField::Json || ff.kind == FormField::Multiline;
        std::shared_ptr<bool> l = life;
        prompt(ff.label, vals[i], ff.hint, ff.kind == FormField::Secret, ml, [this, l, i](const std::string& v) {
            if (!*l) return;
            vals[i] = v;
            render();
        });
    }

    void submit() {
        JsonDocument d;
        JsonObject o = d.to<JsonObject>();
        for (size_t i = 0; i < f.fields.size(); i++) {
            const FormField& ff = f.fields[i];
            const std::string& v = vals[i];
            if (v.empty() && ff.kind != FormField::Bool) {
                if (ff.required) {
                    toast(ff.label + " is required", Tone::Warning);
                    return;
                }
                continue;
            }
            switch (ff.kind) {
                case FormField::Int: o[ff.key] = atoll(v.c_str()); break;
                case FormField::Bool: o[ff.key] = (v == "true"); break;
                case FormField::Json: {
                    JsonDocument j;
                    if (deserializeJson(j, v)) {
                        toast(ff.label + ": invalid JSON", Tone::Danger);
                        return;
                    }
                    o[ff.key] = j;
                    break;
                }
                default: o[ff.key] = v; break;
            }
        }
        std::string body;
        if (f.wrapArray) {
            JsonDocument w;
            w.add<JsonVariant>().set(d);
            serializeJson(w, body);
        } else {
            serializeJson(d, body);
        }
        if (busy) return;
        busy = true;
        auto done = onDone;
        call(f.method.c_str(), resolvedPath, body, [this, done](api::Response& res) {
            busy = false;
            if (res.ok()) {
                toast("Saved", Tone::Success);
                nav::popPage(this);
                if (done) done();
            } else {
                toast(res.message(), Tone::Danger);
            }
        });
    }
};

void openForm(const Form& f, JsonVariantConst prefill, std::function<void()> onDone) {
    auto p = std::make_unique<FormPage>();
    p->f = f;
    p->onDone = std::move(onDone);
    p->resolvedPath = util::subst(f.path, prefill);
    for (auto& ff : f.fields) {
        JsonVariantConst v = prefill[ff.key.c_str()];
        std::string s;
        if (!v.isNull()) {
            if (ff.kind == FormField::Json) serializeJson(v, s);
            else s = util::str(v);
        } else {
            s = ff.def;
        }
        p->vals.push_back(s);
    }
    nav::push(std::move(p));
}

static std::map<std::string, uint32_t> lastFetch;   // when a list was last confirmed with the server

class ListPage : public Page {
public:
    ListSpec s;
    std::vector<std::shared_ptr<JsonDocument>> chunks;
    std::vector<JsonObjectConst> items;
    std::string cursor, needle;
    bool loading = false, loaded = false, more = false;
    std::string err;
    int chipSel = 0, gen = 0;
    std::string shownKey;   // cacheKey() of the request whose rows are on screen
    uint32_t loadedAt = 0;
    bool prefetched = false, reloadQueued = false;
    lv_obj_t *list = nullptr, *filterLbl = nullptr, *chipRow = nullptr, *fbar = nullptr;
    int fillPages = 0;

    void build() override {
        title = s.title;
        if (!s.creates.empty())
            actions.push_back({LV_SYMBOL_PLUS, [this] { create(); }});
        for (auto& a : s.extraActions) actions.push_back(a);
        actions.push_back({LV_SYMBOL_REFRESH, [this] { load(true); }});
        if (!s.chips.empty()) {
            chipRow = row(body);
            lv_obj_set_flex_flow(chipRow, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_style_pad_hor(chipRow, M().pad, 0);
            lv_obj_set_style_pad_top(chipRow, M().gap, 0);
            renderChips();
        }
        if (s.filterBar) {
            fbar = row(body);
            lv_obj_set_flex_flow(fbar, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_style_pad_hor(fbar, M().pad, 0);
            lv_obj_set_style_pad_top(fbar, M().gap, 0);
            s.filterBar(*this, fbar, [this](bool reload) {
                if (reload) {
                    fillPages = 0;
                    load(true);
                } else {
                    fillPages = 0;
                    render();
                }
            });
        }
        if (s.search) {
            lv_obj_t* bar = row(body);
            lv_obj_set_style_pad_hor(bar, M().pad, 0);
            lv_obj_set_style_pad_top(bar, M().gap, 0);
            lv_obj_t* f = card(bar);
            lv_obj_set_flex_grow(f, 1);
            lv_obj_set_width(f, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_ver(f, M().compact ? 5 : 12, 0);
            lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
            lv_obj_add_flag(f, LV_OBJ_FLAG_CLICKABLE);
            filterLbl = subtext(f, "Filter loaded items");
            lv_obj_set_flex_grow(filterLbl, 1);
            onClick(f, [this] {
                prompt("Filter", needle, "text", false, false, [this](const std::string& v) {
                    needle = v;
                    setText(filterLbl, needle.empty() ? "Filter loaded items" : needle);
                    render();
                });
            });
        }
        list = scroller(body);
        if (!s.prefetch) {   // instant paint from the RAM/SD cache (also right after a reboot), revalidated below
            if (auto d = api::peek(makeRequest(true))) {
                absorb(d, true);
                loaded = true;
                render();
                auto lf = lastFetch.find(cacheKey());
                if (lf != lastFetch.end() && plat::millis() - lf->second < 20000) return;   // confirmed a moment ago
            }
        }
        load(true);
    }

    // the request load() sends for the first page (also used to look the answer up in the cache)
    api::Request makeRequest(bool reset) const {
        api::Request rq;
        rq.path = s.path;
        rq.filter = s.filter;
        std::string q = s.query;
        if (!s.chips.empty() && !s.chips[chipSel].second.empty()) {
            if (!q.empty()) q += "&";
            q += s.chips[chipSel].second;
        }
        if (s.dynQuery) {
            std::string d = s.dynQuery();
            if (!d.empty()) {
                if (!q.empty()) q += "&";
                q += d;
            }
        }
        if (s.paged) {
            if (!q.empty()) q += "&";
            q += "limit=" + std::to_string(cfg::s().pageSize);
            if (!reset && !cursor.empty()) q += "&cursor=" + util::urlEncode(cursor);
        }
        rq.query = q;
        return rq;
    }

    // take the rows (and next cursor) from a response document
    void absorb(const std::shared_ptr<JsonDocument>& doc, bool reset) {
        if (reset) shownKey = cacheKey();
        if (reset) {
            chunks.clear();
            items.clear();
        }
        chunks.push_back(doc);
        JsonVariantConst root = doc->as<JsonVariantConst>();
        JsonVariantConst arr = s.dataKey.empty() ? root : root[s.dataKey.c_str()];
        if (!arr.is<JsonArrayConst>() && root.is<JsonArrayConst>()) arr = root;
        for (JsonObjectConst o : arr.as<JsonArrayConst>()) items.push_back(o);
        const char* nc = root["nextCursor"] | (const char*)nullptr;
        cursor = nc ? nc : "";
        more = !cursor.empty();
    }

    std::string cacheKey() const { return s.path + "?" + s.query + "|" + std::to_string(chipSel) + "|" + std::to_string(cfg::s().pageSize) + "|" + (s.dynQuery ? s.dynQuery() : ""); }

    void renderChips() {
        lv_obj_clean(chipRow);
        for (size_t i = 0; i < s.chips.size(); i++)
            chip(chipRow, s.chips[i].first, (int)i == chipSel, [this, i] {
                chipSel = (int)i;
                renderChips();
                load(true);
            });
    }

    bool autoRefresh() override { return true; }
    void refresh() override { load(true); }

    void create() {
        if (s.creates.size() == 1) {
            openForm(s.creates[0], JsonVariantConst(), [this] { load(true); });
            return;
        }
        std::vector<MenuItem> m;
        for (auto& f : s.creates) m.push_back({f.title});
        menu("Create", m, [this](int i) { openForm(s.creates[i], JsonVariantConst(), [this] { load(true); }); });
    }

    void load(bool reset) {
        if (loading) {
            if (reset) reloadQueued = true;  // e.g. a filter chip tapped mid-load: run again when this one finishes
            return;
        }
        if (!prefetched && s.prefetch) {
            prefetched = true;
            loading = true;
            lv_obj_clean(list);
            loading_ = ui::loading(list, "Loading...");
            std::shared_ptr<bool> l = life;
            s.prefetch(*this, [this, l] {
                if (!*l) return;
                loading = false;
                load(true);
            });
            return;
        }
        loading = true;
        if (reset) {
            if (!loaded) {
                lv_obj_clean(list);
                loading_ = ui::loading(list, "Loading...");
            }
        }
        bool wasShown = loaded && !items.empty() && reset && shownKey == cacheKey();   // rows from the cache are already on screen
        send(makeRequest(reset), [this, reset, wasShown](api::Response& res) {
            loading = false;
            loaded = true;
            if (reloadQueued) {
                reloadQueued = false;
                load(true);
                return;
            }
            if (!res.ok()) {
                err = res.message();
                if (reset && !wasShown) {
                    chunks.clear();
                    items.clear();
                }
                if (!wasShown) render();
                else toast("Offline: showing saved data (" + res.message() + ")", Tone::Warning);
                return;
            }
            err.clear();
            loadedAt = plat::millis();
            if (reset) lastFetch[cacheKey()] = loadedAt;
            if (res.notModified && wasShown) {   // server confirmed the cached rows: nothing to redraw (just finish the extras)
                if (s.afterLoad) {
                    std::shared_ptr<bool> l = life;
                    s.afterLoad(*this, items, [this, l] {
                        if (*l) render();
                    });
                }
                if (s.autoFill) render();   // the cached pages are still valid: carry on filling from the saved cursor
                return;
            }
            absorb(res.doc, reset);
            render();
            if (s.afterLoad) {
                std::shared_ptr<bool> l = life;
                s.afterLoad(*this, items, [this, l] {
                    if (*l) render();
                });
            }
        });
    }

    lv_obj_t* loading_ = nullptr;

    static std::string lower(std::string x) {
        for (auto& c : x) c = (char)tolower((unsigned char)c);
        return x;
    }

    void render() {
        gen++;  // invalidates callbacks that captured row widgets
        lv_obj_clean(list);
        loading_ = nullptr;
        if (!err.empty()) {
            empty(list, LV_SYMBOL_WARNING, "Could not load", err);
            button(list, "Retry", Kind::Primary, [this] { load(true); });
            return;
        }
        int shown = 0;
        std::string nl = lower(needle);
        for (size_t i = 0; i < items.size(); i++) {
            if (s.keep && !s.keep(items[i])) continue;
            RowView rv = s.row(items[i]);
            if (!nl.empty() && lower(rv.title + " " + rv.sub).find(nl) == std::string::npos) continue;
            shown++;
            addRow(i, rv);
        }
        if (!shown && !(s.autoFill && more)) empty(list, LV_SYMBOL_LIST, needle.empty() ? s.emptyTitle : "No match", needle.empty() ? s.emptySub : "");
        if (s.autoFill && more && !loading && shown < (int)cfg::s().pageSize && fillPages < 40) {   // hidden rows ate the page: fetch the next one
            fillPages++;
            if (!shown) loading_ = ui::loading(list, "Loading...");
            load(false);
            return;
        }
        if (more) {
            lv_obj_t* r = row(list);
            lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            button(r, "Load more", Kind::Secondary, [this] { load(false); });
        }
    }

    void addRow(size_t idx, const RowView& rv) {
        lv_obj_t* c = card(list);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(c, M().gap, 0);
        lv_obj_set_style_pad_ver(c, M().compact ? 6 : 14, 0);
        lv_obj_set_style_bg_color(c, C().surface2, LV_STATE_PRESSED);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        if (rv.hasDot) dot(c, rv.dot);
        lv_obj_t* tc = col(c);
        lv_obj_set_width(tc, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(tc, 1);
        lv_obj_set_style_pad_row(tc, 2, 0);
        lv_obj_t* t = text(tc, rv.title.empty() ? "(unnamed)" : rv.title, M().md);
        ellipsis(t);
        lv_obj_set_width(t, LV_PCT(100));
        if (!rv.sub.empty()) {
            lv_obj_t* st = subtext(tc, rv.sub);
            ellipsis(st);
            lv_obj_set_width(st, LV_PCT(100));
        }
        for (auto& b : rv.badges) badge(c, b.first, b.second);
        if (rv.sw >= 0 && s.onSwitch) {
            JsonObjectConst item = items[idx];
            lv_obj_t* sw = nullptr;
            sw = toggle(c, rv.sw == 1, nullptr);
            int g = gen;
            onChange(sw, [this, item, sw, g] {
                bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
                lv_obj_add_state(sw, LV_STATE_DISABLED);
                std::shared_ptr<bool> l = life;
                s.onSwitch(*this, item, on, [this, l, sw, on, g](bool ok) {
                    if (!*l || g != gen) return;  // list was rebuilt meanwhile: the switch no longer exists
                    lv_obj_remove_state(sw, LV_STATE_DISABLED);
                    if (!ok) {
                        if (on) lv_obj_remove_state(sw, LV_STATE_CHECKED);
                        else lv_obj_add_state(sw, LV_STATE_CHECKED);
                    }
                });
            });
        } else if (!s.actions.empty() || s.onOpen) {
            label(c, LV_SYMBOL_RIGHT, M().sm, C().muted);
        }
        onClick(c, [this, idx] { open(idx); });
    }

    void open(size_t idx) {
        // deep copy: a refresh may replace the list while a menu is still open
        auto keep = std::make_shared<JsonDocument>();
        keep->set(items[idx]);
        JsonObjectConst item = keep->as<JsonObjectConst>();
        if (s.onOpen) {
            s.onOpen(*this, item);
            return;
        }
        std::vector<MenuItem> m;
        std::vector<const RowAction*> acts;
        m.push_back({"View JSON"});
        acts.push_back(nullptr);
        for (auto& a : s.actions) {
            if (a.when && !a.when(item)) continue;
            m.push_back({a.label, a.tone});
            acts.push_back(&a);
        }
        RowView rv = s.row(item);
        menu(rv.title.empty() ? s.title : rv.title, m, [this, item, keep, acts](int i) {
            if (i == 0) {
                if (!s.detailPath.empty()) showJson(*this, "Details", util::subst(s.detailPath, item));
                else {
                    std::string raw;
                    serializeJson(item, raw);
                    showJsonText("Details", raw);
                }
                return;
            }
            exec(*acts[i], item);
        });
    }

    void exec(const RowAction& a, JsonObjectConst item) {
        if (a.custom) {
            a.custom(*this, item);
            return;
        }
        if (a.form) {
            openForm(*a.form, item, [this] { load(true); });
            return;
        }
        std::string path = util::subst(a.path, item), body = util::subst(a.body, item, false);
        std::string method = a.method;
        auto go = [this, method, path, body, label = a.label] {
            runCall(*this, method, path, body, label + ": done", [this] { load(true); });
        };
        if (!a.confirm.empty()) confirmDanger(a.label, util::subst(a.confirm, item, false), a.label, go);
        else go();
    }
};

PagePtr makeList(ListSpec spec) {
    auto p = std::make_unique<ListPage>();
    p->s = std::move(spec);
    return p;
}

}  // namespace ui
