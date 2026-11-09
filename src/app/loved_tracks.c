#include "loved_tracks.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/audio.h"
#include "hal/storage.h"
#include "hal/system.h"

const char LOVED_TRACKS_PATH[] = "/.echo/loved.bin";

static bool read_loved(UserPlaylist *loved)
{
    HalFile file;
    uint8_t bytes[USER_PLAYLIST_SLOT_BYTES];
    if (!hal_file_open(&file, LOVED_TRACKS_PATH)) {
        return false;
    }
    bool read = file.size >= USER_PLAYLIST_SLOT_BYTES &&
                hal_file_read(file, 0, bytes, USER_PLAYLIST_SLOT_BYTES);
    hal_file_close(&file);
    return read && user_playlist_decode(bytes, USER_PLAYLIST_SLOT_BYTES, loved) &&
           loved->number == LOVED_TRACKS_NUMBER;
}

static bool write_loved(const UserPlaylist *loved)
{
    uint8_t bytes[USER_PLAYLIST_SLOT_BYTES];
    user_playlist_encode(loved, bytes);
    return hal_file_write(LOVED_TRACKS_PATH, bytes, USER_PLAYLIST_SLOT_BYTES);
}

static void keep_audio_fed(void *context)
{
    (void)context;
    hal_audio_service();
    hal_system_keep_alive();
}

static bool checked_now(const LovedTracks *store)
{
    return store->checked && store->checked_fingerprint == library_fingerprint(store->library);
}

static void ensure_checked(LovedTracks *store)
{
    if (checked_now(store)) {
        return;
    }
    if (!user_playlist_is_current(&store->loved, store->library)) {
        user_playlist_resolve(&store->loved, store->library,
                              (UserPlaylistPause){keep_audio_fed, NULL});
        (void)write_loved(&store->loved);
    }
    store->checked = true;
    store->checked_fingerprint = store->loved.fingerprint;
}

static int find_key(const UserPlaylist *loved, uint32_t key)
{
    for (uint16_t index = 0; index < loved->count; index++) {
        if (loved->keys[index] == key) {
            return index;
        }
    }
    return -1;
}

static bool playable_at(const UserPlaylist *loved, uint32_t position, uint16_t *track)
{
    uint32_t seen = 0;
    for (uint16_t index = 0; index < loved->count; index++) {
        if (loved->tracks[index] == USER_PLAYLIST_NO_TRACK) {
            continue;
        }
        if (seen++ == position) {
            *track = loved->tracks[index];
            return true;
        }
    }
    return false;
}

static bool owns(uint16_t list)
{
    return (list & ~LOVED_TRACKS_PINNED) == LOVED_TRACKS_LIST;
}

static uint32_t list_count(void *context, uint16_t list)
{
    LovedTracks *store = context;
    if (!owns(list)) {
        const LibraryTrackLists *others = store->others;
        return others != NULL ? others->count(others->context, list) : 0;
    }
    if ((list & LOVED_TRACKS_PINNED) != 0) {
        if (!store->pinned) {
            loved_tracks_pin(store);
        }
        return store->playing_count;
    }
    return loved_tracks_count(store);
}

static bool list_track(void *context, uint16_t list, uint32_t position, uint16_t *track)
{
    LovedTracks *store = context;
    if (!owns(list)) {
        const LibraryTrackLists *others = store->others;
        return others != NULL && others->track(others->context, list, position, track);
    }
    if ((list & LOVED_TRACKS_PINNED) != 0) {
        if (!store->pinned) {
            loved_tracks_pin(store);
        }
        if (position >= store->playing_count) {
            return false;
        }
        *track = store->playing[position];
        return true;
    }
    ensure_checked(store);
    return playable_at(&store->loved, position, track);
}

void loved_tracks_load(LovedTracks *store, Library *library)
{
    store->library = library;
    store->lists = (LibraryTrackLists){.count = list_count, .track = list_track, .context = store};
    if (library->track_lists != &store->lists) {
        store->others = library->track_lists;
    }
    library->track_lists = &store->lists;
    store->checked = false;
    store->pinned = false;
    store->playing_count = 0;
    store->revision++;
    if (!read_loved(&store->loved)) {
        user_playlist_clear(&store->loved, LOVED_TRACKS_NUMBER);
        store->loved.fingerprint = library_fingerprint(library);
        store->checked = true;
        store->checked_fingerprint = store->loved.fingerprint;
    }
}

uint16_t loved_tracks_count(LovedTracks *store)
{
    ensure_checked(store);
    uint16_t count = 0;
    for (uint16_t index = 0; index < store->loved.count; index++) {
        if (store->loved.tracks[index] != USER_PLAYLIST_NO_TRACK) {
            count++;
        }
    }
    return count;
}

bool loved_tracks_contains(LovedTracks *store, uint16_t track)
{
    if (store->loved.count == 0) {
        return false;
    }
    if (checked_now(store)) {
        for (uint16_t index = 0; index < store->loved.count; index++) {
            if (store->loved.tracks[index] == track) {
                return true;
            }
        }
        return false;
    }
    uint32_t key = 0;
    return user_playlist_track_key(store->library, track, &key) &&
           find_key(&store->loved, key) >= 0;
}

LovedChange loved_tracks_toggle(LovedTracks *store, uint16_t track)
{
    uint32_t key = 0;
    if (!user_playlist_track_key(store->library, track, &key)) {
        return LOVED_FAILED;
    }
    int position = find_key(&store->loved, key);
    LovedChange change = LOVED_ADDED;
    if (position >= 0) {
        user_playlist_remove_at(&store->loved, (uint16_t)position);
        change = LOVED_REMOVED;
    } else if (!user_playlist_add(&store->loved, key, track)) {
        return LOVED_FULL;
    }
    store->revision++;
    return write_loved(&store->loved) ? change : LOVED_FAILED;
}

void loved_tracks_pin(LovedTracks *store)
{
    ensure_checked(store);
    store->playing_count =
        user_playlist_playable(&store->loved, store->playing, USER_PLAYLIST_TRACKS);
    store->pinned = true;
}

BrowseQuery loved_tracks_query(bool pinned)
{
    uint16_t list = LOVED_TRACKS_LIST | (pinned ? LOVED_TRACKS_PINNED : 0);
    return (BrowseQuery){.kind = BROWSE_TRACK_LIST,
                         .group = LIBRARY_ARTISTS,
                         .subject = list,
                         .artist = LIBRARY_NONE};
}

bool loved_tracks_is_list(BrowseQuery query)
{
    return query.kind == BROWSE_TRACK_LIST && owns(query.subject);
}

const char *loved_tracks_message(LovedChange change, char *buffer, size_t capacity)
{
    const char *arguments[] = {i18n_text(STR_MENU_LOVED_TRACKS)};
    StringBuilder builder = string_builder_make(buffer, capacity);
    switch (change) {
    case LOVED_ADDED:
        i18n_format(&builder, STR_MENU_ADDED_TO_PLAYLIST, arguments, 1);
        return buffer;
    case LOVED_REMOVED:
        i18n_format(&builder, STR_MENU_REMOVED_FROM, arguments, 1);
        return buffer;
    case LOVED_FULL:
        return i18n_text(STR_MENU_PLAYLIST_FULL);
    case LOVED_FAILED:
        break;
    }
    return i18n_text(STR_MENU_PLAYLIST_SAVE_FAILED);
}
