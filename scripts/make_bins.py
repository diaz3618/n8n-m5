# After build: copy the app-only .bin (for Launcher/SD) and a merged flash image (esptool at 0x0 / M5Burner) to dist/
Import("env")
import os, shutil, subprocess

def post(source, target, env):
    name = env["PIOENV"]
    out = os.path.join(env.subst("$PROJECT_DIR"), "dist")
    os.makedirs(out, exist_ok=True)
    app = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    shutil.copy(app, os.path.join(out, f"n8n-remote-{name}-launcher.bin"))
    board = env.BoardConfig()
    chip = board.get("build.mcu", "esp32s3")
    parts = []
    for off, f in env.get("FLASH_EXTRA_IMAGES", []):
        parts += [off, env.subst(f)]
    parts += [env.subst("$ESP32_APP_OFFSET"), app]
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    merged = os.path.join(out, f"n8n-remote-{name}-merged.bin")
    subprocess.check_call([env.subst("$PYTHONEXE"), esptool, "--chip", chip, "merge_bin", "-o", merged,
                           "--flash_mode", board.get("build.flash_mode", "qio"), "--flash_size", "16MB"] + parts)
    print("dist/: wrote launcher + merged images for", name)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", post)
