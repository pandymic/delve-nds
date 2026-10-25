// Save files: encoding, decoding, slots and safe writes (save.h).

#include <stdio.h>
#include <string.h>

#include "fixed.h"
#include "save.h"

#ifdef __NDS__
#include <fat.h>
#include <sys/stat.h>
#endif

static char path[256] = "delve.sav";
static char stem[240] = "delve";    // slot files: "<stem>-<n>.sav"
static int selected = -1;           // slot path points at, or -1

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

// Version 2+ field tags. Never reuse or renumber a tag; retire it instead.
typedef enum {
    TAG_TOTAL_RUNS   = 1,   // u32
    TAG_BEST_DEPTH   = 2,   // u32
    TAG_CURRENCY     = 3,   // u32
    TAG_UPGRADES     = 4,   // u32 bitfield
    TAG_TOTAL_KILLS  = 5,   // u32
    TAG_BOSSES_SLAIN = 6,   // u32
    TAG_HINTS_SEEN   = 7,   // u32 bitfield
    TAG_WAYPOINTS    = 8,   // u32 bitfield
    TAG_PLAY_SECONDS = 9,   // u32
    TAG_LAST_PLAYED  = 10,  // u32
    TAG_CHEATS_USED  = 11,  // u32 bitfield
    TAG_MUSIC_VOLUME = 16,  // u8
    TAG_SFX_VOLUME   = 17,  // u8
    TAG_DEFAULT_ZOOM = 18,  // u8
    TAG_AUTO_ATTACK  = 19,  // u8 bool
    TAG_LEFT_HANDED  = 20,  // u8 bool
    TAG_SCREEN_SHAKE = 21,  // u8 bool
    TAG_HIT_NUMBERS  = 22,  // u8 bool
    TAG_HINTS        = 23,  // u8 bool
    TAG_ROGUE_BOARD  = 32,  // u8 count, then per entry: u32 score, u8 level,
                            // u8 cause, RG_NAME_LEN name bytes
    TAG_RUN_HISTORY  = 33,  // u8 count, then per run (newest first):
                            // u32 seed; u16 depth, kills, gold, earned,
                            // rooms, seconds; u8 cause, bosses, flags
                            // (HISTORY_*);
                            // 8 bytes of 4-bit boon stacks; u8
                            // start depth (absent in 27-byte entries)
} SaveTag;

#define HISTORY_ENTRY_SIZE 28
#define HISTORY_ENTRY_V1   27   // 2.1.0: no start depth
#define HISTORY_NEW_BEST   1
#define HISTORY_ABANDONED  2
#define HISTORY_CHEATED    4

#define ROGUE_ENTRY_SIZE (6 + RG_NAME_LEN)

#define V1_PAYLOAD_SIZE 24

static uint8_t *put_tag_u32(uint8_t *p, SaveTag tag, uint32_t v)
{
    p[0] = (uint8_t)tag;
    p[1] = 4;
    put_u32(p + 2, v);
    return p + 6;
}

static uint8_t *put_tag_u8(uint8_t *p, SaveTag tag, uint8_t v)
{
    p[0] = (uint8_t)tag;
    p[1] = 1;
    p[2] = v;
    return p + 3;
}

static uint8_t *put_rogue_board(uint8_t *p, const RogueBoard *b)
{
    int n = imin(b->count, RG_SCORES);
    p[0] = TAG_ROGUE_BOARD;
    p[1] = (uint8_t)(1 + n * ROGUE_ENTRY_SIZE);
    p[2] = (uint8_t)n;
    p += 3;
    for (int i = 0; i < n; i++) {
        const RogueScore *e = &b->entries[i];
        put_u32(p, e->score);
        p[4] = e->level;
        p[5] = e->cause;
        memcpy(p + 6, e->name, RG_NAME_LEN);
        p[6 + RG_NAME_LEN - 1] = '\0';
        p += ROGUE_ENTRY_SIZE;
    }
    return p;
}

