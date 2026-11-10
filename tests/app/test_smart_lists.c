#include <string.h>

#include "check.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "listen_history.h"
#include "player.h"
#include "smart_lists.h"
#include "smart_store.h"

enum {
    BACK_IN_BLACK = 0,
    SEED = 7,
    RING_SLOTS = 16,
    SECOND_MS = 1000,
    LISTEN_MS = 40 * SECOND_MS,
    SKIP_MS = 5 * SECOND_MS
};

static FixtureLibrary fixture;
static Library *library;
static Player player;
static ListenHistory history;
static SmartLists smart;
static uint8_t ring[RING_SLOTS * HISTORY_SLOT_BYTES];
static uint8_t plays[PLAY_STATS_FILE_BYTES];
static uint8_t added[4096];
static uint32_t now_ms;

static void start(void)
{
    fake_hal_reset();
    memset(ring, 0, sizeof ring);
    memset(plays, 0, sizeof plays);
    memset(added, 0, sizeof added);
    fake_hal_add_file(LISTEN_HISTORY_PATH, ring, sizeof ring);
    fake_hal_add_file(PLAY_STATS_PATH, plays, sizeof plays);
    fake_hal_add_file(ADDED_FILES_PATH, added, sizeof added);
    history = (ListenHistory){.open = false};
    library = fixture_library_open(&fixture);
    smart_lists_load(&smart, library);
    player_init(&player, library);
    now_ms = 5000;
}

static void follow(void)
{
    PlayerStatus status = player_status(&player);
    listen_history_follow(&history, library, &status, now_ms);
}

static void wait(uint32_t milliseconds)
{
    now_ms += milliseconds;
    fake_hal_audio_set_position(player_status(&player).position_ms + milliseconds);
    player_update(&player, now_ms);
    follow();
}

static uint16_t listen(uint32_t position, uint32_t milliseconds)
{
    BrowseQuery album = {.kind = BROWSE_ALBUM_TRACKS,
                         .group = LIBRARY_ARTISTS,
                         .subject = BACK_IN_BLACK,
                         .artist = LIBRARY_NONE};
    CHECK(player_play_list(&player, album, position, SEED));
    follow();
    uint16_t track = player_status(&player).track;
    wait(milliseconds);
    return track;
}

static void stop(void)
{
    listen_history_close(&history, now_ms);
}

static uint32_t count_of(BrowseQuery query)
{
    return browse_count(library, query);
}

static uint16_t track_at(BrowseQuery query, uint32_t position)
{
    BrowseItem item;
    CHECK(browse_item(library, query, position, &item));
    CHECK_EQUAL(BROWSE_ITEM_TRACK, item.kind);
    return item.index;
}

static void lists_start_empty(void)
{
    start();
    for (int kind = 0; kind < SMART_KIND_COUNT; kind++) {
        CHECK_EQUAL(0, count_of(smart_lists_query((SmartKind)kind, false)));
    }
}

static void listened_songs_are_counted_and_skips_are_not(void)
{
    start();
    uint16_t first = listen(0, LISTEN_MS);
    uint16_t skipped = listen(1, SKIP_MS);
    (void)listen(0, LISTEN_MS);
    stop();
    CHECK(skipped != first);
    BrowseQuery most = smart_lists_query(SMART_MOST_PLAYED, false);
    CHECK_EQUAL(1, count_of(most));
    CHECK_EQUAL(first, track_at(most, 0));
    BrowseQuery recent = smart_lists_query(SMART_RECENTLY_PLAYED, false);
    CHECK_EQUAL(1, count_of(recent));
    CHECK_EQUAL(first, track_at(recent, 0));
}

static void recently_played_puts_the_last_listen_first(void)
{
    start();
    uint16_t first = listen(0, LISTEN_MS);
    (void)listen(0, LISTEN_MS);
    uint16_t later = listen(1, LISTEN_MS);
    stop();
    BrowseQuery recent = smart_lists_query(SMART_RECENTLY_PLAYED, false);
    CHECK_EQUAL(later, track_at(recent, 0));
    CHECK_EQUAL(first, track_at(recent, 1));
    BrowseQuery most = smart_lists_query(SMART_MOST_PLAYED, false);
    CHECK_EQUAL(first, track_at(most, 0));
}

