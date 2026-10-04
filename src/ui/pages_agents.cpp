// AI & Agents: the AI agents built into your workflows (AI Agent nodes with their model, tools and memory), and a way to talk to them.
// n8n's own "Agents" product (Agents tab in the editor) is managed through n8n's UI and has no public API; the supported ways to reach one
// from a device are a workflow with a Webhook / Chat Trigger -> "Message an Agent" node, or an AI Agent workflow with a Chat/MCP trigger.
#include <algorithm>

#include "../core/config.h"
#include "../core/plat.h"
#include "../core/util.h"
#include "hooks.h"
#include "res.h"

namespace ui {

namespace {
struct Data {
    std::vector<wh::Agent> agents;
    std::vector<wh::Endpoint> eps;
};
Data g_data;
uint32_t g_at = 0;
bool g_inactive = false;

const wh::Endpoint* chatEntry(const std::string& wfId) {
    const wh::Endpoint* best = nullptr;
    int bestScore = -1;
    for (auto& e : g_data.eps) {
        if (e.wfId != wfId || !e.usable()) continue;
        int sc = e.kind == wh::Kind::Chat ? 4 : e.openai ? 3 : (e.kind == wh::Kind::Webhook && !e.models) ? 1 : 0;
        if (sc > bestScore) {
            bestScore = sc;
            best = &e;
        }
    }
    return bestScore >= 1 ? best : nullptr;
}

class AgentDetail : public Page {
public:
    wh::Agent a;
    void build() override {
        title = a.nodeName;
        lv_obj_t* l = scroller(body);
        lv_obj_t* c = card(l);
        kv(c, "Workflow", a.wfName + (a.wfActive ? "" : "  (not published)"));
        if (a.messagesAgent) kv(c, "Type", "Message an Agent (calls an n8n Agent)");
        else kv(c, "Type", a.isSub ? "Agent Tool (sub-agent)" : "AI Agent");
        if (!a.model.empty()) kv(c, "Model", a.model + (a.provider.empty() ? "" : "  (" + a.provider + ")"));
        else if (!a.provider.empty()) kv(c, "Provider", a.provider);
        if (!a.memory.empty()) kv(c, "Memory", a.memory);
        kv(c, "Tools", a.tools.empty() ? "none" : std::to_string(a.tools.size()));
        for (auto& t : a.tools) {
            lv_obj_t* tl = subtext(c, "- " + t, M().sm);
            lv_obj_set_width(tl, LV_PCT(100));
        }
        if (!a.prompt.empty()) {
            text(c, "System message", M().md);
            lv_obj_t* pl = subtext(c, util::trunc(a.prompt, 500), M().sm);
            lv_obj_set_width(pl, LV_PCT(100));
        }
        lv_obj_t* r = row(l);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_column(r, M().gap, 0);
        if (const wh::Endpoint* e = chatEntry(a.wfId)) {
            wh::Endpoint ec = *e;
            button(r, "Chat", Kind::Primary, [ec] { nav::push(makeChat(ec, "")); });
        }
        std::string id = a.wfId, nm = a.wfName;
        button(r, "Open workflow", Kind::Secondary, [id, nm] { nav::push(makeWorkflowDetail(id, nm)); });

        text(l, "Ways in", M().md);
        bool any = false;
        for (auto& e : g_data.eps) {
            if (e.wfId != a.wfId) continue;
            any = true;
            wh::Endpoint ec = e;
            lv_obj_t* c2 = card(l);
            lv_obj_set_flex_flow(c2, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(c2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_add_flag(c2, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(c2, C().surface2, LV_STATE_PRESSED);
            dot(c2, e.usable() ? Tone::Success : Tone::Neutral);
            lv_obj_t* t = text(c2, e.kindName() + "  /" + e.path, M().sm);
            ellipsis(t);
            lv_obj_set_flex_grow(t, 1);
            onClick(c2, [ec] { nav::push(makeEndpointPage(ec)); });
        }
        if (!any) {
            lv_obj_t* h = subtext(l, "This agent has no Webhook, Chat or MCP trigger, so it only runs inside n8n (schedule, other workflows). "
                                     "Add a Webhook trigger and a Respond to Webhook node to reach it from here.", M().sm);
            lv_obj_set_width(h, LV_PCT(100));
        }
    }
};

class AgentsPage : public Page {
public:
    bool inactive = false, loading_ = false;
    std::string err;
    lv_obj_t *chipRow = nullptr, *list = nullptr;
    int pages = 0;

    void build() override {
        title = "AI & Agents";
        actions.push_back({LV_SYMBOL_REFRESH, [this] { load(); }});
        chipRow = row(body);
        lv_obj_set_flex_flow(chipRow, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_hor(chipRow, M().pad, 0);
        lv_obj_set_style_pad_top(chipRow, M().gap, 0);
        build_chip();
        list = scroller(body);
        if (!g_data.agents.empty()) render();
        if (g_data.agents.empty() || plat::millis() - g_at > 20000 || g_inactive != inactive) load();
    }
    void build_chip() {
        lv_obj_clean(chipRow);
        chip(chipRow, "Include unpublished", inactive, [this] {
            inactive = !inactive;
            build_chip();
            load();
        });
    }

    void load() {
        if (loading_) return;
        loading_ = true;
        err.clear();
        pages = 0;
        fetch("", std::make_shared<Data>());
    }

    void fetch(const std::string& cursor, std::shared_ptr<Data> acc) {
        std::string q = "limit=100";
        if (!inactive) q += "&active=true";
        if (!cursor.empty()) q += "&cursor=" + util::urlEncode(cursor);
        if (g_data.agents.empty() && acc->agents.empty()) {
            lv_obj_clean(list);
            loading(list, "Looking for AI agents...");
        }
        get("/workflows", q, wh::agentFilter(), [this, acc](api::Response& r) {
            if (!r.ok()) {
                loading_ = false;
                err = r.message();
                render();
                return;
            }
            for (JsonObjectConst w : r.json()["data"].as<JsonArrayConst>()) {
                wh::discover(w, acc->eps);
                wh::discoverAgents(w, acc->agents);
            }
            const char* nc = r.json()["nextCursor"] | (const char*)nullptr;
            if (nc && *nc && ++pages < 20) {
                fetch(nc, acc);
                return;
            }
            loading_ = false;
            auto keep = [&](bool arch) { return inactive || !arch; };
            acc->agents.erase(std::remove_if(acc->agents.begin(), acc->agents.end(), [&](const wh::Agent& a) { return !keep(a.wfArchived); }), acc->agents.end());
            acc->eps.erase(std::remove_if(acc->eps.begin(), acc->eps.end(), [&](const wh::Endpoint& e) { return !keep(e.wfArchived); }), acc->eps.end());
            std::stable_sort(acc->agents.begin(), acc->agents.end(), [](const wh::Agent& a, const wh::Agent& b) {
                if (a.wfActive != b.wfActive) return a.wfActive;
                return a.wfName < b.wfName;
            });
            g_data = *acc;
            g_at = plat::millis();
            g_inactive = inactive;
            render();
        });
    }

    void render() {
        lv_obj_clean(list);
        lv_obj_t* intro = card(list);
        lv_obj_t* t1 = text(intro, "Agents in your workflows", M().md);
        lv_obj_set_width(t1, LV_PCT(100));
        lv_obj_t* t2 = subtext(intro,
                               "These are the AI Agent nodes of your n8n workflows, with the model, tools and memory they use. Models, tools and memory run on your "
                               "server; this device just sends text and shows the answer. n8n's own Agents tab is managed in the n8n editor and has no public API: "
                               "expose one with a Webhook or Chat trigger and it shows up here.",
                               M().sm);
        lv_obj_set_width(t2, LV_PCT(100));
        if (!err.empty() && g_data.agents.empty()) {
            empty(list, LV_SYMBOL_WARNING, "Could not load", err);
            button(list, "Retry", Kind::Primary, [this] { load(); });
            return;
        }
        int shown = 0;
        for (size_t i = 0; i < g_data.agents.size(); i++) {
            const wh::Agent& a = g_data.agents[i];
            shown++;
            lv_obj_t* c = card(list);
            lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(c, M().gap, 0);
            lv_obj_set_style_pad_ver(c, M().compact ? 6 : 12, 0);
            lv_obj_set_style_bg_color(c, C().surface2, LV_STATE_PRESSED);
            lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
            dot(c, a.wfActive ? Tone::Success : Tone::Neutral);
            lv_obj_t* tc = col(c);
            lv_obj_set_width(tc, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(tc, 1);
            lv_obj_set_style_pad_row(tc, 2, 0);
            lv_obj_t* tt = text(tc, a.wfName + (a.nodeName == "AI Agent" ? "" : "  -  " + a.nodeName), M().md);
            ellipsis(tt);
            lv_obj_set_width(tt, LV_PCT(100));
            std::string sub = a.model.empty() ? a.provider : a.model;
            if (!a.tools.empty()) sub += (sub.empty() ? "" : "  -  ") + std::to_string(a.tools.size()) + " tools";
            if (!a.memory.empty() && !M().compact) sub += "  -  memory";
            if (a.messagesAgent) sub = "calls an n8n Agent";
            lv_obj_t* st = subtext(tc, sub.empty() ? "AI agent" : sub, M().sm);
            ellipsis(st);
            lv_obj_set_width(st, LV_PCT(100));
            const wh::Endpoint* ce = chatEntry(a.wfId);
            if (ce) badge(c, "Chat", Tone::Primary);
            else {
                bool hasMcp = std::any_of(g_data.eps.begin(), g_data.eps.end(), [&](const wh::Endpoint& e) { return e.wfId == a.wfId && e.kind == wh::Kind::Mcp; });
                if (hasMcp) badge(c, "MCP", Tone::Info);
            }
            wh::Agent ac = a;
            onClick(c, [ac] {
                auto d = std::make_unique<AgentDetail>();
                d->a = ac;
                nav::push(std::move(d));
            });
        }
        if (!shown) {
            if (loading_) loading(list, "Looking for AI agents...");
            else empty(list, LV_SYMBOL_CHARGE, "No AI agents found", inactive ? "No workflow contains an AI Agent node." : "No published workflow contains an AI Agent node. Try \"Include unpublished\".");
        }
    }
};
}  // namespace

PagePtr makeAgents() { return std::make_unique<AgentsPage>(); }

}  // namespace ui
