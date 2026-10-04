// Chat with an AI workflow through n8n: the model, tools and memory all run on the server; the device only sends text and shows the answer.
#include <algorithm>

#include "../core/config.h"
#include "../core/plat.h"
#include "../core/util.h"
#include "hooks.h"
#include "res.h"

namespace ui {

namespace {
struct Msg {
    enum Role { User, Bot, Error } role;
    std::string text;
};

std::string randomId() {
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 24; i++) s += hex[plat::random32() & 15];
    return s;
}

class ChatPage : public Page {
public:
    wh::Endpoint ep;
    std::string field;               // generic webhook: JSON field that carries the message
    std::vector<Msg> msgs;
    std::string session = randomId(), model, system, draft;
    std::vector<std::string> models;
    bool busy = false;
    lv_obj_t *list = nullptr, *input = nullptr;

    bool isOpenAI() const { return ep.openai; }
    bool isChatTrigger() const { return ep.kind == wh::Kind::Chat; }

    void build() override {
        title = ep.wfName.empty() ? "Chat" : ep.wfName;
        actions.push_back({LV_SYMBOL_SETTINGS, [this] { options(); }});
        list = scroller(body);
        lv_obj_set_style_pad_bottom(list, M().gap, 0);
        lv_obj_t* bar = row(body);
        lv_obj_set_style_pad_all(bar, M().pad, 0);
        lv_obj_set_style_pad_column(bar, M().gap, 0);
        lv_obj_set_style_bg_color(bar, C().surface, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_t* f = card(bar);
        lv_obj_set_flex_grow(f, 1);
        lv_obj_set_width(f, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_ver(f, M().compact ? 6 : 14, 0);
        lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(f, LV_OBJ_FLAG_CLICKABLE);
        input = subtext(f, "Message");
        ellipsis(input);
        lv_obj_set_flex_grow(input, 1);
        onClick(f, [this] { ask(); });
        iconButton(bar, LV_SYMBOL_RIGHT, [this] { ask(); }, true);
        render();
        if (isOpenAI()) loadModels();
        if (msgs.empty()) ask();   // straight into typing
    }

    void ask() {
        if (busy) {
            toast("Waiting for the answer...", Tone::Warning);
            return;
        }
        prompt(title, draft, "Message (Enter sends)", false, false, [this](const std::string& v) {
            draft.clear();
            if (util::trim(v).empty()) return;
            sendMsg(v);
        });
    }

    void render() {
        lv_obj_clean(list);
        if (msgs.empty()) {
            std::string what = isChatTrigger() ? "n8n Chat Trigger" : isOpenAI() ? "OpenAI-compatible endpoint" : "webhook";
            empty(list, LV_SYMBOL_ENVELOPE, "Say something", "Talking to " + ep.wfName + " through its " + what + ". The model runs on your n8n server.");
        }
        for (auto& m : msgs) bubble(m);
        if (busy) {
            lv_obj_t* r = row(list);
            loading(r, "Thinking...");
        }
        lv_obj_update_layout(list);
        lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF);
    }

    void bubble(const Msg& m) {
        lv_obj_t* r = row(list);
        lv_obj_set_flex_align(r, m.role == Msg::User ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_t* c = card(r);
        lv_obj_set_width(c, LV_PCT(M().compact ? 88 : 78));
        if (m.role == Msg::User) {
            lv_obj_set_style_bg_color(c, C().primary, 0);
            lv_obj_set_style_border_width(c, 0, 0);
        } else if (m.role == Msg::Error) {
            lv_obj_set_style_border_color(c, C().danger, 0);
        }
        lv_obj_t* t = m.role == Msg::User ? label(c, m.text, M().md, C().onPrimary) : text(c, m.text, M().md);
        lv_obj_set_width(t, LV_PCT(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        if (m.role == Msg::Error) lv_obj_set_style_text_color(t, C().danger, 0);
        // long answers can be read in full in the viewer (copy-friendly, scrollable)
        if (m.role != Msg::User && m.text.size() > 600) {
            lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
            std::string full = m.text;
            onClick(c, [full] { viewer("Answer", full); });
            lv_label_set_text(t, (m.text.substr(0, 600) + "...  (tap to read all)").c_str());
        }
    }

    // sibling /models endpoint of an OpenAI-compatible /chat/completions webhook
    const wh::Endpoint* modelsEndpoint() const {
        for (auto& e : endpointCache())
            if (e.models && e.wfId == ep.wfId) return &e;
        return nullptr;
    }

    void loadModels() {
        api::Request r;
        r.method = "GET";
        r.raw = true;
        if (const wh::Endpoint* me = modelsEndpoint()) {
            r.path = me->url();
            r.headers = wh::authHeaders(*me, wh::loadSecret(me->id()));
        } else {
            std::string p = ep.url();
            size_t i = p.rfind("chat/completions");
            if (i == std::string::npos) return;
            r.path = p.substr(0, i) + "models";
            r.headers = wh::authHeaders(ep, wh::loadSecret(ep.id()));
        }
        Page::send(r, [this](api::Response& res) {
            if (!res.ok()) return;
            JsonDocument d;
            if (deserializeJson(d, res.body) != DeserializationError::Ok) return;
            models.clear();
            for (JsonObjectConst m : d["data"].as<JsonArrayConst>()) models.push_back(util::str(m["id"]));
            if (model.empty() && !models.empty()) model = models[0];
        });
    }

    void options() {
        std::vector<MenuItem> m;
        if (isOpenAI()) {
            m.push_back({"Model: " + (model.empty() ? std::string("(server default)") : model)});
            m.push_back({"System prompt"});
        }
        m.push_back({"New conversation"});
        m.push_back({"Endpoint details"});
        menu("Chat", m, [this](int i) {
            int k = i - (isOpenAI() ? 2 : 0);
            if (isOpenAI() && i == 0) pickModel();
            else if (isOpenAI() && i == 1)
                prompt("System prompt", system, "You are a helpful assistant", false, true, [this](const std::string& v) { system = v; });
            else if (k == 0) {
                msgs.clear();
                session = randomId();
                render();
            } else nav::push(makeEndpointPage(ep));
        });
    }

    void pickModel() {
        if (models.empty()) {
            prompt("Model name", model, "model id", false, false, [this](const std::string& v) { model = v; });
            return;
        }
        std::vector<MenuItem> m;
        for (auto& x : models) m.push_back({x});
        menu("Model", m, [this](int i) { model = models[i]; });
    }

    void sendMsg(const std::string& text_) {
        msgs.push_back({Msg::User, text_});
        busy = true;
        render();
        JsonDocument d;
        if (isChatTrigger()) {
            d["action"] = "sendMessage";
            d["sessionId"] = session;
            d["chatInput"] = text_;
        } else if (isOpenAI()) {
            if (!model.empty()) d["model"] = model;
            d["stream"] = false;
            JsonArray a = d["messages"].to<JsonArray>();
            if (!system.empty()) {
                JsonObject s = a.add<JsonObject>();
                s["role"] = "system";
                s["content"] = system;
            }
            for (auto& m : msgs) {
                if (m.role == Msg::Error) continue;
                JsonObject o = a.add<JsonObject>();
                o["role"] = m.role == Msg::User ? "user" : "assistant";
                o["content"] = m.text;
            }
        } else {
            d[field.empty() ? "message" : field] = text_;
            d["sessionId"] = session;
        }
        std::string body;
        serializeJson(d, body);
        api::Request r;
        r.method = "POST";
        r.path = ep.url();
        r.raw = true;
        r.body = body;
        r.contentType = "application/json";
        r.headers = wh::authHeaders(ep, wh::loadSecret(ep.id()));
        r.minTimeoutSec = 120;   // models are slow
        r.maxKb = 256;
        Page::send(r, [this](api::Response& res) {
            busy = false;
            if (!res.ok()) {
                std::string e = res.status > 0 ? "HTTP " + std::to_string(res.status) : "";
                std::string detail = res.error.empty() ? replyText_(res.body) : res.error;
                if (res.status == 401 || res.status == 403) detail = "Not authorised: set the credentials on the endpoint page.";
                msgs.push_back({Msg::Error, e + (e.empty() || detail.empty() ? "" : ": ") + util::trunc(detail, 300)});
            } else {
                std::string t = replyText_(res.body);
                msgs.push_back({Msg::Bot, t.empty() ? "(empty answer)" : t});
            }
            render();
            if (plat::hasKeyboard() && !msgs.empty() && msgs.back().role != Msg::Error) ask();   // keyboard users keep typing
        });
    }

    static std::string replyText_(const std::string& b) { return wh::replyText(b); }
};
}  // namespace

PagePtr makeChat(const wh::Endpoint& e, const std::string& field) {
    auto p = std::make_unique<ChatPage>();
    p->ep = e;
    p->field = field;
    return p;
}

}  // namespace ui
