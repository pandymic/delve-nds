#!/usr/bin/env python3
"""Generates Delve's placeholder pixel art as indexed PNGs in gfx/.

Deterministic (fixed seeds), so re-running produces identical files. The PNGs
are committed; re-run only after changing this script. Hand-drawn art can
replace any output file as long as it stays a 16-colour indexed PNG with
palette index 0 = magenta (the transparent colour key).

Requires Pillow:  python3 tools/make_art.py
"""

import os
import random

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
GFX = os.path.join(ROOT, "gfx")

MAGENTA = (255, 0, 255)

# Grit flags per output kind. 4bpp texture, magenta = transparent index 0.
GRIT_TEXTURE = "# 4bpp paletted texture, magenta colour key\n-gx -gb -gB4 -gTFF00FF\n"


def save(name, img, palette, grit=GRIT_TEXTURE):
    """img: list of rows of palette indices. palette: list of RGB tuples."""
    h = len(img)
    w = len(img[0])
    out = Image.new("P", (w, h))
    flat = []
    for rgb in palette:
        flat.extend(rgb)
    flat.extend([0, 0, 0] * (256 - len(palette)))
    out.putpalette(flat)
    out.putdata([px for row in img for px in row])
    out.save(os.path.join(GFX, name + ".png"))
    with open(os.path.join(GFX, name + ".grit"), "w") as f:
        f.write(grit)


def grid(w, h, v=0):
    return [[v] * w for _ in range(h)]


