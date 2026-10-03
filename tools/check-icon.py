#!/usr/bin/env python3
"""Checks assets/icon0.png against what the console's installed apps use."""
import os
import sys

from PIL import Image

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "icon0.png")
im = Image.open(path)
ok = True


def check(cond, what):
    global ok
    print(("ok   " if cond else "FAIL ") + what)
    ok = ok and cond


check(im.format == "PNG", "a real PNG (not a JPEG with a .png name)")
check(im.size == (512, 512), "512x512, like Shadow Mount+ and WebKit Autoloader")
check(im.mode in ("RGBA", "LA", "P"), "has an alpha channel")
rgba = im.convert("RGBA")
a = rgba.getchannel("A")
lo, hi = a.getextrema()
check(lo == 0 and hi == 255, "uses the whole alpha range (transparent background, opaque body)")
check(rgba.getpixel((2, 2))[3] == 0 and rgba.getpixel((509, 509))[3] == 0, "corners are transparent")
bad = sum(1 for (r, g, b, al) in rgba.getdata() if al == 0 and (r, g, b) != (0, 0, 0))
check(bad == 0, "transparent pixels are stored black (an alpha-less renderer shows black)")
bbox = a.point(lambda v: 255 if v > 8 else 0).getbbox()
margin = min(bbox[0], bbox[1], 512 - bbox[2], 512 - bbox[3])
check(margin >= 16, f"keeps a margin around the picture ({margin} px)")
size = os.path.getsize(path)
check(size < 250 * 1024, f"small enough ({size // 1024} KB; Payload Manager's is 231 KB)")
sys.exit(0 if ok else 1)
