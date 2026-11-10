#include <string.h>

#include "check.h"
#include "fixture_library.h"
#include "library/smart_list.h"

static FixtureLibrary fixture;
static SmartEntry entries[SMART_LIST_TRACKS];
static uint8_t bucket[PLAY_STATS_BUCKET_BYTES];
static uint8_t added[ADDED_FILES_BYTES];

static SmartEntry entry(uint32_t key, uint32_t primary, uint32_t secondary)
{
    return (SmartEntry){.key = key,
                        .primary = primary,
                        .secondary = secondary,
                        .place = 0,
                        .track = SMART_LIST_NO_TRACK};
}

static uint32_t key_of(const Library *library, uint16_t track)
{
    uint32_t key = 0;
    CHECK(user_playlist_track_key(library, track, &key));
    return key;
}

static void ranking_puts_the_highest_first(void)
{
    SmartRank rank;
    smart_rank_init(&rank, entries, SMART_LIST_TRACKS);
    smart_rank_offer(&rank, entry(1, 3, 0));
    smart_rank_offer(&rank, entry(2, 9, 0));
    smart_rank_offer(&rank, entry(3, 5, 0));
    CHECK_EQUAL(3, rank.count);
    CHECK_EQUAL(2, entries[0].key);
    CHECK_EQUAL(3, entries[1].key);
    CHECK_EQUAL(1, entries[2].key);
}

static void ties_go_to_the_second_value_then_to_the_first_offered(void)
{
    SmartRank rank;
    smart_rank_init(&rank, entries, SMART_LIST_TRACKS);
    smart_rank_offer(&rank, entry(1, 4, 10));
    smart_rank_offer(&rank, entry(2, 4, 20));
    smart_rank_offer(&rank, entry(3, 4, 20));
    smart_rank_offer(&rank, entry(4, 4, 5));
    CHECK_EQUAL(2, entries[0].key);
    CHECK_EQUAL(3, entries[1].key);
    CHECK_EQUAL(1, entries[2].key);
    CHECK_EQUAL(4, entries[3].key);
}

static void ranking_keeps_only_the_best_hundred(void)
{
    SmartRank rank;
    smart_rank_init(&rank, entries, SMART_LIST_TRACKS);
    for (uint32_t index = 0; index < 1000; index++) {
        uint32_t score = (index * 7919u) % 1000u;
        smart_rank_offer(&rank, entry(score + 1, score, 0));
    }
    CHECK_EQUAL(SMART_LIST_TRACKS, rank.count);
    for (uint16_t index = 0; index < SMART_LIST_TRACKS; index++) {
        CHECK_EQUAL(999u - index, entries[index].primary);
    }
}

static void a_small_rank_drops_what_does_not_fit(void)
{
    SmartEntry few[2];
    SmartRank rank;
    smart_rank_init(&rank, few, 2);
    smart_rank_offer(&rank, entry(1, 1, 0));
    smart_rank_offer(&rank, entry(2, 2, 0));
    smart_rank_offer(&rank, entry(3, 0, 0));
    smart_rank_offer(&rank, entry(4, 3, 0));
    CHECK_EQUAL(2, rank.count);
    CHECK_EQUAL(4, few[0].key);
    CHECK_EQUAL(2, few[1].key);
}

static PlayStat stat_at(uint16_t place)
{
    PlayStat stat;
    CHECK(play_stat_decode(bucket + place * PLAY_STAT_BYTES, &stat));
    return stat;
}

static void a_play_stat_survives_a_round_trip_and_a_blank_one_is_empty(void)
{
    PlayStat stat = {.key = 0xDEADBEEF, .plays = 7, .track = 42, .last = 123456};
    PlayStat read;
    uint8_t bytes[PLAY_STAT_BYTES];
    play_stat_encode(&stat, bytes);
    CHECK(play_stat_decode(bytes, &read));
    CHECK_EQUAL(stat.key, read.key);
    CHECK_EQUAL(7, read.plays);
    CHECK_EQUAL(42, read.track);
    CHECK_EQUAL(123456, read.last);
    bytes[3] ^= 1;
    CHECK(!play_stat_decode(bytes, &read));
    memset(bytes, 0, sizeof bytes);
    CHECK(!play_stat_decode(bytes, &read));
    memset(bytes, 0xFF, sizeof bytes);
    CHECK(!play_stat_decode(bytes, &read));
}

