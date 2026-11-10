#include <string.h>

#include "browse_screen.h"
#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "library/playlist_writer.h"
#include "player.h"
#include "ui/toast.h"

static FixtureLibrary fixture;
static BrowsePool pool;
static FavoritesStore favorites;
static UserPlaylists playlists;
static Player player;
static Toast toast;
static uint8_t empty_playlists[USER_PLAYLIST_FILE_BYTES];

static BrowseScreen *opened(BrowseQuery query)
{
    fake_hal_reset();
    fake_hal_add_file(USER_PLAYLISTS_PATH, empty_playlists, sizeof empty_playlists);
    Library *library = fixture_library_open(&fixture);
    favorites = (FavoritesStore){.library = library};
    user_playlists_load(&playlists, library);
    toast = (Toast){.visible = false};
    browse_pool_init(&pool, (BrowsePoolConfig){.library = library,
                                               .theme = &DEFAULT_THEME,
                                               .favorites = &favorites,
                                               .playlists = &playlists,
                                               .toast = &toast});
    Screen *screen = browse_pool_open(&pool, 0, query);
    screen->enter(screen->context, 0);
    return screen->context;
}

static int section_at(const BrowseScreen *browse, int index)
{
    return browse->model.section(browse->model.context, index);
}

static void albums_are_lettered_by_their_sort_key(void)
{
    BrowseScreen *albums = opened(browse_root_query(BROWSE_ALBUMS));
    CHECK(albums->model.section != NULL);
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 'b' - 'a', section_at(albums, 0));
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 'b' - 'a', section_at(albums, 1));
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 'h' - 'a', section_at(albums, 2));
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 't' - 'a', section_at(albums, 4));
}

static void lettered_lists_never_go_backwards(void)
{
    static const BrowseKind LETTERED[] = {BROWSE_ARTISTS, BROWSE_ALBUM_ARTISTS, BROWSE_GENRES,
                                          BROWSE_ALBUMS, BROWSE_ALL_SONGS};
    for (size_t kind = 0; kind < sizeof LETTERED / sizeof LETTERED[0]; kind++) {
        BrowseScreen *browse = opened(browse_root_query(LETTERED[kind]));
        CHECK(browse->model.section != NULL);
        for (int index = 1; index < (int)browse->count; index++) {
            CHECK(section_at(browse, index - 1) <= section_at(browse, index));
        }
    }
}

static void album_tracks_have_no_letters(void)
{
    BrowseQuery tracks = {.kind = BROWSE_ALBUM_TRACKS,
                          .group = LIBRARY_ARTISTS,
                          .subject = 0,
                          .artist = LIBRARY_NONE};
    CHECK(opened(tracks)->model.section == NULL);
}

static void check_rows_match_the_library(BrowseScreen *browse, int rows)
{
    int top = list_view_top(&browse->list);
    for (int row = 0; row < rows; row++) {
        char label[BROWSE_LABEL_CAPACITY];
        char detail[BROWSE_DETAIL_CAPACITY];
        BrowseText text = {label, sizeof label, detail, sizeof detail};
        BrowseItem item;
        CHECK(
            browse_item(browse->pool->config.library, browse->query, (uint32_t)(top + row), &item));
        CHECK(browse_item_text(browse->pool->config.library, browse->query, item, text));
        CHECK(strcmp(label, browse->model.label(browse, top + row)) == 0);
    }
}

static void scrolling_one_row_at_a_time_keeps_every_row_right(void)
{
    BrowseScreen *albums = opened(browse_root_query(BROWSE_ALBUMS));
    albums->list.visible_rows = 3;
    albums->screen.update(albums, 0);
    for (int step = 0; step < 4; step++) {
        list_view_move(&albums->list, 1, false, 0);
        albums->screen.update(albums, 0);
        check_rows_match_the_library(albums, 3);
    }
    for (int step = 0; step < 4; step++) {
        list_view_move(&albums->list, -1, false, 0);
        albums->screen.update(albums, 0);
        check_rows_match_the_library(albums, 3);
    }
}

