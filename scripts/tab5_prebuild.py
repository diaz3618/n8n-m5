# Tab5 TLS 1.3 build: the SDK is rebuilt, and the component manager would fetch the newest esp_hosted (2.12.13). The Tab5's C6 slave (2.12.x,
# e.g. 2.12.6) answers that host's wifi_init with INVALID_ARG -> no Wi-Fi at all. Seed the solver with the stock libs' lock (esp_hosted 2.12.8,
# the version Launcher/Bruce use) so every component resolves exactly like the prebuilt SDK.
import shutil
from pathlib import Path
Import("env")
root = Path(env.subst("$PROJECT_DIR"))
shutil.copy(root / "scripts" / "tab5.dependencies.lock", root / "dependencies.lock")
