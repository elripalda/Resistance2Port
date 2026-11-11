"""Draws the Resistance 2 port's sidebar artwork (original art, no game assets).

    python tools/art/make_r2_art.py [--preview DIR]

writes
    project/launcher_side.png  a tall 480x1000 picture (used by the installer's wizard images,
                               tools/installer/make_wizard_images.py)

The scene: an Arctic coast at the end of the day. Dark basalt cliffs with column
joints, a cold slate sea, and far out on the water a huge stepped alien tower
lit by rows of red-amber lights, under a heavy sky with drifting ash and snow.
Palette: charcoal and slate with a deep red / amber accent (R1 is warm orange,
R3 is cyan). Everything is drawn with PIL from shapes and noise (fixed seed, so
the output is reproducible); no fonts or outside images are used.
"""
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, '..', '..', 'project'))

# palette
BASE = (13, 12, 14)          # what the picture fades into at the bottom
SKY_TOP = (10, 10, 13)
SKY_LOW = (74, 34, 28)       # the sky over the horizon, lit by the tower
CLOUD = (66, 62, 66)
SEA = (17, 23, 29)
BASALT = (9, 9, 11)
RED = (226, 54, 26)          # accent: the tower's lights
AMBER = (255, 150, 60)
HOT = (255, 226, 180)


def vgrad(w, h, stops, a=255):
    """Vertical gradient through (t, colour) stops."""
    im = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / max(1, h - 1)
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t0 <= t <= t1:
                k = (t - t0) / max(1e-6, t1 - t0)
                d.line((0, y, w, y), fill=tuple(int(c0[i] + (c1[i] - c0[i]) * k) for i in range(3)) + (a,))
                break
    return im


def glow(img, mask, layers):
    for blur, gain, col in layers:
        g = mask.filter(ImageFilter.GaussianBlur(blur)) if blur else mask
        g = g.point(lambda v, k=gain: min(255, int(v * k)))
        lay = Image.new('RGBA', img.size, col + (0,))
        lay.putalpha(g)
        img = Image.alpha_composite(img, lay)
    return img


def tint(img, mask, col):
    lay = Image.new('RGBA', img.size, col + (0,))
    lay.putalpha(mask)
    return Image.alpha_composite(img, lay)


def tower_mask(w, h, cx, base_y, top_y, width):
    """A massive stepped tower: a broad splayed foot, three tiers that narrow
    upwards, side buttresses and a crown of prongs around a central mast."""
    m = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(m)
    H = base_y - top_y
    # foot, flaring out into the sea
    d.polygon([(cx - width * 0.62, base_y), (cx - width * 0.30, base_y - H * 0.10),
               (cx + width * 0.30, base_y - H * 0.10), (cx + width * 0.62, base_y)], fill=255)
    tiers = ((0.10, 0.40, 0.30, 0.25), (0.38, 0.62, 0.21, 0.17), (0.60, 0.80, 0.14, 0.10))
    for lo, hi, wl, wh in tiers:
        y0, y1 = base_y - H * lo, base_y - H * hi
        d.polygon([(cx - width * wl, y0), (cx - width * wh, y1), (cx + width * wh, y1), (cx + width * wl, y0)], fill=255)
        # a ledge at the top of each tier
        d.polygon([(cx - width * (wh + 0.06), y1 + H * 0.012), (cx - width * wh, y1 - H * 0.01),
                   (cx + width * wh, y1 - H * 0.01), (cx + width * (wh + 0.06), y1 + H * 0.012)], fill=255)
    # buttresses leaning on the lower tiers
    for side in (-1, 1):
        d.polygon([(cx + side * width * 0.52, base_y - H * 0.02), (cx + side * width * 0.26, base_y - H * 0.34),
                   (cx + side * width * 0.24, base_y - H * 0.30), (cx + side * width * 0.40, base_y - H * 0.02)], fill=255)
        d.polygon([(cx + side * width * 0.30, base_y - H * 0.40), (cx + side * width * 0.17, base_y - H * 0.58),
                   (cx + side * width * 0.16, base_y - H * 0.54), (cx + side * width * 0.24, base_y - H * 0.40)], fill=255)
    # crown: prongs curving inwards around the mast
    cy = base_y - H * 0.80
    for side in (-1, 1):
        for k, (spread, tall) in enumerate(((0.11, 0.10), (0.06, 0.13))):
            bx = cx + side * width * spread
            d.polygon([(bx - width * 0.022, cy), (bx + side * width * 0.01 * (1 - k), cy - H * tall),
                       (bx - side * width * 0.012, cy - H * tall * 0.95), (bx + width * 0.022, cy)], fill=255)
    d.polygon([(cx - width * 0.018, cy), (cx - width * 0.006, top_y + H * 0.02), (cx, top_y),
               (cx + width * 0.006, top_y + H * 0.02), (cx + width * 0.018, cy)], fill=255)
    return m


