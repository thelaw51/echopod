#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "player.h"
#include "screens/up_next_screen.h"
#include "ui/toast.h"
#include "user_playlists.h"

enum {
    BACK_IN_BLACK = 0,
    HELP = 2,
    ARMY_OF_ME = 3,
    SEED = 7
};

static const char ARMY_OF_ME_PATH[] = "/Various Artists/The Best of Something/02 - Army of Me.flac";
static const char SHOOT_TO_THRILL_PATH[] = "/AC DC/Back In Black/1-02 - Shoot to Thrill.flac";

static FixtureLibrary fixture;
static Player player;
static UpNextScreen up_next;
static Screen *screen;
static UserPlaylists playlists;
static Toast toast;
static uint8_t empty_playlists[USER_PLAYLIST_FILE_BYTES];

static BrowseQuery back_in_black(void)
{
    return (BrowseQuery){.kind = BROWSE_ALBUM_TRACKS,
                         .group = LIBRARY_ARTISTS,
                         .subject = BACK_IN_BLACK,
                         .artist = LIBRARY_NONE};
}

static void play_album_from(uint32_t position)
{
    fake_hal_reset();
    player_init(&player, fixture_library_open(&fixture));
    CHECK(player_play_list(&player, back_in_black(), position, SEED));
}

static void open_up_next(void)
{
    screen = up_next_screen_init(
        &up_next,
        (UpNextConfig){.library = &fixture.library, .theme = &DEFAULT_THEME, .player = &player});
    screen->enter(screen->context, 100);
}

static void open_up_next_with_playlists(void)
{
    fake_hal_add_file(USER_PLAYLISTS_PATH, empty_playlists, sizeof empty_playlists);
    user_playlists_load(&playlists, &fixture.library);
    toast = (Toast){.visible = false};
    screen = up_next_screen_init(&up_next, (UpNextConfig){.library = &fixture.library,
                                                          .theme = &DEFAULT_THEME,
                                                          .player = &player,
                                                          .playlists = &playlists,
                                                          .toast = &toast});
    screen->enter(screen->context, 100);
}

static int row_count(void)
{
    return up_next.model.count(up_next.model.context);
}

static bool row_is(int index, const char *label, const char *detail)
{
    const char *shown_label = up_next.model.label(up_next.model.context, index);
    const char *shown_detail = up_next.model.detail(up_next.model.context, index);
    return strcmp(label, shown_label) == 0 && shown_detail != NULL &&
           strcmp(detail, shown_detail) == 0;
}

static ScreenAction press(Key key)
{
    return screen->handle_key(screen->context, (KeyEvent){.key = key, .gesture = KEY_GESTURE_PRESS},
                              200);
}

static ScreenAction choose_row(int index)
{
    for (int step = 0; step < index; step++) {
        press(KEY_VOLUME_UP);
    }
    return press(KEY_PLAY);
}

static bool playing_path_is(const char *expected)
{
    const char *path = fake_hal_audio_playing_path();
    return path != NULL && strcmp(expected, path) == 0;
}

static void the_playing_song_comes_first_then_the_list(void)
{
    play_album_from(0);
    open_up_next();
    CHECK_EQUAL(2, row_count());
    CHECK(row_is(0, "Hells Bells", "Playing"));
    CHECK(row_is(1, "Shoot to Thrill", "AC/DC"));
}

static void a_paused_song_says_so(void)
{
    play_album_from(0);
    player_toggle_pause(&player);
    open_up_next();
    CHECK(row_is(0, "Hells Bells", "Paused"));
}

static void added_songs_come_before_the_list(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    open_up_next();
    CHECK_EQUAL(3, row_count());
    CHECK(row_is(1, "Help!", "The Beatles"));
    CHECK(row_is(2, "Shoot to Thrill", "AC/DC"));
}

static void the_last_song_has_nothing_after_it(void)
{
    play_album_from(1);
    open_up_next();
    CHECK_EQUAL(1, row_count());
}

static void repeat_all_shows_the_list_wrapping_round(void)
{
    play_album_from(1);
    player_set_repeat(&player, REPEAT_ALL);
    open_up_next();
    CHECK_EQUAL(2, row_count());
    CHECK(row_is(1, "Hells Bells", "AC/DC"));
}

