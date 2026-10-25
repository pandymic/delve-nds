// The persistent game data and its defaults (game.h).

#include <string.h>

#include "game.h"

GameData game;
uint32_t game_frames;

void game_set_defaults(GameData *g)
{
    memset(g, 0, sizeof(*g));
    g->options.music_volume = 7;
    g->options.sfx_volume = 8;
    g->options.default_zoom = ZOOM_CLOSE;
    g->options.screen_shake = 1;
    g->options.hit_numbers = 1;
    g->options.hints = 1;
}