static void the_playing_copy_keeps_its_order_while_plays_change_it(void)
{
    start();
    uint16_t first = listen(0, LISTEN_MS);
    uint16_t second = listen(1, LISTEN_MS);
    stop();
    BrowseQuery recent = smart_lists_query(SMART_RECENTLY_PLAYED, false);
    BrowseQuery pinned = smart_lists_pin(&smart, recent);
    CHECK(smart_lists_owns(pinned));
    CHECK_EQUAL(second, track_at(pinned, 0));
    (void)listen(0, LISTEN_MS);
    stop();
    CHECK_EQUAL(first, track_at(recent, 0));
    CHECK_EQUAL(second, track_at(pinned, 0));
    CHECK_EQUAL(first, track_at(pinned, 1));
}

static void counts_survive_a_restart(void)
{
    start();
    uint16_t first = listen(0, LISTEN_MS);
    stop();
    SmartLists fresh;
    smart_lists_load(&fresh, library);
    BrowseQuery most = smart_lists_query(SMART_MOST_PLAYED, false);
    CHECK_EQUAL(1, count_of(most));
    CHECK_EQUAL(first, track_at(most, 0));
}

static SmartEntry added_entry(uint16_t track, uint32_t modified)
{
    uint32_t key = 0;
    CHECK(user_playlist_track_key(library, track, &key));
    return (SmartEntry){
        .key = key, .primary = modified, .secondary = 0, .place = 0, .track = SMART_LIST_NO_TRACK};
}

static void recently_added_follows_the_saved_order_and_keeps_it_after_a_rescan(void)
{
    start();
    SmartEntry newest[3] = {added_entry(4, 300), added_entry(1, 200), added_entry(6, 100)};
    newest[2].key ^= 1;
    CHECK(smart_store_save_added(newest, 3));
    BrowseQuery query = smart_lists_query(SMART_RECENTLY_ADDED, false);
    CHECK_EQUAL(2, count_of(query));
    CHECK_EQUAL(4, track_at(query, 0));
    CHECK_EQUAL(1, track_at(query, 1));
    SmartEntry stored[SMART_LIST_TRACKS];
    SmartRank rank;
    uint32_t fingerprint = 0;
    smart_rank_init(&rank, stored, SMART_LIST_TRACKS);
    smart_store_load_added(&rank, &fingerprint);
    CHECK_EQUAL(library_fingerprint(library), fingerprint);
    CHECK_EQUAL(4, stored[0].track);
    CHECK_EQUAL(SMART_LIST_NO_TRACK, stored[2].track);
    CHECK(smart_store_save_added(newest, 3));
    smart_store_load_added(&rank, &fingerprint);
    CHECK_EQUAL(library_fingerprint(library), fingerprint);
}

static void other_track_lists_still_reach_the_next_provider(void)
{
    start();
    BrowseQuery playlist = {
        .kind = BROWSE_TRACK_LIST, .group = LIBRARY_ARTISTS, .subject = 3, .artist = LIBRARY_NONE};
    CHECK(!smart_lists_owns(playlist));
    CHECK_EQUAL(0, count_of(playlist));
    CHECK(smart_lists_owns(smart_lists_query(SMART_RECENTLY_ADDED, true)));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(lists_start_empty),
        TEST_CASE(listened_songs_are_counted_and_skips_are_not),
        TEST_CASE(recently_played_puts_the_last_listen_first),
        TEST_CASE(the_playing_copy_keeps_its_order_while_plays_change_it),
        TEST_CASE(counts_survive_a_restart),
        TEST_CASE(recently_added_follows_the_saved_order_and_keeps_it_after_a_rescan),
        TEST_CASE(other_track_lists_still_reach_the_next_provider),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
