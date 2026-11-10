#include "play_lists.h"

#include <stddef.h>

#include "hal/audio.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "pinned_tracks.h"

const char PLAY_LISTS_PATH[] = "/.echo/plays.bin";

static void keep_audio_fed(void *context)
{
    (void)context;
    hal_audio_service();
    hal_system_keep_alive();
}

static bool read_copy(HalFile file, uint32_t copy, uint8_t *log)
{
    uint32_t offset = copy * PLAY_LOG_COPY_STRIDE;
    return file.size >= offset + PLAY_LOG_BYTES &&
           hal_file_read(file, offset, log, PLAY_LOG_BYTES) && play_log_valid(log);
}

static bool read_newest(HalFile file, uint8_t *log)
{
    bool first = read_copy(file, 0, log);
    uint32_t first_generation = first ? play_log_generation(log) : 0;
    if (read_copy(file, 1, log) && (!first || play_log_generation(log) > first_generation)) {
        return true;
    }
    return first && read_copy(file, 0, log);
}

static void read_log(const Library *library, uint8_t *log)
{
    HalFile file;
    bool opened = hal_file_open(&file, PLAY_LISTS_PATH);
    bool read = opened && read_newest(file, log);
    if (opened) {
        hal_file_close(&file);
    }
    if (!read) {
        play_log_clear(log, library_fingerprint(library));
    }
}

static void write_log(uint8_t *log)
{
    uint32_t generation = play_log_generation(log) + 1;
    play_log_seal(log, generation);
    (void)hal_file_write_at(PLAY_LISTS_PATH, generation % 2 * PLAY_LOG_COPY_STRIDE, log,
                            PLAY_LOG_BYTES);
}

static bool resolve_if_stale(const Library *library, uint8_t *log)
{
    if (play_log_fingerprint(log) == library_fingerprint(library)) {
        return false;
    }
    play_log_resolve(log, library, (UserPlaylistPause){keep_audio_fed, NULL});
    return true;
}

void play_lists_count(void *context, uint16_t track)
{
    PlayLists *store = context;
    uint8_t log[PLAY_LOG_BYTES];
    uint32_t key = 0;
    if (store->library == NULL || !user_playlist_track_key(store->library, track, &key)) {
        return;
    }
    read_log(store->library, log);
    resolve_if_stale(store->library, log);
    play_log_add(log, key, track);
    write_log(log);
}

bool play_lists_show(PlayLists *store, uint16_t list, bool fresh)
{
    if (store->shown_list == list && !fresh) {
        return false;
    }
    uint8_t log[PLAY_LOG_BYTES];
    read_log(store->library, log);
    if (resolve_if_stale(store->library, log)) {
        write_log(log);
    }
    PlayLogOrder order = list == PLAY_LISTS_MOST ? PLAY_LOG_MOST : PLAY_LOG_RECENT;
    store->shown_count = play_log_list(log, order, store->tracks, PLAY_LOG_LIST_TRACKS);
    store->shown_list = list;
    return true;
}

static uint16_t list_id(uint16_t list)
{
    return (uint16_t)(list & ~PLAY_LISTS_PINNED);
}

static bool owns(uint16_t list)
{
    return list_id(list) == PLAY_LISTS_RECENT || list_id(list) == PLAY_LISTS_MOST;
}

void play_lists_pin(PlayLists *store, uint16_t list)
{
    (void)play_lists_show(store, list_id(list), false);
    pinned_tracks_take_list(store, list_id(list), store->tracks, store->shown_count);
}

static void hold_pinned(PlayLists *store, uint16_t list)
{
    if (!pinned_tracks_held(store, list_id(list))) {
        play_lists_pin(store, list);
    }
}

static uint32_t list_count(void *context, uint16_t list)
{
    PlayLists *store = context;
    if (!owns(list)) {
        const LibraryTrackLists *others = store->others;
        return others != NULL ? others->count(others->context, list) : 0;
    }
    if ((list & PLAY_LISTS_PINNED) != 0) {
        hold_pinned(store, list);
        return pinned_tracks_count();
    }
    (void)play_lists_show(store, list_id(list), false);
    return store->shown_count;
}

static bool list_track(void *context, uint16_t list, uint32_t position, uint16_t *track)
{
    PlayLists *store = context;
    if (!owns(list)) {
        const LibraryTrackLists *others = store->others;
        return others != NULL && others->track(others->context, list, position, track);
    }
    if ((list & PLAY_LISTS_PINNED) != 0) {
        hold_pinned(store, list);
        return pinned_tracks_at(position, track);
    }
    (void)play_lists_show(store, list_id(list), false);
    if (position >= store->shown_count) {
        return false;
    }
    *track = store->tracks[position];
    return true;
}

void play_lists_load(PlayLists *store, Library *library)
{
    store->library = library;
    store->lists = (LibraryTrackLists){.count = list_count, .track = list_track, .context = store};
    if (library->track_lists != &store->lists) {
        store->others = library->track_lists;
    }
    library->track_lists = &store->lists;
    store->shown_list = PLAY_LISTS_NONE;
    store->shown_count = 0;
    pinned_tracks_release(store);
}

BrowseQuery play_lists_query(uint16_t list, bool pinned)
{
    return (BrowseQuery){.kind = BROWSE_TRACK_LIST,
                         .group = LIBRARY_ARTISTS,
                         .subject = (uint16_t)(list | (pinned ? PLAY_LISTS_PINNED : 0)),
                         .artist = LIBRARY_NONE};
}

uint16_t play_lists_list_of(BrowseQuery query)
{
    return query.kind == BROWSE_TRACK_LIST && owns(query.subject) ? list_id(query.subject)
                                                                  : (uint16_t)PLAY_LISTS_NONE;
}

StringId play_lists_title(uint16_t list)
{
    return list == PLAY_LISTS_MOST ? STR_MENU_MOST_PLAYED : STR_MENU_RECENTLY_PLAYED;
}
