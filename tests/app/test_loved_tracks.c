#include "big_library.h"
#include "check.h"
#include "fake_hal.h"
#include "favorites_store.h"
#include "fixture_library.h"
#include "library/library_browse.h"
#include "library/user_playlist.h"
#include "loved_tracks.h"
#include "user_playlists.h"

enum {
    TRACKS_PER_ALBUM = 4,
    LIBRARY_TRACKS_BEFORE = 8,
    LIBRARY_TRACKS_SHRUNK = 4,
    LIBRARY_TRACKS_GROWN = 12,
    LAST_TRACK = 7
};

static FixtureLibrary fixture;
static LovedTracks loved;
static UserPlaylists playlists;
static FavoritesStore favorites;
static uint8_t empty_playlists[USER_PLAYLIST_FILE_BYTES];

static uint32_t list_count(const Library *library, BrowseQuery query)
{
    return browse_count(library, query);
}

static uint16_t list_track(const Library *library, BrowseQuery query, uint32_t position)
{
    BrowseItem item;
    CHECK(browse_item(library, query, position, &item));
    return (uint16_t)item.index;
}

static Library *fresh_fixture(void)
{
    fake_hal_reset();
    return fixture_library_open(&fixture);
}

static void without_a_file_nothing_is_loved(void)
{
    Library *library = fresh_fixture();
    loved_tracks_load(&loved, library);
    CHECK_EQUAL(0, loved_tracks_count(&loved));
    CHECK(!loved_tracks_contains(&loved, 2));
    CHECK_EQUAL(0, list_count(library, loved_tracks_query(false)));
}

static void loving_adds_in_order_and_loving_again_removes(void)
{
    Library *library = fresh_fixture();
    loved_tracks_load(&loved, library);
    CHECK_EQUAL(LOVED_ADDED, loved_tracks_toggle(&loved, 5));
    CHECK_EQUAL(LOVED_ADDED, loved_tracks_toggle(&loved, 2));
    CHECK(loved_tracks_contains(&loved, 5));
    CHECK(loved_tracks_contains(&loved, 2));
    CHECK(!loved_tracks_contains(&loved, 3));
    CHECK_EQUAL(2, list_count(library, loved_tracks_query(false)));
    CHECK_EQUAL(5, list_track(library, loved_tracks_query(false), 0));
    CHECK_EQUAL(2, list_track(library, loved_tracks_query(false), 1));
    CHECK_EQUAL(LOVED_REMOVED, loved_tracks_toggle(&loved, 5));
    CHECK(!loved_tracks_contains(&loved, 5));
    CHECK_EQUAL(1, list_count(library, loved_tracks_query(false)));
    CHECK_EQUAL(2, list_track(library, loved_tracks_query(false), 0));
}

static void loved_songs_are_saved_and_loaded_again(void)
{
    Library *library = fresh_fixture();
    loved_tracks_load(&loved, library);
    loved_tracks_toggle(&loved, 4);
    loved_tracks_toggle(&loved, 1);
    uint32_t size = 0;
    CHECK(fake_hal_file_bytes(LOVED_TRACKS_PATH, &size) != NULL);
    CHECK_EQUAL(USER_PLAYLIST_SLOT_BYTES, size);
    loved_tracks_load(&loved, library);
    CHECK_EQUAL(2, loved_tracks_count(&loved));
    CHECK(loved_tracks_contains(&loved, 4));
    CHECK(loved_tracks_contains(&loved, 1));
    CHECK_EQUAL(4, list_track(library, loved_tracks_query(false), 0));
}

static void the_list_is_full_at_two_hundred_songs(void)
{
    Library *library = fresh_fixture();
    loved_tracks_load(&loved, library);
    for (uint16_t index = 0; index < USER_PLAYLIST_TRACKS; index++) {
        uint32_t key = 0;
        CHECK(user_playlist_track_key(library, 0, &key));
        CHECK(user_playlist_add(&loved.loved, key + 1 + index, USER_PLAYLIST_NO_TRACK));
    }
    CHECK_EQUAL(LOVED_FULL, loved_tracks_toggle(&loved, 3));
    CHECK(!loved_tracks_contains(&loved, 3));
}

static void a_damaged_file_loads_as_empty(void)
{
    Library *library = fresh_fixture();
    loved_tracks_load(&loved, library);
    loved_tracks_toggle(&loved, 6);
    uint32_t size = 0;
    const uint8_t *saved = fake_hal_file_bytes(LOVED_TRACKS_PATH, &size);
    static uint8_t damaged[USER_PLAYLIST_SLOT_BYTES];
    for (uint32_t index = 0; index < size; index++) {
        damaged[index] = saved[index];
    }
    damaged[USER_PLAYLIST_HEADER_BYTES] ^= 1;
    library = fresh_fixture();
    fake_hal_add_file(LOVED_TRACKS_PATH, damaged, sizeof damaged);
    loved_tracks_load(&loved, library);
    CHECK_EQUAL(0, loved_tracks_count(&loved));
}