static void a_jump_longer_than_the_window_reloads_every_row(void)
{
    BrowseScreen *albums = opened(browse_root_query(BROWSE_ALBUMS));
    albums->list.visible_rows = 2;
    albums->screen.update(albums, 0);
    list_view_select(&albums->list, 4, 0);
    albums->screen.update(albums, 0);
    check_rows_match_the_library(albums, 1);
    list_view_select(&albums->list, 0, 0);
    albums->screen.update(albums, 0);
    check_rows_match_the_library(albums, 2);
}

static void scrolling_one_row_loads_only_the_new_row(void)
{
    BrowseScreen *albums = opened(browse_root_query(BROWSE_ALBUMS));
    albums->list.visible_rows = 3;
    albums->screen.update(albums, 0);
    favorites.revision++;
    fixture.memory.reads = 0;
    albums->screen.update(albums, 0);
    int full_reload = fixture.memory.reads;
    list_view_select(&albums->list, 3, 0);
    fixture.memory.reads = 0;
    albums->screen.update(albums, 0);
    CHECK(fixture.memory.reads < full_reload);
}

static void coming_back_to_a_list_reads_nothing(void)
{
    BrowseScreen *albums = opened(browse_root_query(BROWSE_ALBUMS));
    albums->screen.update(albums, 0);
    fixture.memory.reads = 0;
    albums->screen.enter(albums, 0);
    albums->screen.update(albums, 0);
    CHECK_EQUAL(0, fixture.memory.reads);
    check_rows_match_the_library(albums, 3);
}

static bool row_has_star(const BrowseScreen *browse, int index)
{
    const char *detail = browse->model.detail(browse, index);
    return detail != NULL && strstr(detail, "\xE2\x98\x85") != NULL;
}

static void a_favorite_added_elsewhere_shows_when_the_list_comes_back(void)
{
    BrowseScreen *albums = opened(browse_root_query(BROWSE_ALBUMS));
    BrowseItem item;
    CHECK(browse_item(pool.config.library, albums->query, 0, &item));
    CHECK(!row_has_star(albums, 0));
    favorites_store_toggle_album(&favorites, item.index);
    albums->screen.enter(albums, 0);
    CHECK(row_has_star(albums, 0));
}

static void opening_another_list_at_the_same_level_shows_its_own_rows(void)
{
    BrowseScreen *albums = opened(browse_root_query(BROWSE_ALBUMS));
    albums->screen.update(albums, 0);
    Screen *artists = browse_pool_open(&pool, 0, browse_root_query(BROWSE_ARTISTS));
    artists->enter(artists->context, 0);
    check_rows_match_the_library(artists->context, 3);
}

static int playlist_with_tracks(int tracks)
{
    int slot = user_playlists_create(&playlists);
    for (int track = 0; track < tracks; track++) {
        CHECK_EQUAL(USER_PLAYLISTS_ADDED,
                    user_playlists_add_track(&playlists, slot, (uint16_t)track));
    }
    return slot;
}

static ScreenAction press_play(BrowseScreen *browse, KeyGesture gesture)
{
    return browse->screen.handle_key(browse, (KeyEvent){.key = KEY_PLAY, .gesture = gesture}, 0);
}

static void playlists_made_on_the_player_come_first(void)
{
    BrowseScreen *lists = opened(browse_root_query(BROWSE_PLAYLISTS));
    uint32_t card_lists = lists->count;
    playlist_with_tracks(2);
    lists->screen.enter(lists, 0);
    CHECK_EQUAL(card_lists + 1, lists->count);
    CHECK(strcmp("Playlist 1", lists->model.label(lists, 0)) == 0);
    CHECK(lists->model.opens(lists, 0));
    for (int index = 1; index < (int)lists->count; index++) {
        CHECK(section_at(lists, index - 1) <= section_at(lists, index));
    }
}

