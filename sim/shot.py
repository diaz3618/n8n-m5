#!/usr/bin/env python3
"""python3 shot.py <device> <theme> <section> <name> [sim args...] -> shots/<name>.png"""
import subprocess, sys
from PIL import Image
dev, th, sec, name, *rest = sys.argv[1:]
subprocess.run(["./sim", dev, th, sec, f"shots/{name}.ppm", *rest], check=True, stdout=subprocess.DEVNULL, timeout=60)
Image.open(f"shots/{name}.ppm").save(f"shots/{name}.png")
print(f"shots/{name}.png")
