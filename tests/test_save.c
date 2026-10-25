// Save encoding, decoding, migration, slots and paths.

#include <stdio.h>
#include <string.h>

#include "test.h"
#include "boon.h"
#include "meta.h"
#include "run.h"
#include "save.h"

#define TMP_SAVE "tests/bin/test_delve.sav"

static void sample(GameData *g)
{
    game_set_defaults(g);
    g->meta.total_runs = 12;
    g->meta.best_depth = 34;
    g->meta.currency = 0xA1B2C3u;
    g->meta.upgrades = 5;
    g->meta.total_kills = 999;
    g->options.music_volume = 3;
    g->options.sfx_volume = 10;
    g->options.default_zoom = ZOOM_FAR;
}

static void sample_board(GameData *g)
{
    RogueBoard *b = &g->rogue_board;
    b->count = 2;
    b->entries[0] = (RogueScore){ 4321, 9, 19, "Mike" };
    b->entries[1] = (RogueScore){ 77, 2, DEATH_STARVED, "A long nam" };
}

static bool same(const GameData *a, const GameData *b)
{
    if (a->rogue_board.count != b->rogue_board.count)
        return false;
    for (int i = 0; i < a->rogue_board.count; i++) {
        const RogueScore *x = &a->rogue_board.entries[i];
        const RogueScore *y = &b->rogue_board.entries[i];
        if (x->score != y->score || x->level != y->level
            || x->cause != y->cause || 0 != strcmp(x->name, y->name))
            return false;
    }
    if (a->history.count != b->history.count)
        return false;
    for (int i = 0; i < a->history.count; i++) {
        const RunResult *x = &a->history.runs[i];
        const RunResult *y = &b->history.runs[i];
        if (x->depth != y->depth || x->kills != y->kills || x->gold != y->gold
            || x->earned != y->earned || x->rooms_cleared != y->rooms_cleared
            || x->seconds != y->seconds || x->bosses != y->bosses
            || x->cause != y->cause || x->new_best != y->new_best
            || x->abandoned != y->abandoned || x->seed != y->seed
            || x->cheated != y->cheated
            || 0 != memcmp(x->boons, y->boons, sizeof(x->boons)))
            return false;
    }
    return 0 == memcmp(&a->meta, &b->meta, sizeof(a->meta))
        && 0 == memcmp(&a->options, &b->options, sizeof(a->options));
}

// Builds a record by hand: header + payload, checksum filled in.
static size_t make_record(uint8_t *buf, int version, const uint8_t *payload,
                          size_t n)
{
    memcpy(buf, "DLVS", 4);
    buf[4] = (uint8_t)version;
    buf[5] = 0;
    buf[6] = (uint8_t)n;
    buf[7] = (uint8_t)(n >> 8);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= payload[i];
        h *= 16777619u;
    }
    for (int i = 0; i < 4; i++)
        buf[8 + i] = (uint8_t)(h >> (8 * i));
    memcpy(buf + SAVE_HEADER_SIZE, payload, n);
    return SAVE_HEADER_SIZE + n;
}

static void migration_tests(void)
{
    // A version 1 file, as written by 1.0.0-rc1.
    static const uint8_t v1[24] = {
        12, 0, 0, 0,   34, 0, 0, 0,   0xC3, 0xB2, 0xA1, 0,   5, 0, 0, 0,
        0xE7, 3, 0, 0,   3, 10, ZOOM_FAR, 0,
    };
    uint8_t buf[SAVE_MAX_SIZE];
    size_t n = make_record(buf, 1, v1, sizeof(v1));
    GameData g, want;
    sample(&want);
    game_set_defaults(&g);
    CHECK(save_decode(buf, n, &g));
    CHECK(same(&g, &want));
    buf[SAVE_HEADER_SIZE + 3] ^= 1;
    CHECK(!save_decode(buf, n, &g)); // checksum still applies
    // Version 1 with the wrong payload size is rejected.
    n = make_record(buf, 1, v1, 20);
    CHECK(!save_decode(buf, n, &g));

    // Unknown tags (from a newer build) are skipped; missing tags default;
    // out-of-range values are clamped.
    static const uint8_t tagged[] = {
        200, 3, 1, 2, 3,          // unknown tag with 3 bytes
        3, 4, 77, 0, 0, 0,        // currency = 77
        16, 1, 250,               // music volume, clamped
        19, 1, 7,                 // auto-attack: any non-zero is on
        4, 2, 9, 9,               // known tag, wrong length: ignored
    };
    n = make_record(buf, 3, tagged, sizeof(tagged));
    sample(&g);
    CHECK(save_decode(buf, n, &g));
    CHECK_EQ(g.meta.currency, 77);
    CHECK_EQ(g.meta.total_runs, 0);
    CHECK_EQ(g.meta.upgrades, 0);
    CHECK_EQ(g.options.music_volume, VOLUME_MAX);
    CHECK_EQ(g.options.auto_attack, 1);
    CHECK_EQ(g.options.screen_shake, 1);   // missing: the default

    // A field running past the end of the payload is corruption.
    static const uint8_t overrun[] = { 3, 4, 1, 2 };
    n = make_record(buf, 2, overrun, sizeof(overrun));
    sample(&g);
    CHECK(!save_decode(buf, n, &g));
    CHECK_EQ(g.meta.total_runs, 12); // untouched
    static const uint8_t lone_tag[] = { 3 };
    n = make_record(buf, 2, lone_tag, sizeof(lone_tag));
    CHECK(!save_decode(buf, n, &g));
    n = make_record(buf, 0, v1, sizeof(v1));
    CHECK(!save_decode(buf, n, &g)); // version 0 never existed
}

