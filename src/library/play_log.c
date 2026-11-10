#include "library/play_log.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    VERSION = 1,
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    COUNT_FIELD = 6,
    GENERATION_FIELD = 8,
    FINGERPRINT_FIELD = 12,
    ENTRIES_FIELD = PLAY_LOG_HEADER_BYTES,
    TRACK_OFFSET = 4,
    PLAYS_OFFSET = 6,
    LAST_OFFSET = 8,
    CHECKSUM_FIELD = PLAY_LOG_BYTES - 4,
    TAKEN_BYTES = (PLAY_LOG_ENTRIES + 7) / 8,
    BITS_PER_BYTE = 8
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'E', 'P', 'L', 'G'};

static void put_u16(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> BITS_PER_BYTE);
}

static void put_u32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    put_u16(bytes, offset, value & 0xFFFFu);
    put_u16(bytes, offset + 2, value >> 16);
}

static uint16_t get_u16(const uint8_t *bytes, uint32_t offset)
{
    return (uint16_t)(bytes[offset] | bytes[offset + 1] << BITS_PER_BYTE);
}

static uint32_t get_u32(const uint8_t *bytes, uint32_t offset)
{
    return (uint32_t)get_u16(bytes, offset) | (uint32_t)get_u16(bytes, offset + 2) << 16;
}

static uint32_t entry_field(uint16_t index)
{
    return ENTRIES_FIELD + (uint32_t)index * PLAY_LOG_ENTRY_BYTES;
}

static void put_entry(uint8_t *log, uint16_t index, PlayLogEntry entry)
{
    uint32_t field = entry_field(index);
    put_u32(log, field, entry.key);
    put_u16(log, field + TRACK_OFFSET, entry.track);
    put_u16(log, field + PLAYS_OFFSET, entry.plays);
    put_u32(log, field + LAST_OFFSET, entry.last);
}

void play_log_clear(uint8_t *log, uint32_t fingerprint)
{
    for (uint32_t index = 0; index < PLAY_LOG_BYTES; index++) {
        log[index] = index < MAGIC_SIZE ? MAGIC[index] : 0;
    }
    put_u16(log, VERSION_FIELD, VERSION);
    put_u32(log, FINGERPRINT_FIELD, fingerprint);
}

bool play_log_valid(const uint8_t *log)
{
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (log[index] != MAGIC[index]) {
            return false;
        }
    }
    return get_u16(log, VERSION_FIELD) == VERSION &&
           get_u16(log, COUNT_FIELD) <= PLAY_LOG_ENTRIES &&
           get_u32(log, CHECKSUM_FIELD) == hash_update(HASH_START, log, CHECKSUM_FIELD);
}

uint32_t play_log_generation(const uint8_t *log)
{
    return get_u32(log, GENERATION_FIELD);
}

uint16_t play_log_count(const uint8_t *log)
{
    return get_u16(log, COUNT_FIELD);
}

uint32_t play_log_fingerprint(const uint8_t *log)
{
    return get_u32(log, FINGERPRINT_FIELD);
}

PlayLogEntry play_log_entry(const uint8_t *log, uint16_t index)
{
    uint32_t field = entry_field(index);
    return (PlayLogEntry){.key = get_u32(log, field),
                          .track = get_u16(log, field + TRACK_OFFSET),
                          .plays = get_u16(log, field + PLAYS_OFFSET),
                          .last = get_u32(log, field + LAST_OFFSET)};
}

void play_log_seal(uint8_t *log, uint32_t generation)
{
    put_u32(log, GENERATION_FIELD, generation);
    put_u32(log, CHECKSUM_FIELD, hash_update(HASH_START, log, CHECKSUM_FIELD));
}

static uint32_t newest_last(const uint8_t *log)
{
    uint32_t newest = 0;
    for (uint16_t index = 0; index < play_log_count(log); index++) {
        uint32_t last = play_log_entry(log, index).last;
        newest = last > newest ? last : newest;
    }
    return newest;
}

static int find_key(const uint8_t *log, uint32_t key)
{
    for (uint16_t index = 0; index < play_log_count(log); index++) {
        if (play_log_entry(log, index).key == key) {
            return index;
        }
    }
    return -1;
}

static bool among_recent(const uint8_t *log, PlayLogEntry entry)
{
    uint16_t newer = 0;
    for (uint16_t index = 0; index < play_log_count(log); index++) {
        if (play_log_entry(log, index).last > entry.last) {
            newer++;
        }
    }
    return newer < PLAY_LOG_LIST_TRACKS;
}

