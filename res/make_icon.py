import os
from PIL import Image, ImageDraw

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
S = 1024
SS = 4  # supersample
W = S * SS

# 背景圆角方块（蓝色渐变，Win11 accent 风格）
img = Image.new("RGBA", (W, W), (0, 0, 0, 0))

grad = Image.new("RGBA", (W, W), (0, 0, 0, 0))
gd = ImageDraw.Draw(grad)
top = (60, 168, 245)
bot = (10, 88, 168)
for y in range(W):
    f = y / (W - 1)
    r = int(top[0] + (bot[0] - top[0]) * f)
    g = int(top[1] + (bot[1] - top[1]) * f)
    b = int(top[2] + (bot[2] - top[2]) * f)
    gd.line([(0, y), (W, y)], fill=(r, g, b, 255))

mask = Image.new("L", (W, W), 0)
md = ImageDraw.Draw(mask)
pad = W * 0.055
radius = W * 0.225
md.rounded_rectangle([pad, pad, W - pad, W - pad], radius=radius, fill=255)

img.paste(grad, (0, 0), mask)

# 顶部柔和高光（渐变衰减，避免硬边）
hl = Image.new("RGBA", (W, W), (0, 0, 0, 0))
hal = Image.new("L", (W, W), 0)
hld = ImageDraw.Draw(hal)
for y in range(W):
    f = max(0.0, 1.0 - y / (W * 0.55))
    hld.line([(0, y), (W, y)], fill=int(46 * f * f))
hl.putalpha(Image.composite(hal, Image.new("L", (W, W), 0), mask))
img = Image.alpha_composite(img, hl)

# 白色对勾
d = ImageDraw.Draw(img)
lw = int(W * 0.105)
pts = [(W * 0.275, W * 0.525), (W * 0.445, W * 0.695), (W * 0.735, W * 0.345)]

def cap(p, r, fill=(255, 255, 255, 255)):
    d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=fill)

for i in range(len(pts) - 1):
    d.line([pts[i], pts[i + 1]], fill=(255, 255, 255, 255), width=lw, joint="curve")
for p in pts:
    cap(p, lw / 2)

img = img.resize((S, S), Image.LANCZOS)

sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
img.save(os.path.join(OUT_DIR, "icon.ico"), format="ICO",
         sizes=[(s, s) for s in sizes])
img.save(os.path.join(OUT_DIR, "icon.png"), format="PNG")
print("ok", img.size)