static void a_player_playlist_opens_as_its_tracks(void)
{
    BrowseScreen *lists = opened(browse_root_query(BROWSE_PLAYLISTS));
    playlist_with_tracks(3);
    lists->screen.enter(lists, 0);
    list_view_select(&lists->list, 0, 0);
    ScreenAction action = press_play(lists, KEY_GESTURE_PRESS);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    BrowseScreen *tracks = action.target->context;
    tracks->screen.enter(tracks, 0);
    CHECK_EQUAL(BROWSE_TRACK_LIST, tracks->query.kind);
    CHECK_EQUAL(3, tracks->count);
    CHECK(strcmp("Playlist 1", tracks->title) == 0);
    CHECK(!tracks->model.opens(tracks, 0));
}

static void a_removed_track_leaves_the_open_playlist(void)
{
    opened(browse_root_query(BROWSE_PLAYLISTS));
    int slot = playlist_with_tracks(3);
    Screen *screen = browse_pool_open(&pool, 1, user_playlists_query(slot, false));
    BrowseScreen *tracks = screen->context;
    screen->enter(tracks, 0);
    list_view_select(&tracks->list, 2, 0);
    CHECK(user_playlists_remove(&playlists, slot, 0));
    CHECK(user_playlists_remove(&playlists, slot, 0));
    screen->enter(tracks, 0);
    CHECK_EQUAL(1, tracks->count);
    CHECK_EQUAL(0, tracks->list.selected);
}

static void deleting_a_playlist_takes_its_row_away(void)
{
    BrowseScreen *lists = opened(browse_root_query(BROWSE_PLAYLISTS));
    uint32_t card_lists = lists->count;
    playlist_with_tracks(1);
    lists->screen.enter(lists, 0);
    list_view_select(&lists->list, 0, 0);
    ScreenAction action = press_play(lists, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    CHECK(action.target == &pool.song_actions.menu.screen);
    CHECK_EQUAL(2, pool.song_actions.menu.config.item_count);
    CHECK(strcmp("Export to card", pool.song_actions.items[0].label) == 0);
    CHECK(strcmp("Delete playlist", pool.song_actions.items[1].label) == 0);
    action = pool.song_actions.items[1].select(pool.song_actions.items[1].context);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
    lists->screen.enter(lists, 0);
    CHECK_EQUAL(card_lists, lists->count);
    CHECK_EQUAL(0, user_playlists_count(&playlists));
}

static BrowseScreen *opened_with_player(BrowseQuery query)
{
    opened(query);
    player_init(&player, pool.config.library);
    pool.config.player = &player;
    Screen *screen = browse_pool_open(&pool, 0, query);
    screen->enter(screen->context, 0);
    return screen->context;
}

static ScreenAction choose_action(int item)
{
    MenuItem *chosen = &pool.song_actions.items[item];
    return chosen->select(chosen->context);
}

static BrowseQuery first_album(void)
{
    return (BrowseQuery){.kind = BROWSE_ALBUM_TRACKS,
                         .group = LIBRARY_ARTISTS,
                         .subject = 0,
                         .artist = LIBRARY_NONE};
}

static uint32_t expected_export(int tracks, char *text, uint32_t capacity)
{
    uint32_t length = (uint32_t)strlen(PLAYLIST_WRITER_HEADER);
    memcpy(text, PLAYLIST_WRITER_HEADER, length);
    for (int track = 0; track < tracks; track++) {
        length += playlist_writer_entry(&fixture.library, (uint16_t)track, "../", text + length,
                                        capacity - length);
    }
    text[length] = '\0';
    return length;
}

static ScreenAction export_from_menu(BrowseScreen *lists)
{
    lists->screen.enter(lists, 0);
    list_view_select(&lists->list, 0, 0);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, press_play(lists, KEY_GESTURE_LONG_PRESS).type);
    CHECK(strcmp("Export to card", pool.song_actions.items[0].label) == 0);
    return pool.song_actions.items[0].select(pool.song_actions.items[0].context);
}

