#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library.h"
#include "library/user_playlist.h"

enum {
    SMART_LIST_TRACKS = 100,
    SMART_LIST_NO_TRACK = 0xFFFF,
    PLAY_STAT_BYTES = 16,
    PLAY_STATS_BUCKET_ENTRIES = 32,
    PLAY_STATS_BUCKET_BYTES = PLAY_STAT_BYTES * PLAY_STATS_BUCKET_ENTRIES,
    PLAY_STATS_BUCKETS = 32,
    PLAY_STATS_ENTRIES = PLAY_STATS_BUCKET_ENTRIES * PLAY_STATS_BUCKETS,
    PLAY_STATS_FILE_BYTES = PLAY_STAT_BYTES * PLAY_STATS_ENTRIES,
    ADDED_FILES_HEADER_BYTES = 12,
    ADDED_FILE_BYTES = 12,
    ADDED_FILES_BYTES = ADDED_FILES_HEADER_BYTES + SMART_LIST_TRACKS * ADDED_FILE_BYTES + 4
};

typedef struct SmartEntry {
    uint32_t key;
    uint32_t primary;
    uint32_t secondary;
    uint16_t place;
    uint16_t track;
} SmartEntry;

typedef struct SmartRank {
    SmartEntry *entries;
    uint16_t capacity;
    uint16_t count;
} SmartRank;

typedef struct PlayStat {
    uint32_t key;
    uint16_t plays;
    uint16_t track;
    uint32_t last;
} PlayStat;

void smart_rank_init(SmartRank *rank, SmartEntry *entries, uint16_t capacity);
void smart_rank_offer(SmartRank *rank, SmartEntry entry);
bool smart_list_resolve(const Library *library, SmartEntry *entries, uint16_t count, bool walk,
                        bool files, UserPlaylistPause pause);
uint16_t smart_list_tracks(const SmartEntry *entries, uint16_t count, uint16_t *tracks);

void play_stat_encode(const PlayStat *stat, uint8_t *bytes);
bool play_stat_decode(const uint8_t *bytes, PlayStat *stat);
uint32_t play_stats_bucket(uint32_t key);
uint16_t play_stats_note(uint8_t *bucket, uint32_t key, uint16_t track, uint32_t tick);

uint32_t added_files_key(const char *folder, const char *name);
void added_files_offer(SmartRank *rank, const char *folder, const char *name, uint32_t modified,
                       uint32_t order);
uint32_t added_files_encode(const SmartEntry *entries, uint16_t count, uint32_t fingerprint,
                            uint8_t *bytes);
bool added_files_decode(const uint8_t *bytes, uint32_t length, SmartRank *rank,
                        uint32_t *fingerprint);
