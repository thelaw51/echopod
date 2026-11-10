#include "check.h"
#include "fixture_library.h"
#include "library/play_log.h"

static FixtureLibrary fixture;
static uint8_t log_bytes[PLAY_LOG_BYTES];

static uint8_t *fresh_log(void)
{
    play_log_clear(log_bytes, 0x1234u);
    return log_bytes;
}

static void play(uint8_t *log, uint32_t key, int times)
{
    for (int index = 0; index < times; index++) {
        play_log_add(log, key, (uint16_t)key);
    }
}

static uint16_t listed(const uint8_t *log, PlayLogOrder order, uint16_t *tracks)
{
    return play_log_list(log, order, tracks, PLAY_LOG_LIST_TRACKS);
}

static void a_sealed_log_is_valid_until_a_byte_changes(void)
{
    uint8_t *log = fresh_log();
    CHECK(!play_log_valid(log));
    play_log_seal(log, 7);
    CHECK(play_log_valid(log));
    CHECK_EQUAL(7u, play_log_generation(log));
    CHECK_EQUAL(0x1234u, play_log_fingerprint(log));
    CHECK_EQUAL(0, play_log_count(log));
    log[PLAY_LOG_HEADER_BYTES] ^= 1;
    CHECK(!play_log_valid(log));
    log[PLAY_LOG_HEADER_BYTES] ^= 1;
    log[0] = 'X';
    CHECK(!play_log_valid(log));
}

static void plays_of_the_same_track_add_up(void)
{
    uint8_t *log = fresh_log();
    play(log, 10, 3);
    play(log, 11, 1);
    CHECK_EQUAL(2, play_log_count(log));
    CHECK_EQUAL(3, play_log_entry(log, 0).plays);
    CHECK_EQUAL(1, play_log_entry(log, 1).plays);
    CHECK(play_log_entry(log, 1).last > play_log_entry(log, 0).last);
}

static void recently_played_is_newest_first(void)
{
    uint8_t *log = fresh_log();
    play(log, 10, 1);
    play(log, 11, 1);
    play(log, 12, 1);
    play(log, 10, 1);
    uint16_t tracks[PLAY_LOG_LIST_TRACKS];
    CHECK_EQUAL(3, listed(log, PLAY_LOG_RECENT, tracks));
    CHECK_EQUAL(10, tracks[0]);
    CHECK_EQUAL(12, tracks[1]);
    CHECK_EQUAL(11, tracks[2]);
}

static void most_played_ranks_by_plays_then_by_recency(void)
{
    uint8_t *log = fresh_log();
    play(log, 10, 2);
    play(log, 11, 5);
    play(log, 12, 2);
    play(log, 13, 1);
    uint16_t tracks[PLAY_LOG_LIST_TRACKS];
    CHECK_EQUAL(4, listed(log, PLAY_LOG_MOST, tracks));
    CHECK_EQUAL(11, tracks[0]);
    CHECK_EQUAL(12, tracks[1]);
    CHECK_EQUAL(10, tracks[2]);
    CHECK_EQUAL(13, tracks[3]);
    CHECK_EQUAL(2, play_log_list(log, PLAY_LOG_MOST, tracks, 2));
}

static void lists_skip_tracks_the_library_no_longer_has(void)
{
    uint8_t *log = fresh_log();
    play(log, 10, 1);
    play_log_add(log, 11, PLAY_LOG_NO_TRACK);
    uint16_t tracks[PLAY_LOG_LIST_TRACKS];
    CHECK_EQUAL(1, listed(log, PLAY_LOG_RECENT, tracks));
    CHECK_EQUAL(10, tracks[0]);
}

static void a_full_log_keeps_the_most_recent_and_drops_the_least_played_older_track(void)
{
    uint8_t *log = fresh_log();
    play(log, 1000, 3);
    for (uint32_t key = 1; key < PLAY_LOG_ENTRIES; key++) {
        play(log, key, key == 50 ? 1 : 2);
    }
    CHECK_EQUAL(PLAY_LOG_ENTRIES, play_log_count(log));
    play(log, 5000, 1);
    CHECK_EQUAL(PLAY_LOG_ENTRIES, play_log_count(log));
    uint16_t tracks[PLAY_LOG_LIST_TRACKS];
    listed(log, PLAY_LOG_RECENT, tracks);
    CHECK_EQUAL(5000 & 0xFFFF, tracks[0]);
    bool kept_50 = false;
    bool kept_1000 = false;
    for (uint16_t index = 0; index < play_log_count(log); index++) {
        kept_50 = kept_50 || play_log_entry(log, index).key == 50;
        kept_1000 = kept_1000 || play_log_entry(log, index).key == 1000;
    }
    CHECK(!kept_50);
    CHECK(kept_1000);
    play(log, 6000, 1);
    listed(log, PLAY_LOG_RECENT, tracks);
    CHECK_EQUAL(6000, tracks[0]);
    CHECK_EQUAL(5000, tracks[1]);
}

static void play_counts_stop_at_their_limit(void)
{
    uint8_t *log = fresh_log();
    play(log, 10, 1);
    for (int index = 0; index < 3; index++) {
        log[PLAY_LOG_HEADER_BYTES + 6] = 0xFF;
        log[PLAY_LOG_HEADER_BYTES + 7] = 0xFF;
        play(log, 10, 1);
    }
    CHECK_EQUAL(PLAY_LOG_MAX_PLAYS, play_log_entry(log, 0).plays);
}

static uint32_t key_of(const Library *library, uint16_t track)
{
    uint32_t key = 0;
    CHECK(user_playlist_track_key(library, track, &key));
    return key;
}

static void resolving_finds_the_tracks_again_by_their_paths(void)
{
    const Library *library = fixture_library_open(&fixture);
    uint8_t *log = fresh_log();
    play_log_add(log, key_of(library, 4), 0);
    play_log_add(log, 0x1u, 3);
    play_log_add(log, key_of(library, 2), 9);
    play_log_resolve(log, library, (UserPlaylistPause){NULL, NULL});
    CHECK_EQUAL(4, play_log_entry(log, 0).track);
    CHECK_EQUAL(PLAY_LOG_NO_TRACK, play_log_entry(log, 1).track);
    CHECK_EQUAL(2, play_log_entry(log, 2).track);
    CHECK_EQUAL(library_fingerprint(library), play_log_fingerprint(log));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_sealed_log_is_valid_until_a_byte_changes),
        TEST_CASE(plays_of_the_same_track_add_up),
        TEST_CASE(recently_played_is_newest_first),
        TEST_CASE(most_played_ranks_by_plays_then_by_recency),
        TEST_CASE(lists_skip_tracks_the_library_no_longer_has),
        TEST_CASE(a_full_log_keeps_the_most_recent_and_drops_the_least_played_older_track),
        TEST_CASE(play_counts_stop_at_their_limit),
        TEST_CASE(resolving_finds_the_tracks_again_by_their_paths),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