static void exporting_writes_an_m3u8_file_to_the_card(void)
{
    BrowseScreen *lists = opened(browse_root_query(BROWSE_PLAYLISTS));
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    playlist_with_tracks(3);
    CHECK_EQUAL(SCREEN_ACTION_POP, export_from_menu(lists).type);
    char expected[4096];
    uint32_t length = expected_export(3, expected, sizeof expected);
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_file_bytes("/EchoPod Playlists/Playlist 1.m3u8", &size);
    CHECK(bytes != NULL);
    CHECK_EQUAL(length, size);
    CHECK(memcmp(expected, bytes, length) == 0);
    CHECK(strcmp("Added to EchoPod Playlists", toast.text) == 0);
}

static void a_shorter_export_blanks_the_old_tail(void)
{
    BrowseScreen *lists = opened(browse_root_query(BROWSE_PLAYLISTS));
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    int slot = playlist_with_tracks(3);
    export_from_menu(lists);
    uint32_t longer = 0;
    fake_hal_file_bytes("/EchoPod Playlists/Playlist 1.m3u8", &longer);
    CHECK(user_playlists_remove(&playlists, slot, 2));
    CHECK(user_playlists_export(&playlists, slot));
    char expected[4096];
    uint32_t length = expected_export(2, expected, sizeof expected);
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_file_bytes("/EchoPod Playlists/Playlist 1.m3u8", &size);
    CHECK(length < longer);
    CHECK_EQUAL(longer, size);
    CHECK(memcmp(expected, bytes, length) == 0);
    for (uint32_t at = length; at < size; at++) {
        CHECK_EQUAL('\n', bytes[at]);
    }
}

static void an_export_the_card_refuses_says_so(void)
{
    BrowseScreen *lists = opened(browse_root_query(BROWSE_PLAYLISTS));
    fake_hal_set_reserve_result(HAL_RESERVE_NO_SPACE);
    playlist_with_tracks(1);
    export_from_menu(lists);
    uint32_t size = 0;
    CHECK(fake_hal_file_bytes("/EchoPod Playlists/Playlist 1.m3u8", &size) == NULL);
}

static void holding_a_song_adds_it_to_a_new_then_an_existing_playlist(void)
{
    BrowseScreen *album = opened_with_player(first_album());
    list_view_select(&album->list, album->extra, 0);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, press_play(album, KEY_GESTURE_LONG_PRESS).type);
    CHECK_EQUAL(3, pool.song_actions.menu.config.item_count);
    CHECK(strcmp("Add to playlist", pool.song_actions.items[2].label) == 0);
    CHECK_EQUAL(SCREEN_ACTION_NONE, choose_action(2).type);
    CHECK_EQUAL(1, pool.song_actions.menu.config.item_count);
    CHECK(strcmp("New playlist", pool.song_actions.items[0].label) == 0);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(0).type);
    CHECK_EQUAL(1, user_playlists_count(&playlists));

    list_view_select(&album->list, album->extra + 1, 0);
    press_play(album, KEY_GESTURE_LONG_PRESS);
    choose_action(2);
    CHECK_EQUAL(2, pool.song_actions.menu.config.item_count);
    CHECK(strcmp("Playlist 1", pool.song_actions.items[1].label) == 0);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(1).type);
    CHECK_EQUAL(1, user_playlists_count(&playlists));
    CHECK_EQUAL(2, browse_count(pool.config.library, user_playlists_query(0, false)));
}

