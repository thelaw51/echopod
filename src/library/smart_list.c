#include "library/smart_list.h"

#include "core/hash.h"
#include "library/music_fingerprint.h"

enum {
    STAT_KEY = 0,
    STAT_PLAYS = 4,
    STAT_TRACK = 6,
    STAT_LAST = 8,
    STAT_CHECK = 12,
    ADDED_VERSION = 1,
    ADDED_VERSION_FIELD = 4,
    ADDED_COUNT_FIELD = 6,
    ADDED_FINGERPRINT_FIELD = 8,
    ADDED_KEY = 0,
    ADDED_MODIFIED = 4,
    ADDED_TRACK = 8,
    MOST_PLAYS = 0xFFFF,
    TRACKS_PER_PAUSE = 32
};

static const uint8_t ADDED_MAGIC[4] = {'E', 'M', 'A', 'D'};

static void put_u16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    put_u16(bytes, value & 0xFFFFu);
    put_u16(bytes + 2, value >> 16);
}

static uint16_t get_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static uint32_t get_u32(const uint8_t *bytes)
{
    return (uint32_t)get_u16(bytes) | (uint32_t)get_u16(bytes + 2) << 16;
}

void smart_rank_init(SmartRank *rank, SmartEntry *entries, uint16_t capacity)
{
    *rank = (SmartRank){.entries = entries, .capacity = capacity, .count = 0};
}

static bool ranks_above(SmartEntry entry, SmartEntry other)
{
    if (entry.primary != other.primary) {
        return entry.primary > other.primary;
    }
    return entry.secondary > other.secondary;
}

void smart_rank_offer(SmartRank *rank, SmartEntry entry)
{
    uint16_t position = rank->count;
    while (position > 0 && ranks_above(entry, rank->entries[position - 1])) {
        position--;
    }
    if (position >= rank->capacity) {
        return;
    }
    uint16_t last = rank->count < rank->capacity ? rank->count : (uint16_t)(rank->capacity - 1);
    for (uint16_t index = last; index > position; index--) {
        rank->entries[index] = rank->entries[index - 1];
    }
    rank->entries[position] = entry;
    if (rank->count < rank->capacity) {
        rank->count++;
    }
}

static bool key_at(const Library *library, uint16_t track, uint32_t *key)
{
    return track < library_music_count(library, LIBRARY_TRACKS) &&
           user_playlist_track_key(library, track, key);
}

static bool check_tracks(const Library *library, SmartEntry *entries, uint16_t count, bool *changed)
{
    bool all = true;
    for (uint16_t index = 0; index < count; index++) {
        uint32_t key = 0;
        SmartEntry *entry = &entries[index];
        if (entry->track == SMART_LIST_NO_TRACK) {
            all = false;
        } else if (!key_at(library, entry->track, &key) || key != entry->key) {
            entry->track = SMART_LIST_NO_TRACK;
            *changed = true;
            all = false;
        }
    }
    return all;
}

static void match_track(SmartEntry *entries, uint16_t count, uint32_t key, uint16_t track,
                        bool *changed)
{
    for (uint16_t index = 0; index < count; index++) {
        if (entries[index].key == key && entries[index].track == SMART_LIST_NO_TRACK) {
            entries[index].track = track;
            *changed = true;
        }
    }
}

bool smart_list_resolve(const Library *library, SmartEntry *entries, uint16_t count, bool walk,
                        UserPlaylistPause pause)
{
    bool changed = false;
    if (check_tracks(library, entries, count, &changed) || !walk) {
        return changed;
    }
    uint32_t tracks = library_music_count(library, LIBRARY_TRACKS);
    for (uint32_t track = 0; track < tracks; track++) {
        uint32_t key = 0;
        if (user_playlist_track_key(library, (uint16_t)track, &key)) {
            match_track(entries, count, key, (uint16_t)track, &changed);
        }
        if (pause.between_tracks != NULL && track % TRACKS_PER_PAUSE == 0) {
            pause.between_tracks(pause.context);
        }
    }
    return changed;
}

uint16_t smart_list_tracks(const SmartEntry *entries, uint16_t count, uint16_t *tracks)
{
    uint16_t found = 0;
    for (uint16_t index = 0; index < count; index++) {
        if (entries[index].track != SMART_LIST_NO_TRACK) {
            tracks[found++] = entries[index].track;
        }
    }
    return found;
}

static uint32_t stat_check(const uint8_t *bytes)
{
    return hash_update(HASH_START, bytes, STAT_CHECK);
}

void play_stat_encode(const PlayStat *stat, uint8_t *bytes)
{
    put_u32(bytes + STAT_KEY, stat->key);
    put_u16(bytes + STAT_PLAYS, stat->plays);
    put_u16(bytes + STAT_TRACK, stat->track);
    put_u32(bytes + STAT_LAST, stat->last);
    put_u32(bytes + STAT_CHECK, stat_check(bytes));
}

bool play_stat_decode(const uint8_t *bytes, PlayStat *stat)
{
    *stat = (PlayStat){.key = get_u32(bytes + STAT_KEY),
                       .plays = get_u16(bytes + STAT_PLAYS),
                       .track = get_u16(bytes + STAT_TRACK),
                       .last = get_u32(bytes + STAT_LAST)};
    return stat->plays > 0 && get_u32(bytes + STAT_CHECK) == stat_check(bytes);
}

