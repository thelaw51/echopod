#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library.h"

enum {
    USER_PLAYLIST_SLOTS = 16,
    USER_PLAYLIST_TRACKS = 200,
    USER_PLAYLIST_HEADER_BYTES = 16,
    USER_PLAYLIST_ENTRY_BYTES = 6,
    USER_PLAYLIST_SLOT_BYTES = USER_PLAYLIST_HEADER_BYTES +
        USER_PLAYLIST_TRACKS * USER_PLAYLIST_ENTRY_BYTES + 4,
    USER_PLAYLIST_FILE_BYTES = USER_PLAYLIST_SLOTS * USER_PLAYLIST_SLOT_BYTES,
    USER_PLAYLIST_NO_TRACK = 0xFFFF,
    USER_PLAYLIST_TRACKS_PER_PAUSE = 32
};

typedef struct UserPlaylist {
    uint16_t number;
    uint16_t count;
    uint32_t fingerprint;
    uint32_t keys[USER_PLAYLIST_TRACKS];
    uint16_t tracks[USER_PLAYLIST_TRACKS];
} UserPlaylist;

typedef struct UserPlaylistPause {
    void (*between_tracks)(void *context);
    void *context;
} UserPlaylistPause;

void user_playlist_clear(UserPlaylist *playlist, uint16_t number);
bool user_playlist_add(UserPlaylist *playlist, uint32_t key, uint16_t track);
bool user_playlist_remove_at(UserPlaylist *playlist, uint16_t position);
void user_playlist_encode(const UserPlaylist *playlist, uint8_t *bytes);
bool user_playlist_decode(const uint8_t *bytes, uint32_t length, UserPlaylist *playlist);
bool user_playlist_track_key(const Library *library, uint16_t track, uint32_t *key);
bool user_playlist_is_current(const UserPlaylist *playlist, const Library *library);
void user_playlist_resolve(UserPlaylist *playlist, const Library *library, UserPlaylistPause pause);
uint16_t user_playlist_playable(const UserPlaylist *playlist, uint16_t *tracks, uint16_t capacity);