static void save_tests(void)
{
    GameData g, h;
    uint8_t buf[SAVE_MAX_SIZE];

    sample(&g);
    sample_board(&g);
    // The newer options: every one away from its default.
    g.options.auto_attack = 1;
    g.options.left_handed = 1;
    g.options.screen_shake = 0;
    g.options.hit_numbers = 0;
    g.options.hints = 0;
    g.meta.hints_seen = 0x5A;
    size_t size = save_encode(&g, buf);
    CHECK(size > SAVE_HEADER_SIZE && size <= SAVE_MAX_SIZE);
    game_set_defaults(&h);
    CHECK(save_decode(buf, size, &h));
    CHECK(same(&g, &h));

    // Any corrupted payload byte is caught by the checksum.
    for (size_t i = SAVE_HEADER_SIZE; i < size; i++) {
        uint8_t bad[SAVE_MAX_SIZE];
        memcpy(bad, buf, sizeof(bad));
        bad[i] ^= 0x10;
        CHECK(!save_decode(bad, size, &h));
    }
    uint8_t bad[SAVE_MAX_SIZE];
    memcpy(bad, buf, sizeof(bad));
    bad[0] = 'X';
    CHECK(!save_decode(bad, size, &h));               // magic
    CHECK(!save_decode(buf, size - 1, &h));           // truncated
    CHECK(!save_decode(buf, 4, &h));                  // header only
    CHECK(same(&g, &h)); // failed decodes leave the data untouched

    // File round trip; no temporary file is left behind.
    save_set_path(TMP_SAVE);
    g.storage_ok = true;
    CHECK(save_write(&g));
    CHECK(save_write(&g)); // replacing an existing file
    game_set_defaults(&h);
    h.storage_ok = true;
    CHECK(save_load(&h));
    CHECK(same(&g, &h));
    FILE *f = fopen(TMP_SAVE ".tmp", "rb");
    CHECK(NULL == f);
    if (f)
        fclose(f);

    // Power cut between removing the old file and renaming the new one:
    // the .tmp copy is used.
    rename(TMP_SAVE, TMP_SAVE ".tmp");
    game_set_defaults(&h);
    CHECK(save_load(&h));
    CHECK(same(&g, &h));
    remove(TMP_SAVE ".tmp");

    // Missing or garbage file: defaults, storage flag kept, no crash.
    remove(TMP_SAVE);
    sample(&h);
    h.storage_ok = true;
    CHECK(!save_load(&h));
    CHECK_EQ(h.meta.total_runs, 0);
    CHECK(h.storage_ok);
    f = fopen(TMP_SAVE, "wb");
    fputs("not a save file at all, definitely not", f);
    fclose(f);
    CHECK(!save_load(&h));
    CHECK_EQ(h.meta.best_depth, 0);
    remove(TMP_SAVE);

    // No storage: writes are refused.
    g.storage_ok = false;
    CHECK(!save_write(&g));

    migration_tests();
}