static void get_rogue_board(const uint8_t *d, int len, RogueBoard *b)
{
    memset(b, 0, sizeof(*b));
    if (len < 1)
        return;
    int n = d[0];
    if (n > RG_SCORES || len != 1 + n * ROGUE_ENTRY_SIZE)
        return;
    for (int i = 0; i < n; i++) {
        const uint8_t *e = d + 1 + i * ROGUE_ENTRY_SIZE;
        RogueScore *s = &b->entries[i];
        s->score = get_u32(e);
        s->level = e[4];
        s->cause = e[5] <= DEATH_WON ? e[5] : DEATH_QUIT;
        memcpy(s->name, e + 6, RG_NAME_LEN);
        s->name[RG_NAME_LEN - 1] = '\0';
    }
    b->count = (uint8_t)n;
}

static uint8_t *put_history(uint8_t *p, const RunHistory *h)
{
    int n = imin(h->count, RUN_HISTORY);
    p[0] = TAG_RUN_HISTORY;
    p[1] = (uint8_t)(1 + n * HISTORY_ENTRY_SIZE);
    p[2] = (uint8_t)n;
    p += 3;
    for (int i = 0; i < n; i++) {
        const RunResult *r = &h->runs[i];
        put_u32(p, r->seed);
        const int nums[6] = { r->depth, r->kills, r->gold, r->earned,
                              r->rooms_cleared, r->seconds };
        for (int k = 0; k < 6; k++)
            put_u16(p + 4 + 2 * k, (uint16_t)iclamp(nums[k], 0, 0xFFFF));
        p[16] = r->cause;
        p[17] = (uint8_t)iclamp(r->bosses, 0, 255);
        p[18] = (uint8_t)((r->new_best ? HISTORY_NEW_BEST : 0)
                          | (r->abandoned ? HISTORY_ABANDONED : 0)
                          | (r->cheated ? HISTORY_CHEATED : 0));
        for (int k = 0; k < RUN_BOON_SLOTS / 2; k++) {
            int a = imin(r->boons[2 * k], 15);
            int b = imin(r->boons[2 * k + 1], 15);
            p[19 + k] = (uint8_t)(a | (b << 4));
        }
        p[27] = (uint8_t)iclamp(r->start_depth, 0, 255);
        p += HISTORY_ENTRY_SIZE;
    }
    return p;
}

static void get_history(const uint8_t *d, int len, RunHistory *h)
{
    memset(h, 0, sizeof(*h));
    if (len < 1)
        return;
    int n = d[0];
    int size = len == 1 + n * HISTORY_ENTRY_SIZE ? HISTORY_ENTRY_SIZE
             : len == 1 + n * HISTORY_ENTRY_V1 ? HISTORY_ENTRY_V1 : 0;
    if (n > RUN_HISTORY || 0 == size)
        return;
    for (int i = 0; i < n; i++) {
        const uint8_t *e = d + 1 + i * size;
        RunResult *r = &h->runs[i];
        r->seed = get_u32(e);
        r->depth = get_u16(e + 4);
        r->kills = get_u16(e + 6);
        r->gold = get_u16(e + 8);
        r->earned = get_u16(e + 10);
        r->rooms_cleared = get_u16(e + 12);
        r->seconds = get_u16(e + 14);
        r->cause = e[16];
        r->bosses = e[17];
        r->new_best = 0 != (e[18] & HISTORY_NEW_BEST);
        r->abandoned = 0 != (e[18] & HISTORY_ABANDONED);
        r->cheated = 0 != (e[18] & HISTORY_CHEATED);
        for (int k = 0; k < RUN_BOON_SLOTS / 2; k++) {
            r->boons[2 * k] = e[19 + k] & 15;
            r->boons[2 * k + 1] = e[19 + k] >> 4;
        }
        r->start_depth = size > HISTORY_ENTRY_V1 ? e[27] : 1;
    }
    h->count = (uint8_t)n;
}

