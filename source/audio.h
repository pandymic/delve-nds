// Music and sound effects (Maxmod). Volumes follow game.options.

#ifndef DELVE_AUDIO_H
#define DELVE_AUDIO_H

// Tracks (tools/import_music.py; credits in LICENSES.md). Changing track
// fades the old one out and the new one in.
typedef enum {
    MUSIC_NONE,
    MUSIC_MENU,      // "Calm and simple title music": title, menus, summary
    MUSIC_CAMP,      // "Piano tune": the camp
    MUSIC_DUNGEON,   // "Wander in a dungeon": the Dungeon
    MUSIC_SEWERS,    // "Song0": the Sunken Sewers
    MUSIC_BOSS,      // "Evil boss music": boss arenas
    MUSIC_CAVERNS,   // "Ambient Starfield": the Crystal Caverns
    MUSIC_MOLTEN,    // "Crusaders": the Molten Depths
    MUSIC_COUNT
} Music;

typedef enum {
    SND_SELECT,
    SND_SWING,
    SND_HIT,
    SND_HURT,
    SND_DASH,
    SND_DOOR,
    SND_PICKUP,
    SND_DEATH,
    SND_SHOOT,
    SND_POP,
    SND_THROW,
    SND_TINK,
    SND_BLAST,
    SND_CHARGED,
    SND_SHATTER,
    SND_WOOD,
    SND_EXPLODE,
    SND_COIN,
    SND_FALL,
    SND_SPIKES,
    SND_ROAR,        // a boss charges
    SND_SLAM,        // something heavy hits the ground or a wall
    SND_SPLAT,       // the Slime King lands
    SND_FANFARE,     // a boss falls
    SND_PORTAL,      // stepping through the camp's portal
    SND_CHIME,       // the shrine, a boon chosen
    SND_BACK,        // menus: back
    SND_DENY,        // can't: too poor, locked
    SND_EMPTY,       // an action with nothing left to use
    SND_HEARTBEAT,   // one heart left
    SND_BUY,         // an upgrade bought
    SND_COUNT
} Sound;

// Loads the soundbank, every track and effect, and applies the volumes.
void audio_init(void);

// Starts a track (looping); does nothing if it is already playing.
void audio_music(Music m);

// Plays a sound effect at the SFX volume (nothing at volume 0).
void audio_sfx(Sound s);

// Runs the music fades; call once per frame.
void audio_update(void);

// Re-reads the volumes from game.options.
void audio_apply_volume(void);

#endif // DELVE_AUDIO_H
