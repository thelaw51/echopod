#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/i18n.h"
#include "library/library.h"
#include "library/library_browse.h"
#include "library/play_log.h"

extern const char PLAY_LISTS_PATH[];

enum {
    PLAY_LISTS_NONE = 0,
    PLAY_LISTS_RECENT = 0x2001,
    PLAY_LISTS_MOST = 0x2002,
    PLAY_LISTS_PINNED = 0x8000
};

typedef struct PlayLists {
    Library *library;
    LibraryTrackLists lists;
    const LibraryTrackLists *others;
    uint16_t shown_list;
    uint16_t shown_count;
    uint16_t tracks[PLAY_LOG_LIST_TRACKS];
} PlayLists;

void play_lists_load(PlayLists *store, Library *library);
void play_lists_count(void *store, uint16_t track);
bool play_lists_show(PlayLists *store, uint16_t list, bool fresh);
void play_lists_pin(PlayLists *store, uint16_t list);
BrowseQuery play_lists_query(uint16_t list, bool pinned);
uint16_t play_lists_list_of(BrowseQuery query);
StringId play_lists_title(uint16_t list);