def cliff_mask(w, h, rnd, pts):
    m = Image.new('L', (w, h), 0)
    ImageDraw.Draw(m).polygon(pts + [(pts[-1][0], h), (pts[0][0], h)], fill=255)
    return m


def jagged(rnd, x0, x1, y_at, step=(6, 16), jitter=6):
    pts, x = [], x0
    while x < x1:
        pts.append((x, y_at(x) + rnd.uniform(-jitter, jitter)))
        x += rnd.uniform(*step)
    pts.append((x1, y_at(x1)))
    return pts


def draw_sidebar(w=480, h=1000):
    rnd = random.Random(2008)
    horizon = int(h * 0.64)
    img = vgrad(w, h, ((0.0, SKY_TOP), (0.30, (24, 18, 22)), (0.56, (52, 28, 26)), (0.64, SKY_LOW), (1.0, SKY_LOW)))

    tx, top_y, tw = int(w * 0.60), int(h * 0.10), 230

    # the sky behind the tower glows red, brightest low down
    halo = Image.new('L', (w, h), 0)
    ImageDraw.Draw(halo).ellipse((tx - 260, horizon - 330, tx + 260, horizon + 60), fill=150)
    img = glow(img, halo, ((90, 0.9, (120, 36, 22)),))

    # heavy cloud banks: dark masses on top, red-lit undersides
    for layer, (y0, y1) in enumerate(((0, 0.22), (0.12, 0.40), (0.30, 0.56))):
        cm = Image.new('L', (w, h), 0)
        lit = Image.new('L', (w, h), 0)
        cd, ld = ImageDraw.Draw(cm), ImageDraw.Draw(lit)
        for _ in range(70):
            cx = rnd.uniform(-80, w + 80)
            cy = rnd.uniform(y0 * h, y1 * h)
            rx, ry = rnd.uniform(50, 170), rnd.uniform(10, 30)
            v = rnd.randrange(60, 150)
            cd.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=v)
            ld.ellipse((cx - rx * 0.8, cy + ry * 0.2, cx + rx * 0.8, cy + ry * 1.1), fill=v)
        cm = cm.filter(ImageFilter.GaussianBlur(12 - layer * 2))
        lit = ImageChops.subtract(lit.filter(ImageFilter.GaussianBlur(8)), cm.filter(ImageFilter.GaussianBlur(3)))
        img = tint(img, cm.point(lambda v: min(255, v * 230 // 255)), (30 + layer * 10, 26 + layer * 4, 30 + layer * 3))
        img = tint(img, lit.point(lambda v, k=layer: min(255, v * (90 + 70 * k) // 255)), (168, 58, 34))
    # fine streaks of wind-driven cloud
    st = Image.new('L', (w, h), 0)
    sd = ImageDraw.Draw(st)
    for _ in range(60):
        y = rnd.uniform(0.05, 0.55) * h
        x = rnd.uniform(-100, w)
        sd.line((x, y, x + rnd.uniform(80, 260), y + rnd.uniform(-6, 2)), fill=rnd.randrange(30, 90), width=rnd.randrange(1, 4))
    img = tint(img, st.filter(ImageFilter.GaussianBlur(2)), CLOUD)

    # far mountains on the left horizon, pale with distance
    mt = Image.new('L', (w, h), 0)
    md = ImageDraw.Draw(mt)
    pts = [(-10, horizon)]
    x = -10
    while x < w * 0.48:
        x += rnd.uniform(14, 34)
        pts.append((x, horizon - 18 - 30 * max(0.0, 1 - abs(x - w * 0.18) / (w * 0.3)) + rnd.uniform(-6, 6)))
    pts += [(w * 0.5, horizon), (-10, horizon)]
    md.polygon(pts, fill=255)
    img = tint(img, mt, (40, 30, 34))
    snowcap = ImageChops.subtract(mt, ImageChops.offset(mt, 0, -4))
    img = tint(img, snowcap.point(lambda v: v * 90 // 255), (170, 150, 150))

    # the tower, dimmed by distance and haze
    tm = tower_mask(w, h, tx, horizon + 4, top_y, tw)
    img = glow(img, tm, ((26, 0.5, (140, 40, 24)),))
    img = tint(img, tm, (22, 17, 20))
    # the side away from the light is darker; a thin red rim on the lit side
    shade = ImageChops.multiply(tm, Image.linear_gradient('L').rotate(90, expand=True).resize((w, h)))
    img = tint(img, shade.point(lambda v: v * 120 // 255), (8, 7, 9))
    rim = ImageChops.subtract(tm, ImageChops.offset(tm, -3, 0))
    img = glow(img, rim, ((0, 0.55, (180, 60, 36)),))
    # rows of lights on the tiers, a ring at the crown and a beacon on the mast
    H = horizon + 4 - top_y
    lights = Image.new('L', (w, h), 0)
    ld = ImageDraw.Draw(lights)
    for lo, half in ((0.16, 0.27), (0.27, 0.25), (0.45, 0.19), (0.54, 0.18), (0.66, 0.12), (0.74, 0.11)):
        y = horizon + 4 - H * lo
        n = max(3, int(half * tw / 7))
        for i in range(n):
            if rnd.random() < 0.82:
                lx = tx - half * tw + (2 * half * tw) * (i + 0.5) / n
                ld.rectangle((lx - 1.2, y - 1, lx + 1.2, y + 1.5), fill=rnd.randrange(170, 256))
    cy = horizon + 4 - H * 0.80
    for side in (-1, 1):
        for spread in (0.11, 0.06):
            ld.ellipse((tx + side * tw * spread - 2, cy - 4, tx + side * tw * spread + 2, cy), fill=255)
    ld.ellipse((tx - 2.5, top_y + 6, tx + 2.5, top_y + 11), fill=255)
    img = glow(img, lights, ((10, 1.6, RED), (3, 1.3, AMBER), (0, 1.0, HOT)))
    beacon = Image.new('L', (w, h), 0)
    ImageDraw.Draw(beacon).ellipse((tx - 3, top_y + 5, tx + 3, top_y + 12), fill=255)
    img = glow(img, beacon, ((40, 1.0, RED), (14, 1.2, AMBER)))
    # the open core: a slit of light down the middle tier
    core = Image.new('L', (w, h), 0)
    ImageDraw.Draw(core).polygon([(tx - 2, horizon - H * 0.22), (tx, horizon - H * 0.52), (tx + 2, horizon - H * 0.22)], fill=230)
    img = glow(img, core, ((12, 1.0, RED), (2, 1.0, AMBER)))

    # haze along the horizon
    hz = Image.new('L', (w, h), 0)
    hd = ImageDraw.Draw(hz)
    for y in range(h):
        t = 1 - min(1.0, abs(y - horizon) / (h * 0.05))
        hd.line((0, y, w, y), fill=int(110 * t * t))
    img = tint(img, hz, (110, 54, 44))

    # the sea: cold slate, with long swells and the tower's lights reflected in it
    sea_h = h - horizon
    sea = vgrad(w, sea_h, ((0.0, (36, 32, 38)), (0.15, SEA), (1.0, (8, 11, 14))))
    img.paste(sea, (0, horizon))
    sw = Image.new('L', (w, h), 0)
    swd = ImageDraw.Draw(sw)
    for _ in range(260):
        y = horizon + 2 + (rnd.random() ** 1.6) * sea_h * 0.6
        depth = (y - horizon) / (sea_h * 0.6)
        ln = rnd.uniform(10, 60) * (0.4 + depth * 1.6)
        x = rnd.uniform(-ln, w)
        swd.line((x, y, x + ln, y), fill=rnd.randrange(30, 90), width=1 if depth < 0.5 else 2)
    img = tint(img, sw, (96, 108, 118))
    refl = Image.new('L', (w, h), 0)
    rd = ImageDraw.Draw(refl)
    for _ in range(170):
        y = horizon + 3 + (rnd.random() ** 1.3) * sea_h * 0.55
        spread = 10 + (y - horizon) * 0.35
        x = tx + rnd.gauss(0, spread * 0.45)
        ln = rnd.uniform(3, 10) + (y - horizon) * 0.06
        rd.line((x - ln, y, x + ln, y), fill=rnd.randrange(80, 230), width=1)
    img = glow(img, refl, ((3, 0.9, RED), (0, 0.8, AMBER)))

    # foreground basalt: a tall cliff on the left, a lower headland on the right
    left_edge = lambda x: h * 0.50 + (x / (w * 0.42)) ** 1.8 * h * 0.30
    lp = [(-10, h * 0.48)] + jagged(rnd, 0, w * 0.44, left_edge, (5, 12), 4) + [(w * 0.47, h * 0.86), (w * 0.52, h)]
    lm = cliff_mask(w, h, rnd, lp)
    right_edge = lambda x: h * 0.78 - ((x - w * 0.56) / (w * 0.44)) * h * 0.08
    rp = [(w * 0.52, h)] + [(w * 0.56, h * 0.80)] + jagged(rnd, w * 0.58, w + 10, right_edge, (6, 14), 4)
    rm = cliff_mask(w, h, rnd, rp)
    rocks = ImageChops.lighter(lm, rm)
    # a low shelf of rock in the foreground, joining the two
    shelf = lambda x: h * 0.885 + 10 * ((x - w * 0.5) / (w * 0.5)) ** 2 - 14 * max(0.0, 1 - abs(x - w * 0.3) / 90)
    rocks = ImageChops.lighter(rocks, cliff_mask(w, h, rnd, jagged(rnd, -10, w + 10, shelf, (5, 14), 3)))
    # stacks standing off the headland
    sk = ImageDraw.Draw(rocks)
    for sx0, sw0, sh0 in ((w * 0.47, 12, 46), (w * 0.53, 8, 28), (w * 0.90, 14, 34)):
        base = h * 0.80 if sx0 < w * 0.8 else h * 0.75
        sk.polygon([(sx0, base + 30), (sx0 + 2, base - sh0), (sx0 + sw0 * 0.6, base - sh0 - 6),
                    (sx0 + sw0, base - sh0 + 4), (sx0 + sw0 + 2, base + 30)], fill=255)
    img = tint(img, rocks, BASALT)
    # column joints: vertical facets catching a little cold light from the sky
    cols = Image.new('L', (w, h), 0)
    cd = ImageDraw.Draw(cols)
    x = 2.0
    while x < w:
        cw = rnd.uniform(5, 11)
        top = rnd.uniform(h * 0.46, h * 0.55)
        cd.line((x, top, x + rnd.uniform(-1.5, 1.5), h), fill=rnd.randrange(30, 80), width=1)
        for _ in range(3):
            yy = rnd.uniform(top, h * 0.95)
            cd.line((x, yy, x + cw, yy + rnd.uniform(-2, 2)), fill=rnd.randrange(20, 50), width=1)
        x += cw
    cols = ImageChops.multiply(cols, rocks.filter(ImageFilter.MinFilter(5)))
    fall = Image.linear_gradient('L').resize((w, h)).point(lambda v: 255 - v)
    img = tint(img, ImageChops.multiply(cols, fall), (90, 96, 108))
    # edge light along the cliff tops, warm towards the tower
    crest = ImageChops.subtract(rocks, ImageChops.offset(rocks, 0, 3))
    warm = Image.new('L', (w, h), 0)
    ImageDraw.Draw(warm).ellipse((tx - 300, horizon - 200, tx + 300, h), fill=255)
    warm = warm.filter(ImageFilter.GaussianBlur(80))
    img = tint(img, ImageChops.multiply(crest, warm).point(lambda v: v * 150 // 255), (220, 90, 50))
    img = tint(img, ImageChops.multiply(crest, ImageChops.invert(warm)).point(lambda v: v * 80 // 255), (140, 140, 156))
    # surf at the foot of the cliffs
    sm = Image.new('L', (w, h), 0)
    smd = ImageDraw.Draw(sm)
    for _ in range(140):
        x = rnd.uniform(0, w)
        y = rnd.uniform(h * 0.74, h * 0.90)
        if rocks.getpixel((int(min(w - 1, x)), int(min(h - 1, y)))) == 0 and \
                rocks.getpixel((int(min(w - 1, x)), int(min(h - 1, y + 6)))) > 0:
            smd.ellipse((x - 6, y - 1.5, x + 6, y + 1.5), fill=rnd.randrange(60, 140))
    img = tint(img, sm.filter(ImageFilter.GaussianBlur(1.2)), (150, 160, 170))

    # drifting snow (pale, slanted by the wind) and ash (darker flecks)
    snow = Image.new('L', (w, h), 0)
    sd = ImageDraw.Draw(snow)
    for _ in range(520):
        x, y = rnd.uniform(-20, w + 20), rnd.uniform(0, h)
        r = rnd.uniform(0.5, 1.9)
        dx = r * rnd.uniform(1.5, 3.5)
        sd.line((x, y, x - dx, y - dx * 0.35), fill=rnd.randrange(80, 220), width=max(1, int(r)))
    for _ in range(26):     # a few big flakes close to the viewer
        x, y, r = rnd.uniform(0, w), rnd.uniform(0, h * 0.9), rnd.uniform(2.5, 4.2)
        sd.ellipse((x - r, y - r, x + r, y + r), fill=rnd.randrange(60, 130))
    img = tint(img, snow.filter(ImageFilter.GaussianBlur(0.8)), (226, 228, 234))
    ash = Image.new('L', (w, h), 0)
    ad = ImageDraw.Draw(ash)
    for _ in range(260):
        x, y, r = rnd.uniform(0, w), rnd.uniform(0, h * 0.7), rnd.uniform(0.6, 1.6)
        ad.ellipse((x - r, y - r, x + r, y + r), fill=rnd.randrange(80, 200))
    img = tint(img, ash.filter(ImageFilter.GaussianBlur(0.5)), (30, 24, 24))
    embers = Image.new('L', (w, h), 0)
    ed = ImageDraw.Draw(embers)
    for _ in range(40):
        x = tx + rnd.gauss(0, 90)
        y = rnd.uniform(top_y, horizon)
        ed.ellipse((x - 0.9, y - 0.9, x + 0.9, y + 0.9), fill=rnd.randrange(120, 255))
    img = glow(img, embers, ((3, 1.0, RED), (0, 0.9, AMBER)))

    # a soft vignette, then the bottom fades into the base colour
    vig = Image.new('L', (w, h), 0)
    ImageDraw.Draw(vig).ellipse((-w * 0.5, -h * 0.15, w * 1.5, h * 1.1), fill=255)
    vig = ImageChops.invert(vig.filter(ImageFilter.GaussianBlur(90))).point(lambda v: v * 150 // 255)
    img = tint(img, vig, BASE)
    fade = Image.new('L', (w, h), 0)
    fd = ImageDraw.Draw(fade)
    for y in range(h):
        t = max(0.0, (y - h * 0.82) / (h * 0.18))
        fd.line((0, y, w, y), fill=int(255 * min(1.0, t) ** 1.3))
    img = tint(img, fade, BASE)
    return img.convert('RGB')


def main():
    side = draw_sidebar()
    out = os.path.join(PROJECT, 'launcher_side.png')
    side.save(out, optimize=True)
    print('wrote', out)
    if '--preview' in sys.argv:
        preview = sys.argv[sys.argv.index('--preview') + 1]
        os.makedirs(preview, exist_ok=True)
        side.resize((240, 500), Image.LANCZOS).save(os.path.join(preview, 'launcher_side_half.png'))


if __name__ == '__main__':
    main()
