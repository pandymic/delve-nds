#!/usr/bin/env python3
"""Imports third-party pixel art from art/third_party/ into gfx/.

Crops frames out of the original sheets, packs them into power-of-two
textures, reduces each texture to 15 colours plus the magenta colour key
(index 0), and writes indexed PNGs with their .grit files. Also writes
source/art_layout.h, the frame layout the game code indexes into.

Deterministic; outputs are committed. Re-run after changing this script or
the sources. tools/make_art.py still generates the remaining placeholder
art; the two scripts write different files.

Requires Pillow:  python3 tools/import_art.py
"""

import colorsys
import os

from PIL import Image

import flames

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "art", "third_party")
GFX = os.path.join(ROOT, "gfx")
LAYOUT_H = os.path.join(ROOT, "source", "art_layout.h")
PALETTES_C = os.path.join(ROOT, "source", "art_palettes.c")

KEY = (255, 0, 255)
GRIT_TEXTURE = "# 4bpp paletted texture, magenta colour key\n-gx -gb -gB4 -gTFF00FF\n"

CHARS = os.path.join(SRC, "puny-characters")
DUNGEON = os.path.join(SRC, "puny-dungeon", "punyworld-dungeon-tileset.png")
WORLD = os.path.join(SRC, "puny-world", "punyworld-overworld-tileset.png")
ENV = os.path.join(CHARS, "Environment")
MONSTERS = os.path.join(SRC, "miniworld", "Monsters")

# Puny Characters column layouts (see art/third_party/README.md).
LAYOUT_A = dict(idle=[0, 1], walk=[0, 1, 2, 3], sword=[4, 5, 6, 7],
                bow=[8, 9, 10, 11], staff=[12, 13, 14], throw=[15, 16, 17, 18],
                flash=[19], hurt=[20], death=[21, 22, 23])
LAYOUT_B = dict(idle=[0, 1], walk=[1, 2, 3, 4], sword=[5, 6, 7, 8],
                bow=[9, 10, 11, 12], staff=[13, 14, 15], throw=[16, 17, 18, 19],
                flash=[20], hurt=[21], death=[22, 23])

# Facing rows kept in the game's sheets: down, down-right, right, up-right,
# up. The left-facing rows are drawn mirrored.
DIR_ROWS = [0, 1, 2, 3, 4]


# ---------------------------------------------------------------------------
# Palette reduction
# ---------------------------------------------------------------------------

