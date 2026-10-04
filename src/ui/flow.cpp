// Workflow canvas: draws a workflow's nodes and connections like n8n's editor and edits it (parameters, move, add, delete, connect).
// All logic on the JSON lives in core/flowdoc; this file is the touch UI. Saving sends the whole workflow with PUT /workflows/{id}.
#include <algorithm>
#include <cmath>
#include <map>

#include "../core/config.h"
#include "../core/flowdoc.h"
#include "../core/plat.h"
#include "../core/util.h"
#include "res.h"

namespace ui {

namespace {
using DocPtr = std::shared_ptr<JsonDocument>;

lv_color_t catColor(flow::Cat c) {
    switch (c) {
        case flow::Cat::Trigger: return lv_color_hex(0xFF6D5A);
        case flow::Cat::Ai: return lv_color_hex(0x9B6DFF);
        case flow::Cat::Logic: return lv_color_hex(0x4C9BE8);
        case flow::Cat::Data: return lv_color_hex(0x2FB67C);
        case flow::Cat::Code: return lv_color_hex(0xF0A030);
        case flow::Cat::Http: return lv_color_hex(0x1FB5C9);
        default: return lv_color_hex(0x8A8FA3);
    }
}
const char* catGlyph(flow::Cat c, const std::string& st) {
    if (st == "webhook") return LV_SYMBOL_UPLOAD;
    if (st == "scheduleTrigger" || st == "cron" || st == "wait") return LV_SYMBOL_LOOP;
    switch (c) {
        case flow::Cat::Trigger: return LV_SYMBOL_CHARGE;
        case flow::Cat::Ai: return LV_SYMBOL_EYE_OPEN;
        case flow::Cat::Logic: return LV_SYMBOL_SHUFFLE;
        case flow::Cat::Data: return LV_SYMBOL_LIST;
        case flow::Cat::Code: return LV_SYMBOL_KEYBOARD;
        case flow::Cat::Http: return LV_SYMBOL_WIFI;
        default: return LV_SYMBOL_SETTINGS;
    }
}

// connection type for a sub-node feeding an AI node
std::string connTypeFor(const std::string& type) {
    std::string st = flow::shortType(type);
    if (type.find("n8n-nodes-langchain") == std::string::npos) return "main";
    if (st.find("lmChat") == 0 || st.find("lmOpen") == 0 || st.find("lm") == 0) return "ai_languageModel";
    if (st.find("memory") == 0) return "ai_memory";
    if (st.find("embeddings") == 0) return "ai_embedding";
    if (st.find("outputParser") == 0) return "ai_outputParser";
    if (st.find("textSplitter") == 0) return "ai_textSplitter";
    if (st.find("retriever") == 0) return "ai_retriever";
    if (st.find("document") == 0) return "ai_document";
    if (st.find("vectorStore") == 0 && st.find("Tool") == std::string::npos) return "ai_vectorStore";
    if (st.find("Tool") != std::string::npos || st.find("tool") == 0 || st.find("mcpClient") == 0) return "ai_tool";
    return "main";
}

std::string valueSummary(JsonVariantConst v) {
    if (v.is<const char*>()) return util::trunc(v.as<const char*>(), 60);
    if (v.is<bool>()) return v.as<bool>() ? "true" : "false";
    if (v.is<JsonObjectConst>()) return "{ " + std::to_string(v.size()) + " keys }";
    if (v.is<JsonArrayConst>()) return "[ " + std::to_string(v.size()) + " items ]";
    if (v.isNull()) return "null";
    std::string s;
    serializeJson(v, s);
    return s;
}

class NodePage : public Page {
public:
    DocPtr wf;
    std::string name;
    bool editable = false;
    std::function<void()> changed;
    lv_obj_t* box_ = nullptr;

    JsonObject n() { return flow::node(*wf, name); }

    void build() override {
        title = name;
        box_ = scroller(body);
        render();
    }

    void touch() {
        if (changed) changed();
    }

