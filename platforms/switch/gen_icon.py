#!/usr/bin/env python3
"""Generate a 256x256 home/hbmenu icon (icon.jpg) for the Switch build.
Themed (Zelda gold Triforce on a green gradient). Supersampled for clean edges.
Run in a Pillow-capable env (see build_icon.sh, which uses a Docker python image)."""
import math
from PIL import Image, ImageDraw, ImageFont

S = 1024  # supersample, downscaled to 256 at the end


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


img = Image.new("RGB", (S, S))
d = ImageDraw.Draw(img)

# Vertical green gradient background.
top, bot = (9, 38, 18), (32, 100, 46)
for y in range(S):
    d.line([(0, y), (S, y)], fill=lerp(top, bot, y / S))

# Soft radial highlight behind the Triforce.
glow = Image.new("L", (S, S), 0)
gd = ImageDraw.Draw(glow)
gd.ellipse([S * 0.18, S * 0.05, S * 0.82, S * 0.7], fill=70)
img = Image.composite(Image.new("RGB", (S, S), (60, 150, 80)), img, glow)
d = ImageDraw.Draw(img)

# Triforce: three gold triangles forming a larger one with a hollow centre.
gold, goldD, goldHi = (255, 214, 74), (176, 132, 24), (255, 240, 170)
size = S * 0.52
topY = S * 0.15
cx = S / 2
h = size * math.sqrt(3) / 2
tp = (cx, topY)
bl = (cx - size / 2, topY + h)
br = (cx + size / 2, topY + h)
ml = ((tp[0] + bl[0]) / 2, (tp[1] + bl[1]) / 2)
mr = ((tp[0] + br[0]) / 2, (tp[1] + br[1]) / 2)
mb = ((bl[0] + br[0]) / 2, (bl[1] + br[1]) / 2)
ow = max(2, S // 170)
# drop shadow
sh = int(S * 0.012)
for p in ([tp, ml, mr], [ml, bl, mb], [mr, mb, br]):
    d.polygon([(x + sh, y + sh) for x, y in p], fill=(0, 0, 0))
for p in ([tp, ml, mr], [ml, bl, mb], [mr, mb, br]):
    d.polygon(p, fill=gold, outline=goldD, width=ow)
# tiny top highlight on each triangle apex
for apex in (tp, ml, mr):
    r = S * 0.012
    d.ellipse([apex[0] - r, apex[1] - r, apex[0] + r, apex[1] + r], fill=goldHi)


def load_font(px):
    for path in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "C:/Windows/Fonts/arialbd.ttf",
    ):
        try:
            return ImageFont.truetype(path, px)
        except OSError:
            continue
    return ImageFont.load_default()


def draw_centered(text, font, y, fill, outline=(8, 26, 14)):
    bbox = d.textbbox((0, 0), text, font=font)
    w = bbox[2] - bbox[0]
    x = (S - w) / 2 - bbox[0]
    o = max(2, S // 150)
    for dx in range(-o, o + 1):
        for dy in range(-o, o + 1):
            d.text((x + dx, y + dy), text, font=font, fill=outline)
    d.text((x, y), text, font=font, fill=fill)


draw_centered("THE", load_font(int(S * 0.085)), S * 0.66, (220, 240, 210))
draw_centered("MINISH CAP", load_font(int(S * 0.135)), S * 0.74, (255, 240, 170))

# Gold frame.
bw = int(S * 0.028)
d.rounded_rectangle(
    [bw // 2, bw // 2, S - bw // 2, S - bw // 2],
    radius=int(S * 0.06), outline=(255, 214, 74), width=bw,
)

img = img.resize((256, 256), Image.LANCZOS)
img.save("platforms/switch/icon.jpg", quality=95)
img.save("platforms/switch/icon_preview.png")
print("wrote platforms/switch/icon.jpg + icon_preview.png")
