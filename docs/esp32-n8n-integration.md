# Using an ESP32 with n8n

n8n has no "run workflow" API call, so a device reaches a workflow through a trigger node that exposes an HTTP endpoint.
The Webhooks screen finds these and the AI & Agents screen lists the AI Agent nodes behind them.

| Trigger | Path | Notes |
|---|---|---|
| Webhook | `/webhook/<path>` | any method; none, basic, header or JWT auth |
| Chat Trigger | `/webhook/<webhookId>/chat` | POST `{action: "sendMessage", sessionId, chatInput}`; must be public |
| Form Trigger | `/form/<path>` | fields are named `field-0`, `field-1`, ... |
| MCP Server Trigger | `/mcp/<path>` | JSON-RPC (`tools/list`, `tools/call`), bearer auth |

n8n never returns credentials, so a protected endpoint's secret is entered once on the device and kept in NVS.

n8n's own Agents feature has no public API, so it cannot be listed or driven directly. A workflow with a Chat, Webhook or
MCP trigger in front of an AI Agent node is the supported way in.

## OpenAI-compatible bridge

A webhook at `POST /v1/chat/completions` (and `GET /v1/models`) that wraps an AI Agent or LLM node and answers in OpenAI's shape
lets anything that speaks that API use your n8n models, tools and memory without holding a provider key. The Chat screen
detects these endpoints and lists the models.

## Ideas

- Voice: record on the device, POST the WAV to a webhook (speech to text, agent, text to speech), play the answer back.
- Camera: send a CoreS3 snapshot to a vision model and show the answer, or store time-lapse frames.
- Image and video generation: start the job from the device, show the result or poll a second webhook for async jobs.
- Sensors and buttons: post readings on a timer, or use a button as a trigger for a workflow.
- Notifications: let n8n push messages to the device (polling webhook or MQTT).
- MCP: call n8n tools from the device through an MCP Server Trigger.
- Home automation: scene buttons that call webhooks.

Keep replies small (use filters and `limit`), downscale images in n8n before sending them back, and use header or basic auth on
public webhooks.