    void render() {
        lv_obj_clean(box_);
        JsonObject o = n();
        if (o.isNull()) {
            empty(box_, LV_SYMBOL_WARNING, "Node removed", "");
            return;
        }
        std::string type = o["type"] | "";
        lv_obj_t* c = card(box_);
        kv(c, "Type", flow::label(type) + "  (" + type + " v" + util::str(o["typeVersion"]) + ")");
        lv_obj_t* nm = field(c, "Name", name, [this] {
            if (!editable) return;
            prompt("Rename node", name, "name", false, false, [this](const std::string& v) {
                if (flow::rename(*wf, name, v)) {
                    name = v;
                    title = v;
                    nav::refreshTopBar();
                    touch();
                    render();
                } else toast("That name is empty or already used", Tone::Warning);
            });
        });
        (void)nm;
        kv(c, "Position", util::str(o["position"][0]) + ", " + util::str(o["position"][1]));
        lv_obj_t* dr = row(c);
        lv_obj_t* dl = text(dr, "Disabled", M().md);
        lv_obj_set_flex_grow(dl, 1);
        toggle(dr, o["disabled"] | false, [this](bool on) {
            if (!editable) return;
            n()["disabled"] = on;
            touch();
        });
        if (o["notes"].is<const char*>() && *o["notes"].as<const char*>()) kv(c, "Notes", util::trunc(o["notes"].as<const char*>(), 200));
        if (!editable) {
            lv_obj_t* h = subtext(c, "Turn on edit mode (pencil) to change this node.", M().sm);
            lv_obj_set_width(h, LV_PCT(100));
        }

        auto ls = flow::links(*wf);
        text(box_, "Connections", M().md);
        bool anyLink = false;
        for (auto& l : ls) {
            if (l.from != name && l.to != name) continue;
            anyLink = true;
            lv_obj_t* r = card(box_);
            lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            std::string desc = l.from == name ? LV_SYMBOL_RIGHT " " + l.to : LV_SYMBOL_LEFT " " + l.from;
            if (l.type != "main") desc += "  (" + l.type.substr(3) + ")";
            else if (l.from == name && flow::outputs(*wf, n()) > 1) desc += "  (output " + std::to_string(l.out + 1) + ")";
            lv_obj_t* t = text(r, desc, M().sm);
            ellipsis(t);
            lv_obj_set_flex_grow(t, 1);
            if (editable) {
                Link_ lk{l.from, l.out, l.to, l.type};
                iconButton(r, LV_SYMBOL_CLOSE, [this, lk] {
                    flow::disconnect(*wf, lk.from, lk.out, lk.to, lk.type);
                    touch();
                    render();
                });
            }
        }
        if (!anyLink) subtext(box_, "Not connected", M().sm);

        lv_obj_t* ph = row(box_);
        lv_obj_t* pt = text(ph, "Parameters", M().md);
        lv_obj_set_flex_grow(pt, 1);
        if (editable) iconButton(ph, LV_SYMBOL_PLUS, [this] { addParam(); });
        JsonObject p = o["parameters"].as<JsonObject>();
        if (p.isNull() || p.size() == 0) subtext(box_, "No parameters", M().sm);
        std::vector<std::string> keys;
        for (JsonPair kv_ : p) keys.push_back(kv_.key().c_str());
        for (auto& k : keys) {
            JsonVariant v = p[k.c_str()];
            lv_obj_t* f = field(box_, k, valueSummary(v), [this, k] { editParam(k); });
            (void)f;
        }
        if (editable) {
            lv_obj_t* r = row(box_);
            lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
            button(r, "Edit all parameters (JSON)", Kind::Secondary, [this] {
                std::string raw;
                serializeJsonPretty(n()["parameters"], raw);
                prompt("Parameters (JSON)", raw, "{}", false, true, [this](const std::string& v) {
                    JsonDocument t;
                    if (deserializeJson(t, v) != DeserializationError::Ok || !t.is<JsonObject>()) {
                        toast("Not valid JSON", Tone::Danger);
                        return;
                    }
                    n()["parameters"] = t.as<JsonVariant>();
                    touch();
                    render();
                });
            });
            button(r, "Delete node", Kind::Danger, [this] {
                confirm("Delete node", "Remove \"" + name + "\" and its connections?", "Delete", true, [this] {
                    flow::removeNode(*wf, name);
                    touch();
                    nav::popPage(this);
                });
            });
        }
    }

    struct Link_ {
        std::string from;
        int out;
        std::string to, type;
    };