static void noting_a_play_counts_it_and_moves_it_to_now(void)
{
    memset(bucket, 0, sizeof bucket);
    uint16_t first = play_stats_note(bucket, 100, 3, 1);
    uint16_t other = play_stats_note(bucket, 200, 4, 2);
    uint16_t again = play_stats_note(bucket, 100, 5, 3);
    CHECK_EQUAL(first, again);
    CHECK(first != other);
    PlayStat stat = stat_at(first);
    CHECK_EQUAL(100, stat.key);
    CHECK_EQUAL(2, stat.plays);
    CHECK_EQUAL(5, stat.track);
    CHECK_EQUAL(3, stat.last);
    CHECK_EQUAL(1, stat_at(other).plays);
}

static void a_full_bucket_forgets_its_least_played_song(void)
{
    memset(bucket, 0, sizeof bucket);
    for (uint32_t key = 1; key <= PLAY_STATS_BUCKET_ENTRIES; key++) {
        (void)play_stats_note(bucket, key, 0, key);
        (void)play_stats_note(bucket, key, 0, 100 + key);
    }
    (void)play_stats_note(bucket, 7, 0, 200);
    uint16_t newcomer = play_stats_note(bucket, 999, 0, 300);
    PlayStat stat = stat_at(newcomer);
    CHECK_EQUAL(999, stat.key);
    CHECK_EQUAL(1, stat.plays);
    for (uint16_t place = 0; place < PLAY_STATS_BUCKET_ENTRIES; place++) {
        CHECK(stat_at(place).key != 1);
    }
}

static void added_keys_match_the_library_paths(void)
{
    const Library *library = fixture_library_open(&fixture);
    for (uint16_t track = 0; track < library_count(library, LIBRARY_TRACKS); track++) {
        LibraryTrack record;
        char path[256];
        char folder[256] = "/";
        CHECK(library_read_track(library, track, &record));
        CHECK(library_read_string(library, record.path, path, sizeof path));
        const char *name = strrchr(path, '/');
        if (name == NULL) {
            name = path;
        } else {
            memcpy(folder + 1, path, (size_t)(name - path));
            folder[1 + (name - path)] = '\0';
            name++;
        }
        CHECK_EQUAL(key_of(library, track), added_files_key(folder, name));
    }
}

static void only_music_files_are_offered_and_ties_keep_the_walk_order(void)
{
    SmartRank rank;
    smart_rank_init(&rank, entries, SMART_LIST_TRACKS);
    added_files_offer(&rank, "/A", "cover.jpg", 50, 0);
    added_files_offer(&rank, "/A", "list.m3u8", 50, 1);
    added_files_offer(&rank, "/A", "01.flac", 10, 2);
    added_files_offer(&rank, "/A", "02.FLAC", 10, 3);
    added_files_offer(&rank, "/B", "01.mp3", 20, 4);
    CHECK_EQUAL(3, rank.count);
    CHECK_EQUAL(added_files_key("/B", "01.mp3"), entries[0].key);
    CHECK_EQUAL(added_files_key("/A", "01.flac"), entries[1].key);
    CHECK_EQUAL(added_files_key("/A", "02.FLAC"), entries[2].key);
}

