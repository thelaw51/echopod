#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/i18n.h"
#include "library/library.h"
#include "library/library_browse.h"
#include "library/smart_list.h"

typedef enum SmartKind {
    SMART_MOST_PLAYED,
    SMART_RECENTLY_PLAYED,
    SMART_RECENTLY_ADDED,
    SMART_KIND_COUNT
} SmartKind;

enum {
    SMART_LISTS_FIRST_ID = 0x0100,
    SMART_LISTS_PINNED = 0x8000
};

typedef struct SmartTracks {
    uint16_t tracks[SMART_LIST_TRACKS];
    uint16_t count;
    uint8_t kind;
    bool valid;
    uint32_t fingerprint;
    uint32_t revision;
} SmartTracks;

typedef struct SmartLists {
    Library *library;
    LibraryTrackLists lists;
    const LibraryTrackLists *next;
    SmartTracks shown;
    SmartTracks playing;
    uint32_t walked[SMART_KIND_COUNT];
} SmartLists;

void smart_lists_load(SmartLists *smart, Library *library);
bool smart_lists_owns(BrowseQuery query);
BrowseQuery smart_lists_query(SmartKind kind, bool pinned);
BrowseQuery smart_lists_pin(SmartLists *smart, BrowseQuery query);
StringId smart_lists_title(BrowseQuery query);