    void addParam() {
        prompt("New parameter name", "", "name", false, false, [this](const std::string& k) {
            if (k.empty()) return;
            n()["parameters"][k] = "";
            touch();
            editParam(k);
        });
    }

    void editParam(const std::string& k) {
        JsonVariant v = n()["parameters"][k.c_str()];
        if (v.isNull() && !n()["parameters"].containsKey(k)) return;
        if (!editable) {
            std::string raw;
            serializeJsonPretty(v, raw);
            viewer(k, raw);
            return;
        }
        if (v.is<bool>()) {
            n()["parameters"][k.c_str()] = !v.as<bool>();
            touch();
            render();
            return;
        }
        std::string cur;
        bool isStr = v.is<const char*>();
        if (isStr) cur = v.as<const char*>();
        else serializeJsonPretty(v, cur);
        prompt(k, cur, isStr ? "text or ={{ expression }}" : "JSON value", false, true, [this, k, isStr](const std::string& s) {
            if (isStr) n()["parameters"][k.c_str()] = s;
            else {
                JsonDocument t;
                if (deserializeJson(t, s) != DeserializationError::Ok) {
                    toast("Not valid JSON", Tone::Danger);
                    return;
                }
                n()["parameters"][k.c_str()] = t.as<JsonVariant>();
            }
            touch();
            render();
        });
    }
};

class FlowPage : public Page {
public:
    std::string id, wname;
    DocPtr wf;
    bool editing = false, dirty = false, connecting = false, loadFailed = false;
    std::string selected, connectFrom;
    int zoomIdx = -1;
    lv_obj_t *vp = nullptr, *world = nullptr, *info = nullptr, *toolbar = nullptr;

    struct NV {
        std::string name, type;
        double x, y, w, h;
        int outs;
        flow::Cat cat;
        bool disabled;
        lv_obj_t* obj;
    };
    std::vector<NV> nv;
    double ox = 0, oy = 0;   // canvas origin (world px of node coordinate 0,0 after margin)

    static constexpr double ZOOMS[4] = {0.4, 0.6, 0.85, 1.2};
    double z() const { return ZOOMS[zoomIdx < 0 ? 1 : zoomIdx]; }
    double NW() const { return 100 * z(); }

