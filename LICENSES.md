# Licences and attribution

Delve's own code, and the art and sound it generates itself (see below), are
under the MIT licence in [LICENSE](LICENSE). The third-party pieces keep
their own licences: this page lists each one with its author, licence and
source. The originals are kept unmodified in `art/third_party/`; the tools
that convert them are named below. A short version of this list is on the
game's About screen and in its credits.

## Music

All CC0 or public domain. Thank you to the composers.

| Track | Artist | Licence | Source | In the game |
|---|---|---|---|---|
| *Calm and simple title music* | Kosmo The Cat | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) | [OpenGameArt](https://opengameart.org/content/calm-and-simple-title-music) | Title and menus |
| *Wander in a dungeon* | Kosmo The Cat | CC0 1.0 | [OpenGameArt](https://opengameart.org/content/wander-in-a-dungeon) | The Dungeon (depths 1–10) |
| *Evil boss music* | Kosmo The Cat | CC0 1.0 | [OpenGameArt](https://opengameart.org/content/evil-boss-music) | Boss fights |
| *Piano tune* | Beyond | Public domain | [The Mod Archive #213498](https://modarchive.org/index.php?request=view_by_moduleid&query=213498) | The camp |
| *Song0* | Beyond | Public domain | [The Mod Archive #184094](https://modarchive.org/index.php?request=view_by_moduleid&query=184094) | The Sunken Sewers (11–20) |
| *Ambient Starfield* | Drozerix | Public domain | [The Mod Archive #174160](https://modarchive.org/index.php?request=view_by_moduleid&query=174160) | The Crystal Caverns (21–30) |
| *Crusaders* | JAM | Public domain | [The Mod Archive #177809](https://modarchive.org/index.php?request=view_by_moduleid&query=177809) | The Molten Depths (31–40) |

- **Files:** `art/third_party/music/`. `tools/import_music.py` writes the
  game's copies (`audio/music_*`):
  - the XM modules' 16-bit samples become 8-bit, to fit the DS's memory;
  - two empty credit "instruments" in *Crusaders* lose their sample
    headers;
  - the notes and arrangements are unchanged.
- **Public domain:** this is The Mod Archive's licence attribution. Per its
  [licensing FAQ](https://modarchive.org/index.php?faq-licensing), the
  composer has released the module to the public domain, free to use for
  any purpose.

## Art

| Asset | Author | Licence | Source | Used for |
|---|---|---|---|---|
| Puny Characters | Shade | CC0 1.0 | [OpenGameArt](https://opengameart.org/content/puny-characters) | The hero, the orcs, mages and archers, the slimes (and their recolours), the training dummy; grass, dirt and tree tiles |
| 16x16 Puny Dungeon Tileset | Shade | CC0 1.0 | [OpenGameArt](https://opengameart.org/content/16x16-puny-dungeon-tileset) | Dungeon floors and walls, props, traps, gems, the basalt textures (recoloured) |
| 16x16 Puny World Tileset | Shade | CC0 1.0 | [OpenGameArt](https://opengameart.org/content/16x16-puny-world-tileset) | The camp, cave rock and floor (recoloured) |
| Miniworld Sprites | Shade | CC0 1.0 | [OpenGameArt](https://opengameart.org/content/miniworld-sprites) | The imp (red demon), magma brute (minotaur, recoloured), cave crab, Cavern Yeti and Magma Lord (red dragon) |
| Victoria Bold, from "a package of 8 bit fonts for grafx2 and linux" | devurandom | CC0 1.0 | [OpenGameArt](https://opengameart.org/content/a-package-of-8-bit-fonts-for-grafx2-and-linux) | The text font and the title logo |

`tools/import_art.py` crops, recolours and packs them; for the Miniworld
monsters it also draws hurt and death frames from their first walk frame. The bat, the
clutter, the effects, the flames (`tools/flames.py`: torch flames on
Shade's torch handles, campfires, fire jets), the lava and murk textures and
the placeholder icons are original (drawn by `tools/import_art.py` and
`tools/make_art.py`).

## Sound effects

Original to Delve: synthesized by `tools/make_audio.py`. The script can
also write Delve's earlier original soundtrack (`--songs`, into
`art/generated/music/`); the game no longer plays it.

## Libraries linked into delve.nds

| Library | Author | Licence |
|---|---|---|
| [BlocksDS](https://github.com/blocksds/sdk) (libnds, crt, ARM7 core) | Antonio Niño Díaz and contributors; libnds from devkitPro | zlib |
| [Nitro Engine](https://github.com/AntonioND/nitro-engine) | Antonio Niño Díaz | MIT |
| [Maxmod](https://github.com/blocksds/maxmod) | Mukunda Johnson and contributors | ISC |
| [picolibc](https://github.com/picolibc/picolibc) | Keith Packard and contributors | BSD-style (see the project) |

## Homage

The hidden Rogue tribute is a new implementation of *Rogue* (1980) by
Michael Toy, Glenn Wichman and Ken Arnold, with no original code. A few
names and messages are quoted in homage.
