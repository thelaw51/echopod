#include "smart_lists.h"

#include "hal/audio.h"
#include "hal/system.h"
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

static void resolve(SmartLists *smart, SmartKind kind, SmartEntry *entries, uint16_t count)
{
    const Library *library = smart->library;
    uint32_t fingerprint = library_fingerprint(library);
    UserPlaylistPause pause = {keep_audio_fed, NULL};
    if (kind == SMART_RECENTLY_ADDED) {
        uint32_t stored = smart->walked[kind];
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
    if (walk) {
        smart->walked[kind] = fingerprint;
    }
}

static void build(SmartLists *smart, SmartKind kind, SmartTracks *target)
{
    SmartEntry entries[SMART_LIST_TRACKS];
    SmartRank rank;
    smart_rank_init(&rank, entries, SMART_LIST_TRACKS);
    if (kind == SMART_RECENTLY_ADDED) {
        smart_store_load_added(&rank, &smart->walked[kind]);
    } else {
        smart_store_rank_plays(&rank, kind == SMART_MOST_PLAYED);
    }
    resolve(smart, kind, entries, rank.count);
    target->count = smart_list_tracks(entries, rank.count, target->tracks);
    target->kind = (uint8_t)kind;
    target->valid = true;
    target->fingerprint = library_fingerprint(smart->library);
    target->revision = smart_store_revision();
}

static bool current(const SmartLists *smart, const SmartTracks *tracks, SmartKind kind)
{
    return tracks->valid && tracks->kind == kind &&
           tracks->fingerprint == library_fingerprint(smart->library) &&
           tracks->revision == smart_store_revision();
}

static const SmartTracks *tracks_for(SmartLists *smart, SmartKind kind, bool pinned)
{
    if (pinned) {
        if (!smart->playing.valid || smart->playing.kind != kind) {
            build(smart, kind, &smart->playing);
        }
        return &smart->playing;
    }
    if (!current(smart, &smart->shown, kind)) {
        build(smart, kind, &smart->shown);
    }
    return &smart->shown;
}

static uint32_t list_count(void *context, uint16_t list)
{
    SmartLists *smart = context;
    SmartKind kind;
    bool pinned = false;
    if (!owned(list, &kind, &pinned)) {
        return smart->next != NULL ? smart->next->count(smart->next->context, list) : 0;
    }
    return tracks_for(smart, kind, pinned)->count;
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
    const SmartTracks *tracks = tracks_for(smart, kind, pinned);
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
    smart->shown.valid = false;
    smart->playing.valid = false;
    for (int kind = 0; kind < SMART_KIND_COUNT; kind++) {
        smart->walked[kind] = 0;
    }
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
    smart->playing = smart->shown;
    if (!current(smart, &smart->playing, kind)) {
        build(smart, kind, &smart->playing);
    }
    return smart_lists_query(kind, true);
}

StringId smart_lists_title(BrowseQuery query)
{
    SmartKind kind;
    bool pinned = false;
    return owned(query.subject, &kind, &pinned) ? TITLES[kind] : STR_MENU_PLAYLISTS;
}