    void build() override {
        title = wname;
        refreshActions();
        vp = lv_obj_create(body);
        lv_obj_set_width(vp, LV_PCT(100));
        lv_obj_set_flex_grow(vp, 1);
        lv_obj_set_style_bg_color(vp, C().bg, 0);
        lv_obj_set_style_bg_opa(vp, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(vp, 0, 0);
        lv_obj_set_style_radius(vp, 0, 0);
        lv_obj_set_style_pad_all(vp, 0, 0);
        lv_obj_set_scroll_dir(vp, LV_DIR_ALL);
        lv_obj_set_scrollbar_mode(vp, LV_SCROLLBAR_MODE_ACTIVE);
        info = subtext(body, "Loading workflow...", M().sm);
        lv_obj_set_style_pad_all(info, 6, 0);
        toolbar = row(body);
        lv_obj_set_style_pad_all(toolbar, M().gap, 0);
        lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_add_flag(toolbar, LV_OBJ_FLAG_HIDDEN);
        load();
    }

    void refreshActions() {
        actions.clear();
        actions.push_back({LV_SYMBOL_MINUS, [this] { setZoom(zoomIdx - 1); }});
        actions.push_back({LV_SYMBOL_PLUS, [this] { setZoom(zoomIdx + 1); }});
        actions.push_back({editing ? LV_SYMBOL_OK : LV_SYMBOL_EDIT, [this] { toggleEdit(); }});
        if (dirty) actions.push_back({LV_SYMBOL_SAVE, [this] { save(); }});
        nav::refreshTopBar();
    }

    void setZoom(int i) {
        i = std::max(0, std::min(3, i));
        if (i == zoomIdx) return;
        zoomIdx = i;
        draw();
    }

    void toggleEdit() {
        editing = !editing;
        connecting = false;
        refreshActions();
        buildToolbar();
        draw();
    }

    void load() {
        get("/workflows/" + util::urlEncode(id), "", "*", [this](api::Response& r) {
            if (!r.ok() || r.truncated) {
                loadFailed = true;
                setText(info, r.truncated ? "This workflow is too large to open on the device." : "Could not load: " + r.message());
                return;
            }
            wf = r.doc;
            if (zoomIdx < 0) zoomIdx = (M().compact || (*wf)["nodes"].size() > 24) ? 0 : 2;
            wname = (*wf)["name"] | "";
            title = wname;
            dirty = false;
            refreshActions();
            draw();
        });
    }

    void markDirty() {
        if (!dirty) {
            dirty = true;
            refreshActions();
        }
        updateInfo();
    }

    void updateInfo() {
        if (!wf) return;
        std::string s = std::to_string((*wf)["nodes"].size()) + " nodes  -  " + std::to_string(flow::links(*wf).size()) + " connections";
        if (connecting) s = "Tap the node to connect " + connectFrom + " to";
        else if (editing) s += "  -  drag to move, tap to edit";
        if (dirty) s += "  -  unsaved changes";
        setText(info, s);
    }

    void buildToolbar() {
        lv_obj_clean(toolbar);
        if (!editing) {
            lv_obj_add_flag(toolbar, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        lv_obj_remove_flag(toolbar, LV_OBJ_FLAG_HIDDEN);
        button(toolbar, "Add node", Kind::Primary, [this] { addNode(); });
        button(toolbar, "Connect", connecting ? Kind::Primary : Kind::Secondary, [this] {
            if (selected.empty()) {
                toast("Tap a node first, then Connect", Tone::Warning);
                return;
            }
            connecting = !connecting;
            connectFrom = selected;
            buildToolbar();
            updateInfo();
        });
        button(toolbar, "Delete", Kind::Danger, [this] {
            if (selected.empty()) {
                toast("Tap a node first", Tone::Warning);
                return;
            }
            std::string nm = selected;
            confirm("Delete node", "Remove \"" + nm + "\" and its connections?", "Delete", true, [this, nm] {
                flow::removeNode(*wf, nm);
                selected.clear();
                markDirty();
                draw();
            });
        });
        if (dirty) button(toolbar, "Save", Kind::Primary, [this] { save(); });
    }

    void draw() {
        if (!wf) return;
        lv_obj_clean(vp);
        nv.clear();
        flow::Bounds b = flow::bounds(*wf);
        double zz = z(), nw = NW();
        double margin = 40;
        ox = margin - b.x0 * zz;
        oy = margin - b.y0 * zz;
        double ww = (b.x1 - b.x0) * zz + nw + 2 * margin + 40, wh_ = (b.y1 - b.y0) * zz + nw + 2 * margin + 40;
        world = lv_obj_create(vp);
        lv_obj_remove_style_all(world);
        lv_obj_set_size(world, (int)std::max(ww, (double)lv_obj_get_width(vp)), (int)std::max(wh_, (double)lv_obj_get_height(vp)));
        lv_obj_remove_flag(world, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(world, [](lv_event_t* e) { static_cast<FlowPage*>(lv_event_get_user_data(e))->drawLinks(e); }, LV_EVENT_DRAW_MAIN, this);
        for (int pass = 0; pass < 2; pass++)   // sticky notes first so they sit behind the nodes
        for (JsonObject n : (*wf)["nodes"].as<JsonArray>()) {
            bool sticky = flow::shortType(n["type"] | "") == "stickyNote";
            if (sticky != (pass == 0)) continue;
            NV v;
            v.name = n["name"] | "";
            v.type = n["type"] | "";
            v.x = ox + (double)(n["position"][0] | 0.0) * zz;
            v.y = oy + (double)(n["position"][1] | 0.0) * zz;
            v.w = v.h = nw;
            if (sticky) {
                v.w = std::max(60.0, (double)(n["parameters"]["width"] | 240) * zz);
                v.h = std::max(40.0, (double)(n["parameters"]["height"] | 160) * zz);
            }
            v.outs = flow::outputs(*wf, n);
            v.cat = flow::category(v.type);
            v.disabled = n["disabled"] | false;
            v.obj = makeNode(v);
            nv.push_back(v);
        }
        updateInfo();
        buildToolbar();
        lv_obj_invalidate(world);
    }

    lv_obj_t* makeSticky(const NV& v) {
        lv_obj_t* o = lv_obj_create(world);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, (int)v.w, (int)v.h);
        lv_obj_set_pos(o, (int)v.x, (int)v.y);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(o, 6, 0);
        lv_obj_set_style_bg_color(o, lv_color_hex(0xF5D76E), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_30, 0);
        lv_obj_set_style_border_width(o, 1, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(0xF5D76E), 0);
        lv_obj_set_style_pad_all(o, 6, 0);
        JsonObject n = flow::node(*wf, v.name);
        std::string c = n["parameters"]["content"] | "";
        lv_obj_t* t = label(o, util::trunc(c, 220), M().sm, C().text2);
        lv_obj_set_width(t, LV_PCT(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_add_event_cb(o, [](lv_event_t* e) { static_cast<FlowPage*>(lv_event_get_user_data(e))->nodeEvent(e); }, LV_EVENT_ALL, this);
        return o;
    }

    lv_obj_t* makeNode(const NV& v) {
        if (flow::shortType(v.type) == "stickyNote") return makeSticky(v);
        double nw = v.w;
        int lw = (int)(nw + 40);
        lv_obj_t* o = lv_obj_create(world);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, lw, (int)(nw + 26));
        lv_obj_set_pos(o, (int)(v.x - 20), (int)v.y);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_t* sq = lv_obj_create(o);
        lv_obj_remove_style_all(sq);
        lv_obj_set_size(sq, (int)nw, (int)nw);
        lv_obj_set_pos(sq, 20, 0);
        lv_obj_remove_flag(sq, LV_OBJ_FLAG_CLICKABLE);
        lv_color_t cc = catColor(v.cat);
        bool sel = v.name == selected;
        lv_obj_set_style_radius(sq, (int)(nw * 0.18), 0);
        lv_obj_set_style_bg_color(sq, C().surface, 0);
        lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sq, sel ? 3 : 2, 0);
        lv_obj_set_style_border_color(sq, sel ? C().primary : cc, 0);
        lv_obj_set_style_opa(sq, v.disabled ? LV_OPA_50 : LV_OPA_COVER, 0);
        lv_obj_t* g = label(sq, catGlyph(v.cat, flow::shortType(v.type)), nw > 60 ? M().lg : M().md, cc);
        lv_obj_center(g);
        lv_obj_t* t = label(o, v.name, nw > 60 ? M().sm : M().sm, C().text2);
        lv_obj_set_width(t, lw);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(t, 0, (int)nw + 4);
        lv_obj_set_height(t, nw > 60 ? 22 : 16);
        std::string nm = v.name;
        lv_obj_add_event_cb(
            o,
            [](lv_event_t* e) {
                auto* self = static_cast<FlowPage*>(lv_event_get_user_data(e));
                self->nodeEvent(e);
            },
            LV_EVENT_ALL, this);
        return o;
    }

    lv_obj_t* dragObj = nullptr;
    bool dragMoved = false;

    NV* byObj(lv_obj_t* o) {
        for (auto& v : nv)
            if (v.obj == o) return &v;
        return nullptr;
    }

    void nodeEvent(lv_event_t* e) {
        lv_obj_t* o = lv_event_get_target_obj(e);
        NV* v = byObj(o);
        if (!v) return;
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED && editing && !connecting) {
            dragObj = o;
            dragMoved = false;
            lv_obj_remove_flag(vp, LV_OBJ_FLAG_SCROLLABLE);
        } else if (code == LV_EVENT_PRESSING && dragObj == o) {
            lv_point_t vec;
            lv_indev_get_vect(lv_indev_active(), &vec);
            if (vec.x || vec.y) {
                dragMoved = dragMoved || std::abs(vec.x) + std::abs(vec.y) > 1;
                lv_obj_set_pos(o, lv_obj_get_x(o) + vec.x, lv_obj_get_y(o) + vec.y);
                v->x += vec.x;
                v->y += vec.y;
                lv_obj_invalidate(world);
            }
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            lv_obj_add_flag(vp, LV_OBJ_FLAG_SCROLLABLE);
            if (dragObj == o) {
                dragObj = nullptr;
                if (dragMoved) {
                    JsonObject n = flow::node(*wf, v->name);
                    flow::move(n, (v->x - ox) / z(), (v->y - oy) / z());
                    markDirty();
                    dragMoved = false;
                    return;
                }
            }
            if (code == LV_EVENT_RELEASED) tapped(*v);
        }
    }

    void tapped(NV& v) {
        if (connecting) {
            connectTo(v.name);
            return;
        }
        selected = v.name;
        if (!editing) {
            openNode(v.name);
            return;
        }
        // in edit mode a tap selects; a second tap opens the editor
        static std::string lastTap;
        static uint32_t lastAt = 0;
        uint32_t now = plat::millis();
        if (lastTap == v.name && now - lastAt < 1500) {
            lastTap.clear();
            openNode(v.name);
            return;
        }
        lastTap = v.name;
        lastAt = now;
        draw();
        toast(v.name + ": tap again to edit");
    }

    void openNode(const std::string& name) {
        auto np = std::make_unique<NodePage>();
        np->wf = wf;
        np->name = name;
        np->editable = editing;
        std::shared_ptr<bool> l = life;
        np->changed = [this, l] {
            if (!*l) return;
            markDirty();
            needRedraw = true;
        };
        nav::push(std::move(np));
    }
    void refresh() override {}
    void poll() override {
        if (needRedraw) {
            needRedraw = false;
            draw();
        }
    }
    bool needRedraw = false;

    void connectTo(const std::string& to) {
        connecting = false;
        std::string from = connectFrom;
        JsonObject fn = flow::node(*wf, from), tn = flow::node(*wf, to);
        if (fn.isNull() || tn.isNull() || from == to) {
            buildToolbar();
            updateInfo();
            return;
        }
        std::string ctype = connTypeFor(fn["type"] | "");
        // AI sub-nodes connect upward into the agent; everything else flows left to right
        auto done = [this, from, to, ctype](int out) {
            if (flow::connect(*wf, from, out, to, 0, ctype)) {
                markDirty();
                toast("Connected", Tone::Success);
            } else toast("Already connected", Tone::Warning);
            draw();
        };
        int outs = flow::outputs(*wf, fn);
        if (ctype == "main" && outs > 1) {
            std::vector<MenuItem> m;
            for (int i = 0; i < outs; i++) m.push_back({"Output " + std::to_string(i + 1) + (flow::shortType(fn["type"] | "") == "if" ? (i == 0 ? " (true)" : " (false)") : "")});
            menu("From which output?", m, [done](int i) { done(i); });
        } else done(0);
        buildToolbar();
    }

    void addNode() {
        std::vector<MenuItem> m;
        for (auto& t : flow::templates()) m.push_back({t.label});
        menu("Add node", m, [this](int i) {
            const flow::Template& t = flow::templates()[i];
            flow::Bounds b = flow::bounds(*wf);
            double x = b.x1 + 200, y = b.y0;
            std::string prev = selected;
            if (!prev.empty()) {
                JsonObject pn = flow::node(*wf, prev);
                if (!pn.isNull()) {
                    x = (double)(pn["position"][0] | 0.0) + 220;
                    y = (double)(pn["position"][1] | 0.0);
                }
            }
            JsonObject nn = flow::addNode(*wf, t, x, y);
            std::string name = nn["name"] | "";
            if (!prev.empty() && flow::category(t.type) != flow::Cat::Trigger) flow::connect(*wf, prev, 0, name);
            selected = name;
            markDirty();
            draw();
            toast("Added " + name + " - tap it twice to edit its parameters");
        });
    }

    void drawLinks(lv_event_t* e) {
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t a;
        lv_obj_get_coords(world, &a);
        auto links = flow::links(*wf);
        std::map<std::string, NV*> byName;
        for (auto& v : nv) byName[v.name] = &v;
        // incoming AI links per target, to spread them along the agent's bottom edge
        std::map<std::string, std::vector<std::string>> aiIn;
        for (auto& l : links)
            if (flow::isAiInput(l.type)) aiIn[l.to].push_back(l.from);
        for (auto& kv_ : aiIn)
            std::sort(kv_.second.begin(), kv_.second.end(), [&](const std::string& p, const std::string& q) { return byName.count(p) && byName.count(q) && byName[p]->x < byName[q]->x; });
        for (auto& l : links) {
            if (!byName.count(l.from) || !byName.count(l.to)) continue;
            NV &s = *byName[l.from], &t = *byName[l.to];
            bool ai = flow::isAiInput(l.type);
            double x0, y0, x1, y1, c0x, c0y, c1x, c1y;
            if (ai) {
                auto& vec = aiIn[l.to];
                size_t k = std::find(vec.begin(), vec.end(), l.from) - vec.begin();
                x0 = s.x + s.w / 2;
                y0 = s.y;
                x1 = t.x + t.w * (k + 1) / (vec.size() + 1);
                y1 = t.y + t.h;
                double d = std::max(30.0 * z(), std::abs(y1 - y0) / 2);
                c0x = x0;
                c0y = y0 - d;
                c1x = x1;
                c1y = y1 + d;
            } else {
                x0 = s.x + s.w;
                y0 = s.y + s.h * (l.out + 1) / (s.outs + 1);
                x1 = t.x;
                y1 = t.y + t.h / 2;
                double d = std::max(40.0 * z(), std::abs(x1 - x0) / 2);
                c0x = x0 + d;
                c0y = y0;
                c1x = x1 - d;
                c1y = y1;
            }
            lv_draw_line_dsc_t dsc;
            lv_draw_line_dsc_init(&dsc);
            dsc.color = ai ? lv_color_hex(0x9B6DFF) : C().text2;
            dsc.width = std::max(1, (int)(2 * z() + 0.5));
            dsc.opa = LV_OPA_80;
            dsc.round_start = dsc.round_end = 1;
            if (ai) {
                dsc.dash_width = 6;
                dsc.dash_gap = 4;
            }
            double px = x0, py = y0;
            const int N = 16;
            for (int i = 1; i <= N; i++) {
                double u = (double)i / N, v = 1 - u;
                double bx = v * v * v * x0 + 3 * v * v * u * c0x + 3 * v * u * u * c1x + u * u * u * x1;
                double by = v * v * v * y0 + 3 * v * v * u * c0y + 3 * v * u * u * c1y + u * u * u * y1;
                dsc.p1.x = a.x1 + (float)px;
                dsc.p1.y = a.y1 + (float)py;
                dsc.p2.x = a.x1 + (float)bx;
                dsc.p2.y = a.y1 + (float)by;
                lv_draw_line(layer, &dsc);
                px = bx;
                py = by;
            }
            double ang = std::atan2(y1 - c1y, x1 - c1x);
            if (std::abs(x1 - c1x) + std::abs(y1 - c1y) < 0.5) ang = ai ? -M_PI / 2 : 0;
            double al = 8 * std::max(0.7, z());
            dsc.dash_width = dsc.dash_gap = 0;
            for (double da : {2.7, -2.7}) {
                dsc.p1.x = a.x1 + (float)x1;
                dsc.p1.y = a.y1 + (float)y1;
                dsc.p2.x = a.x1 + (float)(x1 + al * std::cos(ang + da));
                dsc.p2.y = a.y1 + (float)(y1 + al * std::sin(ang + da));
                lv_draw_line(layer, &dsc);
            }
        }
    }

    void save() {
        if (!wf || !dirty) return;
        bool active = (*wf)["active"] | false;
        auto doSave = [this](bool publish) {
            std::string path = "/workflows/" + util::urlEncode(id) + (publish ? "" : "?publishIfActive=false");
            std::string body = flow::putBody(*wf);
            toast("Saving...");
            call("PUT", path, body, [this](api::Response& r) {
                if (!r.ok()) {
                    toast("Save failed: " + r.message(), Tone::Danger);
                    return;
                }
                toast("Saved", Tone::Success);
                dirty = false;
                refreshActions();
                load();
            });
        };
        if (active) {
            menu("This workflow is published", {{"Save and keep it published"}, {"Save as draft only"}}, [doSave](int i) { doSave(i == 0); });
        } else doSave(true);
    }
};
constexpr double FlowPage::ZOOMS[4];
}  // namespace

PagePtr makeFlow(const std::string& id, const std::string& name) {
    auto p = std::make_unique<FlowPage>();
    p->id = id;
    p->wname = name;
    return p;
}

}  // namespace ui
