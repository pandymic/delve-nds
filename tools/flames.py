"""Pixel-art flames for tools/import_art.py: torches, fire jets, campfires.

A flame is drawn from a heat field: hottest along a swaying centre line,
cooling towards its edges and its tip. Heat bands map to the Puny torch's
colours, plus a pale yellow and an ember red for a little more detail.
Tongues break off the tip and rise.

Every time-varying term is a whole number of cycles over the n frames of an
animation, so the frames loop seamlessly. Deterministic.
"""

import math

# The Puny Dungeon torch's flame colours, plus PALE and EMBER.
WHITE = (255, 255, 255)
PALE = (255, 246, 170)
YELLOW = (251, 240, 20)
ORANGE = (248, 172, 52)
DEEP = (213, 119, 59)
EMBER = (196, 72, 46)
DARK = (150, 69, 65)

BANDS = [(0.74, WHITE), (0.58, PALE), (0.42, YELLOW), (0.27, ORANGE),
         (0.13, DEEP), (-1.0, EMBER)]


def _wave(t, n, k, phase):
    return math.sin(2 * math.pi * (k * t / n) + phase)


def flame(img, cx, base_y, height, width, t, n, seed=0.0, sway=1.0,
          tongues=1, core=True, lean=0.0, bulb=0.0, ripple=0.12):
    """Draws one frame of a flame onto an RGBA image (over what is there).

    cx, base_y: centre of the flame's base (pixel coordinates, floats ok).
    height, width: size at rest; both breathe a little over the loop.
    t, n: frame t of an n-frame loop. seed: phase offset (shapes differ).
    sway: how far the tip swings (pixels). tongues: detached licks rising
    off the tip. core: a white-hot core near the base. lean: a constant
    tilt of the tip (pixels). bulb: 0 for a cone; above 0 the base narrows
    (a teardrop, widest at that fraction of the height). ripple: how much
    the edges waver.
    """
    h = height * (1 + 0.13 * _wave(t, n, 2, seed) + 0.05 * _wave(t, n, 3, seed * 2))
    w = width * (1 + 0.08 * _wave(t, n, 1, seed + 1.3))
    px = img.load()

    def centre(v):
        s = (0.7 * _wave(t, n, 1, seed + v * 2.6)
             + 0.3 * _wave(t, n, 2, seed * 1.7 + v * 4.1))
        return cx + (sway * s + lean) * v ** 1.4

    top = int(math.floor(base_y - h))
    for y in range(max(0, top), int(math.ceil(base_y))):
        v = (base_y - (y + 0.5)) / h          # 0 at the base, 1 at the tip
        if v < 0 or v > 1:
            continue
        hw = w / 2 * (1 - v) ** 0.62 * (1 + ripple * _wave(t, n, 3, seed + v * 6))
        if bulb > 0 and v < bulb:
            hw *= 0.45 + 0.55 * math.sqrt(v / bulb)
        if hw < 0.35:
            continue
        c = centre(v)
        for x in range(max(0, int(c - hw - 1)), min(img.width, int(c + hw + 2))):
            d = abs(x + 0.5 - c)
            if d >= hw:
                continue
            heat = (1 - d / hw) * (1 - 0.55 * v) - 0.18 * v
            if not core:
                heat = min(heat, 0.73)
            colour = next(col for limit, col in BANDS if heat > limit)
            if v > 0.82 and colour in (EMBER, DEEP):
                colour = DARK
            px[x, y] = colour + (255,)

    # Tongues: small licks breaking off the tip and rising, cooling.
    tip_y = base_y - h
    for k in range(tongues):
        p = (2 * t / n + k / max(1, tongues) + seed * 0.13) % 1.0
        y = int(round(tip_y - 0.5 - p * max(2.0, height * 0.35)))
        x = int(round(centre(1.0) + 0.9 * sway * _wave(t, n, 1, seed + k * 2.1)))
        colour = ORANGE if p < 0.3 else DEEP if p < 0.6 else DARK
        if 0 <= x < img.width and 0 <= y < img.height:
            px[x, y] = colour + (255,)
            if p < 0.35 and y + 1 < img.height and px[x, y + 1][3] == 0:
                px[x, y + 1] = DEEP + (255,)


# The campfire's logs and ash (from the old hand-drawn campfire), drawn in
# front of the flames' base.
LOGS = [
    "..11aa1111aa11..",
    ".1afaaffaafaaf1.",
    ".1ffddddddddff1.",
    "..1dddddddddd1..",
    "...1111111111...",
]
LOG_COLOURS = {
    "1": (16, 8, 24),       # outline
    "a": (172, 106, 41),    # wood
    "f": (82, 49, 16),      # wood, darkest
    "d": (65, 49, 49),      # ash
}


def logs(img, x0, y0, t, n):
    """The campfire's logs; a few ash texels glow, changing every frame."""
    px = img.load()
    for y, row in enumerate(LOGS):
        for x, ch in enumerate(row):
            if ch == ".":
                continue
            c = LOG_COLOURS[ch]
            if ch == "d" and (x * 7 + y * 3 + t * 5) % n == 0:
                c = EMBER if (x + t) % 2 else DEEP
            px[x0 + x, y0 + y] = c + (255,)