static void songs_are_found_by_path_when_their_indices_are_stale(void)
{
    Library *library = fresh_fixture();
    loved_tracks_load(&loved, library);
    loved_tracks_toggle(&loved, 5);
    loved.loved.tracks[0] = 2;
    loved.loved.fingerprint ^= 1;
    static uint8_t stale[USER_PLAYLIST_SLOT_BYTES];
    user_playlist_encode(&loved.loved, stale);
    library = fresh_fixture();
    fake_hal_add_file(LOVED_TRACKS_PATH, stale, sizeof stale);
    loved_tracks_load(&loved, library);
    CHECK(loved_tracks_contains(&loved, 5));
    CHECK(!loved_tracks_contains(&loved, 2));
    CHECK_EQUAL(1, list_count(library, loved_tracks_query(false)));
    CHECK_EQUAL(5, list_track(library, loved_tracks_query(false), 0));
    CHECK(loved_tracks_contains(&loved, 5));
}

static void a_song_missing_after_a_rescan_drops_out_and_comes_back(void)
{
    BigLibrary before;
    BigLibrary shrunk;
    BigLibrary grown;
    fake_hal_reset();
    Library *library = big_library_open(&before, LIBRARY_TRACKS_BEFORE, TRACKS_PER_ALBUM);
    loved_tracks_load(&loved, library);
    loved_tracks_toggle(&loved, LAST_TRACK);
    loved_tracks_toggle(&loved, 1);
    library = big_library_open(&shrunk, LIBRARY_TRACKS_SHRUNK, TRACKS_PER_ALBUM);
    loved_tracks_load(&loved, library);
    CHECK_EQUAL(1, loved_tracks_count(&loved));
    CHECK_EQUAL(1, list_track(library, loved_tracks_query(false), 0));
    library = big_library_open(&grown, LIBRARY_TRACKS_GROWN, TRACKS_PER_ALBUM);
    loved_tracks_load(&loved, library);
    CHECK_EQUAL(2, loved_tracks_count(&loved));
    CHECK(loved_tracks_contains(&loved, LAST_TRACK));
    CHECK_EQUAL(LAST_TRACK, list_track(library, loved_tracks_query(false), 0));
    big_library_close(&before);
    big_library_close(&shrunk);
    big_library_close(&grown);
}

static void playback_keeps_its_copy_while_the_list_changes(void)
{
    Library *library = fresh_fixture();
    loved_tracks_load(&loved, library);
    loved_tracks_toggle(&loved, 3);
    loved_tracks_toggle(&loved, 6);
    loved_tracks_pin(&loved);
    loved_tracks_toggle(&loved, 3);
    CHECK_EQUAL(1, list_count(library, loved_tracks_query(false)));
    CHECK_EQUAL(2, list_count(library, loved_tracks_query(true)));
    CHECK_EQUAL(3, list_track(library, loved_tracks_query(true), 0));
}

static void user_playlists_still_answer_through_the_loved_list(void)
{
    Library *library = fresh_fixture();
    fake_hal_add_file(USER_PLAYLISTS_PATH, empty_playlists, sizeof empty_playlists);
    user_playlists_load(&playlists, library);
    loved_tracks_load(&loved, library);
    int slot = user_playlists_create(&playlists);
    CHECK(slot != USER_PLAYLISTS_NO_SLOT);
    CHECK_EQUAL(USER_PLAYLISTS_ADDED, user_playlists_add_track(&playlists, slot, 4));
    loved_tracks_toggle(&loved, 2);
    CHECK_EQUAL(1, list_count(library, user_playlists_query(slot, false)));
    CHECK_EQUAL(4, list_track(library, user_playlists_query(slot, false), 0));
    CHECK_EQUAL(2, list_track(library, loved_tracks_query(false), 0));
    CHECK(loved_tracks_is_list(loved_tracks_query(true)));
    CHECK(!loved_tracks_is_list(user_playlists_query(slot, true)));
    loved_tracks_load(&loved, library);
    CHECK_EQUAL(4, list_track(library, user_playlists_query(slot, false), 0));
}

static void album_favorites_load_as_before(void)
{
    Library *library = fresh_fixture();
    favorites_store_load(&favorites, library);
    favorites_store_toggle_album(&favorites, 2);
    loved_tracks_load(&loved, library);
    loved_tracks_toggle(&loved, 2);
    favorites_store_load(&favorites, library);
    CHECK(favorites_store_contains_album(&favorites, 2));
    CHECK(!favorites_store_contains_album(&favorites, 1));
    CHECK_EQUAL(1, favorites.favorites.count);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(without_a_file_nothing_is_loved),
        TEST_CASE(loving_adds_in_order_and_loving_again_removes),
        TEST_CASE(loved_songs_are_saved_and_loaded_again),
        TEST_CASE(the_list_is_full_at_two_hundred_songs),
        TEST_CASE(a_damaged_file_loads_as_empty),
        TEST_CASE(songs_are_found_by_path_when_their_indices_are_stale),
        TEST_CASE(a_song_missing_after_a_rescan_drops_out_and_comes_back),
        TEST_CASE(playback_keeps_its_copy_while_the_list_changes),
        TEST_CASE(user_playlists_still_answer_through_the_loved_list),
        TEST_CASE(album_favorites_load_as_before),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
