// Maxmod music and sound effects, with fades between tracks (audio.h).

#include <maxmod9.h>
#include <nds.h>

#include "audio.h"
#include "game.h"
#include "soundbank.h"
#include "soundbank_bin.h"

// Maxmod setup as in the BlocksDS rom_arm9_only template.

static const mm_word sound_ids[SND_COUNT] = {
    [SND_SELECT] = SFX_SELECT,
    [SND_SWING]  = SFX_SWING,
    [SND_HIT]    = SFX_HIT,
    [SND_HURT]   = SFX_HURT,
    [SND_DASH]   = SFX_DASH,
    [SND_DOOR]   = SFX_DOOR,
    [SND_PICKUP] = SFX_PICKUP,
    [SND_DEATH]  = SFX_DEATH,
    [SND_SHOOT]  = SFX_SHOOT,
    [SND_POP]    = SFX_POP,
    [SND_THROW]  = SFX_THROW,
    [SND_TINK]   = SFX_TINK,
    [SND_BLAST]  = SFX_BLAST,
    [SND_CHARGED] = SFX_CHARGED,
    [SND_SHATTER] = SFX_SHATTER,
    [SND_WOOD]   = SFX_WOOD,
    [SND_EXPLODE] = SFX_EXPLODE,
    [SND_COIN]   = SFX_COIN,
    [SND_FALL]   = SFX_FALL,
    [SND_SPIKES] = SFX_SPIKES,
    [SND_ROAR]   = SFX_ROAR,
    [SND_SLAM]   = SFX_SLAM,
    [SND_SPLAT]  = SFX_SPLAT,
    [SND_FANFARE] = SFX_FANFARE,
    [SND_PORTAL] = SFX_PORTAL,
    [SND_CHIME]  = SFX_CHIME,
    [SND_BACK]   = SFX_BACK,
    [SND_DENY]   = SFX_DENY,
    [SND_EMPTY]  = SFX_EMPTY,
    [SND_HEARTBEAT] = SFX_HEARTBEAT,
    [SND_BUY]    = SFX_BUY,
};

// CC0 and public-domain tracker music (tools/import_music.py, LICENSES.md).
static const mm_word module_ids[MUSIC_COUNT] = {
    [MUSIC_MENU]    = MOD_MUSIC_TITLE,
    [MUSIC_CAMP]    = MOD_MUSIC_CAMP,
    [MUSIC_DUNGEON] = MOD_MUSIC_DUNGEON,
    [MUSIC_SEWERS]  = MOD_MUSIC_SEWERS,
    [MUSIC_BOSS]    = MOD_MUSIC_BOSS,
    [MUSIC_CAVERNS] = MOD_MUSIC_CAVERNS,
    [MUSIC_MOLTEN]  = MOD_MUSIC_MOLTEN,
};

// Per-track level (percent of full), so every track sits near -22 dB.
// Measured in melonDS at full volume (Maxmod treats the IT mix volume and
// channel counts differently from desktop players); modules can only be
// turned down, and the Sewers' track is the quietest.
static const uint8_t module_gain[MUSIC_COUNT] = {
    [MUSIC_MENU]    = 84,    // -20.5 dB at full
    [MUSIC_CAMP]    = 58,    // -17.2
    [MUSIC_DUNGEON] = 62,    // -17.9
    [MUSIC_SEWERS]  = 100,   // -24.2
    [MUSIC_BOSS]    = 56,    // -16.0 (a touch louder: the fight)
    [MUSIC_CAVERNS] = 25,    // -9.8, clipping at full
    [MUSIC_MOLTEN]  = 46,    // -15.3
};

// Music fades: the playing track fades out, then the wanted one starts and
// fades in. fade runs 0 (silent) .. FADE_STEPS (full volume).
#define FADE_STEPS 24
static Music playing = MUSIC_NONE;   // the module Maxmod is playing
static Music wanted = MUSIC_NONE;    // what audio_music last asked for
static int fade;

static void set_module_volume(void)
{
    // Module volume range is 0-1024.
    int gain = MUSIC_NONE == playing ? 100 : module_gain[playing];
    int full = game.options.music_volume * 1024 / VOLUME_MAX * gain / 100;
    mmSetModuleVolume((mm_word)(full * fade / FADE_STEPS));
}

void audio_init(void)
{
    mmInitDefaultMem((mm_addr)soundbank_bin);
    // Interpolated mixing (maxmod9.h): linear interpolation in software on
    // top of the hardware channels. The default full-hardware mode plays
    // samples with none, so low-rate ones come out raspy and crackly.
    mmSelectMode(MM_MODE_B);
    for (int m = MUSIC_NONE + 1; m < MUSIC_COUNT; m++)
        mmLoad(module_ids[m]);
    for (int i = 0; i < SND_COUNT; i++)
        mmLoadEffect(sound_ids[i]);
    audio_apply_volume();
}

void audio_music(Music m)
{
    wanted = m;
    // Nothing playing yet: start at once (from silence, fading in).
    if (MUSIC_NONE == playing && MUSIC_NONE != m) {
        playing = m;
        fade = 0;
        mmStart(module_ids[m], MM_PLAY_LOOP);
        set_module_volume();
    }
}

void audio_update(void)
{
    if (playing != wanted) {
        // Fade the old track out, then switch.
        if (fade > 0) {
            fade--;
        } else {
            if (MUSIC_NONE != playing)
                mmStop();
            playing = wanted;
            if (MUSIC_NONE != playing)
                mmStart(module_ids[playing], MM_PLAY_LOOP);
        }
        set_module_volume();
    } else if (MUSIC_NONE != playing && fade < FADE_STEPS) {
        fade++;
        set_module_volume();
    }
}

void audio_sfx(Sound s)
{
    int vol = game.options.sfx_volume * 255 / VOLUME_MAX;
    if (0 == vol)
        return;
    mm_sound_effect fx = {
        .id = sound_ids[s],
        .rate = 1024,       // original pitch
        .handle = 0,
        .volume = (mm_byte)vol,
        .panning = 128,
    };
    mmEffectEx(&fx);
}

void audio_apply_volume(void)
{
    set_module_volume();
}
