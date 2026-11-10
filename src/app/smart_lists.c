#include "smart_lists.h"

#include "hal/audio.h"
#include "hal/system.h"
#include "pinned_tracks.h"
#include "smart_store.h"

static const StringId TITLES[SMART_KIND_COUNT] = {STR_MENU_MOST_PLAYED, STR_MENU_RECENTLY_PLAYED,
                                                  STR_MENU_RECENTLY_ADDED};

static bool owned(uint16_t list, SmartKind *kind, bool *pinned)
{
    uint16_t id = (uint16_t)(list & ~SMART_LISTS_PINNED);
    if (id < SMART_LISTS_FIRST_ID || id >= SMART_LISTS_FIRST_ID + SMART_KIND_COUNT) {
        return false;
    }
    *kind = (SmartKind)(id - SMART_LISTS_FIRST_ID);
    *pinned = (list & SMART_LISTS_PINNED) != 0;
    return true;
}

static void keep_audio_fed(void *context)
{
    (void)context;
    hal_audio_service();
    hal_system_keep_alive();
}

static void resolve(SmartLists *smart, SmartKind kind, SmartEntry *entries, uint16_t count,
                    uint32_t stored)
{
    const Library *library = smart->library;
    uint32_t fingerprint = library_fingerprint(library);
    UserPlaylistPause pause = {keep_audio_fed, NULL};
    if (kind == SMART_RECENTLY_ADDED) {
        bool changed =
            smart_list_resolve(library, entries, count, stored != fingerprint, true, pause);
        if (changed || stored != fingerprint) {
            smart_store_keep_added_tracks(entries, count, fingerprint);
        }
        return;
    }
    bool walk = smart->walked[kind] != fingerprint;
    if (smart_list_resolve(library, entries, count, walk, false, pause)) {
        for (uint16_t index = 0; index < count; index++) {
            smart_store_set_play_track(entries[index].place, entries[index].key,
                                       entries[index].track);
        }
    }
    smart->walked[kind] = fingerprint;
}

static void build(SmartLists *smart, SmartKind kind)
{
    SmartEntry entries[SMART_LIST_TRACKS];
    SmartRank rank;
    uint32_t stored = 0;
    smart_rank_init(&rank, entries, SMART_LIST_TRACKS);
    if (kind == SMART_RECENTLY_ADDED) {
        smart_store_load_added(&rank, &stored);
    } else {
        smart_store_rank_plays(&rank, kind == SMART_MOST_PLAYED);
    }
    resolve(smart, kind, entries, rank.count, stored);
    SmartTracks *shown = &smart->shown;
    shown->count = smart_list_tracks(entries, rank.count, shown->tracks);
    shown->kind = (uint8_t)kind;
    shown->fingerprint = library_fingerprint(smart->library);
    shown->revision = smart_store_revision();
}

static const SmartTracks *shown_for(SmartLists *smart, SmartKind kind)
{
    const SmartTracks *shown = &smart->shown;
    if (shown->kind != kind || shown->fingerprint != library_fingerprint(smart->library) ||
        shown->revision != smart_store_revision()) {
        build(smart, kind);
    }
    return shown;
}

static void pin(SmartLists *smart, SmartKind kind)
{
    const SmartTracks *shown = shown_for(smart, kind);
    pinned_tracks_copy(smart, (int)kind, shown->tracks, shown->count);
}

static void hold(SmartLists *smart, SmartKind kind)
{
    if (!pinned_tracks_held(smart, (int)kind)) {
        pin(smart, kind);
    }
}

static uint32_t list_count(void *context, uint16_t list)
{
    SmartLists *smart = context;
    SmartKind kind;
    bool pinned = false;
    if (!owned(list, &kind, &pinned)) {
        return smart->next != NULL ? smart->next->count(smart->next->context, list) : 0;
    }
    if (pinned) {
        hold(smart, kind);
        return pinned_tracks_count();
    }
    return shown_for(smart, kind)->count;
}

static bool list_track(void *context, uint16_t list, uint32_t position, uint16_t *track)
{
    SmartLists *smart = context;
    SmartKind kind;
    bool pinned = false;
    if (!owned(list, &kind, &pinned)) {
        return smart->next != NULL &&
               smart->next->track(smart->next->context, list, position, track);
    }
    if (pinned) {
        hold(smart, kind);
        return pinned_tracks_at(position, track);
    }
    const SmartTracks *tracks = shown_for(smart, kind);
    if (position >= tracks->count) {
        return false;
    }
    *track = tracks->tracks[position];
    return true;
}

void smart_lists_load(SmartLists *smart, Library *library)
{
    smart->library = library;
    smart->next = library->track_lists;
    smart->lists = (LibraryTrackLists){.count = list_count, .track = list_track, .context = smart};
    library->track_lists = &smart->lists;
    smart->shown.kind = SMART_KIND_COUNT;
    for (int kind = 0; kind < SMART_RECENTLY_ADDED; kind++) {
        smart->walked[kind] = 0;
    }
    pinned_tracks_release(smart);
}

bool smart_lists_owns(BrowseQuery query)
{
    SmartKind kind;
    bool pinned = false;
    return query.kind == BROWSE_TRACK_LIST && owned(query.subject, &kind, &pinned);
}

BrowseQuery smart_lists_query(SmartKind kind, bool pinned)
{
    uint16_t list = (uint16_t)(SMART_LISTS_FIRST_ID + kind) | (pinned ? SMART_LISTS_PINNED : 0);
    return (BrowseQuery){.kind = BROWSE_TRACK_LIST,
                         .group = LIBRARY_ARTISTS,
                         .subject = list,
                         .artist = LIBRARY_NONE};
}

BrowseQuery smart_lists_pin(SmartLists *smart, BrowseQuery query)
{
    SmartKind kind;
    bool pinned = false;
    if (!smart_lists_owns(query) || !owned(query.subject, &kind, &pinned)) {
        return query;
    }
    pin(smart, kind);
    return smart_lists_query(kind, true);
}

StringId smart_lists_title(BrowseQuery query)
{
    SmartKind kind;
    bool pinned = false;
    return owned(query.subject, &kind, &pinned) ? TITLES[kind] : STR_MENU_PLAYLISTS;
}
