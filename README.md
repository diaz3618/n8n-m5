# n8n Remote

A touchscreen client for [n8n](https://n8n.io) on the M5Stack CoreS3 and Tab5. Enter your server URL and API key and manage
what the n8n public API exposes, from the device.

- Workflows: publish/unpublish, filter by status, tags and archived, rename, transfer, history, delete, edit on a canvas
- Executions: filter, retry, stop, delete, view data
- Credentials, tags, variables, users, projects, folders, data tables, roles, community nodes, insights, audit, source control
- Webhooks: finds Webhook, Form, Chat and MCP triggers, shows how each is called and authenticated, and calls them
- Chat with AI workflows (n8n Chat Trigger or an OpenAI-compatible webhook) and an overview of the AI agents in your workflows
- API Explorer for every public endpoint (`docs/n8n-api-routes.txt`)
- Light and dark theme, Tab5 keyboard support, SD backup, setup from a phone

| Device | Chip | Screen | Environment |
|---|---|---|---|
| CoreS3 | ESP32-S3 | 320x240 | `cores3` |
| Tab5 | ESP32-P4 (pre-rev3, not P4X) | 1280x720 | `tab5` |

## Install

Download a release image, or build it yourself (below).

- Launcher (bmorcelli): copy `n8n-remote-<device>-launcher.bin` to the SD card root and install it from Launcher.
- esptool or M5Burner: `esptool.py write_flash 0x0 n8n-remote-<device>-merged.bin`

Use the `-tls13` image if your server only accepts TLS 1.3 (some Cloudflare setups).

## Build

```
python3 -m venv .venv && .venv/bin/pip install platformio
.venv/bin/pio run -e cores3 -e tab5
```

Images are written to `dist/`. The `*-tls13` environments rebuild the Arduino SDK and modify the platform package, so build
each of them with its own `PLATFORMIO_CORE_DIR`, for example
`PLATFORMIO_CORE_DIR=$HOME/.platformio-n8n-tls13 .venv/bin/pio run -e cores3-tls13`. For `tab5-tls13` run
`scripts/setup_tab5_core.sh` once, after the first (failing) build has fetched the platform.

## First run

Overview lists the steps: Wi-Fi, server URL, API key, test. Create the key in n8n under Settings > n8n API. For a long key use
Settings > Setup from phone/PC, or put an `n8n-remote.json` on the SD card and use Settings > Import from SD.

## Tab5 notes

Wi-Fi on the Tab5 comes from an ESP32-C6 running an ESP-Hosted slave. The host side in the firmware has to be compatible with it:
Arduino core 3.3.12 (esp_hosted 2.12.13) is refused by a 2.12.6 slave. The Tab5 environments use pioarduino 55.03.39
(Arduino 3.3.9, esp_hosted 2.12.8), and the TLS 1.3 rebuild is pinned to the same component versions through
`scripts/tab5.dependencies.lock`. The C6 firmware is never modified.

## Security

The API key is stored in NVS (not encrypted unless flash encryption is enabled), masked in the UI and never logged.
Redirects are not followed. TLS is verified by default; custom CA and insecure modes are explicit settings. The phone setup
portal is plain HTTP on the LAN and protected by a PIN. Webhook credentials are kept on the device, since n8n never returns secrets.

## Development

`make -C sim` builds a host simulator that renders the real UI, and `sim/run_selftest.sh` runs the unit tests for the
workflow editing and webhook discovery code. Builds with `-dev` in the environment name add a USB serial console and are not
meant for release. `git config core.hooksPath .githooks` enables a pre-commit check for secrets and a pre-push test run.
Releases are built by GitHub Actions when a `v*` tag is pushed.

## License

GPL-3.0, see `LICENSE`.
