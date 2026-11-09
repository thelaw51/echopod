#include "user_playlists.h"

#include <stddef.h>

#include "hal/audio.h"
#include "hal/storage.h"
#include "hal/system.h"

const char USER_PLAYLISTS_PATH[] = "/.echo/playlists.bin";

static uint32_t slot_offset(int slot)
{
    return (uint32_t)slot * USER_PLAYLIST_SLOT_BYTES;
}

static bool valid_slot(int slot)
{
    return slot >= 0 && slot < USER_PLAYLIST_SLOTS;
}

static bool read_slot(int slot, UserPlaylist *playlist)
{
    HalFile file;
    uint8_t slot_bytes[USER_PLAYLIST_SLOT_BYTES];
    user_playlist_clear(playlist, 0);
    if (!hal_file_open(&file, USER_PLAYLISTS_PATH)) {
        return false;
    }
    uint32_t offset = slot_offset(slot);
    bool read = file.size >= offset + USER_PLAYLIST_SLOT_BYTES &&
                hal_file_read(file, offset, slot_bytes, USER_PLAYLIST_SLOT_BYTES);
    hal_file_close(&file);
    return read && user_playlist_decode(slot_bytes, USER_PLAYLIST_SLOT_BYTES, playlist);
}

static bool write_slot(int slot, const UserPlaylist *playlist)
{
    uint8_t slot_bytes[USER_PLAYLIST_SLOT_BYTES];
    user_playlist_encode(playlist, slot_bytes);
    return hal_file_write_at(USER_PLAYLISTS_PATH, slot_offset(slot), slot_bytes,
                             USER_PLAYLIST_SLOT_BYTES);
}

static void keep_audio_fed(void *context)
{
    (void)context;
    hal_audio_service();
    hal_system_keep_alive();
}

static bool loaded_is_fresh(const UserPlaylists *store, int slot)
{
    return store->loaded_slot == slot &&
           store->loaded.fingerprint == library_fingerprint(store->library);
}

static bool ensure_loaded(UserPlaylists *store, int slot)
{
    if (!valid_slot(slot) || store->numbers[slot] == 0) {
        return false;
    }
    if (loaded_is_fresh(store, slot)) {
        return true;
    }
    store->loaded_slot = USER_PLAYLISTS_NO_SLOT;
    if (!read_slot(slot, &store->loaded) || store->loaded.number == 0) {
        return false;
    }
    if (!user_playlist_is_current(&store->loaded, store->library)) {
        user_playlist_resolve(&store->loaded, store->library,
                              (UserPlaylistPause){keep_audio_fed, NULL});
        (void)write_slot(slot, &store->loaded);
    }
    store->loaded_slot = slot;
    return true;
}

static bool playable_at(const UserPlaylist *playlist, uint32_t position, uint16_t *entry)
{
    uint32_t seen = 0;
    for (uint16_t index = 0; index < playlist->count; index++) {
        if (playlist->tracks[index] == USER_PLAYLIST_NO_TRACK) {
            continue;
        }
        if (seen++ == position) {
            *entry = index;
            return true;
        }
    }
    return false;
}

void user_playlists_pin(UserPlaylists *store, int slot)
{
    store->playing_slot = USER_PLAYLISTS_NO_SLOT;
    store->playing_count = 0;
    if (ensure_loaded(store, slot)) {
        store->playing_count =
            user_playlist_playable(&store->loaded, store->playing, USER_PLAYLIST_TRACKS);
        store->playing_slot = slot;
    }
}

static int slot_of(uint16_t list, bool *pinned)
{
    *pinned = (list & USER_PLAYLISTS_PINNED) != 0;
    return (int)(list & ~USER_PLAYLISTS_PINNED);
}

static uint32_t list_count(void *context, uint16_t list)
{
    UserPlaylists *store = context;
    bool pinned = false;
    int slot = slot_of(list, &pinned);
    if (pinned) {
        if (store->playing_slot != slot) {
            user_playlists_pin(store, slot);
        }
        return store->playing_slot == slot ? store->playing_count : 0;
    }
    if (!ensure_loaded(store, slot)) {
        return 0;
    }
    uint32_t count = 0;
    for (uint16_t index = 0; index < store->loaded.count; index++) {
        count += store->loaded.tracks[index] != USER_PLAYLIST_NO_TRACK ? 1 : 0;
    }
    return count;
}

static bool list_track(void *context, uint16_t list, uint32_t position, uint16_t *track)
{
    UserPlaylists *store = context;
    bool pinned = false;
    int slot = slot_of(list, &pinned);
    uint16_t entry = 0;
    if (pinned) {
        if (store->playing_slot != slot) {
            user_playlists_pin(store, slot);
        }
        if (store->playing_slot != slot || position >= store->playing_count) {
            return false;
        }
        *track = store->playing[position];
        return true;
    }
    if (!ensure_loaded(store, slot) || !playable_at(&store->loaded, position, &entry)) {
        return false;
    }
    *track = store->loaded.tracks[entry];
    return true;
}

