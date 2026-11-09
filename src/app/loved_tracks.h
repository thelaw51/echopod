#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library/library.h"
#include "library/library_browse.h"
#include "library/user_playlist.h"

extern const char LOVED_TRACKS_PATH[];

enum {
    LOVED_TRACKS_LIST = 0x4000,
    LOVED_TRACKS_PINNED = 0x8000,
    LOVED_TRACKS_NUMBER = 1
};

typedef enum LovedChange {
    LOVED_ADDED,
    LOVED_REMOVED,
    LOVED_FULL,
    LOVED_FAILED
} LovedChange;

typedef struct LovedTracks {
    Library *library;
    LibraryTrackLists lists;
    const LibraryTrackLists *others;
    UserPlaylist loved;
    uint32_t checked_fingerprint;
    bool checked;
    uint16_t playing[USER_PLAYLIST_TRACKS];
    uint16_t playing_count;
    bool pinned;
    uint32_t revision;
} LovedTracks;

void loved_tracks_load(LovedTracks *store, Library *library);
uint16_t loved_tracks_count(LovedTracks *store);
bool loved_tracks_contains(LovedTracks *store, uint16_t track);
LovedChange loved_tracks_toggle(LovedTracks *store, uint16_t track);
void loved_tracks_pin(LovedTracks *store);
BrowseQuery loved_tracks_query(bool pinned);
bool loved_tracks_is_list(BrowseQuery query);
const char *loved_tracks_message(LovedChange change, char *buffer, size_t capacity);