def snap5(c):
    """Rounds an RGB colour to what the DS shows (5 bits per channel)."""
    return tuple(round(v * 31 / 255) * 255 // 31 for v in c[:3])


def reduce_colors(counts, n):
    """Greedy merge of the closest colour pair until n remain.

    counts: {rgb: pixel count}. Rare colours fold into their nearest
    neighbour first, so the dominant shades of pixel art survive intact.
    Returns (palette list, mapping rgb -> palette rgb).
    """
    groups = {c: (c, w) for c, w in counts.items()}  # rep -> (rgb, weight)
    mapping = {c: c for c in counts}

    def dist(a, b):
        # Weighted RGB distance, close enough to perceptual for this.
        dr, dg, db = a[0] - b[0], a[1] - b[1], a[2] - b[2]
        return 2 * dr * dr + 4 * dg * dg + 3 * db * db

    while len(groups) > n:
        reps = sorted(groups)
        best = None
        for i, a in enumerate(reps):
            for b in reps[i + 1:]:
                wa, wb = groups[a][1], groups[b][1]
                cost = dist(a, b) * min(wa, wb)
                if best is None or cost < best[0]:
                    best = (cost, a, b)
        _, a, b = best
        keep, drop = (a, b) if groups[a][1] >= groups[b][1] else (b, a)
        groups[keep] = (keep, groups[keep][1] + groups[drop][1])
        del groups[drop]
        for c, m in mapping.items():
            if m == drop:
                mapping[c] = keep
    palette = sorted(groups, key=lambda c: sum(c))
    return palette, mapping


def to_indexed(img, max_colors=15):
    """RGBA image -> (P image with index 0 = key, palette list)."""
    counts = {}
    for p in img.getdata():
        if p[3] >= 128:
            c = snap5(p)
            counts[c] = counts.get(c, 0) + 1
    palette, mapping = reduce_colors(counts, max_colors)
    index = {c: i + 1 for i, c in enumerate(palette)}
    out = Image.new("P", img.size, 0)
    out.putdata([index[mapping[snap5(p)]] if p[3] >= 128 else 0
                 for p in img.getdata()])
    flat = list(KEY)
    for c in palette:
        flat.extend(c)
    flat.extend([0, 0, 0] * (256 - 1 - len(palette)))
    out.putpalette(flat)
    return out, palette


def save(name, img, max_colors=15):
    out, palette = to_indexed(img, max_colors)
    out.save(os.path.join(GFX, name + ".png"))
    with open(os.path.join(GFX, name + ".grit"), "w") as f:
        f.write(GRIT_TEXTURE)
    print("  %-14s %3dx%-3d %2d colours" % (name, img.width, img.height,
                                            len(palette)))


def load(path):
    return Image.open(path).convert("RGBA")


def tile(sheet, col, row, w=1, h=1, size=16):
    return sheet.crop((col * size, row * size, (col + w) * size,
                       (row + h) * size))


# ---------------------------------------------------------------------------
# Character sheets
# ---------------------------------------------------------------------------

CELL = 24          # game frame size; the 32x32 source cell is cropped to it
CELL_OFFSET = 4    # crop starts 4 px into the source cell
SHEET_COLS = 10    # 24 px cells across a 256 px texture


def pack_character(src, columns, rows=DIR_ROWS):
    """Packs (row, column) source frames into 24x24 cells, row-major.

    Slot of source column columns[k] in direction d = d * len(columns) + k.
    """
    sheet = load(src)
    count = len(rows) * len(columns)
    out_h = 1
    while out_h < -(-count // SHEET_COLS) * CELL:
        out_h *= 2
    out = Image.new("RGBA", (256, out_h), (0, 0, 0, 0))
    for d, row in enumerate(rows):
        for k, col in enumerate(columns):
            x0 = col * 32 + CELL_OFFSET
            y0 = row * 32 + CELL_OFFSET
            frame = sheet.crop((x0, y0, x0 + CELL, y0 + CELL))
            # Report anything the crop cuts off (a staff tip on a death
            # frame is acceptable; a body part is not).
            full = sheet.crop((col * 32, row * 32, col * 32 + 32, row * 32 + 32))
            lost = (sum(1 for p in full.getdata() if p[3] >= 128)
                    - sum(1 for p in frame.getdata() if p[3] >= 128))
            if lost:
                print("    note: %s row %d col %d: crop cuts %d px"
                      % (os.path.basename(src), row, col, lost))
                assert lost <= 12, "crop would cut off too much"
            slot = d * len(columns) + k
            out.paste(frame, ((slot % SHEET_COLS) * CELL,
                              (slot // SHEET_COLS) * CELL))
    return out


def feet_row(src, columns, layout_idle, rows=DIR_ROWS):
    """Lowest opaque row of the idle frames, in cell coordinates."""
    sheet = load(src)
    low = 0
    for row in rows:
        for col in layout_idle:
            bb = sheet.crop((col * 32, row * 32, col * 32 + 32,
                             row * 32 + 32)).getbbox()
            low = max(low, bb[3] - CELL_OFFSET)
    return low


SHADOW_U, SHADOW_V = 240, 248   # 16x8 shadow ellipse in every character sheet


def draw_shadow(img):
    """Ellipse in the sheet's darkest colour, for render_shadow()."""
    dark = min((p for p in img.getdata() if p[3] >= 128),
               key=lambda p: p[0] + p[1] + p[2])
    for y in range(8):
        for x in range(16):
            dx, dy = (x - 7.5) / 8, (y - 3.5) / 4
            if dx * dx + dy * dy <= 1.0:
                img.putpixel((SHADOW_U + x, SHADOW_V + y), dark)


def elite_palette(normal_idx, elite_img):
    """Palette for drawing normal_idx's pixels in elite_img's colours.

    The Puny colour variants share one pixel layout, so each palette index
    takes the colour its pixels have most often in the variant sheet.
    """
    votes = {}
    for i, p in zip(normal_idx.getdata(), elite_img.getdata()):
        if i and p[3] >= 128:
            c = snap5(p)
            votes.setdefault(i, {}).setdefault(c, 0)
            votes[i][c] += 1
    pal = normal_idx.getpalette()[:16 * 3]
    out = [tuple(pal[i * 3:i * 3 + 3]) for i in range(16)]
    for i, v in votes.items():
        out[i] = max(v, key=v.get)
    return out


def rgb15(c):
    return (c[0] * 31 // 255) | ((c[1] * 31 // 255) << 5) | ((c[2] * 31 // 255) << 10)


# Characters: (sheet name, define prefix, source, elite source, layout,
#              animations kept in order). Slot k of a direction is
#              columns[k]; each animation's frames are consecutive slots.
# The slime has one row of 15 frames: squash (idle), hop, splat (death).
LAYOUT_SLIME = dict(idle=[0, 1, 2], walk=[0, 1, 2, 3, 4, 5, 7, 8], hurt=[6],
                    death=[10, 11, 12, 13])

# Elite source: a red variant sheet, "swap" for a red/green channel swap of
# the normal palette (green orcs turn red), or None for no elites.
CHARACTERS = [
    ("spr_player", "PF", "Human-Worker-Red.png", None, LAYOUT_B,
     ["idle", "walk", "sword", "throw", "hurt", "death"]),
    ("spr_chaser", "CF", "Orc-Peon-Cyan.png", "Orc-Peon-Red.png", LAYOUT_B,
     ["idle", "walk", "sword", "hurt", "death"]),
    ("spr_shooter", "SF", "Mage-Cyan.png", "Mage-Red.png", LAYOUT_A,
     ["idle", "walk", "staff", "hurt", "death"]),
    # Roster.
    ("spr_brute", "BF", "Orc-Grunt.png", "swap", LAYOUT_B,
     ["idle", "walk", "sword", "hurt", "death"]),
    ("spr_archer", "AF", "Archer-Green.png", "swap", LAYOUT_A,
     ["idle", "walk", "bow", "hurt", "death"]),
    ("spr_caster", "KF", "Archer-Purple.png", "swap", LAYOUT_A,
     ["idle", "walk", "staff", "hurt", "death"]),
    ("spr_shaman", "HF", "Orc-Soldier-Cyan.png", "Orc-Soldier-Red.png",
     LAYOUT_B, ["idle", "walk", "staff", "hurt", "death"]),
]

# Single-direction characters: (sheet, prefix, source, elite, layout, anims).
CHARACTERS_1DIR = [
    ("spr_slime", "MF", "Slime.png", "swap", LAYOUT_SLIME,
     ["idle", "walk", "hurt", "death"]),
    # The hub's training dummy: the bare Puny mannequin, facing the camera.
    ("spr_dummy", "DF", "Character-Base.png", None, LAYOUT_A,
     ["idle", "hurt"]),
]


def character_columns(layout, anims):
    """Source columns in slot order, sharing frames that animations reuse
    (layout B's walk starts on the second idle frame)."""
    cols = []
    starts = {}
    for name in anims:
        frames = layout[name]
        # Reuse a run of already-placed columns if the animation starts
        # inside it (e.g. walk 1-4 after idle 0-1).
        if frames[0] in cols:
            k = cols.index(frames[0])
            n = 0
            while n < len(frames) and k + n < len(cols) and cols[k + n] == frames[n]:
                n += 1
            starts[name] = k
            cols += frames[n:]
        else:
            starts[name] = len(cols)
            cols += frames
    return cols, starts


def swap_palette(idx):
    """Red/green channel swap of an indexed sheet's palette (elites)."""
    pal = idx.getpalette()[:16 * 3]
    out = []
    for i in range(16):
        r, g, b = pal[i * 3:i * 3 + 3]
        out.append((r, g, b) if i == 0 else (g, r, b))
    return out


# Area recolours: (sheet, variant, hue 0-1, saturation scale,
# value scale). Written as <sheet>_<variant>_pal; the game draws an area's
# own enemies with them. (The other recoloured enemies have their own
# sheets: MONSTER_SHEETS.)
VARIANTS = [
    ("spr_slime", "magma", 0.06, 1.00, 1.15),   # fire slime
]


def hue_moved(c, hue, sat, val):
    """One colour moved to a hue, keeping its shading. Outlines and
    near-white highlights stay as they are."""
    r, g, b = c[:3]
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    if v < 0.16 or (s < 0.12 and v > 0.85):
        return (r, g, b)
    s2 = min(1.0, max(0.2, s) * sat + 0.15)
    v2 = min(1.0, v * val)
    r2, g2, b2 = colorsys.hsv_to_rgb(hue, s2, v2)
    return (int(r2 * 255), int(g2 * 255), int(b2 * 255))


def variant_palette(idx, hue, sat, val):
    """Moves every colour of an indexed sheet to one hue (hue_moved)."""
    pal = idx.getpalette()[:16 * 3]
    out = []
    for i in range(16):
        c = tuple(pal[i * 3:i * 3 + 3])
        out.append(c if i == 0 else hue_moved(c, hue, sat, val))
    return out


def make_variants(palettes):
    for sheet, name, hue, sat, val in VARIANTS:
        idx = Image.open(os.path.join(GFX, sheet + ".png"))
        palettes.append(("%s_%s_pal" % (sheet, name),
                         [rgb15(c) for c in variant_palette(idx, hue, sat, val)]))


def make_characters(defines, palettes):
    entries = [(c, DIR_ROWS) for c in CHARACTERS]
    entries += [(c, [0]) for c in CHARACTERS_1DIR]
    for (sheet, prefix, src, elite_src, layout, anims), rows in entries:
        cols, starts = character_columns(layout, anims)
        path = os.path.join(CHARS, src)
        img = pack_character(path, cols, rows)
        full_h = 256 if rows == DIR_ROWS else img.height
        assert img.height <= full_h
        if img.height < full_h:
            full = Image.new("RGBA", (256, full_h), (0, 0, 0, 0))
            full.paste(img, (0, 0))
            img = full
        if full_h == 256:
            draw_shadow(img)
        save(sheet, img)
        defines.append(("%s_SHEET_H" % prefix, full_h))
        defines.append(("%s_FRAMES_PER_DIR" % prefix, len(cols)))
        defines.append(("%s_FEET_ROW" % prefix,
                        feet_row(path, cols, layout["idle"], rows)))
        for name in anims:
            defines.append(("%s_%s" % (prefix, name.upper()), starts[name]))
            defines.append(("%s_%s_COUNT" % (prefix, name.upper()),
                            len(layout[name])))
        if elite_src == "swap":
            idx = Image.open(os.path.join(GFX, sheet + ".png"))
            palettes.append((sheet + "_elite_pal",
                             [rgb15(c) for c in swap_palette(idx)]))
        elif elite_src:
            idx = Image.open(os.path.join(GFX, sheet + ".png"))
            elite = pack_character(os.path.join(CHARS, elite_src), cols)
            full = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
            full.paste(elite, (0, 0))
            draw_shadow(full)
            palettes.append((sheet + "_elite_pal",
                             [rgb15(c) for c in elite_palette(idx, full)]))


# ---------------------------------------------------------------------------
# Monster sheets: Shade's Miniworld Sprites
# ---------------------------------------------------------------------------
#
# Miniworld characters face four ways: rows for down, up and the two sides,
# then the same again attacking. The game keeps down, right and up (left is
# right mirrored), in that order: three direction rows. Each direction's
# slots are the walk frames (the first doubles as idle), the attack frames,
# a hurt frame and three death frames; Miniworld has no hurt or death
# frames, so those are drawn from the first walk frame (a flinch, then the
# body sinking flat).
#
# (sheet, prefix, source, cell, walk rows (down, right, up), attack rows or
#  None (walk frames instead), walk columns, attack columns, recolour
#  (hue, saturation, value) or None, elite recolour or None)
MONSTER_SHEETS = [
    # The imp (Molten Depths): the red demon.
    ("spr_imp", "IF", "Demons/RedDemon.png", 16, (0, 2, 1), (4, 6, 5),
     [0, 1, 2, 3, 4], [0, 1, 3, 5], None, (0.80, 1.0, 1.0)),
    # The magma brute: the minotaur, burnt dark red. It charges.
    ("spr_bull", "UF", "Orcs/Minotaur.png", 16, (0, 2, 1), (3, 5, 4),
     [0, 1, 2, 3], [0, 1, 3, 5], (0.015, 1.3, 0.85), (0.10, 1.4, 1.1)),
    # The digger (Caverns): the giant crab. No attack rows.
    ("spr_crab", "RF", "GiantAnimals/GiantCrab.png", 16, (0, 3, 1), None,
     [0, 1, 2], None, None, (0.33, 1.0, 0.9)),
    # The Caverns' boss: the yeti.
    ("spr_yeti", "YF", "Frostborn/Yeti.png", 16, (0, 2, 1), (4, 6, 5),
     [0, 1, 2, 3, 4], [0, 1, 3, 5], None, None),
    # The Molten Depths' boss, the Magma Lord: the red dragon (32x32).
    ("spr_dragon", "GF", "Dragons/RedDragon.png", 32, (0, 3, 1), (4, 7, 5),
     [0, 1, 2, 3], [0, 1], None, None),
]

MONSTER_DEATH = [0.7, 0.45, 0.2]   # death frames: height left


def monster_frames(sheet, cell, walk_row, attack_row, walk_cols, attack_cols):
    """One direction's frames in slot order: walk, attack, hurt, death."""
    def at(row, col):
        return sheet.crop((col * cell, row * cell, (col + 1) * cell,
                           (row + 1) * cell))
    walk = [at(walk_row, c) for c in walk_cols]
    if attack_row is None:
        attack = walk[:]
    else:
        attack = [at(attack_row, c) for c in attack_cols]
    base = walk[0]
    hurt = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
    hurt.paste(base, (0, -1))           # a flinch: a pixel up
    bb = base.getbbox()
    death = []
    for k in MONSTER_DEATH:
        body = base.crop(bb)
        h = max(1, int(body.height * k))
        body = body.resize((body.width, h), Image.NEAREST)
        frame = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
        frame.paste(body, (bb[0], bb[3] - h))
        death.append(frame)
    return walk, attack, [hurt], death


def recolored(img, hue, sat, val):
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            p = px[x, y]
            if p[3] >= 128:
                px[x, y] = hue_moved(p, hue, sat, val) + (255,)
    return out


def make_monsters(defines, palettes):
    for (sheet, prefix, src, cell, walk_rows, attack_rows, walk_cols,
         attack_cols, recolour, elite) in MONSTER_SHEETS:
        img = load(os.path.join(MONSTERS, src))
        if recolour:
            img = recolored(img, *recolour)
        cols = 256 // cell
        per_dir = len(walk_cols) + len(attack_cols or walk_cols) + 1 + 3
        lines = -(-3 * per_dir // cols)
        sheet_h = 1
        while sheet_h < lines * cell:
            sheet_h *= 2
        out = Image.new("RGBA", (256, sheet_h), (0, 0, 0, 0))
        feet = 0
        for d in range(3):
            parts = monster_frames(
                img, cell, walk_rows[d],
                attack_rows[d] if attack_rows else None, walk_cols,
                attack_cols)
            frames = [f for part in parts for f in part]
            assert len(frames) == per_dir
            for k, f in enumerate(frames):
                slot = d * per_dir + k
                out.paste(f, ((slot % cols) * cell, (slot // cols) * cell))
            feet = max(feet, max(f.getbbox()[3] for f in parts[0]))
        save(sheet, out)
        n_walk = len(walk_cols)
        n_attack = len(attack_cols or walk_cols)
        for name, value in [
                ("SHEET_H", sheet_h), ("CELL", cell), ("FRAMES_PER_DIR", per_dir),
                ("FEET_ROW", feet), ("IDLE", 0), ("WALK", 0),
                ("WALK_COUNT", n_walk), ("ATTACK", n_walk),
                ("ATTACK_COUNT", n_attack), ("HURT", n_walk + n_attack),
                ("DEATH", n_walk + n_attack + 1), ("DEATH_COUNT", 3)]:
            defines.append(("%s_%s" % (prefix, name), value))
        if elite:
            idx = Image.open(os.path.join(GFX, sheet + ".png"))
            palettes.append((sheet + "_elite_pal",
                             [rgb15(c) for c in variant_palette(idx, *elite)]))


# ---------------------------------------------------------------------------
# Dungeon textures and props (16x16 Puny Dungeon tiles)
# ---------------------------------------------------------------------------

def opaque(img, fill=(24, 20, 28)):
    """Solid textures have no colour key: fill see-through texels."""
    out = Image.new("RGBA", img.size, fill + (255,))
    out.alpha_composite(img)
    return out


def moss(img):
    """Green-tinted, slightly darker stone for the sewers."""
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if a:
                # Every other blotch gets a little more green.
                wet = 18 if ((x // 5 + y // 3) % 3 == 0) else 0
                px[x, y] = (r * 3 // 4, min(255, g * 7 // 8 + 14 + wet),
                            b * 3 // 4, a)
    return out


def save_tex(name, img):
    save(name, opaque(img))


def make_textures():
    d = load(DUNGEON)
    save_tex("tex_floor", tile(d, 4, 0, 4, 4))       # plain flagstones, 4x4 tiles
    save_tex("tex_floor2", tile(d, 0, 5, 4, 4))      # patterned tiles
    # Brick wall front face, stored bottom-aligned: the renderer maps the
    # base of a wall to the bottom row of the texture.
    save_tex("tex_wall", tile(d, 1, 2, 2, 2))
    save_tex("tex_walltop", tile(d, 3, 9))           # dark stone cap
    save_tex("tex_pillar", tile(d, 0, 0, 1, 2))      # stone column, 16x32
    save_tex("tex_crate", tile(d, 16, 17))           # banded crate
    # The Sunken Sewers: green brick with drain grates, and a mossy
    # version of the flagstones.
    save_tex("tex_wall2", tile(d, 9, 2, 2, 2))
    save_tex("tex_floor3", moss(tile(d, 4, 0, 4, 4)))
    # The Molten Depths: basalt brick with glowing mortar, dark
    # flagstones with ember cracks, black columns.
    save_tex("tex_basalt", ember_mortar(tinted(tile(d, 1, 2, 2, 2),
                                               (0.42, 0.34, 0.36), True)))
    save_tex("tex_basaltfloor", blotched(ember_mortar(
        tinted(tile(d, 4, 0, 4, 4), (0.40, 0.33, 0.33), True), 12, 9), 3141))
    save_tex("tex_basaltcol", tinted(tile(d, 0, 0, 1, 2), (0.45, 0.36, 0.38), True))
    make_world_textures()


def tiled(tiles, pattern):
    """A texture from 16x16 tiles: pattern rows of indices into tiles
    (a trailing "f" flips the tile horizontally)."""
    rows = [r.split() for r in pattern]
    out = Image.new("RGBA", (16 * len(rows[0]), 16 * len(rows)), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        for x, key in enumerate(row):
            t = tiles[int(key.rstrip("f"))]
            if key.endswith("f"):
                t = t.transpose(Image.FLIP_LEFT_RIGHT)
            out.alpha_composite(t, (x * 16, y * 16))
    return out


def make_world_textures():
    """The camp: grass and dirt floors and the rock of its cliff edge,
    from the Puny World overworld tiles."""
    w = load(WORLD)
    # (The Environment grass and dirt tiles are a darker shade: not mixed
    # in.)
    grass = [tile(w, c, r) for r in range(3) for c in range(3)]
    save_tex("tex_grass", tiled(grass, ["0 1 4 3", "5f 0 2 8",
                                        "6 3f 0 1f", "2f 7 4f 0"]))
    dirt = [tile(w, 8, r) for r in range(3)]
    save_tex("tex_dirt", tiled(dirt, ["0 1 0 2", "1 2f 0 1f",
                                      "0 1f 2 0", "2f 0 1 0f"]))
    rock = [tile(w, 1, 6)]
    save_tex("tex_cliff", tiled(rock, ["0 0f", "0f 0"]))
    # The Caverns: the same rock, cold grey-blue, and a pebbly
    # stone floor from the grass (its tufts turn to pebbles).
    cave = tinted(tiled(rock, ["0 0f", "0f 0"]), (0.80, 0.88, 1.08), True)
    save_tex("tex_cave", cave)
    # Liquids in pits, drawn here (the tileset's deep water is flat):
    # wrapping noise, as lava (dark crust to yellow veins) and as the
    # Sewers' murk.
    save_tex("tex_lava", graded(noise_tex(64, 4141, veins=True),
                                [(0.0, (70, 10, 6)), (0.35, (150, 26, 8)),
                                 (0.62, (226, 82, 14)), (0.82, (252, 156, 30)),
                                 (1.0, (255, 232, 132))]))
    save_tex("tex_murk", graded(noise_tex(64, 777),
                                [(0.0, (12, 20, 12)), (0.55, (30, 46, 28)),
                                 (0.85, (52, 70, 42)), (1.0, (92, 110, 70))]))
    save_tex("tex_cavefloor", blotched(
        tinted(tiled(grass, ["0 1 4 3", "5f 0 2 8", "6 3f 0 1f",
                             "2f 7 4f 0"]), (0.62, 0.64, 0.74), True, 3.0),
        2721))


def ember_mortar(img, share=10, seed=5):
    """Some of the darkest texels (mortar, cracks) glow a dull ember red:
    the darkest 1/share of them, where wrapping noise is high."""
    import random
    rnd = random.Random(seed)
    n = max(1, img.width // 8)
    lat = [[rnd.random() for _ in range(n)] for _ in range(n)]
    out = img.copy()
    px = out.load()
    lums = sorted((p[0] * 30 + p[1] * 59 + p[2] * 11) // 100
                  for p in out.getdata())
    cut = lums[len(lums) // share]
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if (r * 30 + g * 59 + b * 11) // 100 > cut:
                continue
            if lat[(y // 8) % n][(x // 8) % n] < 0.45:
                continue
            px[x, y] = (128, 36, 14, a)
    return out


def blotched(img, seed, levels=(0.82, 0.9, 1.0)):
    """Darker patches from wrapping value noise (8 px cells), so a plain
    texture reads as uneven rock."""
    import random
    rnd = random.Random(seed)
    n = img.width // 8
    lat = [[rnd.random() for _ in range(n)] for _ in range(n)]
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            cx, cy = x / 8.0, y / 8.0
            x0, y0 = int(cx) % n, int(cy) % n
            fx, fy = cx - int(cx), cy - int(cy)
            a = lat[y0][x0] * (1 - fx) + lat[y0][(x0 + 1) % n] * fx
            b = lat[(y0 + 1) % n][x0] * (1 - fx) + lat[(y0 + 1) % n][(x0 + 1) % n] * fx
            v = a * (1 - fy) + b * fy
            k = levels[0] if v < 0.33 else levels[1] if v < 0.55 else levels[2]
            r, g, bl, al = px[x, y]
            px[x, y] = (int(r * k), int(g * k), int(bl * k), al)
    return out


def noise_tex(size, seed, veins=False):
    """Greyscale wrapping value noise, two octaves (size/4, size/8 cells).
    veins folds it (1 - |2n - 1|) into bright ridges, like lava cracks."""
    import random
    rnd = random.Random(seed)

    def octave(cell):
        n = size // cell
        lat = [[rnd.random() for _ in range(n)] for _ in range(n)]

        def at(x, y):
            cx, cy = x / cell, y / cell
            x0, y0 = int(cx) % n, int(cy) % n
            fx, fy = cx - int(cx), cy - int(cy)
            fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
            a = lat[y0][x0] * (1 - fx) + lat[y0][(x0 + 1) % n] * fx
            b = lat[(y0 + 1) % n][x0] * (1 - fx) + lat[(y0 + 1) % n][(x0 + 1) % n] * fx
            return a * (1 - fy) + b * fy
        return at

    o1, o2 = octave(size // 4), octave(size // 8)
    out = Image.new("RGBA", (size, size))
    for y in range(size):
        for x in range(size):
            v = o1(x, y) * 0.7 + o2(x, y) * 0.3
            if veins:
                v = 1 - abs(2 * v - 1)
                v = v ** 2.2
            c = int(v * 255)
            out.putpixel((x, y), (c, c, c, 255))
    return out


def graded(img, stops):
    """Maps each pixel's brightness (stretched to the image's range) onto a
    colour gradient: stops of (0-1, rgb)."""
    out = img.copy()
    px = out.load()
    lums = [(p[0] * 30 + p[1] * 59 + p[2] * 11) / 100 for p in out.getdata()]
    lo, hi = min(lums), max(lums)
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            t = ((r * 30 + g * 59 + b * 11) / 100 - lo) / max(1, hi - lo)
            for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
                if t <= t1:
                    k = (t - t0) / max(1e-6, t1 - t0)
                    px[x, y] = tuple(int(c0[i] + (c1[i] - c0[i]) * k)
                                     for i in range(3)) + (255,)
                    break
    return out


def tinted(img, mul, grey=False, contrast=1.0):
    """Scales each channel (after greying, if grey: keeps the shading but
    not the hue; contrast stretches it around the image's mean)."""
    out = img.copy()
    px = out.load()
    lums = [(p[0] * 30 + p[1] * 59 + p[2] * 11) // 100
            for p in out.getdata() if p[3]]
    mean = sum(lums) // max(1, len(lums))
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if not a:
                continue
            if grey:
                l = (r * 30 + g * 59 + b * 11) // 100
                r = g = b = max(0, min(255, int(mean + (l - mean) * contrast)))
            px[x, y] = tuple(min(255, int(c * m)) for c, m in zip((r, g, b), mul)) + (a,)
    return out


# Props sheet: 16x16 cells, 8 per row (128 px wide).
PROPS = [
    # name, (col, row) in the tileset; animated props list several cells
    # (the torch moved to the fire sheet, make_fire)
    ("BARREL", [(21, 19)]),
    ("BARREL2", [(22, 19)]),
    ("POT", [(23, 19)]),
    ("CHEST", [(21, 18)]),
    ("CHEST_OPEN", [(22, 18)]),
    ("IDOL", [(23, 18)]),
    ("LEVER", [(21, 17)]),
    ("LEVER2", [(23, 17)]),
    ("POST", [(16, 19)]),
    ("POST2", [(18, 19)]),
    ("FENCE", [(18, 17)]),
    ("GRATE", [(16, 1)]),
    ("SWITCH", [(12, 8)]),
    ("CHAIN", [(25, 17)]),
    ("GEAR", [(24, 17)]),
    ("SPIKES", [(16 + i, 7) for i in range(6)]),
]


def make_props(defines):
    d = load(DUNGEON)
    cells = sum(len(frames) for _, frames in PROPS)
    rows = -(-cells // 8)
    h = 1
    while h < rows * 16:
        h *= 2
    out = Image.new("RGBA", (128, h), (0, 0, 0, 0))
    slot = 0
    for name, frames in PROPS:
        defines.append(("PROP_" + name, slot))
        if len(frames) > 1:
            defines.append(("PROP_" + name + "_FRAMES", len(frames)))
        for col, row in frames:
            out.paste(tile(d, col, row), ((slot % 8) * 16, (slot // 8) * 16))
            slot += 1
    defines.append(("PROPS_SHEET_H", h))
    save("spr_props", out)


# ---------------------------------------------------------------------------
# Effects sheet (128x64): thrown dagger, fireball, coin, particles, damage
# digits, blast ring. The dagger comes from the Puny Dungeon arrow; the rest
# is drawn here.
# ---------------------------------------------------------------------------

FX_DIGIT_W, FX_DIGIT_H = 5, 7

# 3x5 digit glyphs, outlined to 5x7 when drawn.
DIGITS = [
    ["###", "#.#", "#.#", "#.#", "###"], [".#.", "##.", ".#.", ".#.", "###"],
    ["###", "..#", "###", "#..", "###"], ["###", "..#", "###", "..#", "###"],
    ["#.#", "#.#", "###", "..#", "..#"], ["###", "#..", "###", "..#", "###"],
    ["###", "#..", "###", "#.#", "###"], ["###", "..#", ".#.", ".#.", ".#."],
    ["###", "#.#", "###", "#.#", "###"], ["###", "#.#", "###", "..#", "###"],
]

PARTICLE_COLORS = [
    (255, 255, 255),   # spark
    (198, 104, 58),    # shard (terracotta)
    (148, 96, 52),     # splinter
    (150, 146, 140),   # dust
    (255, 156, 40),    # ember
    (70, 66, 74),      # smoke
    (255, 214, 64),    # gold
    (150, 24, 32),     # blood
]


# Particles that change as they age (fx.c): 4x4 cells, young to old. The
# inks are colours the sheet already has (sparks, fireball, gold, shards,
# blood, dust, smoke), so its 15-colour palette holds.
PARTICLE_INKS = {
    "W": (255, 255, 255), "P": (255, 244, 170), "y": (255, 214, 64),
    "o": (255, 156, 40), "d": (198, 104, 58), "r": (200, 40, 32),
    "k": (150, 24, 32),
    "1": (150, 146, 140), "2": (150, 146, 140), "3": (70, 66, 74),
}
AGEING_PARTICLES = [
    # The ember (PART_EMBER's own cell first) cooling to a dull red.
    ("EMBER_OLD", [[".rr.", "rddr", "rddr", ".rr."],
                   ["....", ".kr.", ".rk.", "...."]]),
    # Cinders: a white-hot speck, cooling and shrinking.
    ("CINDER", [["....", ".WP.", ".PW.", "...."],
                ["....", ".yP.", ".y..", "...."],
                ["....", ".o..", "....", "...."],
                ["....", "..r.", "....", "...."]]),
    # A flame lick: a little tongue that shrinks and reddens as it rises.
    ("FLAME", [[".o..", ".yo.", "oyPo", "oPWo"],
               ["..o.", ".oo.", ".oyo", "..y."],
               ["....", "..d.", ".dr.", ".r.."]]),
    # A smoke wisp: a dark puff, spreading and thinning out.
    ("WISP", [["....", ".11.", ".11.", "...."],
              [".1..", "1.13", "31.1", ".3.."],
              ["3.3.", ".3.3", "3.3.", ".3.."]]),
]


def put(img, x, y, c):
    img.putpixel((x, y), c + (255,) if len(c) == 3 else c)


def make_fx(defines):
    d = load(DUNGEON)
    out = Image.new("RGBA", (128, 64), (0, 0, 0, 0))

    # Row 0: the dagger spinning through 8 angles (45 degree steps).
    arrow = tile(d, 16, 8)
    for i in range(8):
        frame = arrow.rotate(-45 * i, resample=Image.NEAREST, expand=False)
        out.paste(frame, (i * 16, 0), frame)
    defines += [("FX_DAGGER_V", 0), ("FX_DAGGER_FRAMES", 8)]

    # Fireball (2 frames, 8x8) and coin (4 frames, 8x8) at y = 16.
    for f in range(2):
        for y in range(8):
            for x in range(8):
                r2 = (x - 3.5) ** 2 + (y - 3.5) ** 2
                wob = 0.6 if f else 0.0
                if r2 <= 3.2 + wob:
                    put(out, f * 8 + x, 16 + y, (255, 244, 170))
                elif r2 <= 7.5 + wob:
                    put(out, f * 8 + x, 16 + y, (255, 156, 40))
                elif r2 <= 12.5 + wob:
                    put(out, f * 8 + x, 16 + y, (200, 40, 32))
    coin_w = [6, 4, 2, 4]
    for f, w in enumerate(coin_w):
        x0 = 16 + f * 8 + (8 - w) // 2
        for y in range(1, 7):
            for x in range(w):
                edge = x in (0, w - 1) or y in (1, 6)
                put(out, x0 + x, 16 + y, (170, 112, 20) if edge else (255, 214, 64))
        if w >= 4:
            put(out, x0 + 1, 18, (255, 250, 200))
    heart = [".kk.kk.", "krrkrrk", "krrrrrk", "krrrrrk", ".krrrk.",
             "..krk..", "...k..."]
    for y, row in enumerate(heart):
        for x, ch in enumerate(row):
            if ch != ".":
                put(out, 48 + x, 17 + y,
                    (20, 12, 16) if ch == "k" else (230, 50, 60))
    defines += [("FX_HEART_U", 48), ("FX_HEART_V", 16)]
    defines += [("FX_FIREBALL_U", 0), ("FX_FIREBALL_V", 16),
                ("FX_COIN_U", 16), ("FX_COIN_V", 16), ("FX_COIN_FRAMES", 4)]

    # Particles: 4x4 dots at y = 24, one per kind (round-ish).
    for k, c in enumerate(PARTICLE_COLORS):
        for y in range(4):
            for x in range(4):
                if (x in (0, 3)) and (y in (0, 3)):
                    continue
                put(out, k * 4 + x, 24 + y, c)
    # Then the ageing particles' cells, each a run of phases (young first):
    # an ember cooling, cinders, flame licks, smoke wisps.
    cell = len(PARTICLE_COLORS)
    for name, phases in AGEING_PARTICLES:
        defines.append(("FX_PART_" + name, cell))
        defines.append(("FX_PART_" + name + "_PHASES", len(phases)))
        for rows in phases:
            for y, row in enumerate(rows):
                for x, ch in enumerate(row):
                    if ch != ".":
                        put(out, cell * 4 + x, 24 + y, PARTICLE_INKS[ch])
            cell += 1
    assert cell <= 32
    defines += [("FX_PARTICLE_V", 24), ("FX_PARTICLE_SIZE", 4)]

    # Digits 0-9, white with a dark outline, at y = 28; a gold copy at
    # y = 36 for killing blows.
    for row, fill in ((28, (255, 255, 255)), (36, (255, 214, 64))):
        for n, glyph in enumerate(DIGITS):
            x0 = n * FX_DIGIT_W
            for y in range(5):
                for x in range(3):
                    if glyph[y][x] != "#":
                        continue
                    for oy in (-1, 0, 1):
                        for ox in (-1, 0, 1):
                            px, py = x0 + 1 + x + ox, row + 1 + y + oy
                            if out.getpixel((px, py))[3] == 0:
                                put(out, px, py, (20, 12, 16))
            for y in range(5):
                for x in range(3):
                    if glyph[y][x] == "#":
                        put(out, x0 + 1 + x, row + 1 + y, fill)
    defines += [("FX_DIGIT_V", 28), ("FX_DIGIT_GOLD_V", 36),
                ("FX_DIGIT_W", FX_DIGIT_W), ("FX_DIGIT_H", FX_DIGIT_H)]

    # Blast ring: 32x32 at (96, 32), drawn flat on the floor and scaled.
    for y in range(32):
        for x in range(32):
            r = ((x - 15.5) ** 2 + (y - 15.5) ** 2) ** 0.5
            if 12.5 <= r < 14.5:
                put(out, 96 + x, 32 + y, (210, 250, 255))
            elif 14.5 <= r < 15.8:
                put(out, 96 + x, 32 + y, (80, 200, 255))
    defines += [("FX_RING_U", 96), ("FX_RING_V", 32), ("FX_RING_SIZE", 32)]

    save("spr_fx", out)


# ---------------------------------------------------------------------------
# Clutter sheet (128x64, 16x16 cells): drawn here in the Puny palette, plus
# recoloured / broken versions of Puny props.
# ---------------------------------------------------------------------------

PUNY = {
    ".": None,
    "k": (20, 16, 24),      # outline
    "w": (236, 232, 214),   # bone light
    "b": (186, 176, 150),   # bone shade
    "g": (120, 116, 112),   # stone
    "d": (78, 74, 76),      # stone dark
    "c": (52, 48, 56),      # crack
    "s": (200, 200, 210),   # web
    "r": (176, 40, 44),     # banner red
    "R": (120, 24, 34),     # banner red dark
    "u": (52, 84, 156),     # banner blue
    "U": (34, 52, 104),     # banner blue dark
    "y": (232, 184, 72),    # gold trim
    "o": (122, 82, 48),     # wood
    "p": (92, 60, 112),     # bat
    "P": (58, 38, 74),      # bat dark
    "m": (146, 112, 164),   # bat wing light
    "e": (232, 64, 56),     # eyes
}

CLUTTER_ART = {
    "BONES": [
        "................",
        "................",
        "................",
        "................",
        "................",
        ".....kk.........",
        "....kwwk.....kk.",
        ".....kwwk...kwbk",
        "......kwwk.kwbk.",
        ".......kwwkwbk..",
        "........kwwbk...",
        ".......kbkkwwk..",
        "......kbk..kwwk.",
        ".....kwk....kwk.",
        ".....kk......k..",
        "................",
    ],
    "SKULL": [
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        ".....kkkkkk.....",
        "....kwwwwwwk....",
        "...kwwwwwwwbk...",
        "...kwkkwkkwbk...",
        "...kwkkwkkwbk...",
        "...kbwwkwwbbk...",
        "....kbkwkwbk....",
        ".....kbbbbk.....",
        "......kkkk......",
    ],
    "COBWEB": [
        "ssssssssssss....",
        "s.s...s....s....",
        "s..s..s...s.....",
        "s...sssssss.....",
        "s..s.s.s.s......",
        "s.s..s.s.s......",
        "ss...sss.s......",
        "s...s..s.s......",
        "s..s...ss.......",
        "s.s.....s.......",
        "ss......s.......",
        "s......s........",
        "s......s........",
        "s.....s.........",
        "......s.........",
        "................",
    ],
    "CRACK": [
        "................",
        "......c.........",
        "......c.........",
        ".......c........",
        ".......cc.......",
        "........c.......",
        ".......cc...c...",
        "......c..c.c....",
        ".....c....cc....",
        "....c......c....",
        "...cc.......c...",
        "..c..........c..",
        "..............c.",
        "................",
        "................",
        "................",
    ],
    "RUBBLE": [
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "..........kkk...",
        ".....kk..kgggk..",
        "....kgdk.kgddk..",
        "....kddk..kkk...",
        "..kkk.kk...kk...",
        ".kgggk....kgdk..",
        ".kgddk.kk..kk...",
        "..kkk.kgdk......",
        ".......kk.......",
        "................",
    ],
    "BANNER_RED": [
        "..kkkkkkkkkkkk..",
        "..koooooooooook.",
        "...krrrrrrrrk...",
        "...krryyyyrrk...",
        "...kryrrrryrk...",
        "...krrrrrrrRk...",
        "...krryyyrrRk...",
        "...krrryrrrRk...",
        "...krrrrrrrRk...",
        "...krrrrrrrRk...",
        "...krrrrrrrRk...",
        "...krrrkkrrRk...",
        "...krrk..krRk...",
        "...krk....kRk...",
        "...kk......kk...",
        "................",
    ],
}


def ascii_tile(rows):
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            c = PUNY[ch]
            if c:
                img.putpixel((x, y), c + (255,))
    return img


def recolor_reds(img):
    """Wood browns to explosive-barrel reds (hue swap, keeps shading)."""
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if a and r > g + 12 and r > b + 12:
                px[x, y] = (min(255, r + 40), g // 3, b // 3, a)
    return out


def broken_pot(pot):
    """The lower half of the pot on the floor, with a few shards beside."""
    out = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    base = pot.crop((0, 9, 16, 16))
    out.paste(base, (0, 9), base)
    shard = pot.crop((5, 4, 8, 7))
    out.paste(shard, (1, 12), shard)
    out.paste(shard.transpose(Image.FLIP_LEFT_RIGHT), (12, 13), shard)
    return out


def make_clutter(defines):
    d = load(DUNGEON)
    cells = []
    for name in ("BONES", "SKULL", "COBWEB", "CRACK", "RUBBLE", "BANNER_RED"):
        cells.append((name, [ascii_tile(CLUTTER_ART[name])]))
    blue = ascii_tile([row.replace("r", "u").replace("R", "U")
                       for row in CLUTTER_ART["BANNER_RED"]])
    cells.append(("BANNER_BLUE", [blue]))
    cells.append(("POT_BROKEN", [broken_pot(tile(d, 23, 19))]))
    cells.append(("BARREL_RED", [recolor_reds(tile(d, 21, 19))]))
    # Crate debris: two loose planks from the crate texture.
    crate = tile(d, 16, 17)
    debris = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    plank = crate.crop((1, 6, 15, 9))
    debris.paste(plank, (1, 10), plank)
    debris.paste(plank.rotate(90, expand=True), (11, 3))
    cells.append(("CRATE_DEBRIS", [debris]))
    # Hazards from the tileset: saw blade (4 spin frames). (Fire jets are
    # on the fire sheet, make_fire.)
    cells.append(("SAW", [tile(d, 16 + i, 6) for i in range(4)]))
    # Boulders, for rocks floating in the void.
    cells.append(("BOULDER", [tile(d, 16 + i, 13) for i in range(0, 8, 2)]))
    out = Image.new("RGBA", (128, 64), (0, 0, 0, 0))
    slot = 0
    for name, frames in cells:
        defines.append(("CLUT_" + name, slot))
        if len(frames) > 1:
            defines.append(("CLUT_" + name + "_FRAMES", len(frames)))
        for f in frames:
            out.paste(f, ((slot % 8) * 16, (slot // 8) * 16), f)
            slot += 1
    assert slot <= 32
    save("spr_clutter", out)


# ---------------------------------------------------------------------------
# Fire sheet (128x128): every flame, drawn by tools/flames.py
#   16x16 cells from the top: the wall torch (12 frames; Shade's handle with
#   a new flame), a small fire on the ground (6, the Flame Dash trail), the
#   campfire (6, flames over logs). At y = 64: the fire jet, 16x32 (8).
# ---------------------------------------------------------------------------

TORCH_FRAMES, FLAME_FRAMES, CAMP_FRAMES, JET_FRAMES = 12, 6, 6, 8
TORCH_HANDLE = [(46, 32, 54), (205, 143, 81), (121, 84, 45), (69, 40, 60),
                (100, 66, 39)]


def torch_handle():
    """The Puny torch's handle and cup (rows 9 and down), without its
    flame."""
    src = tile(load(DUNGEON), 16, 0)
    out = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    for y in range(9, 16):
        for x in range(16):
            p = src.getpixel((x, y))
            if p[3] >= 128 and p[:3] in TORCH_HANDLE:
                out.putpixel((x, y), p)
    return out


def make_fire(defines):
    out = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    handle = torch_handle()
    cells = []
    for t in range(TORCH_FRAMES):
        f = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
        flames.flame(f, 8.0, 10.4, 9.0, 6.2, t, TORCH_FRAMES, seed=0.4,
                     sway=1.0, tongues=2, bulb=0.4, ripple=0.08)
        f.alpha_composite(handle)
        cells.append(f)
    for t in range(FLAME_FRAMES):
        f = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
        flames.flame(f, 8.0, 15.5, 11.0, 6.4, t, FLAME_FRAMES, seed=2.7,
                     sway=1.1, tongues=2, bulb=0.25, ripple=0.08)
        cells.append(f)
    for t in range(CAMP_FRAMES):
        # Two low side fires leaning out, a tall one in the middle, logs.
        f = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
        n = CAMP_FRAMES
        flames.flame(f, 5.0, 12.5, 6.5, 4.4, t, n, seed=2.0, sway=0.7,
                     tongues=1, lean=-0.8, core=False, ripple=0.05, bulb=0.2)
        flames.flame(f, 11.0, 12.5, 6.0, 4.2, t, n, seed=3.5, sway=0.7,
                     tongues=1, lean=0.8, core=False, ripple=0.05, bulb=0.2)
        flames.flame(f, 8.0, 12.8, 10.5, 6.6, t, n, seed=0.9, sway=1.1,
                     tongues=2, ripple=0.06, bulb=0.15)
        flames.logs(f, 0, 11, t, n)
        cells.append(f)
    for i, f in enumerate(cells):
        out.paste(f, ((i % 8) * 16, (i // 8) * 16), f)
    for t in range(JET_FRAMES):
        f = Image.new("RGBA", (16, 32), (0, 0, 0, 0))
        flames.flame(f, 8.0, 32.0, 29.0, 7.4, t, JET_FRAMES, seed=1.1,
                     sway=1.6, tongues=3, bulb=0.1)
        out.paste(f, (t * 16, 64), f)
    defines += [("FIRE_TORCH", 0), ("FIRE_TORCH_FRAMES", TORCH_FRAMES),
                ("FIRE_FLAME", TORCH_FRAMES), ("FIRE_FLAME_FRAMES", FLAME_FRAMES),
                ("FIRE_CAMP", TORCH_FRAMES + FLAME_FRAMES),
                ("FIRE_CAMP_FRAMES", CAMP_FRAMES),
                ("FIRE_JET_V", 64), ("FIRE_JET_FRAMES", JET_FRAMES),
                ("FIRE_SHEET_H", 128)]
    save("spr_fire", out)


# ---------------------------------------------------------------------------
# World sheet: the camp's trees and props, from the Puny World
# overworld tiles. 128x64; items are packed into free 16x16 cells and
# exported as WORLD_<NAME>_U / _V (pixel position) and _W / _H.
# ---------------------------------------------------------------------------

WORLD_ITEMS = [
    # name, (col, row, w, h) in the overworld sheet, or a file in ENV
    ("FOREST", (4, 7, 2, 3)),      # a block of pines: the forest edge
    ("PINE", (8, 7, 1, 1)),
    ("PINE2", (8, 8, 1, 1)),
    ("PINE_SMALL", (0, 30, 1, 1)),
    ("TREE_FRUIT", (2, 26, 1, 1)),
    ("TREE_ROUND", (3, 26, 1, 1)),
    ("TREE_GREEN", (0, 27, 1, 1)),
    ("TREE_PUNY", "Tree.png"),
    ("BUSH", (0, 29, 1, 1)),
    ("ROCK", (0, 26, 1, 1)),
    ("STONES", (1, 26, 1, 1)),
    ("STUMP", (1, 27, 1, 1)),
    ("TENT", (4, 26, 1, 1)),
    ("WELL", (4, 30, 1, 1)),
    ("SIGN", (7, 30, 1, 1)),
    ("BENCH", (4, 31, 1, 1)),
    ("TABLE", (7, 31, 1, 1)),
]

# The Caverns' sheet (128x32): the Puny Dungeon gems and a bat drawn
# here. Its own 15 colours: the forest's greens leave no room for these.
CAVE_ITEMS = [
    ("GEM", [("dungeon", 16 + i, 9) for i in range(4)]),   # spinning crystal
    ("SHARD", [("dungeon", 23, 9)]),
    ("CRYSTAL", [("dungeon", 20, 9), ("dungeon", 25, 9)]),  # wall crystal, glint
    ("BAT", ["bat_up", "bat_down"]),
]

# A bat, wings up and down: the left half of each row, mirrored.
BAT_HALVES = {
    "bat_up": ["........", "........", "........", "kk......", "kpk.....",
               "kmpk..k.", "kmmpk.kP", ".kmmpkPe", ".kmmppPP", "..kmppPP",
               "...kkpkP", "......kk", "........", "........", "........",
               "........"],
    "bat_down": ["........", "........", "........", "........", "........",
                 "......k.", "......kP", ".....kPe", "kkkkkpPP", "kmmmmpPP",
                 "kmmmpkkP", ".kmpk..k", "..kk....", "........", "........",
                 "........"],
}


def bat_frame(name):
    return ascii_tile([h + h[::-1] for h in BAT_HALVES[name]])


def pack_cells(items, cols, rows):
    """Places (name, image) items on a grid of 16x16 cells, first fit, in
    order. Returns {name: (u, v)}."""
    used = [[False] * cols for _ in range(rows)]
    where = {}
    for name, img in items:
        cw, ch = img.width // 16, img.height // 16
        placed = False
        for cy in range(rows - ch + 1):
            for cx in range(cols - cw + 1):
                if any(used[cy + j][cx + i] for j in range(ch) for i in range(cw)):
                    continue
                for j in range(ch):
                    for i in range(cw):
                        used[cy + j][cx + i] = True
                where[name] = (cx * 16, cy * 16)
                placed = True
                break
            if placed:
                break
        assert placed, name
    return where


def make_world(defines):
    make_item_sheet(defines, "spr_world", "WORLD", WORLD_ITEMS, 8, 4)
    make_item_sheet(defines, "spr_cave", "CAVE", CAVE_ITEMS, 8, 2)


def make_item_sheet(defines, sheet, prefix, entries, cols, rows):
    w = load(WORLD)
    d = load(DUNGEON)
    items = []
    frame_counts = {}
    for name, src in entries:
        if isinstance(src, list):
            # Animation frames, packed as NAME, NAME_1, ...
            for k, f in enumerate(src):
                img = bat_frame(f) if isinstance(f, str) else tile(d, f[1], f[2])
                items.append((name if k == 0 else "%s_%d" % (name, k), img))
            frame_counts[name] = len(src)
            continue
        if isinstance(src, str):
            img = load(os.path.join(ENV, src))
        else:
            img = tile(w, *src)
        items.append((name, img))
    where = pack_cells(items, cols, rows)
    out = Image.new("RGBA", (cols * 16, rows * 16), (0, 0, 0, 0))
    for name, img in items:
        u, v = where[name]
        # Drop the tiles' soft drop shadows (half-transparent): the game
        # draws its own.
        px = img.load()
        for y in range(img.height):
            for x in range(img.width):
                if px[x, y][3] < 200:
                    px[x, y] = (0, 0, 0, 0)
        out.alpha_composite(img, (u, v))
        defines += [("%s_%s_U" % (prefix, name), u),
                    ("%s_%s_V" % (prefix, name), v),
                    ("%s_%s_W" % (prefix, name), img.width),
                    ("%s_%s_H" % (prefix, name), img.height)]
    for name, n in frame_counts.items():
        defines.append(("%s_%s_FRAMES" % (prefix, name), n))
    save(sheet, out)


# ---------------------------------------------------------------------------
# Text font: Victoria Bold (CC0, devurandom), 8x8 glyphs '!'..'~' in a Grafx2
# strip (9x9 cells, white on blue: row 0 and column 0 of each cell are the
# separator, so the glyph is at x 1-8, y 1-8; row 8 holds the descenders of
# g j p q y). Written as 4bpp tiles for chars
# 32-127: index 15 = the glyph (tinted per text colour), 14 = a one-pixel
# drop shadow down and to the right.
# ---------------------------------------------------------------------------

FONT_SRC = os.path.join(SRC, "fonts", "VictoriaBold.png")
FONT_C = os.path.join(ROOT, "source", "art_font.c")
LAMP_GLYPH = [
    "...1....",
    "..111...",
    "1.1111.1",
    "1111111.",
    ".11111..",
    "..111...",
    ".11111..",
    "........",
]


def make_font():
    strip = load(FONT_SRC)
    words = []
    for code in range(32, 128):
        glyph = [[0] * 8 for _ in range(8)]
        i = code - 33
        if code == 127:
            # Char 127: Code Genie's lamp, the mark of a save that
            # has used cheats.
            for y, row in enumerate(LAMP_GLYPH):
                for x, c in enumerate(row):
                    if c == "1":
                        glyph[y][x] = 15
        elif 0 <= i < strip.width // 9:
            for y in range(8):
                for x in range(8):
                    r, g, b, a = strip.getpixel((i * 9 + 1 + x, 1 + y))
                    if r > 200 and g > 200 and b > 200:
                        glyph[y][x] = 15
        for y in range(7, -1, -1):
            for x in range(7, -1, -1):
                if glyph[y][x] == 15:
                    for sx, sy in ((x + 1, y), (x, y + 1), (x + 1, y + 1)):
                        if sx < 8 and sy < 8 and glyph[sy][sx] == 0:
                            glyph[sy][sx] = 14
        for y in range(8):
            w = 0
            for x in range(8):
                w |= glyph[y][x] << (4 * x)
            words.append(w)
    lines = ["// Generated by tools/import_art.py. Do not edit.",
             "// Victoria Bold by devurandom (CC0), chars 32-126, 4bpp; char 127",
             "// is Code Genie's lamp.",
             "", '#include "art_layout.h"', "",
             "const unsigned int art_font_tiles[96 * 8] = {"]
    for k in range(0, len(words), 8):
        lines.append("    " + ", ".join("0x%08X" % w for w in words[k:k + 8]) + ",")
    lines += ["};", ""]
    with open(FONT_C, "w") as f:
        f.write("\n".join(lines))
    print("  art_font.c     96 glyphs")


# ---------------------------------------------------------------------------
# Title logo: "DELVE" from the Victoria Bold glyphs, 4x, gold gradient with a
# bevel, a dark outline and a drop shadow. Cut into unique 8x8 tiles for the
# top screen's text layer (source/art_logo.c).
# ---------------------------------------------------------------------------

LOGO_C = os.path.join(ROOT, "source", "art_logo.c")
LOGO_SCALE = 4


def make_icon():
    """The ROM banner icon (icon.gif, 32x32, 16 colours, index 0
    transparent): the hero in a torch-lit doorway, in a gold frame."""
    sheet = Image.open(os.path.join(GFX, "spr_player.png")).convert("RGBA")
    cell = sheet.crop((0, 0, 24, 24))           # idle, facing the camera
    px = cell.load()
    key = px[0, 0]
    for y in range(24):
        for x in range(24):
            if px[x, y][:3] == key[:3]:
                px[x, y] = (0, 0, 0, 0)
    hero = cell.crop(cell.getbbox())
    hero = hero.resize((hero.width * 2, hero.height * 2), Image.NEAREST)

    img = Image.new("RGBA", (32, 32))
    ip = img.load()
    for y in range(32):
        for x in range(32):
            # Warm glow behind the hero fading into indigo dark.
            d = ((x - 16) ** 2 + (y - 13) ** 2) ** 0.5 / 16
            t = max(0.0, min(1.0, d))
            glow = (196, 112, 44)
            dark = (26, 18, 48)
            ip[x, y] = tuple(int(glow[k] * (1 - t) ** 1.6 + dark[k]
                                 * (1 - (1 - t) ** 1.6)) for k in range(3)) + (255,)
    # The doorway: stone jambs and a lintel.
    stone, edge = (92, 86, 104, 255), (54, 48, 66, 255)
    for y in range(4, 31):
        for x in (3, 4, 5, 26, 27, 28):
            ip[x, y] = edge if x in (5, 26) else stone
    for y in (3, 4, 5):
        for x in range(3, 29):
            ip[x, y] = edge if y == 5 else stone
    # Floor.
    for y in (29, 30):
        for x in range(1, 31):
            ip[x, y] = edge
    hx = 16 - hero.width // 2
    hy = 30 - hero.height
    img.alpha_composite(hero, (hx, max(1, hy)))
    # Gold frame with clipped (transparent) corners.
    gold, shade = (250, 214, 96, 255), (150, 88, 28, 255)
    for i in range(32):
        for x, y in ((i, 0), (i, 31), (0, i), (31, i)):
            ip[x, y] = gold if x == 0 or y == 0 else shade
    for x, y in ((0, 0), (31, 0), (0, 31), (31, 31)):
        ip[x, y] = (0, 0, 0, 0)

    # 15 colours plus transparent index 0.
    rgb = Image.new("RGB", (32, 32))
    rgb.paste(img.convert("RGB"))
    q = rgb.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=0)
    pal = q.getpalette()[:45]
    out = Image.new("P", (32, 32))
    out.putpalette([255, 0, 255] + pal + [0] * (768 - 48))
    qp, op = q.load(), out.load()
    for y in range(32):
        for x in range(32):
            op[x, y] = 0 if ip[x, y][3] == 0 else qp[x, y] + 1
    out.save(os.path.join(ROOT, "icon.gif"), transparency=0)
    print("  icon.gif       32x32 %2d colours" % 16)


def glyph_cells(strip, ch):
    """The (x, y) pixels of a Victoria Bold glyph, read as the logo always
    has (cell origin, 8x8)."""
    i = ord(ch) - 33
    cells = []
    for y in range(8):
        for x in range(8):
            r, g, b, a = strip.getpixel((i * 9 + x, y))
            if r > 200 and g > 200 and b > 200:
                cells.append((x, y))
    return cells


def logo_style(mask, width, height):
    """Gold gradient with a bevel, a dark outline and a drop shadow, over a
    mask whose glyph rows start at y 2."""
    img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    gold = [(255, 240, 170), (250, 214, 96), (232, 172, 56), (196, 128, 36),
            (150, 88, 28)]

    def inside(x, y):
        return 0 <= x < width and 0 <= y < height and mask[y][x]

    for y in range(height):
        for x in range(width):
            if inside(x, y):
                band = min(len(gold) - 1, (y - 2) * len(gold) // (8 * LOGO_SCALE))
                c = gold[band]
                # Bevel: lighter on top/left edges, darker bottom/right.
                if not inside(x, y - 1) or not inside(x - 1, y):
                    c = (255, 250, 220)
                elif not inside(x, y + 1) or not inside(x + 1, y):
                    c = (110, 60, 20)
                img.putpixel((x, y), c + (255,))
    # Outline, then a drop shadow two pixels down-right.
    outline = (24, 12, 20, 255)
    shadow = (40, 20, 50, 255)
    base = img.copy()
    for y in range(height):
        for x in range(width):
            if base.getpixel((x, y))[3]:
                continue
            near = any(inside(x + dx, y + dy) for dx in (-1, 0, 1)
                       for dy in (-1, 0, 1))
            if near:
                img.putpixel((x, y), outline)
            elif inside(x - 2, y - 2) or inside(x - 3, y - 3):
                img.putpixel((x, y), shadow)
    # Pad to whole tiles.
    tw, th = -(-width // 8), -(-height // 8)
    full = Image.new("RGBA", (tw * 8, th * 8), (0, 0, 0, 0))
    full.paste(img, (0, 0))
    return full


# Big digits in the logo's style (the Easter egg's version splash), for
# "0"-"9" and ".", each as narrow as its glyph.
BIG_GLYPHS = "0123456789."


def make_logo(defines):
    strip = load(FONT_SRC)
    word = "DELVE"
    gw = 8 * LOGO_SCALE
    gap = 2
    width = len(word) * (gw + gap) + 8
    height = 8 * LOGO_SCALE + 8
    mask = [[False] * width for _ in range(height)]
    for n, ch in enumerate(word):
        for x, y in glyph_cells(strip, ch):
            for sy in range(LOGO_SCALE):
                for sx in range(LOGO_SCALE):
                    mask[2 + y * LOGO_SCALE + sy][2 + n * (gw + gap)
                                                  + x * LOGO_SCALE + sx] = True
    images = [logo_style(mask, width, height)]
    for ch in BIG_GLYPHS:
        cells = glyph_cells(strip, ch)
        x0 = min(x for x, _ in cells)
        x1 = max(x for x, _ in cells)
        w = (x1 - x0 + 1) * LOGO_SCALE + 6
        m = [[False] * w for _ in range(height)]
        for x, y in cells:
            for sy in range(LOGO_SCALE):
                for sx in range(LOGO_SCALE):
                    m[2 + y * LOGO_SCALE + sy][2 + (x - x0) * LOGO_SCALE + sx] = True
        images.append(logo_style(m, w, height))

    # One palette for all of them: side by side on one sheet.
    sheet = Image.new("RGBA", (sum(im.width for im in images), images[0].height),
                      (0, 0, 0, 0))
    ox = 0
    for im in images:
        sheet.paste(im, (ox, 0))
        ox += im.width
    idx, palette = to_indexed(sheet)
    pixels = list(idx.getdata())
    sw = sheet.width

    tiles, index, maps = [], {}, []
    ox = 0
    for im in images:
        tw, th = im.width // 8, im.height // 8
        tmap = []
        for ty in range(th):
            for tx in range(tw):
                t = tuple(pixels[(ty * 8 + y) * sw + ox + tx * 8 + x]
                          for y in range(8) for x in range(8))
                if t not in index:
                    index[t] = len(tiles)
                    tiles.append(t)
                tmap.append(index[t])
        maps.append((tw, th, tmap))
        ox += im.width
    words = []
    for t in tiles:
        for y in range(8):
            w = 0
            for x in range(8):
                w |= t[y * 8 + x] << (4 * x)
            words.append(w)
    pal15 = [0] + [rgb15(c) for c in palette]
    pal15 += [0] * (16 - len(pal15))
    tw, th, tmap = maps[0]
    big_h = maps[1][1]
    big_w = max(m[0] for m in maps[1:])
    lines = ["// Generated by tools/import_art.py. Do not edit.",
             "// The title logo and the big digits: tiles, maps and palette.",
             "", '#include "art_layout.h"', "",
             "const unsigned int art_logo_tiles[%d] = {" % len(words)]
    for k in range(0, len(words), 8):
        lines.append("    " + ", ".join("0x%08X" % w for w in words[k:k + 8]) + ",")
    lines += ["};", "", "const unsigned short art_logo_map[%d] = {" % len(tmap)]
    for k in range(0, len(tmap), 16):
        lines.append("    " + ", ".join("%d" % v for v in tmap[k:k + 16]) + ",")
    lines += ["};", "",
              "// \"%s\": each glyph's map, BIG_H rows of BIG_W (unused columns 0)."
              % BIG_GLYPHS,
              "const unsigned short art_big_map[%d] = {"
              % (len(BIG_GLYPHS) * big_h * big_w)]
    for ch, (gw_, gh, gmap) in zip(BIG_GLYPHS, maps[1:]):
        full = []
        for y in range(gh):
            full += gmap[y * gw_:(y + 1) * gw_] + [0] * (big_w - gw_)
        lines.append("    " + ", ".join("%d" % v for v in full) + ",  // %s" % ch)
    lines += ["};", "", "const unsigned char art_big_w[%d] = {" % len(BIG_GLYPHS),
              "    " + ", ".join("%d" % m[0] for m in maps[1:]) + ",", "};", ""]
    lines += ["const unsigned short art_logo_pal[16] = {",
              "    " + ", ".join("0x%04X" % c for c in pal15) + ",", "};", ""]
    with open(LOGO_C, "w") as f:
        f.write("\n".join(lines))
    defines += [("LOGO_TILES", len(tiles)), ("LOGO_W", tw), ("LOGO_H", th),
                ("BIG_W", big_w), ("BIG_H", big_h)]
    print("  art_logo.c     %dx%d tiles + %d big glyphs, %d unique"
          % (tw, th, len(BIG_GLYPHS), len(tiles)))


# ---------------------------------------------------------------------------
# UI icons (bottom screen OBJ, 16x16): the four placeholder reward icons
# from tools/make_art.py, then menu icons from the Puny packs.
# ---------------------------------------------------------------------------

ICONS_BASE = os.path.join(ROOT, "art", "generated", "ui_icons_base.png")


def make_ui_icons():
    d = load(DUNGEON)
    base = Image.open(ICONS_BASE).convert("RGBA")
    base.putdata([(0, 0, 0, 0) if p[:3] == KEY else p for p in base.getdata()])
    extra = [
        tile(d, 24, 17),                                  # gear: options
        ascii_tile(CLUTTER_ART["BANNER_RED"]),            # banner: credits
        tile(d, 23, 18),                                  # idol: about
        ascii_tile(CLUTTER_ART["SKULL"]),                 # skull: start run
    ]
    out = Image.new("RGBA", (16, 16 * (4 + len(extra))), (0, 0, 0, 0))
    out.paste(base, (0, 0))
    for i, img in enumerate(extra):
        out.paste(img, (0, 64 + i * 16), img)
    idx, palette = to_indexed(out)
    idx.save(os.path.join(GFX, "ui_icons.png"))
    with open(os.path.join(GFX, "ui_icons.grit"), "w") as f:
        f.write("# 16x16 OBJ sprites, 4bpp, 1D mapping order\n"
                "-gt -gB4 -gTFF00FF -Mw2 -Mh2\n")
    print("  ui_icons       16x%d %2d colours" % (out.height, len(palette)))


def write_palettes(palettes):
    lines = ["// Generated by tools/import_art.py. Do not edit.", "",
             '#include "art_layout.h"', ""]
    for name, pal in palettes:
        lines.append("const unsigned short %s[16] = {" % name)
        lines.append("    " + ", ".join("0x%04X" % c for c in pal) + ",")
        lines.append("};")
        lines.append("")
    with open(PALETTES_C, "w") as f:
        f.write("\n".join(lines))


def write_layout(defines, palettes):
    lines = [
        "// Generated by tools/import_art.py. Do not edit.",
        "//",
        "// Character sheets: %d x %d px cells, %d per row; slot = dir * "
        "frames_per_dir + frame." % (CELL, CELL, SHEET_COLS),
        "// Directions: 0 down, 1 down-right, 2 right, 3 up-right, 4 up "
        "(left = mirrored).",
        "// Props sheet: 16 x 16 px cells, 8 per row.",
        "",
        "#ifndef DELVE_ART_LAYOUT_H",
        "#define DELVE_ART_LAYOUT_H",
        "",
        "#define CHAR_CELL %d" % CELL,
        "#define CHAR_SHEET_COLS %d" % SHEET_COLS,
        "#define CHAR_DIRS %d" % len(DIR_ROWS),
        "#define CHAR_SHADOW_U %d" % SHADOW_U,
        "#define CHAR_SHADOW_V %d" % SHADOW_V,
        "",
    ]
    lines += ["// The title logo (art_logo.c): LOGO_TILES tiles, LOGO_W x LOGO_H map;",
              "// big digits \"0123456789.\", glyph g at art_big_map[g * BIG_W *",
              "// BIG_H], BIG_H rows of BIG_W, art_big_w[g] columns used."]
    lines += ["extern const unsigned int art_logo_tiles[];",
              "extern const unsigned short art_logo_map[];",
              "extern const unsigned short art_big_map[];",
              "extern const unsigned char art_big_w[];",
              "extern const unsigned short art_logo_pal[16];", ""]
    lines += ["// Text font tiles (4bpp, chars 32-127), in art_font.c."]
    lines += ["extern const unsigned int art_font_tiles[96 * 8];", ""]
    lines += ["// Elite recolours (RGB15), in art_palettes.c."]
    lines += ["extern const unsigned short %s[16];" % n for n, _ in palettes]
    lines += [""]
    width = max(len(n) for n, _ in defines)
    lines += ["#define %-*s %d" % (width, n, v) for n, v in defines]
    lines += ["", "#endif // DELVE_ART_LAYOUT_H", ""]
    with open(LAYOUT_H, "w") as f:
        f.write("\n".join(lines))


def main():
    os.makedirs(GFX, exist_ok=True)
    defines = []
    palettes = []
    make_characters(defines, palettes)
    make_monsters(defines, palettes)
    make_variants(palettes)
    make_textures()
    make_props(defines)
    make_fx(defines)
    make_clutter(defines)
    make_fire(defines)
    make_world(defines)
    make_font()
    make_ui_icons()
    make_icon()
    make_logo(defines)
    write_layout(defines, palettes)
    write_palettes(palettes)
    print("art imported to", os.path.normpath(GFX))


if __name__ == "__main__":
    main()
