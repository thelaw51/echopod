#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/user_playlist.h"

void pinned_tracks_take(const void *owner, int key, const UserPlaylist *playlist);
void pinned_tracks_copy(const void *owner, int key, const uint16_t *tracks, uint16_t count);
bool pinned_tracks_held(const void *owner, int key);
void pinned_tracks_release(const void *owner);
uint16_t pinned_tracks_count(void);
bool pinned_tracks_at(uint32_t position, uint16_t *track);