void user_playlists_load(UserPlaylists *store, Library *library)
{
    store->library = library;
    store->lists = (LibraryTrackLists){.count = list_count, .track = list_track, .context = store};
    library->track_lists = &store->lists;
    store->loaded_slot = USER_PLAYLISTS_NO_SLOT;
    store->playing_slot = USER_PLAYLISTS_NO_SLOT;
    store->playing_count = 0;
    store->revision++;
    for (int slot = 0; slot < USER_PLAYLIST_SLOTS; slot++) {
        UserPlaylist *playlist = &store->loaded;
        store->numbers[slot] = read_slot(slot, playlist) ? playlist->number : 0;
    }
}

uint16_t user_playlists_count(const UserPlaylists *store)
{
    uint16_t count = 0;
    for (int slot = 0; slot < USER_PLAYLIST_SLOTS; slot++) {
        count += store->numbers[slot] != 0 ? 1 : 0;
    }
    return count;
}

int user_playlists_slot_at(const UserPlaylists *store, uint16_t index)
{
    uint16_t seen = 0;
    for (int slot = 0; slot < USER_PLAYLIST_SLOTS; slot++) {
        if (store->numbers[slot] != 0 && seen++ == index) {
            return slot;
        }
    }
    return USER_PLAYLISTS_NO_SLOT;
}

uint16_t user_playlists_number(const UserPlaylists *store, int slot)
{
    return valid_slot(slot) ? store->numbers[slot] : 0;
}

int user_playlists_create(UserPlaylists *store)
{
    int free_slot = USER_PLAYLISTS_NO_SLOT;
    uint16_t highest = 0;
    for (int slot = 0; slot < USER_PLAYLIST_SLOTS; slot++) {
        if (store->numbers[slot] == 0 && free_slot == USER_PLAYLISTS_NO_SLOT) {
            free_slot = slot;
        }
        highest = store->numbers[slot] > highest ? store->numbers[slot] : highest;
    }
    if (free_slot == USER_PLAYLISTS_NO_SLOT || highest >= USER_PLAYLIST_NO_TRACK - 1) {
        return USER_PLAYLISTS_NO_SLOT;
    }
    user_playlist_clear(&store->loaded, (uint16_t)(highest + 1));
    store->loaded.fingerprint = library_fingerprint(store->library);
    store->loaded_slot = USER_PLAYLISTS_NO_SLOT;
    if (!write_slot(free_slot, &store->loaded)) {
        return USER_PLAYLISTS_NO_SLOT;
    }
    store->numbers[free_slot] = store->loaded.number;
    store->loaded_slot = free_slot;
    store->revision++;
    return free_slot;
}

UserPlaylistsResult user_playlists_add_track(UserPlaylists *store, int slot, uint16_t track)
{
    uint32_t key = 0;
    if (!ensure_loaded(store, slot) || !user_playlist_track_key(store->library, track, &key)) {
        return USER_PLAYLISTS_FAILED;
    }
    if (!user_playlist_add(&store->loaded, key, track)) {
        return USER_PLAYLISTS_FULL;
    }
    store->revision++;
    if (!write_slot(slot, &store->loaded)) {
        store->loaded_slot = USER_PLAYLISTS_NO_SLOT;
        return USER_PLAYLISTS_FAILED;
    }
    return USER_PLAYLISTS_ADDED;
}

bool user_playlists_remove(UserPlaylists *store, int slot, uint32_t position)
{
    uint16_t entry = 0;
    if (!ensure_loaded(store, slot) || !playable_at(&store->loaded, position, &entry) ||
        !user_playlist_remove_at(&store->loaded, entry)) {
        return false;
    }
    store->revision++;
    if (!write_slot(slot, &store->loaded)) {
        store->loaded_slot = USER_PLAYLISTS_NO_SLOT;
        return false;
    }
    return true;
}

bool user_playlists_delete(UserPlaylists *store, int slot)
{
    if (!valid_slot(slot) || store->numbers[slot] == 0) {
        return false;
    }
    UserPlaylist *empty = &store->loaded;
    store->loaded_slot = USER_PLAYLISTS_NO_SLOT;
    user_playlist_clear(empty, 0);
    if (!write_slot(slot, empty)) {
        return false;
    }
    store->numbers[slot] = 0;
    store->revision++;
    return true;
}

BrowseQuery user_playlists_query(int slot, bool pinned)
{
    uint16_t list = (uint16_t)slot | (pinned ? USER_PLAYLISTS_PINNED : 0);
    return (BrowseQuery){.kind = BROWSE_TRACK_LIST,
                         .group = LIBRARY_ARTISTS,
                         .subject = list,
                         .artist = LIBRARY_NONE};
}
