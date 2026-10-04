#!/usr/bin/env python3
"""Backport pioarduino 55.3.x's copy_idf_component_archives() into the 55.03.39 platform used for the Tab5 TLS 1.3 build.

Without it the SDK rebuild only copies top-level component archives, so mbedtls' nested libs (mbedtls/mbedtls/library/*.a, where the
TLS 1.3 code lives) never replace the stock ones and the firmware links a TLS 1.2-only mbedtls.  Idempotent."""
import re, sys
from pathlib import Path
p = Path.home() / ".platformio-n8n-tab5-tls13/platforms/espressif32/builder/frameworks/espidf.py"
helpers = (Path(__file__).parent / "idf_copy_archives.py.txt").read_text()
s = p.read_text()
if "def copy_idf_component_archives" in s:
    print("already patched"); sys.exit(0)
old = re.compile(r"        src = \[str\(Path\(lib_src\) / x\) for x in os\.listdir\(lib_src\)\]\n.*?shutil\.copyfile\(file, str\(Path\(lib_dst\) / file\.split\(os\.path\.sep\)\[-1\]\)\)\n", re.S)
new = "        copy_idf_component_archives(lib_src, lib_dst, str(Path(arduino_libs) / chip_variant / \"pioarduino-build.py\"))\n"
s2, n = old.subn(new, s, count=1)
if n != 1:
    sys.exit("copy loop not found")
s2 = s2.replace("def get_requested_cli_targets():", helpers + "\n\ndef get_requested_cli_targets():", 1)
p.write_text(s2)
print("patched")
