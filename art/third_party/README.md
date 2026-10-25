# Third-party art sources

These are the original files, kept unmodified. `tools/import_art.py` crops,
recolours and packs them into `gfx/`. Every pack here must also be listed in
`/CREDITS.md`.

| Folder | Pack | Author | Licence | Source |
|---|---|---|---|---|
| `puny-characters/` | Puny Characters (orcs included) | Shade | CC0 1.0 | https://opengameart.org/content/puny-characters |
| `puny-dungeon/` | 16x16 Puny Dungeon Tileset v1 | Shade | CC0 1.0 | https://opengameart.org/content/16x16-puny-dungeon-tileset |
| `puny-world/` | 16x16 Puny World Tileset (overworld) | Shade | CC0 1.0 | https://opengameart.org/content/16x16-puny-world-tileset |
| `puny-characters/Environment/` | Grass, dirt and tree tiles from the Puny Characters download | Shade | CC0 1.0 | https://opengameart.org/content/puny-characters |
| `miniworld/Monsters/` | Miniworld Sprites: the monster sheets (characters, buildings and the rest of the pack not copied) | Shade | CC0 1.0 | https://opengameart.org/content/miniworld-sprites |
| `music/` | Seven tracker modules (CC0 and public domain) | Kosmo The Cat, Beyond, Drozerix, JAM | CC0 1.0 / public domain | see `music/README.md` |
| `fonts/` | Victoria Bold, from "a package of 8 bit fonts for grafx2 and linux" | devurandom | CC0 1.0 | https://opengameart.org/content/a-package-of-8-bit-fonts-for-grafx2-and-linux |

CC0 needs no attribution, but the author is credited anyway.

## Puny Characters layout

Each character sheet is 768x256: 24 columns x 8 rows of 32x32 cells. Rows are
facing directions, measured against the screen:

| Row | Facing |
|---|---|
| 0 | down (towards the viewer) |
| 1 | down-right |
| 2 | right |
| 3 | up-right |
| 4 | up |
| 5 | up-left |
| 6 | left |
| 7 | down-left |

There are two column layouts. Layout B has one more early frame, so every
later animation sits one column further right:

| Animation | Layout A: Archer, Character-Base, Mage, Soldier, Warrior | Layout B: Human-*, Orc-* |
|---|---|---|
| idle | 0-1 | 0-1 |
| walk | 0-3 | 1-4 |
| sword attack (arc frame) | 4-7 (6) | 5-8 (7) |
| bow attack | 8-11 | 9-12 |
| staff attack | 12-14 | 13-15 |
| throw | 15-18 | 16-19 |
| hurt flash (white) | 19 | 20 |
| hurt | 20 | 21 |
| death | 21-23 | 22-23 |

`tools/import_art.py` holds these tables as `LAYOUT_A` / `LAYOUT_B`.

## Miniworld monster layout

Each monster sheet is a grid of 16x16 frames (the dragons 32x32). Rows face
down, up, and the two sides, then the same four attacking. Which side row
faces right differs by sheet; `MONSTER_SHEETS` in `tools/import_art.py`
names the rows it uses. The minotaur has one side walking row, and the crab
has no attack rows.
