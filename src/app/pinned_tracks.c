#include "pinned_tracks.h"

#include <stddef.h>

static struct {
    const void *owner;
    int key;
    uint16_t count;
    uint16_t tracks[USER_PLAYLIST_TRACKS];
} pinned;

void pinned_tracks_take(const void *owner, int key, const UserPlaylist *playlist)
{
    pinned.count = user_playlist_playable(playlist, pinned.tracks, USER_PLAYLIST_TRACKS);
    pinned.owner = owner;
    pinned.key = key;
}

void pinned_tracks_copy(const void *owner, int key, const uint16_t *tracks, uint16_t count)
{
    pinned.count = count < USER_PLAYLIST_TRACKS ? count : USER_PLAYLIST_TRACKS;
    for (uint16_t index = 0; index < pinned.count; index++) {
        pinned.tracks[index] = tracks[index];
    }
    pinned.owner = owner;
    pinned.key = key;
}

bool pinned_tracks_held(const void *owner, int key)
{
    return owner != NULL && pinned.owner == owner && pinned.key == key;
}

void pinned_tracks_release(const void *owner)
{
    if (owner != NULL && pinned.owner == owner) {
        pinned.owner = NULL;
        pinned.count = 0;
    }
}

uint16_t pinned_tracks_count(void)
{
    return pinned.count;
}

bool pinned_tracks_at(uint32_t position, uint16_t *track)
{
    if (position >= pinned.count) {
        return false;
    }
    *track = pinned.tracks[position];
    return true;
}