size_t save_encode(const GameData *g, uint8_t buf[SAVE_MAX_SIZE])
{
    memset(buf, 0, SAVE_MAX_SIZE);
    uint8_t *start = buf + SAVE_HEADER_SIZE;
    uint8_t *p = start;
    p = put_tag_u32(p, TAG_TOTAL_RUNS, g->meta.total_runs);
    p = put_tag_u32(p, TAG_BEST_DEPTH, g->meta.best_depth);
    p = put_tag_u32(p, TAG_CURRENCY, g->meta.currency);
    p = put_tag_u32(p, TAG_UPGRADES, g->meta.upgrades);
    p = put_tag_u32(p, TAG_TOTAL_KILLS, g->meta.total_kills);
    p = put_tag_u32(p, TAG_BOSSES_SLAIN, g->meta.bosses_slain);
    p = put_tag_u32(p, TAG_HINTS_SEEN, g->meta.hints_seen);
    p = put_tag_u32(p, TAG_WAYPOINTS, g->meta.waypoints);
    p = put_tag_u32(p, TAG_PLAY_SECONDS, g->meta.play_seconds);
    p = put_tag_u32(p, TAG_LAST_PLAYED, g->meta.last_played);
    p = put_tag_u32(p, TAG_CHEATS_USED, g->meta.cheats_used);
    p = put_tag_u8(p, TAG_MUSIC_VOLUME, g->options.music_volume);
    p = put_tag_u8(p, TAG_SFX_VOLUME, g->options.sfx_volume);
    p = put_tag_u8(p, TAG_DEFAULT_ZOOM, g->options.default_zoom);
    p = put_tag_u8(p, TAG_AUTO_ATTACK, g->options.auto_attack);
    p = put_tag_u8(p, TAG_LEFT_HANDED, g->options.left_handed);
    p = put_tag_u8(p, TAG_SCREEN_SHAKE, g->options.screen_shake);
    p = put_tag_u8(p, TAG_HIT_NUMBERS, g->options.hit_numbers);
    p = put_tag_u8(p, TAG_HINTS, g->options.hints);
    p = put_rogue_board(p, &g->rogue_board);
    p = put_history(p, &g->history);
    size_t payload = (size_t)(p - start);

    memcpy(buf, "DLVS", 4);
    put_u16(buf + 4, SAVE_VERSION);
    put_u16(buf + 6, (uint16_t)payload);
    put_u32(buf + 8, fnv1a(start, payload));
    return SAVE_HEADER_SIZE + payload;
}

// Out-of-range values from a damaged or hand-edited file are clamped.
static void sanitize_options(Options *o)
{
    if (o->music_volume > VOLUME_MAX)
        o->music_volume = VOLUME_MAX;
    if (o->sfx_volume > VOLUME_MAX)
        o->sfx_volume = VOLUME_MAX;
    if (ZOOM_FAR != o->default_zoom)
        o->default_zoom = ZOOM_CLOSE;
    o->auto_attack = 0 != o->auto_attack;
    o->left_handed = 0 != o->left_handed;
    o->screen_shake = 0 != o->screen_shake;
    o->hit_numbers = 0 != o->hit_numbers;
    o->hints = 0 != o->hints;
}

static void decode_v1(const uint8_t *p, MetaProgress *m, Options *o)
{
    m->total_runs = get_u32(p + 0);
    m->best_depth = get_u32(p + 4);
    m->currency = get_u32(p + 8);
    m->upgrades = get_u32(p + 12);
    m->total_kills = get_u32(p + 16);
    o->music_volume = p[20];
    o->sfx_volume = p[21];
    o->default_zoom = p[22];
}