uint32_t play_stats_bucket(uint32_t key)
{
    return (key ^ key >> 16) % PLAY_STATS_BUCKETS;
}

static bool weaker(const PlayStat *stat, const PlayStat *other)
{
    return stat->plays != other->plays ? stat->plays < other->plays : stat->last < other->last;
}

uint16_t play_stats_note(uint8_t *bucket, uint32_t key, uint16_t track, uint32_t tick)
{
    PlayStat stat;
    PlayStat chosen = {.key = key, .plays = 0, .track = track, .last = tick};
    uint16_t place = PLAY_STATS_BUCKET_ENTRIES;
    PlayStat weakest = {.plays = MOST_PLAYS, .last = UINT32_MAX};
    uint16_t weakest_place = 0;
    for (uint16_t index = 0; index < PLAY_STATS_BUCKET_ENTRIES; index++) {
        if (!play_stat_decode(bucket + index * PLAY_STAT_BYTES, &stat)) {
            if (place == PLAY_STATS_BUCKET_ENTRIES) {
                place = index;
            }
            continue;
        }
        if (stat.key == key) {
            chosen.plays = stat.plays;
            place = index;
            break;
        }
        if (weaker(&stat, &weakest)) {
            weakest = stat;
            weakest_place = index;
        }
    }
    if (place == PLAY_STATS_BUCKET_ENTRIES) {
        place = weakest_place;
    }
    chosen.plays = chosen.plays < MOST_PLAYS ? (uint16_t)(chosen.plays + 1) : MOST_PLAYS;
    play_stat_encode(&chosen, bucket + place * PLAY_STAT_BYTES);
    return place;
}

static const char *after_slashes(const char *text)
{
    while (*text == '/') {
        text++;
    }
    return text;
}

static uint32_t text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

uint32_t added_files_key(const char *folder, const char *name)
{
    const char *start = after_slashes(folder);
    uint32_t length = text_length(start);
    uint32_t hash = hash_update(HASH_START, start, length);
    if (length > 0) {
        hash = hash_update(hash, "/", 1);
    }
    return hash_update(hash, name, text_length(name));
}

void added_files_offer(SmartRank *rank, const char *folder, const char *name, uint32_t modified,
                       uint32_t order)
{
    if (!music_fingerprint_is_audio(name)) {
        return;
    }
    smart_rank_offer(rank, (SmartEntry){.key = added_files_key(folder, name),
                                        .primary = modified,
                                        .secondary = UINT32_MAX - order,
                                        .place = 0,
                                        .track = SMART_LIST_NO_TRACK});
}

uint32_t added_files_encode(const SmartEntry *entries, uint16_t count, uint32_t fingerprint,
                            uint8_t *bytes)
{
    count = count < SMART_LIST_TRACKS ? count : SMART_LIST_TRACKS;
    __builtin_memcpy(bytes, ADDED_MAGIC, sizeof ADDED_MAGIC);
    put_u16(bytes + ADDED_VERSION_FIELD, ADDED_VERSION);
    put_u16(bytes + ADDED_COUNT_FIELD, count);
    put_u32(bytes + ADDED_FINGERPRINT_FIELD, fingerprint);
    uint32_t at = ADDED_FILES_HEADER_BYTES;
    for (uint16_t index = 0; index < count; index++) {
        put_u32(bytes + at + ADDED_KEY, entries[index].key);
        put_u32(bytes + at + ADDED_MODIFIED, entries[index].primary);
        put_u16(bytes + at + ADDED_TRACK, entries[index].track);
        put_u16(bytes + at + ADDED_TRACK + 2, 0);
        at += ADDED_FILE_BYTES;
    }
    put_u32(bytes + at, hash_update(HASH_START, bytes, at));
    return at + 4;
}

bool added_files_decode(const uint8_t *bytes, uint32_t length, SmartRank *rank,
                        uint32_t *fingerprint)
{
    rank->count = 0;
    *fingerprint = 0;
    if (length < ADDED_FILES_HEADER_BYTES + 4 ||
        __builtin_memcmp(bytes, ADDED_MAGIC, sizeof ADDED_MAGIC) != 0 ||
        get_u16(bytes + ADDED_VERSION_FIELD) != ADDED_VERSION) {
        return false;
    }
    uint16_t count = get_u16(bytes + ADDED_COUNT_FIELD);
    uint32_t end = ADDED_FILES_HEADER_BYTES + (uint32_t)count * ADDED_FILE_BYTES;
    if (count > SMART_LIST_TRACKS || count > rank->capacity || end + 4 > length ||
        get_u32(bytes + end) != hash_update(HASH_START, bytes, end)) {
        return false;
    }
    for (uint16_t index = 0; index < count; index++) {
        const uint8_t *entry = bytes + ADDED_FILES_HEADER_BYTES + index * ADDED_FILE_BYTES;
        rank->entries[index] = (SmartEntry){.key = get_u32(entry + ADDED_KEY),
                                            .primary = get_u32(entry + ADDED_MODIFIED),
                                            .secondary = 0,
                                            .place = index,
                                            .track = get_u16(entry + ADDED_TRACK)};
    }
    rank->count = count;
    *fingerprint = get_u32(bytes + ADDED_FINGERPRINT_FIELD);
    return true;
}
