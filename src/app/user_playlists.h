#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library.h"
#include "library/library_browse.h"
#include "library/user_playlist.h"

extern const char USER_PLAYLISTS_PATH[];

enum {
    USER_PLAYLISTS_PINNED = 0x8000,
    USER_PLAYLISTS_NO_SLOT = -1
};

typedef enum UserPlaylistsResult {
    USER_PLAYLISTS_ADDED,
    USER_PLAYLISTS_FULL,
    USER_PLAYLISTS_FAILED
} UserPlaylistsResult;

typedef struct UserPlaylists {
    Library *library;
    LibraryTrackLists lists;
    uint16_t numbers[USER_PLAYLIST_SLOTS];
    uint32_t revision;
    UserPlaylist loaded;
    int loaded_slot;
    uint16_t playing[USER_PLAYLIST_TRACKS];
    uint16_t playing_count;
    int playing_slot;
} UserPlaylists;

void user_playlists_load(UserPlaylists *store, Library *library);
uint16_t user_playlists_count(const UserPlaylists *store);
int user_playlists_slot_at(const UserPlaylists *store, uint16_t index);
uint16_t user_playlists_number(const UserPlaylists *store, int slot);
int user_playlists_create(UserPlaylists *store);
int user_playlists_create_from(UserPlaylists *store, const uint16_t *tracks, uint16_t count);
UserPlaylistsResult user_playlists_add_track(UserPlaylists *store, int slot, uint16_t track);
bool user_playlists_remove(UserPlaylists *store, int slot, uint32_t position);
bool user_playlists_delete(UserPlaylists *store, int slot);
void user_playlists_export_folder(char *buffer, uint32_t capacity);
void user_playlists_export_path(uint16_t number, char *buffer, uint32_t capacity);
bool user_playlists_export(UserPlaylists *store, int slot);
void user_playlists_pin(UserPlaylists *store, int slot);
BrowseQuery user_playlists_query(int slot, bool pinned);
