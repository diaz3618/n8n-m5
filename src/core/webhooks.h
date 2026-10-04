// Discovery and understanding of the HTTP entry points n8n workflows expose (Webhook, Form, Chat and MCP triggers).
// Portable: no LVGL, only ArduinoJson. n8n has no "run workflow" API, so these endpoints are how a device talks to a workflow.
#pragma once
#include <ArduinoJson.h>
#include <string>
#include <vector>

namespace wh {

enum class Kind { Webhook, Form, Chat, Mcp };

struct Field {          // an input the workflow reads from the request, found by scanning its expressions
    std::string name;
    std::string where;  // body | query | header | path
};

struct FormField {      // n8n Form Trigger field definition
    std::string label, type;
    bool required = false;
    std::vector<std::string> options;
};

struct Endpoint {
    Kind kind = Kind::Webhook;
    std::string wfId, wfName, nodeName, webhookId;
    bool wfActive = false, wfArchived = false, disabled = false;
    std::vector<std::string> methods;   // upper case ("POST"); Webhook only
    std::string path;                   // registered path, relative to /webhook/ (/form/, /mcp/ for the other kinds)
    std::string auth = "none";          // none | basicAuth | headerAuth | jwtAuth | bearerAuth
    std::string credName;               // name of the credential n8n holds for it (the secret itself is never exposed by the API)
    std::string responseMode = "onReceived";   // onReceived | lastNode | responseNode | streaming
    std::string respond;                // what the Respond to Webhook node sends: "JSON", "text", "binary", ...
    int respondCode = 0;
    std::string title;                  // form title
    bool isPublic = true;               // chat trigger: publicly reachable (else only the editor can use it)
    bool openai = false, models = false;   // looks like an OpenAI-compatible /chat/completions or /models endpoint
    bool rawBody = false, binary = false;
    std::string ipAllow, origins;
    std::vector<Field> fields;
    std::vector<FormField> formFields;
    bool ai = false;                    // the workflow contains an AI Agent / LLM node behind this entry point

    std::string id() const { return wfId + "/" + nodeName; }
    std::string url() const;            // production path on the server, e.g. "/webhook/cli-ai"
    std::string testUrl() const;        // the editor's listen-once variant ("/webhook-test/...")
    std::string method() const { return methods.empty() ? (kind == Kind::Webhook ? "GET" : "POST") : methods[0]; }
    std::string kindName() const;
    std::string authLabel() const;
    bool usable() const { return wfActive && !wfArchived && !disabled && (kind != Kind::Chat || isPublic); }
};

// workflow object from GET /workflows (filtered or full). Appends one Endpoint per trigger node.
void discover(JsonObjectConst workflow, std::vector<Endpoint>& out);

// full workflow (GET /workflows/{id}): fills ep.fields / ep.respond / ep.ai by reading what the nodes do with the request
void analyze(JsonObjectConst workflow, Endpoint& ep);

// stored credentials for calling an endpoint (device-side; n8n never hands secrets out)
struct Secret {
    std::string user, pass;      // basicAuth
    std::string header, value;   // headerAuth (header name defaults to Authorization)
    std::string token;           // jwtAuth / bearerAuth
};
Secret loadSecret(const std::string& endpointId);
void saveSecret(const std::string& endpointId, const Secret& s);
// headers (name, value) that authenticate a request to ep with s
std::vector<std::pair<std::string, std::string>> authHeaders(const Endpoint& ep, const Secret& s);

// pulls the assistant text out of the usual reply shapes (OpenAI choices[], n8n Chat {output}, {text}, {message}, [{...}])
std::string replyText(const std::string& body);

// the keys the filter for GET /workflows must keep for discover()
const char* listFilter();

struct Agent {
    std::string wfId, wfName, nodeName;
    bool wfActive = false, wfArchived = false;
    std::string model, provider, memory, prompt;   // prompt = system message (start)
    std::vector<std::string> tools;                // tool node names connected to it
    bool messagesAgent = false;                    // a "Message an Agent" node (calls an n8n Agent)
    bool isSub = false;                            // Agent Tool: used by another agent
};
const char* agentFilter();                          // listFilter() + connections + model/tool/prompt parameters
void discoverAgents(JsonObjectConst workflow, std::vector<Agent>& out);

}  // namespace wh