// Returns false on a field that runs past the payload.
static bool decode_tags(const uint8_t *p, size_t n, MetaProgress *m,
                        Options *o, RogueBoard *rb, RunHistory *hist)
{
    size_t i = 0;
    while (i < n) {
        if (i + 2 > n || i + 2 + p[i + 1] > n)
            return false;
        int tag = p[i], len = p[i + 1];
        const uint8_t *d = p + i + 2;
        if (TAG_ROGUE_BOARD == tag) {
            get_rogue_board(d, len, rb);
        } else if (TAG_RUN_HISTORY == tag) {
            get_history(d, len, hist);
        } else if (4 == len) {
            switch (tag) {
            case TAG_TOTAL_RUNS:  m->total_runs = get_u32(d); break;
            case TAG_BEST_DEPTH:  m->best_depth = get_u32(d); break;
            case TAG_CURRENCY:    m->currency = get_u32(d); break;
            case TAG_UPGRADES:    m->upgrades = get_u32(d); break;
            case TAG_TOTAL_KILLS: m->total_kills = get_u32(d); break;
            case TAG_BOSSES_SLAIN: m->bosses_slain = get_u32(d); break;
            case TAG_HINTS_SEEN:  m->hints_seen = get_u32(d); break;
            case TAG_WAYPOINTS:   m->waypoints = get_u32(d); break;
            case TAG_PLAY_SECONDS: m->play_seconds = get_u32(d); break;
            case TAG_LAST_PLAYED: m->last_played = get_u32(d); break;
            case TAG_CHEATS_USED: m->cheats_used = get_u32(d); break;
            default: break;
            }
        } else if (1 == len) {
            switch (tag) {
            case TAG_MUSIC_VOLUME: o->music_volume = d[0]; break;
            case TAG_SFX_VOLUME:   o->sfx_volume = d[0]; break;
            case TAG_DEFAULT_ZOOM: o->default_zoom = d[0]; break;
            case TAG_AUTO_ATTACK:  o->auto_attack = d[0]; break;
            case TAG_LEFT_HANDED:  o->left_handed = d[0]; break;
            case TAG_SCREEN_SHAKE: o->screen_shake = d[0]; break;
            case TAG_HIT_NUMBERS:  o->hit_numbers = d[0]; break;
            case TAG_HINTS:        o->hints = d[0]; break;
            default: break;
            }
        }
        i += 2 + (size_t)len;
    }
    return true;
}

bool save_decode(const uint8_t *buf, size_t len, GameData *g)
{
    if (len < SAVE_HEADER_SIZE || 0 != memcmp(buf, "DLVS", 4))
        return false;
    int version = get_u16(buf + 4);
    size_t payload = get_u16(buf + 6);
    if (version < 1 || SAVE_HEADER_SIZE + payload > len)
        return false;
    const uint8_t *p = buf + SAVE_HEADER_SIZE;
    if (get_u32(buf + 8) != fnv1a(p, payload))
        return false;

    GameData d;
    game_set_defaults(&d);
    if (1 == version) {
        if (V1_PAYLOAD_SIZE != payload)
            return false;
        decode_v1(p, &d.meta, &d.options);
    } else if (!decode_tags(p, payload, &d.meta, &d.options, &d.rogue_board,
                            &d.history)) {
        // Versions above SAVE_VERSION are read too: they are tagged, and
        // unknown tags are skipped (their data is lost on the next write).
        return false;
    }
    sanitize_options(&d.options);
    g->meta = d.meta;
    g->options = d.options;
    g->rogue_board = d.rogue_board;
    g->history = d.history;
    return true;
}

void save_set_path(const char *p)
{
    snprintf(path, sizeof(path), "%s", p);
    selected = -1;
}

void save_set_stem(const char *s)
{
    snprintf(stem, sizeof(stem), "%s", s);
    selected = -1;
}

static void slot_file(int slot, char *out, size_t size)
{
    snprintf(out, size, "%s-%d.sav", stem, slot + 1);
}

// A slot's file and the temporary copy save_write() goes through.
typedef struct {
    char file[sizeof(path)];
    char tmp[sizeof(path) + 4];
} SlotPaths;

static void slot_paths(int slot, SlotPaths *sp)
{
    slot_file(slot, sp->file, sizeof(sp->file));
    snprintf(sp->tmp, sizeof(sp->tmp), "%s.tmp", sp->file);
}

// Defaults, but storage_ok describes the card, not the save: it stays.
static void reset_to_defaults(GameData *g)
{
    bool storage = g->storage_ok;
    game_set_defaults(g);
    g->storage_ok = storage;
}

void save_select_slot(int slot)
{
    slot_file(slot, path, sizeof(path));
    selected = slot;
}

int save_selected_slot(void)
{
    return selected;
}

const char *save_path(void)
{
    return path;
}