static void choosing_an_added_song_plays_it_and_returns(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    player_add_to_up_next(&player, ARMY_OF_ME);
    open_up_next();
    ScreenAction action = choose_row(2);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
    CHECK(playing_path_is(ARMY_OF_ME_PATH));
    CHECK_EQUAL(0, player.up_next.count);
}

static void choosing_a_list_song_skips_the_added_ones(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    open_up_next();
    ScreenAction action = choose_row(2);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(0, player.up_next.count);
}

static void choosing_the_playing_song_just_returns(void)
{
    play_album_from(0);
    open_up_next();
    int plays = fake_hal_audio_play_count();
    ScreenAction action = choose_row(0);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
}

static uint16_t playlist_track(int slot, uint32_t position)
{
    uint16_t track = 0;
    CHECK(playlists.lists.track(&playlists, (uint16_t)slot, position, &track));
    return track;
}

static ScreenAction choose_save(void)
{
    press(KEY_VOLUME_DOWN);
    CHECK_EQUAL(0, up_next.list.selected);
    return press(KEY_PLAY);
}

static void new_playlist_sits_above_the_playing_song(void)
{
    play_album_from(0);
    open_up_next_with_playlists();
    CHECK_EQUAL(3, row_count());
    CHECK(strcmp("New playlist", up_next.model.label(up_next.model.context, 0)) == 0);
    CHECK(up_next.model.detail(up_next.model.context, 0) == NULL);
    CHECK(row_is(1, "Hells Bells", "Playing"));
    CHECK_EQUAL(1, up_next.list.selected);
}

static void saving_keeps_the_order_shown(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    open_up_next_with_playlists();
    uint16_t playing = player.track;
    CHECK_EQUAL(SCREEN_ACTION_NONE, choose_save().type);
    CHECK_EQUAL(1, user_playlists_count(&playlists));
    int slot = user_playlists_slot_at(&playlists, 0);
    CHECK_EQUAL(3, playlists.lists.count(&playlists, (uint16_t)slot));
    CHECK_EQUAL(playing, playlist_track(slot, 0));
    CHECK_EQUAL(HELP, playlist_track(slot, 1));
    CHECK(toast.visible);
    CHECK(strcmp("Added to Playlist 1", toast.text) == 0);
    CHECK(playing_path_is("/AC DC/Back In Black/1-01 - Hells Bells.flac"));
}

static void saving_with_every_slot_used_says_so(void)
{
    play_album_from(0);
    open_up_next_with_playlists();
    for (int count = 0; count < USER_PLAYLIST_SLOTS; count++) {
        CHECK(user_playlists_create(&playlists) != USER_PLAYLISTS_NO_SLOT);
    }
    choose_save();
    CHECK_EQUAL(USER_PLAYLIST_SLOTS, user_playlists_count(&playlists));
    CHECK(strcmp("Could not save the playlist", toast.text) == 0);
}

static void without_playlists_there_is_no_save_row(void)
{
    play_album_from(0);
    open_up_next();
    CHECK_EQUAL(0, up_next.extra);
    CHECK(row_is(0, "Hells Bells", "Playing"));
}

static void back_returns_to_now_playing(void)
{
    play_album_from(0);
    open_up_next();
    CHECK_EQUAL(SCREEN_ACTION_POP, press(KEY_BACK).type);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_playing_song_comes_first_then_the_list),
        TEST_CASE(a_paused_song_says_so),
        TEST_CASE(added_songs_come_before_the_list),
        TEST_CASE(the_last_song_has_nothing_after_it),
        TEST_CASE(repeat_all_shows_the_list_wrapping_round),
        TEST_CASE(choosing_an_added_song_plays_it_and_returns),
        TEST_CASE(choosing_a_list_song_skips_the_added_ones),
        TEST_CASE(choosing_the_playing_song_just_returns),
        TEST_CASE(back_returns_to_now_playing),
        TEST_CASE(new_playlist_sits_above_the_playing_song),
        TEST_CASE(saving_keeps_the_order_shown),
        TEST_CASE(saving_with_every_slot_used_says_so),
        TEST_CASE(without_playlists_there_is_no_save_row),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
