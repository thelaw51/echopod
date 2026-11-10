#include <stdio.h>

#include "check.h"
#include "core/hash.h"
#include "fixture_library.h"
#include "library/user_playlist.h"
#include "memory_source.h"

static FixtureLibrary fixture;
static UserPlaylist playlist;
static uint8_t slot[USER_PLAYLIST_SLOT_BYTES];

static UserPlaylist *new_playlist(uint16_t number)
{
    user_playlist_clear(&playlist, number);
    return &playlist;
}

static uint32_t key_of(const Library *library, uint16_t track)
{
    uint32_t key = 0;
    CHECK(user_playlist_track_key(library, track, &key));
    return key;
}

static void cue_tracks_of_one_image_get_their_own_keys(void)
{
    static uint8_t bytes[4096];
    FILE *file = fopen(ECHO_FIXTURE_DIR "/cue_music.db", "rb");
    CHECK(file != NULL);
    size_t size = file == NULL ? 0 : fread(bytes, 1, sizeof bytes, file);
    if (file != NULL) {
        fclose(file);
    }
    MemorySource memory = {.bytes = bytes, .size = (uint32_t)size};
    Library library;
    CHECK_EQUAL(LIBRARY_OK, library_open(&library, memory_source(&memory), (uint32_t)size));
    uint32_t opening = key_of(&library, 5);
    uint32_t second = key_of(&library, 6);
    CHECK(opening != second);
    CHECK(opening != hash_update(HASH_START, "Album/Image.wav", 15));
    CHECK_EQUAL(hash_update(HASH_START, "Album/Bonus.wav", 15), key_of(&library, 0));
}

static void adding_keeps_the_order_and_removing_closes_the_gap(void)
{
    UserPlaylist *list = new_playlist(1);
    CHECK(user_playlist_add(list, 10, 0));
    CHECK(user_playlist_add(list, 20, 1));
    CHECK(user_playlist_add(list, 30, 2));
    CHECK(user_playlist_remove_at(list, 1));
    CHECK_EQUAL(2, list->count);
    CHECK_EQUAL(10, list->keys[0]);
    CHECK_EQUAL(30, list->keys[1]);
    CHECK_EQUAL(2, list->tracks[1]);
    CHECK(!user_playlist_remove_at(list, 2));
}

static void a_full_playlist_refuses_more(void)
{
    UserPlaylist *list = new_playlist(1);
    for (uint16_t index = 0; index < USER_PLAYLIST_TRACKS; index++) {
        CHECK(user_playlist_add(list, index, index));
    }
    CHECK(!user_playlist_add(list, 9999, 0));
    CHECK_EQUAL(USER_PLAYLIST_TRACKS, list->count);
}

static void an_empty_slot_takes_no_tracks(void)
{
    CHECK(!user_playlist_add(new_playlist(0), 1, 1));
}

static void a_slot_survives_a_round_trip(void)
{
    UserPlaylist *list = new_playlist(7);
    user_playlist_add(list, 0xDEADBEEFu, 3);
    user_playlist_add(list, 0xDEADBEEFu, USER_PLAYLIST_NO_TRACK);
    list->fingerprint = 0x12345678u;
    user_playlist_encode(list, slot);
    UserPlaylist decoded;
    CHECK(user_playlist_decode(slot, sizeof slot, &decoded));
    CHECK_EQUAL(7, decoded.number);
    CHECK_EQUAL(2, decoded.count);
    CHECK_EQUAL(0x12345678u, decoded.fingerprint);
    CHECK_EQUAL(0xDEADBEEFu, decoded.keys[1]);
    CHECK_EQUAL(3, decoded.tracks[0]);
    CHECK_EQUAL(USER_PLAYLIST_NO_TRACK, decoded.tracks[1]);
}