static void meta_tests(void)
{
    CHECK_EQ(meta_award(5, 10, 40),
             40 + 5 * META_GOLD_PER_DEPTH + 10 * META_GOLD_PER_KILL);
    CHECK_EQ(meta_award(-1, -1, -1), 0);

    MetaProgress m = { 0 };
    RunResult r = { .depth = 7, .kills = 12, .gold = 30 };
    meta_finish_run(&m, &r);
    CHECK(r.new_best);
    CHECK_EQ(m.best_depth, 7);
    CHECK_EQ(m.total_runs, 1);
    CHECK_EQ(m.currency, (uint32_t)r.earned);
    RunResult r2 = { .depth = 3 };
    meta_finish_run(&m, &r2);
    CHECK(!r2.new_best);
    CHECK_EQ(m.best_depth, 7);
    CHECK_EQ(m.total_runs, 2);

    m.currency = UINT32_MAX - 1;
    RunResult r3 = { .depth = 1, .gold = 100 };
    meta_finish_run(&m, &r3);
    CHECK_EQ(m.currency, UINT32_MAX); // saturates

    MetaProgress shop = { .currency = 60 };
    CHECK_EQ(meta_buy(&shop, UPG_SWIFT_DASH), BUY_TOO_POOR);
    CHECK_EQ(meta_buy(&shop, UPG_VITALITY), BUY_OK);
    CHECK_EQ(shop.currency, 10);
    CHECK(meta_owned(&shop, UPG_VITALITY));
    CHECK_EQ(meta_buy(&shop, UPG_VITALITY), BUY_OWNED);

    // Upgrade effects on a new run.
    static Run run;
    run_start(&run, 1);
    run_apply_upgrades(&run, (1u << UPG_VITALITY) | (1u << UPG_SWIFT_DASH)
                                 | (1u << UPG_HEAVY_BLADE));
    CHECK_EQ(run.player.max_hp, PLAYER_START_HP + 1);
    CHECK_EQ(run.player.hp, PLAYER_START_HP + 1);
    CHECK(run.player.dash_recharge_time < PLAYER_DASH_RECHARGE);
    CHECK_EQ(run.player.attack_damage, 2);
    CHECK(!run.second_wind);

    // Upgrade tiers need their first step.
    MetaProgress tier = { .currency = 5000 };
    CHECK_EQ(meta_buy(&tier, UPG_KEEN_EDGE), BUY_LOCKED);
    CHECK_EQ(meta_buy(&tier, UPG_VITALITY2), BUY_LOCKED);
    CHECK_EQ(tier.currency, 5000);
    CHECK_EQ(meta_buy(&tier, UPG_HEAVY_BLADE), BUY_OK);
    CHECK_EQ(meta_buy(&tier, UPG_KEEN_EDGE), BUY_OK);
    for (int i = 0; i < UPG_COUNT; i++)
        CHECK(strlen(upgrade_defs[i].desc) <= 22);

    run_start(&run, 1);
    run_apply_upgrades(&run, (1u << UPG_COUNT) - 1);
    CHECK_EQ(run.player.max_hp, PLAYER_START_HP + 2);
    CHECK_EQ(run.player.attack_damage, 3);
    CHECK_EQ(run.player.dagger_max, DAGGER_CHARGES + 1);
    CHECK_EQ(run.player.daggers, DAGGER_CHARGES + 1);
    CHECK_EQ(run.player.meter, run.player.meter_max);
    CHECK_EQ(boon_gold(&run, 100), 100 + DEEP_POCKETS_PCT);

    // Second Wind: the first killing blow leaves two hearts, the next kills.
    const PlayerInput idle = { .move = DIR_NONE };
    CHECK(run.second_wind);
    run.player.hp = 0;
    run_update(&run, &idle);
    CHECK(run.events2 & EV2_SECOND_WIND);
    CHECK_EQ(run.status, ROOM_FIGHT);
    CHECK_EQ(run.player.hp, SECOND_WIND_HP);
    CHECK(!run.second_wind);
    run.player.hp = 0;
    run_update(&run, &idle);
    CHECK_EQ(run.status, ROOM_DEAD);
}

