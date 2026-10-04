// Webhook / chat / agents screens (pages_hooks.cpp, chat.cpp, pages_agents.cpp)
#pragma once
#include <string>
#include <vector>

#include "../core/webhooks.h"
#include "nav.h"

namespace ui {
PagePtr makeEndpointPage(const wh::Endpoint& e);
const std::vector<wh::Endpoint>& endpointCache();
// chat with an endpoint: n8n Chat Trigger, OpenAI-compatible webhook, or any webhook taking a text field (`field`)
PagePtr makeChat(const wh::Endpoint& e, const std::string& field);
}  // namespace ui