bool save_stem_for_rom(const char *rom, char *out, size_t size)
{
    size_t n = rom ? strlen(rom) : 0;
    if (n <= 4 || n >= size)
        return false;
    const char *ext = rom + n - 4;   // ".nds", any case
    if ('.' != ext[0] || 'n' != (ext[1] | 0x20) || 'd' != (ext[2] | 0x20)
        || 's' != (ext[3] | 0x20))
        return false;
    n -= 4;
    // A release ROM's "-v<version>" is dropped, so every version finds the
    // same saves: "delve-v2.5.0" -> "delve".
    for (size_t i = n - 1; i >= 2; i--) {
        if ('-' == rom[i - 2] && 'v' == (rom[i - 1] | 0x20)
            && rom[i] >= '0' && rom[i] <= '9') {
            n = i - 2;
            break;
        }
    }
    if (0 == n || '/' == rom[n - 1] || ':' == rom[n - 1])
        return false;
    memcpy(out, rom, n);
    out[n] = '\0';
    return true;
}

bool save_storage_init(int argc, char **argv)
{
#ifdef __NDS__
    // libfat-compatible init in BlocksDS (libnds fat.h).
    if (!fatInitDefault())
        return false;

    // Homebrew loaders that support argv pass the ROM path, e.g.
    // "fat:/games/delve.nds": put the saves beside it.
    bool beside_rom = argc > 0 && argv
                   && save_stem_for_rom(argv[0], stem, sizeof(stem));
    if (!beside_rom) {
        struct stat st;
        if (0 == stat("/saves", &st) && S_ISDIR(st.st_mode))
            save_set_stem("/saves/delve");
        else
            save_set_stem("/delve");
    }
    save_adopt_legacy();
    return true;
#else
    (void)argc;
    (void)argv;
    return true;
#endif
}

static char tmp_path[sizeof(path) + 4];

static const char *temp_path(void)
{
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    return tmp_path;
}

static bool load_file(const char *file, GameData *g)
{
    static uint8_t buf[SAVE_MAX_SIZE];
    FILE *f = fopen(file, "rb");
    if (!f)
        return false;
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return save_decode(buf, n, g);
}

bool save_peek(int slot, GameData *g)
{
    SlotPaths sp;
    slot_paths(slot, &sp);
    reset_to_defaults(g);
    return load_file(sp.file, g) || load_file(sp.tmp, g);
}

bool save_erase(int slot)
{
    SlotPaths sp;
    slot_paths(slot, &sp);
    remove(sp.file);
    remove(sp.tmp);
    FILE *f = fopen(sp.file, "rb");
    if (f)
        fclose(f);
    return NULL == f;
}

// One slot's record at a time (a GameData is about 1 KB: kept off the
// stack).
static GameData peeked;

int save_latest_slot(void)
{
    int best = -1;
    uint32_t best_played = 0;
    for (int i = 0; i < SAVE_SLOTS; i++) {
        if (save_peek(i, &peeked)
            && (best < 0 || peeked.meta.last_played > best_played)) {
            best = i;
            best_played = peeked.meta.last_played;
        }
    }
    return best;
}

void save_mark_played(int slot, GameData *g)
{
    uint32_t top = 0;
    for (int i = 0; i < SAVE_SLOTS; i++) {
        if (i != slot && save_peek(i, &peeked) && peeked.meta.last_played > top)
            top = peeked.meta.last_played;
    }
    if (g->meta.last_played <= top)
        g->meta.last_played = top + 1;
    save_select_slot(slot);
}

bool save_adopt_legacy(void)
{
    for (int i = 0; i < SAVE_SLOTS; i++) {
        if (save_peek(i, &peeked))
            return false;
    }
    char legacy[sizeof(path)], first[sizeof(path)];
    snprintf(legacy, sizeof(legacy), "%s.sav", stem);
    if (!load_file(legacy, &peeked))
        return false;   // none, or not a Delve save: leave it alone
    slot_file(0, first, sizeof(first));
    return 0 == rename(legacy, first);
}

bool save_load(GameData *g)
{
    if (load_file(path, g) || load_file(temp_path(), g))
        return true;
    reset_to_defaults(g);
    return false;
}

bool save_write(const GameData *g)
{
    if (!g->storage_ok)
        return false;
    static uint8_t buf[SAVE_MAX_SIZE];
    size_t n = save_encode(g, buf);
    const char *tmp = temp_path();
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return false;
    bool ok = fwrite(buf, 1, n, f) == n;
    ok = (0 == fclose(f)) && ok;
    if (!ok)
        return false;
    // rename() does not replace an existing file on FAT.
    remove(path);
    return 0 == rename(tmp, path);
}