// The run history and what ended each run.
static void history_tests(void)
{
    GameData g, h;
    uint8_t buf[SAVE_MAX_SIZE];
    sample(&g);
    sample_board(&g);
    g.rogue_board.count = RG_SCORES;   // every table full: the worst case
    for (int i = 0; i < RG_SCORES; i++)
        g.rogue_board.entries[i] = (RogueScore){ 9, 1, 2, "ABCDEFGHIJ" };
    g.meta.bosses_slain = 3;
    g.meta.cheats_used = 0x1005;   // Code Genie's mark
    for (int i = 0; i < RUN_HISTORY + 3; i++) {
        RunResult r = {
            .depth = i + 1, .kills = 1000 * i, .gold = 70000, .earned = i,
            .rooms_cleared = i, .bosses = i & 1, .seconds = 61 * i,
            .cause = (uint8_t)(2 == i ? CAUSE_ABANDONED : ENEMY_ARCHER),
            .new_best = 4 == i, .abandoned = 2 == i, .seed = 0xC0FFEE00u + i,
            .cheated = 5 == i,
        };
        r.boons[BOON_THICK_SKIN] = 3;
        r.boons[BOON_GOLD_RUSH] = (uint8_t)(i % 3);
        meta_push_history(&g.history, &r);
    }
    CHECK_EQ(g.history.count, RUN_HISTORY);
    CHECK_EQ(g.history.runs[0].depth, RUN_HISTORY + 3);   // newest first
    CHECK_EQ(g.history.runs[RUN_HISTORY - 1].depth, 4);
    size_t size = save_encode(&g, buf);
    CHECK(size <= 512);   // a small file, well under the buffer
    // Gold above 16 bits is clamped in the file.
    for (int i = 0; i < RUN_HISTORY; i++)
        g.history.runs[i].gold = 0xFFFF;
    game_set_defaults(&h);
    CHECK(save_decode(buf, size, &h));
    CHECK(same(&g, &h));
    CHECK_EQ(h.meta.bosses_slain, 3);
    CHECK_EQ(h.meta.cheats_used, 0x1005);
    CHECK(h.history.runs[RUN_HISTORY + 3 - 1 - 5].cheated);

    // A history entry of the wrong size is ignored, the rest still loads.
    static const uint8_t bad[] = { 33, 2, 1, 0, 3, 4, 5, 0, 0, 0 };
    {
        uint8_t rec[64];
        size_t n = make_record(rec, 2, bad, sizeof(bad));
        game_set_defaults(&h);
        CHECK(save_decode(rec, n, &h));
        CHECK_EQ(h.history.count, 0);
        CHECK_EQ(h.meta.currency, 5);
    }

    // Cause texts exist for every enemy and hazard.
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
        CHECK(strlen(meta_cause_text((uint8_t)t)) <= 28
              && strlen(meta_cause_short((uint8_t)t)) <= 10);
    CHECK(0 == strcmp(meta_cause_short(CAUSE_SAW), "Saw blade"));

    // The killing blow is recorded: an archer's arrow.
    static Run run;
    run_start(&run, 5);
    run_reset_room(&run);
    run.player.hp = 1;
    run.player.iframes = 0;
    Projectile *s = run_spawn_projectile(&run, PROJ_ARROW, run.player.x - FX(0.2),
                                         run.player.z, FX(0.1), 0, 1);
    CHECK(NULL != s);
    if (s)
        s->owner = ENEMY_ARCHER;
    PlayerInput idle = { .move = DIR_NONE };
    for (int i = 0; i < 5 && ROOM_DEAD != run.status; i++)
        run_update(&run, &idle);
    CHECK_EQ(run.status, ROOM_DEAD);
    CHECK_EQ(run.hurt_by, ENEMY_ARCHER);
    CHECK(run.time > 0);
}

static bool exists(const char *file)
{
    FILE *f = fopen(file, "rb");
    if (f)
        fclose(f);
    return NULL != f;
}

// Three save slots, the slot played last, erasing, and a 2.4.0 save
// becoming slot 1.
// Release ROMs carry their version; their saves do not.
static void stem_tests(void)
{
    char s[64];
    CHECK(save_stem_for_rom("fat:/games/delve.nds", s, sizeof(s)));
    CHECK(0 == strcmp(s, "fat:/games/delve"));
    CHECK(save_stem_for_rom("fat:/roms/nds/delve-v2.5.0.nds", s, sizeof(s)));
    CHECK(0 == strcmp(s, "fat:/roms/nds/delve"));
    CHECK(save_stem_for_rom("fat:/DELVE-V2.5.0-rc1.NDS", s, sizeof(s)));
    CHECK(0 == strcmp(s, "fat:/DELVE"));
    CHECK(save_stem_for_rom("fat:/delve-debug.nds", s, sizeof(s)));
    CHECK(0 == strcmp(s, "fat:/delve-debug"));   // debug keeps its own
    CHECK(save_stem_for_rom("/nds/my-vault.nds", s, sizeof(s)));
    CHECK(0 == strcmp(s, "/nds/my-vault"));      // "-v" without a digit
    CHECK(!save_stem_for_rom("fat:/delve.sav", s, sizeof(s)));
    CHECK(!save_stem_for_rom(".nds", s, sizeof(s)));
    CHECK(!save_stem_for_rom("fat:/-v2.nds", s, sizeof(s)));
    CHECK(!save_stem_for_rom("fat:/games/delve.nds", s, 8));   // too long
    CHECK(!save_stem_for_rom(NULL, s, sizeof(s)));
}