static void a_song_held_inside_a_playlist_can_be_removed(void)
{
    opened_with_player(browse_root_query(BROWSE_PLAYLISTS));
    int slot = playlist_with_tracks(3);
    Screen *screen = browse_pool_open(&pool, 1, user_playlists_query(slot, false));
    BrowseScreen *tracks = screen->context;
    screen->enter(tracks, 0);
    CHECK_EQUAL(1, tracks->extra);
    list_view_select(&tracks->list, tracks->extra + 1, 0);
    press_play(tracks, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(4, pool.song_actions.menu.config.item_count);
    CHECK(strcmp("Remove from playlist", pool.song_actions.items[3].label) == 0);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(3).type);
    screen->enter(tracks, 0);
    CHECK_EQUAL(1 + 2, tracks->count);
    uint16_t track = 0;
    CHECK(pool.config.library->track_lists->track(pool.config.library->track_lists->context,
                                                  (uint16_t)slot, 1, &track));
    CHECK_EQUAL(2, track);
}

static void playing_a_playlist_survives_editing_it(void)
{
    opened_with_player(browse_root_query(BROWSE_PLAYLISTS));
    int slot = playlist_with_tracks(3);
    Screen *screen = browse_pool_open(&pool, 1, user_playlists_query(slot, false));
    BrowseScreen *tracks = screen->context;
    screen->enter(tracks, 0);
    list_view_select(&tracks->list, tracks->extra + 2, 0);
    press_play(tracks, KEY_GESTURE_PRESS);
    CHECK_EQUAL(2, player_status(&player).track);
    CHECK(user_playlists_delete(&playlists, slot));
    BrowseQuery pinned = user_playlists_query(slot, true);
    CHECK_EQUAL(3, browse_count(pool.config.library, pinned));
}

static void a_playing_playlist_resumes_after_a_restart(void)
{
    opened_with_player(browse_root_query(BROWSE_PLAYLISTS));
    int slot = playlist_with_tracks(3);
    Screen *screen = browse_pool_open(&pool, 1, user_playlists_query(slot, false));
    BrowseScreen *tracks = screen->context;
    screen->enter(tracks, 0);
    list_view_select(&tracks->list, tracks->extra + 1, 0);
    press_play(tracks, KEY_GESTURE_PRESS);
    CHECK(player_save(&player));
    Library *library = &fixture.library;
    user_playlists_load(&playlists, library);
    Player resumed;
    player_init(&resumed, library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(1, player_status(&resumed).track);
    player_skip(&resumed, 1);
    CHECK_EQUAL(2, player_status(&resumed).track);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(albums_are_lettered_by_their_sort_key),
        TEST_CASE(lettered_lists_never_go_backwards),
        TEST_CASE(album_tracks_have_no_letters),
        TEST_CASE(scrolling_one_row_at_a_time_keeps_every_row_right),
        TEST_CASE(a_jump_longer_than_the_window_reloads_every_row),
        TEST_CASE(scrolling_one_row_loads_only_the_new_row),
        TEST_CASE(coming_back_to_a_list_reads_nothing),
        TEST_CASE(a_favorite_added_elsewhere_shows_when_the_list_comes_back),
        TEST_CASE(opening_another_list_at_the_same_level_shows_its_own_rows),
        TEST_CASE(playlists_made_on_the_player_come_first),
        TEST_CASE(a_player_playlist_opens_as_its_tracks),
        TEST_CASE(a_removed_track_leaves_the_open_playlist),
        TEST_CASE(deleting_a_playlist_takes_its_row_away),
        TEST_CASE(holding_a_song_adds_it_to_a_new_then_an_existing_playlist),
        TEST_CASE(a_song_held_inside_a_playlist_can_be_removed),
        TEST_CASE(playing_a_playlist_survives_editing_it),
        TEST_CASE(a_playing_playlist_resumes_after_a_restart),
        TEST_CASE(exporting_writes_an_m3u8_file_to_the_card),
        TEST_CASE(a_shorter_export_blanks_the_old_tail),
        TEST_CASE(an_export_the_card_refuses_says_so),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