static bool evicts_first(PlayLogEntry candidate, PlayLogEntry chosen)
{
    return candidate.plays < chosen.plays ||
           (candidate.plays == chosen.plays && candidate.last < chosen.last);
}

static uint16_t victim(const uint8_t *log)
{
    int chosen = -1;
    for (uint16_t index = 0; index < play_log_count(log); index++) {
        PlayLogEntry entry = play_log_entry(log, index);
        if (among_recent(log, entry)) {
            continue;
        }
        if (chosen < 0 || evicts_first(entry, play_log_entry(log, (uint16_t)chosen))) {
            chosen = index;
        }
    }
    return (uint16_t)(chosen < 0 ? 0 : chosen);
}

void play_log_add(uint8_t *log, uint32_t key, uint16_t track)
{
    uint32_t last = newest_last(log) + 1;
    int found = find_key(log, key);
    if (found >= 0) {
        PlayLogEntry entry = play_log_entry(log, (uint16_t)found);
        entry.track = track;
        entry.plays = entry.plays < PLAY_LOG_MAX_PLAYS ? (uint16_t)(entry.plays + 1) : entry.plays;
        entry.last = last;
        put_entry(log, (uint16_t)found, entry);
        return;
    }
    uint16_t count = play_log_count(log);
    PlayLogEntry entry = {.key = key, .track = track, .plays = 1, .last = last};
    if (count < PLAY_LOG_ENTRIES) {
        put_entry(log, count, entry);
        put_u16(log, COUNT_FIELD, count + 1u);
        return;
    }
    put_entry(log, victim(log), entry);
}

static bool ranks_before(PlayLogOrder order, PlayLogEntry candidate, PlayLogEntry chosen)
{
    if (order == PLAY_LOG_MOST && candidate.plays != chosen.plays) {
        return candidate.plays > chosen.plays;
    }
    return candidate.last > chosen.last;
}

static int best_left(const uint8_t *log, PlayLogOrder order, const uint8_t *taken)
{
    int chosen = -1;
    for (uint16_t index = 0; index < play_log_count(log); index++) {
        PlayLogEntry entry = play_log_entry(log, index);
        bool used = (taken[index / BITS_PER_BYTE] >> (index % BITS_PER_BYTE) & 1u) != 0;
        if (used || entry.track == PLAY_LOG_NO_TRACK) {
            continue;
        }
        if (chosen < 0 || ranks_before(order, entry, play_log_entry(log, (uint16_t)chosen))) {
            chosen = index;
        }
    }
    return chosen;
}

uint16_t play_log_list(const uint8_t *log, PlayLogOrder order, uint16_t *tracks, uint16_t capacity)
{
    uint8_t taken[TAKEN_BYTES] = {0};
    uint16_t found = 0;
    while (found < capacity) {
        int index = best_left(log, order, taken);
        if (index < 0) {
            break;
        }
        taken[index / BITS_PER_BYTE] |= (uint8_t)(1u << (index % BITS_PER_BYTE));
        tracks[found++] = play_log_entry(log, (uint16_t)index).track;
    }
    return found;
}

static void match_track(uint8_t *log, uint32_t key, uint16_t track)
{
    for (uint16_t index = 0; index < play_log_count(log); index++) {
        if (play_log_entry(log, index).key == key) {
            put_u16(log, entry_field(index) + TRACK_OFFSET, track);
        }
    }
}

void play_log_resolve(uint8_t *log, const Library *library, UserPlaylistPause pause)
{
    for (uint16_t index = 0; index < play_log_count(log); index++) {
        put_u16(log, entry_field(index) + TRACK_OFFSET, PLAY_LOG_NO_TRACK);
    }
    uint32_t tracks = library_count(library, LIBRARY_TRACKS);
    for (uint32_t track = 0; track < tracks && play_log_count(log) > 0; track++) {
        uint32_t key = 0;
        if (user_playlist_track_key(library, (uint16_t)track, &key)) {
            match_track(log, key, (uint16_t)track);
        }
        if (pause.between_tracks != NULL && track % USER_PLAYLIST_TRACKS_PER_PAUSE == 0) {
            pause.between_tracks(pause.context);
        }
    }
    put_u32(log, FINGERPRINT_FIELD, library_fingerprint(library));
}