static void slot_tests(void)
{
    static GameData g, h;
    const char *stem = "tests/bin/test_slots";
    const char *files[] = { "tests/bin/test_slots-1.sav",
                            "tests/bin/test_slots-2.sav",
                            "tests/bin/test_slots-3.sav",
                            "tests/bin/test_slots.sav" };
    for (int i = 0; i < 4; i++)
        remove(files[i]);
    save_set_stem(stem);
    CHECK_EQ(save_selected_slot(), -1);
    CHECK_EQ(save_latest_slot(), -1);
    CHECK(!save_peek(1, &h));
    CHECK_EQ(h.meta.total_runs, 0);

    // Each slot is its own file.
    sample(&g);
    g.meta.play_seconds = 7384;
    g.storage_ok = true;
    save_mark_played(1, &g);
    CHECK_EQ(save_selected_slot(), 1);
    CHECK(0 == strcmp(save_path(), files[1]));
    CHECK(save_write(&g));
    CHECK(exists(files[1]) && !exists(files[0]) && !exists(files[2]));
    CHECK(save_peek(1, &h));
    CHECK(same(&g, &h));
    CHECK_EQ(save_latest_slot(), 1);

    // Another slot played after it is the latest; its counter goes higher.
    GameData other;
    game_set_defaults(&other);
    other.storage_ok = true;
    other.meta.best_depth = 3;
    save_mark_played(2, &other);
    CHECK(other.meta.last_played > g.meta.last_played);
    CHECK(save_write(&other));
    CHECK_EQ(save_latest_slot(), 2);
    // Back to slot 2 (index 1): latest again.
    save_mark_played(1, &g);
    CHECK(save_write(&g));
    CHECK_EQ(save_latest_slot(), 1);
    CHECK(save_peek(2, &h));
    CHECK_EQ(h.meta.best_depth, 3);

    // Erasing one slot leaves the others.
    CHECK(save_erase(1));
    CHECK(!exists(files[1]));
    CHECK(!save_peek(1, &h));
    CHECK_EQ(save_latest_slot(), 2);
    CHECK(save_erase(0));   // already empty: fine

    // A pre-slot save becomes slot 1, only while every slot is empty.
    save_erase(2);
    save_set_path(files[3]);
    CHECK(save_write(&g));
    save_set_stem(stem);
    CHECK(save_adopt_legacy());
    CHECK(!exists(files[3]));
    CHECK(save_peek(0, &h));
    CHECK(same(&g, &h));
    save_set_path(files[3]);
    CHECK(save_write(&g));
    save_set_stem(stem);
    CHECK(!save_adopt_legacy());   // slot 1 is taken now
    CHECK(exists(files[3]));
    // Something else under that name is never touched.
    save_erase(0);
    FILE *f = fopen(files[3], "wb");
    fputs("someone else's save", f);
    fclose(f);
    CHECK(!save_adopt_legacy());
    CHECK(exists(files[3]) && !exists(files[0]));

    // Play time adds up over runs.
    MetaProgress m = { .play_seconds = 100 };
    RunResult r = { .depth = 2, .seconds = 61 };
    meta_finish_run(&m, &r);
    CHECK_EQ(m.play_seconds, 161);
    m.play_seconds = UINT32_MAX - 5;
    meta_finish_run(&m, &r);
    CHECK_EQ(m.play_seconds, UINT32_MAX);   // saturates

    for (int i = 0; i < 4; i++)
        remove(files[i]);
    save_set_path(TMP_SAVE);
}

void run_save_tests(void)
{
    save_tests();
    stem_tests();
    slot_tests();
    meta_tests();
    history_tests();
}
