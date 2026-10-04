#!/usr/bin/env bash
# One-time setup of the PlatformIO dir used for the Tab5 TLS 1.3 builds (pioarduino 55.03.39, Arduino 3.3.9, esp_hosted 2.12.8).
# That platform asks for scons 4.8.1 and replaces the 4.11.1 that PlatformIO Core 6.2 runs on, which kills the SDK rebuild
# ("No module named SCons.Tool.FortranCommon"). Point it at 4.11.1 instead.
#   scripts/setup_tab5_core.sh   (run once after the first failed `pio run -e tab5-tls13`, which installs the platform)
set -e
D=$HOME/.platformio-n8n-tab5-tls13
cd "$(dirname "$0")/.."
sed -i 's/4\.40801\.0/4.41101.0/;s#scons-4.8.1.zip#scons-4.11.1.zip#' "$D/platforms/espressif32/platform.json"
if [ ! -d "$D/packages/tool-scons" ]; then
  if [ -d "$HOME/.platformio-n8n-tab5/packages/tool-scons" ]; then cp -r "$HOME/.platformio-n8n-tab5/packages/tool-scons" "$D/packages/"
  else echo "build env:tab5 once (PLATFORMIO_CORE_DIR=$HOME/.platformio-n8n-tab5) to fetch tool-scons, then rerun"; exit 1; fi
fi
python3 scripts/patch_tab5_platform.py
echo ok
