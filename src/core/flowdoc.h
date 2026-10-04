// Pure JSON helpers for viewing and editing an n8n workflow (nodes + connections). No UI, so the logic is unit-tested on the host.
#pragma once
#include <ArduinoJson.h>
#include <string>
#include <vector>

namespace flow {

enum class Cat { Trigger, Ai, Logic, Data, Code, Http, Other };

Cat category(const std::string& type);
std::string shortType(const std::string& type);   // "n8n-nodes-base.httpRequest" -> "httpRequest"
std::string label(const std::string& type);        // human friendly: "HTTP Request"

struct Template {
    const char* label;
    const char* type;
    double version;
    const char* params;   // JSON text
};
const std::vector<Template>& templates();

int nodeIndex(JsonDocument& wf, const std::string& name);
JsonObject node(JsonDocument& wf, const std::string& name);
int outputs(JsonDocument& wf, JsonObject n);                 // number of main outputs of this node (If -> 2, Switch -> rules + fallback, else from connections)
bool isAiInput(const std::string& connType);                // ai_languageModel, ai_tool, ai_memory ... (drawn from the sub-node to the agent)
struct Link {
    std::string from, to, type;
    int out = 0, in = 0;
};
std::vector<Link> links(JsonDocument& wf);
struct Bounds {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
Bounds bounds(JsonDocument& wf);

std::string uniqueName(JsonDocument& wf, const std::string& base);
JsonObject addNode(JsonDocument& wf, const Template& t, double x, double y);
bool removeNode(JsonDocument& wf, const std::string& name);
bool connect(JsonDocument& wf, const std::string& from, int out, const std::string& to, int in = 0, const std::string& type = "main");
bool disconnect(JsonDocument& wf, const std::string& from, int out, const std::string& to, const std::string& type = "main");
bool rename(JsonDocument& wf, const std::string& from, const std::string& to);   // also updates connections and $('Name') references
void move(JsonObject n, double x, double y);

// body for PUT /workflows/{id}: only the properties the public API accepts
std::string putBody(JsonDocument& wf);

}  // namespace flow
