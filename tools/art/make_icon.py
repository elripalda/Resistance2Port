"""Draws project/launcher.ico: the R2 mark under a Chimera's glowing eyes (original art, no game assets)."""
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

S = 1024
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'project', 'launcher.ico')


def font(size, style):
    f = ImageFont.truetype(os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts', 'bahnschrift.ttf'), size)
    try:
        f.set_variation_by_name(style)
    except Exception:
        pass
    return f


def rounded_mask(size, radius):
    m = Image.new('L', (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, size - 1, size - 1), radius, fill=255)
    return m


def eyes_layer(cx, cy, w, h, gap, slant):
    """Two angular, slanted eye slits (a scowl), as a mask."""
    m = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(m)
    for side in (-1, 1):
        inner = cx + side * gap / 2
        outer = inner + side * w
        # inner corner low, outer corner high: an angry, inhuman squint
        pts = [(inner, cy + slant), (outer, cy - slant - h * 0.35), (outer - side * w * 0.18, cy - slant + h * 0.35),
               (inner + side * w * 0.10, cy + slant + h * 0.55)]
        d.polygon(pts, fill=255)
    return m


def draw(size_hint):
    small = size_hint <= 32
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    # background: a dark steel plate, lighter at the top
    bg = Image.new('RGBA', (S, S))
    bd = ImageDraw.Draw(bg)
    for y in range(S):
        t = y / S
        c = (int(34 - 20 * t), int(32 - 19 * t), int(32 - 18 * t), 255)
        bd.line((0, y, S, y), fill=c)
    radius = 200 if not small else 160
    img.paste(bg, (0, 0), rounded_mask(S, radius))

    # the eyes' glow
    cy = 330 if not small else 300
    ew, eh, gap, sl = (300, 120, 110, 26) if not small else (360, 190, 90, 30)
    em = eyes_layer(S / 2, cy, ew, eh, gap, sl)
    for blur, alpha, col in ((140, 170, (255, 90, 10)), (60, 220, (255, 130, 30)), (18, 255, (255, 170, 60))):
        g = em.filter(ImageFilter.GaussianBlur(blur))
        g = g.point(lambda v, a=alpha: min(255, v * a // 160))
        layer = Image.new('RGBA', (S, S), col + (0,))
        layer.putalpha(g)
        img = Image.alpha_composite(img, layer)
    core = Image.new('RGBA', (S, S), (255, 236, 190, 0))
    core.putalpha(em.filter(ImageFilter.GaussianBlur(4)))
    img = Image.alpha_composite(img, core)
    hot = eyes_layer(S / 2, cy, ew * 0.55, eh * 0.35, gap + ew * 0.25, sl * 0.6)
    pupil = Image.new('RGBA', (S, S), (255, 255, 240, 0))
    pupil.putalpha(hot.filter(ImageFilter.GaussianBlur(6)))
    img = Image.alpha_composite(img, pupil)

    # R2
    if not small:
        f = font(520, 'Bold Condensed') if True else None
        txt = Image.new('L', (S, S), 0)
        td = ImageDraw.Draw(txt)
        bbox = td.textbbox((0, 0), 'R2', font=f)
        w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
        x, y = (S - w) / 2 - bbox[0], 900 - h - bbox[1]
        td.text((x, y), 'R2', font=f, fill=255)
        # metal: light at the top of the letters, darker below, with the eyes' orange reflected on the top edge
        metal = Image.new('RGBA', (S, S))
        md = ImageDraw.Draw(metal)
        for yy in range(S):
            t = max(0.0, min(1.0, (yy - (900 - h)) / h))
            md.line((0, yy, S, yy), fill=(int(232 - 90 * t), int(226 - 92 * t), int(220 - 94 * t), 255))
        shadow = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        shadow.putalpha(ImageChops.offset(txt, 0, 14).filter(ImageFilter.GaussianBlur(10)).point(lambda v: v * 200 // 255))
        img = Image.alpha_composite(img, shadow)
        metal.putalpha(txt)
        img = Image.alpha_composite(img, metal)
        rim = ImageChops.subtract(txt, ImageChops.offset(txt, 0, 10))
        rl = Image.new('RGBA', (S, S), (255, 150, 60, 0))
        rl.putalpha(rim.point(lambda v: v * 170 // 255))
        img = Image.alpha_composite(img, rl)

    # thin bevel on the plate's edge
    edge = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(edge).rounded_rectangle((6, 6, S - 7, S - 7), radius - 6, outline=(255, 255, 255, 34), width=10)
    img = Image.alpha_composite(img, edge)
    img.putalpha(ImageChops.multiply(img.getchannel('A'), rounded_mask(S, radius)))
    return img


big, small = draw(256), draw(16)
sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
frames = [(small if s <= 32 else big).resize((s, s), Image.LANCZOS) for s in sizes]
frames[-1].save(OUT, format='ICO', sizes=[(s, s) for s in sizes], append_images=frames[:-1])
big.resize((512, 512), Image.LANCZOS).save(os.path.join(os.environ.get('TEMP', '.'), 'r2icon_preview.png'))
sheet = Image.new('RGBA', (16 + 24 + 32 + 48 + 64 + 128 + 70, 140), (40, 40, 44, 255))
x = 10
for s in (16, 24, 32, 48, 64, 128):
    sheet.alpha_composite((small if s <= 32 else big).resize((s, s), Image.LANCZOS), (x, 6))
    x += s + 10
sheet.save(os.path.join(os.environ.get('TEMP', '.'), 'r2icon_sizes.png'))
print('wrote', os.path.normpath(OUT))