def rgb5(r, g, b):
    """DS-style 5-bit components to 8-bit, so the art matches what the DS shows."""
    return (r * 255 // 31, g * 255 // 31, b * 255 // 31)


# ---------------------------------------------------------------------------
# Floor: 32x32 = 2x2 flagstones (one per game tile, 16 texels each)
# ---------------------------------------------------------------------------

def make_floor():
    rnd = random.Random(1)
    pal = [MAGENTA,
           rgb5(3, 3, 5),     # 1 mortar
           rgb5(7, 7, 10),    # 2 stone shadow
           rgb5(10, 9, 13),   # 3 stone dark
           rgb5(12, 11, 15),  # 4 stone mid
           rgb5(14, 13, 17),  # 5 stone light
           rgb5(17, 16, 20),  # 6 highlight
           rgb5(6, 9, 6),     # 7 moss dark
           rgb5(8, 12, 7)]    # 8 moss
    img = grid(32, 32)
    for sy in range(2):
        for sx in range(2):
            base = rnd.choice([4, 4, 3, 5])
            for y in range(16):
                for x in range(16):
                    px, py = sx * 16 + x, sy * 16 + y
                    if x == 15 or y == 15:
                        c = 1
                    elif x == 0 or y == 0:
                        c = 6 if rnd.random() < 0.7 else 5
                    elif x == 14 or y == 14:
                        c = 2
                    else:
                        r = rnd.random()
                        c = base
                        if r < 0.12:
                            c = base - 1
                        elif r < 0.18:
                            c = base + 1
                    img[py][px] = c
            # A crack on some stones.
            if rnd.random() < 0.6:
                x, y = rnd.randint(3, 11), rnd.randint(3, 11)
                for _ in range(rnd.randint(4, 8)):
                    img[sy * 16 + y][sx * 16 + x] = 2
                    x = min(13, max(1, x + rnd.choice([-1, 0, 1])))
                    y = min(13, max(1, y + rnd.choice([0, 1])))
            # Moss creeping out of a mortar line.
            if rnd.random() < 0.5:
                mx = rnd.randint(2, 12)
                for i in range(rnd.randint(2, 4)):
                    img[sy * 16 + 13 - (i % 2)][sx * 16 + mx + i] = 8 if i % 2 else 7
    save("tex_floor", img, pal)


# ---------------------------------------------------------------------------
# Wall: 32x32 bricks (1 game tile = 16 texels wide, 16 texels per unit height)
# ---------------------------------------------------------------------------

def make_wall():
    rnd = random.Random(2)
    pal = [MAGENTA,
           rgb5(3, 2, 4),     # 1 mortar
           rgb5(8, 6, 9),     # 2 brick shadow
           rgb5(11, 8, 11),   # 3 brick dark
           rgb5(13, 10, 13),  # 4 brick mid
           rgb5(16, 12, 15),  # 5 brick light
           rgb5(19, 15, 17)]  # 6 brick highlight
    img = grid(32, 32)
    for y in range(32):
        row = y // 8
        offset = 8 if row % 2 else 0
        for x in range(32):
            bx = (x + offset) % 16
            by = y % 8
            if by == 7 or bx == 15:
                c = 1
            elif by == 0:
                c = 6 if bx < 13 else 5
            elif by == 6 or bx == 14:
                c = 2
            else:
                r = rnd.random()
                c = 4 if r > 0.25 else (3 if r > 0.1 else 5)
            img[y][x] = c
    save("tex_wall", img, pal)


# ---------------------------------------------------------------------------
# Pillar: 32x32 large stone blocks
# ---------------------------------------------------------------------------

def make_pillar():
    rnd = random.Random(3)
    pal = [MAGENTA,
           rgb5(4, 4, 6),     # 1 seam
           rgb5(9, 9, 12),    # 2 shadow
           rgb5(12, 12, 15),  # 3 dark
           rgb5(15, 15, 18),  # 4 mid
           rgb5(18, 18, 21),  # 5 light
           rgb5(22, 22, 24)]  # 6 highlight
    img = grid(32, 32)
    for y in range(32):
        for x in range(32):
            by = y % 16
            bx = x % 16
            if by == 15:
                c = 1
            elif by == 0:
                c = 6
            elif by == 14:
                c = 2
            elif bx in (0, 15):
                c = 3
            else:
                r = rnd.random()
                c = 4 if r > 0.2 else (3 if r > 0.08 else 5)
            img[y][x] = c
    save("tex_pillar", img, pal)


# ---------------------------------------------------------------------------
# Crate: 16x16 wooden planks with a frame
# ---------------------------------------------------------------------------

def make_crate():
    rnd = random.Random(4)
    pal = [MAGENTA,
           rgb5(5, 3, 1),     # 1 outline
           rgb5(11, 6, 2),    # 2 frame dark
           rgb5(15, 9, 3),    # 3 frame
           rgb5(18, 11, 5),   # 4 plank
           rgb5(21, 14, 7),   # 5 plank light
           rgb5(14, 8, 3)]    # 6 grain
    img = grid(16, 16)
    for y in range(16):
        for x in range(16):
            if x in (0, 15) or y in (0, 15):
                c = 1
            elif x in (1, 14) or y in (1, 14):
                c = 3
            elif x == y or x == 15 - y:
                c = 2  # cross brace
            else:
                c = 4 if (y // 4) % 2 else 5
                if rnd.random() < 0.15:
                    c = 6
            img[y][x] = c
    save("tex_crate", img, pal)


# ---------------------------------------------------------------------------
# Player sprite sheet: 128x64, 16x24 frames
#   row 0 (y 0-23)  front view   row 1 (y 24-47) back view
#   cols: 0 idle, 1 walk A, 2 walk B (bob), 3 walk C, 4 attack wind-up,
#         5 attack swing
#   shadow ellipse at (112, 48), 16x8
# ---------------------------------------------------------------------------

SPRITE_PAL = [MAGENTA,
              rgb5(3, 2, 4),      # 1 outline
              rgb5(29, 22, 17),   # 2 skin
              rgb5(22, 14, 11),   # 3 skin shadow
              rgb5(20, 5, 6),     # 4 hood / cloak
              rgb5(12, 3, 5),     # 5 cloak dark
              rgb5(10, 13, 20),   # 6 tunic
              rgb5(6, 7, 12),     # 7 trousers / belt
              rgb5(11, 7, 3),     # 8 boots
              rgb5(25, 27, 30),   # 9 blade
              rgb5(28, 22, 6),    # a gold
              rgb5(2, 1, 2),      # b eyes
              rgb5(27, 9, 9),     # c hood highlight
              rgb5(4, 3, 6),      # d shadow
              rgb5(15, 17, 22)]   # e blade shadow

# Left halves (8 columns), mirrored to 16. Rows 0-17; legs are drawn apart.
FRONT_HALF = [
    "........",
    "......11",
    ".....1c4",
    "....1c44",
    "....1444",
    "...14422",
    "...142b2",
    "...14222",
    "....1423",
    "...15514",
    "..155466",
    "..154666",
    "..15467a",
    "..124666",
    "...14666",
    "...15666",
    "...15466",
    "....1466",
]

BACK_HALF = [
    "........",
    "......11",
    ".....1c4",
    "....1c44",
    "....1444",
    "...14444",
    "...14444",
    "...14444",
    "....1444",
    "...15544",
    "..155544",
    "..155454",
    "..155544",
    "..125544",
    "...15544",
    "...15554",
    "...15555",
    "....1555",
]


def hex_index(ch):
    return 0 if ch == "." else int(ch, 16)


def mirror_rows(half):
    rows = []
    for h in half:
        assert len(h) == 8, h
        rows.append(h + h[::-1])
    return rows


def blit(img, ox, oy, rows):
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                img[oy + y][ox + x] = hex_index(ch)


def draw_leg(img, ox, oy, left, lift):
    """Leg in columns 4-7 (left) or 8-11 (right) of a frame, boots at the bottom."""
    x = ox + (4 if left else 8)
    bottom = oy + 23 - lift
    for y in range(oy + 18, bottom - 2):
        for i, c in enumerate("1771"):
            img[y][x + i] = hex_index(c)
    for i, c in enumerate("1881"):
        img[bottom - 2][x + i] = hex_index(c)
    boot = "18881" if left else "18881"
    bx = x - 1 if left else x
    for i, c in enumerate(boot):
        img[bottom - 1][bx + i] = hex_index(c)
    for i in range(5):
        img[bottom][bx + i] = 1


def draw_sword(img, ox, oy, pixels):
    for (x, y, c) in pixels:
        img[oy + y][ox + x] = hex_index(c)


# Sword overlays (x, y, colour) relative to a frame, held in the right hand
# (screen right, x ~ 12-13).
SWORD_WINDUP = [(12, 12, "a"), (13, 11, "a"), (12, 10, "9"), (13, 10, "e"),
                (12, 9, "9"), (13, 9, "e"), (12, 8, "9"), (13, 8, "e"),
                (12, 7, "9"), (13, 7, "e"), (12, 6, "9"), (12, 5, "1")]
SWORD_SWING = [(12, 14, "a"), (12, 15, "a"), (13, 15, "9"), (14, 15, "9"),
               (15, 15, "9"), (13, 16, "e"), (14, 16, "e"), (15, 16, "1")]


def make_player():
    img = grid(128, 64)
    for row, half in enumerate([FRONT_HALF, BACK_HALF]):
        body = mirror_rows(half)
        oy = row * 24
        for col in range(6):
            ox = col * 16
            bob = 1 if col == 2 else 0
            blit(img, ox, oy + bob, body)
            lift_l = 1 if col == 1 else 0
            lift_r = 1 if col == 3 else 0
            draw_leg(img, ox, oy, True, lift_l)
            draw_leg(img, ox, oy, False, lift_r)
            if col == 4:
                draw_sword(img, ox, oy, SWORD_WINDUP)
            elif col == 5:
                draw_sword(img, ox, oy, SWORD_SWING)
    # Shadow ellipse.
    for y in range(8):
        for x in range(16):
            dx = (x - 7.5) / 7.5
            dy = (y - 3.5) / 3.5
            if dx * dx + dy * dy <= 1.0:
                img[48 + y][112 + x] = 13
    draw_slash(img)
    save("spr_player", img, SPRITE_PAL)


# ---------------------------------------------------------------------------
# Slash arc in the player sheet at (96, 0), 32x16
# ---------------------------------------------------------------------------

def draw_slash(img):
    for y in range(16):
        for x in range(32):
            dx = x + 0.5 - 16
            dy = y + 0.5 - 16
            d = (dx * dx + dy * dy) ** 0.5
            if 13.5 <= d < 15.5:
                img[y][96 + x] = 9
            elif 11.5 <= d < 13.5:
                img[y][96 + x] = 14
            elif 15.5 <= d < 16.3 or 10.8 <= d < 11.5:
                img[y][96 + x] = 1


# ---------------------------------------------------------------------------
# Enemy sheet: 128x64, 16x24 frames
#   row 0: chaser (slime)   cols: 0 idle, 1 squash, 2 stretch (lunge)
#   row 1: shooter (eye bat) cols: 0 wings up, 1 wings down, 2 wind-up, 3 fire
#   projectile orb 8x8 at (96, 48), shadow 16x8 at (112, 48)
# ---------------------------------------------------------------------------

ENEMY_PAL = [MAGENTA,
             rgb5(2, 3, 2),      # 1 outline
             rgb5(16, 29, 10),   # 2 slime light
             rgb5(9, 22, 6),     # 3 slime mid
             rgb5(4, 13, 4),     # 4 slime dark
             rgb5(30, 30, 28),   # 5 eye white
             rgb5(2, 1, 3),      # 6 pupil
             rgb5(24, 4, 6),     # 7 iris red
             rgb5(10, 4, 14),    # 8 wing dark
             rgb5(17, 8, 22),    # 9 wing
             rgb5(31, 14, 2),    # a orange
             rgb5(31, 28, 10),   # b yellow
             rgb5(24, 20, 24),   # c eye shell
             rgb5(4, 3, 6),      # d shadow
             rgb5(26, 31, 20),   # e slime highlight
             rgb5(31, 31, 31)]   # f white

SLIME_IDLE = [  # rows 12-23
    "........",
    "......11",
    "....1122",
    "...12e22",
    "..12e222",
    "..122222",
    ".1225622",
    ".1225622",
    ".1222222",
    ".1333333",
    "..144444",
    "...11111",
]
SLIME_SQUASH = [  # rows 14-23
    "......11",
    "...11122",
    "..12e222",
    ".12e2222",
    "1222f562",
    "1222f562",
    "12222222",
    "13333333",
    ".1444444",
    "..111111",
]
SLIME_STRETCH = [  # rows 8-23
    ".......1",
    "......12",
    ".....12e",
    "....12e2",
    "....1222",
    "...12222",
    "...12562",
    "...12562",
    "...12222",
    "...12222",
    "...13222",
    "...13333",
    "....1444",
    "....1444",
    ".....111",
    "........",
]

EYE_BODY = [  # rows 2-10 of the eye block
    ".....111",
    "....1ccc",
    "...1cccc",
    "...1c555",
    "..1c5557",
    "..1c5576",
    "...1c555",
    "...1cccc",
    "....1111",
]
WING_UP = [  # rows 0-5
    "1.......",
    "91......",
    "991.....",
    "8991....",
    ".889....",
    "..88....",
]
WING_DOWN = [  # rows 5-10
    "..8.....",
    ".889....",
    "8991....",
    "991.....",
    "91......",
    "1.......",
]

ORB = [
    "..1111..",
    ".1aaaa1.",
    "1abbbba1",
    "1abffba1",
    "1abffba1",
    "1abbbba1",
    ".1aaaa1.",
    "..1111..",
]


def recolor(rows, mapping):
    return ["".join(mapping.get(c, c) for c in r) for r in rows]


def make_enemies():
    img = grid(128, 64)
    # Chaser.
    blit(img, 0, 12, mirror_rows(SLIME_IDLE))
    blit(img, 16, 14, mirror_rows(SLIME_SQUASH))
    blit(img, 32, 8, mirror_rows(SLIME_STRETCH))
    # Shooter: eye block floats with its top at row 4 of the frame.
    for col, wing, glow in [(0, WING_UP, False), (1, WING_DOWN, False),
                            (2, WING_UP, True), (3, WING_DOWN, True)]:
        ox, oy = col * 16, 24 + 4
        wing_top = 0 if wing is WING_UP else 5
        blit(img, ox, oy + wing_top, mirror_rows(wing))
        body = EYE_BODY
        if glow:
            body = recolor(body, {"7": "a", "6": "b" if col == 3 else "6",
                                  "5": "b" if col == 3 else "5"})
        blit(img, ox, oy + 2, mirror_rows(body))
    blit(img, 96, 48, ORB)
    for y in range(8):
        for x in range(16):
            dx = (x - 7.5) / 7.5
            dy = (y - 3.5) / 3.5
            if dx * dx + dy * dy <= 1.0:
                img[48 + y][112 + x] = 13
    save("spr_enemies", img, ENEMY_PAL)


# ---------------------------------------------------------------------------
# Items sheet (3D billboards): 128x32
#   row 0 (y 0-15): icons 16x16: 0 sword (combat), 1 heart (rest),
#                   2 chest (treasure), 3 coin
#   row 1 (y 16-31): chest closed, chest open (the campfire is on the fire
#                    sheet, tools/import_art.py)
# Also written as ui_icons.png (16x64, the four icons stacked) for
# bottom-screen OBJ sprites.
# ---------------------------------------------------------------------------

ITEM_PAL = [MAGENTA,
            rgb5(2, 1, 3),      # 1 outline
            rgb5(25, 27, 30),   # 2 steel light
            rgb5(15, 17, 22),   # 3 steel dark
            rgb5(28, 22, 6),    # 4 gold
            rgb5(31, 29, 14),   # 5 gold light
            rgb5(18, 11, 3),    # 6 gold dark / wood dark
            rgb5(29, 6, 8),     # 7 red
            rgb5(31, 18, 18),   # 8 red light
            rgb5(16, 2, 5),     # 9 red dark
            rgb5(21, 13, 5),    # a wood
            rgb5(31, 14, 2),    # b flame orange
            rgb5(31, 28, 10),   # c flame yellow
            rgb5(8, 6, 6),      # d ash
            rgb5(31, 31, 31),   # e white
            rgb5(10, 6, 2)]     # f wood darkest

ICON_SWORD = [
    "............11..",
    "...........1221.",
    "..........12231.",
    ".........12231..",
    "........12231...",
    ".......12231....",
    "..11..12231.....",
    "..161.1231......",
    "...1641231......",
    "....16431.......",
    "....1641........",
    "...1611641......",
    "..1661.161......",
    ".1661...11......",
    ".161............",
    "..1.............",
]
ICON_HEART = [
    "................",
    "..1111...1111...",
    ".188771.177771..",
    "1888777177777791",
    "1887777777777791",
    "1877777777777791",
    "1777777777777991",
    ".17777777777991.",
    "..177777777991..",
    "...1777777991...",
    "....17777991....",
    ".....177991.....",
    "......1791......",
    ".......11.......",
    "................",
    "................",
]
ICON_CHEST = [
    "................",
    "................",
    "...1111111111...",
    "..1aaaaaaaaaa1..",
    ".1aa6aaaaaa6aa1.",
    ".1a6444444446a1.",
    ".16666644666661.",
    ".14444455444441.",
    ".1aaaa1551aaaa1.",
    ".1aaaa1441aaaa1.",
    ".1aaaaa11aaaaa1.",
    ".1a6aaaaaaaa6a1.",
    ".1a6aaaaaaaa6a1.",
    ".16666666666661.",
    "..111111111111..",
    "................",
]
ICON_COIN = [
    "................",
    "................",
    ".....111111.....",
    "....15555541....",
    "...1554444441...",
    "..155446644441..",
    "..154464464441..",
    "..154464444441..",
    "..154446644441..",
    "..154444464441..",
    "..154464464441..",
    "..144446644461..",
    "...1444444461...",
    "....14666661....",
    ".....111111.....",
    "................",
]
CHEST_OPEN = [
    "...1111111111...",
    "..1aaaaaaaaaa1..",
    ".1a6aaaaaaaa6a1.",
    ".16666666666661.",
    ".1ffffffffffff1.",
    ".1f5c5c55c5c5f1.",
    ".16666644666661.",
    ".14444455444441.",
    ".1aaaa1551aaaa1.",
    ".1aaaa1441aaaa1.",
    ".1aaaaa11aaaaa1.",
    ".1a6aaaaaaaa6a1.",
    ".1a6aaaaaaaa6a1.",
    ".16666666666661.",
    "..111111111111..",
    "................",
]
def check16(rows):
    assert len(rows) == 16, len(rows)
    for r in rows:
        assert len(r) == 16, r
    return rows


def make_items():
    img = grid(128, 32)
    for i, icon in enumerate([ICON_SWORD, ICON_HEART, ICON_CHEST, ICON_COIN]):
        blit(img, i * 16, 0, check16(icon))
    for i, spr in enumerate([ICON_CHEST, CHEST_OPEN]):
        blit(img, i * 16, 16, check16(spr))
    save("spr_items", img, ITEM_PAL)

    # The first four UI icons; tools/import_art.py adds Puny ones and
    # writes gfx/ui_icons.png.
    ui = grid(16, 64)
    for i, icon in enumerate([ICON_SWORD, ICON_HEART, ICON_CHEST, ICON_COIN]):
        blit(ui, 0, i * 16, icon)
    out = Image.new("P", (16, 64))
    flat = []
    for rgb in ITEM_PAL:
        flat.extend(rgb)
    flat.extend([0, 0, 0] * (256 - len(ITEM_PAL)))
    out.putpalette(flat)
    out.putdata([px for row in ui for px in row])
    os.makedirs(os.path.join(ROOT, "art", "generated"), exist_ok=True)
    out.save(os.path.join(ROOT, "art", "generated", "ui_icons_base.png"))


def main():
    os.makedirs(GFX, exist_ok=True)
    # Floor, wall, pillar, crate and player now come from
    # tools/import_art.py (Puny packs). The old generators stay for
    # reference but no longer run.
    # The enemies sheet (old slime/eye-bat) was replaced by Puny sprites and
    # the effects sheet (tools/import_art.py, spr_fx).
    make_items()
    print("art written to", os.path.normpath(GFX))


if __name__ == "__main__":
    main()