static void the_added_file_survives_a_round_trip(void)
{
    SmartEntry stored[3] = {entry(10, 300, 0), entry(20, 200, 0), entry(30, 100, 0)};
    stored[1].track = 5;
    uint32_t length = added_files_encode(stored, 3, 0xABCD, added);
    SmartRank rank;
    uint32_t fingerprint = 0;
    smart_rank_init(&rank, entries, SMART_LIST_TRACKS);
    CHECK(added_files_decode(added, length, &rank, &fingerprint));
    CHECK_EQUAL(3, rank.count);
    CHECK_EQUAL(0xABCD, fingerprint);
    CHECK_EQUAL(20, entries[1].key);
    CHECK_EQUAL(200, entries[1].primary);
    CHECK_EQUAL(5, entries[1].track);
    CHECK_EQUAL(SMART_LIST_NO_TRACK, entries[0].track);
    added[14] ^= 1;
    CHECK(!added_files_decode(added, length, &rank, &fingerprint));
    CHECK_EQUAL(0, rank.count);
    memset(added, 0, sizeof added);
    CHECK(!added_files_decode(added, sizeof added, &rank, &fingerprint));
}

static void resolving_finds_tracks_and_skips_missing_ones(void)
{
    const Library *library = fixture_library_open(&fixture);
    SmartEntry list[3] = {entry(key_of(library, 5), 3, 0), entry(0x12345678, 2, 0),
                          entry(key_of(library, 1), 1, 0)};
    uint16_t tracks[3];
    CHECK(smart_list_resolve(library, list, 3, true, (UserPlaylistPause){NULL, NULL}));
    CHECK_EQUAL(2, smart_list_tracks(list, 3, tracks));
    CHECK_EQUAL(5, tracks[0]);
    CHECK_EQUAL(1, tracks[1]);
}

static void a_stale_track_after_a_rescan_is_found_again_in_its_place(void)
{
    const Library *library = fixture_library_open(&fixture);
    SmartEntry list[3] = {entry(key_of(library, 2), 0, 0), entry(key_of(library, 6), 0, 0),
                          entry(key_of(library, 0), 0, 0)};
    list[0].track = 2;
    list[1].track = 3;
    list[2].track = 0;
    uint16_t tracks[3];
    CHECK(smart_list_resolve(library, list, 3, false, (UserPlaylistPause){NULL, NULL}));
    CHECK_EQUAL(SMART_LIST_NO_TRACK, list[1].track);
    CHECK(smart_list_resolve(library, list, 3, true, (UserPlaylistPause){NULL, NULL}));
    CHECK_EQUAL(3, smart_list_tracks(list, 3, tracks));
    CHECK_EQUAL(2, tracks[0]);
    CHECK_EQUAL(6, tracks[1]);
    CHECK_EQUAL(0, tracks[2]);
    CHECK(!smart_list_resolve(library, list, 3, true, (UserPlaylistPause){NULL, NULL}));
}

static void spoken_tracks_never_join_a_list(void)
{
    Library *library = fixture_library_open(&fixture);
    uint32_t spoken = key_of(library, 7);
    library->sections[LIBRARY_TRACKS].music = 7;
    SmartEntry list[2] = {entry(spoken, 2, 0), entry(key_of(library, 3), 1, 0)};
    list[0].track = 7;
    uint16_t tracks[2];
    (void)smart_list_resolve(library, list, 2, true, (UserPlaylistPause){NULL, NULL});
    CHECK_EQUAL(1, smart_list_tracks(list, 2, tracks));
    CHECK_EQUAL(3, tracks[0]);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(ranking_puts_the_highest_first),
        TEST_CASE(ties_go_to_the_second_value_then_to_the_first_offered),
        TEST_CASE(ranking_keeps_only_the_best_hundred),
        TEST_CASE(a_small_rank_drops_what_does_not_fit),
        TEST_CASE(a_play_stat_survives_a_round_trip_and_a_blank_one_is_empty),
        TEST_CASE(noting_a_play_counts_it_and_moves_it_to_now),
        TEST_CASE(a_full_bucket_forgets_its_least_played_song),
        TEST_CASE(added_keys_match_the_library_paths),
        TEST_CASE(only_music_files_are_offered_and_ties_keep_the_walk_order),
        TEST_CASE(the_added_file_survives_a_round_trip),
        TEST_CASE(resolving_finds_tracks_and_skips_missing_ones),
        TEST_CASE(a_stale_track_after_a_rescan_is_found_again_in_its_place),
        TEST_CASE(spoken_tracks_never_join_a_list),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