static void a_damaged_or_blank_slot_reads_as_empty(void)
{
    user_playlist_encode(new_playlist(2), slot);
    slot[USER_PLAYLIST_HEADER_BYTES] ^= 1;
    UserPlaylist decoded;
    CHECK(!user_playlist_decode(slot, sizeof slot, &decoded));
    CHECK_EQUAL(0, decoded.number);
    for (uint32_t index = 0; index < sizeof slot; index++) {
        slot[index] = 0;
    }
    CHECK(!user_playlist_decode(slot, sizeof slot, &decoded));
    CHECK(!user_playlist_decode(slot, sizeof slot - 1, &decoded));
}

static void tracks_have_distinct_keys(void)
{
    const Library *library = fixture_library_open(&fixture);
    CHECK(key_of(library, 0) != key_of(library, 1));
    CHECK_EQUAL(key_of(library, 2), key_of(library, 2));
    uint32_t key = 0;
    CHECK(!user_playlist_track_key(library, FIXTURE_TRACKS, &key));
}

static void resolving_finds_tracks_by_their_paths(void)
{
    const Library *library = fixture_library_open(&fixture);
    UserPlaylist *list = new_playlist(1);
    user_playlist_add(list, key_of(library, 5), 0);
    user_playlist_add(list, 0x1u, 0);
    user_playlist_add(list, key_of(library, 2), 0);
    user_playlist_add(list, key_of(library, 5), 0);
    user_playlist_resolve(list, library, (UserPlaylistPause){NULL, NULL});
    CHECK_EQUAL(5, list->tracks[0]);
    CHECK_EQUAL(USER_PLAYLIST_NO_TRACK, list->tracks[1]);
    CHECK_EQUAL(2, list->tracks[2]);
    CHECK_EQUAL(5, list->tracks[3]);
    CHECK_EQUAL(library_fingerprint(library), list->fingerprint);
    uint16_t tracks[USER_PLAYLIST_TRACKS];
    CHECK_EQUAL(3, user_playlist_playable(list, tracks, USER_PLAYLIST_TRACKS));
    CHECK_EQUAL(5, tracks[0]);
    CHECK_EQUAL(2, tracks[1]);
    CHECK_EQUAL(1, user_playlist_playable(list, tracks, 1));
}

static int pauses;

static void count_pause(void *context)
{
    (void)context;
    pauses++;
}

static void resolving_pauses_for_the_audio(void)
{
    const Library *library = fixture_library_open(&fixture);
    UserPlaylist *list = new_playlist(1);
    user_playlist_add(list, key_of(library, 0), 0);
    pauses = 0;
    user_playlist_resolve(list, library, (UserPlaylistPause){count_pause, NULL});
    CHECK(pauses > 0);
}

static void a_playlist_is_current_only_for_its_library(void)
{
    const Library *library = fixture_library_open(&fixture);
    UserPlaylist *list = new_playlist(1);
    user_playlist_add(list, key_of(library, 3), 3);
    user_playlist_add(list, 0x1u, USER_PLAYLIST_NO_TRACK);
    CHECK(!user_playlist_is_current(list, library));
    list->fingerprint = library_fingerprint(library);
    CHECK(user_playlist_is_current(list, library));
    list->tracks[0] = 4;
    CHECK(!user_playlist_is_current(list, library));
    list->tracks[0] = FIXTURE_TRACKS;
    CHECK(!user_playlist_is_current(list, library));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(adding_keeps_the_order_and_removing_closes_the_gap),
        TEST_CASE(cue_tracks_of_one_image_get_their_own_keys),
        TEST_CASE(a_full_playlist_refuses_more),
        TEST_CASE(an_empty_slot_takes_no_tracks),
        TEST_CASE(a_slot_survives_a_round_trip),
        TEST_CASE(a_damaged_or_blank_slot_reads_as_empty),
        TEST_CASE(tracks_have_distinct_keys),
        TEST_CASE(resolving_finds_tracks_by_their_paths),
        TEST_CASE(resolving_pauses_for_the_audio),
        TEST_CASE(a_playlist_is_current_only_for_its_library),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
