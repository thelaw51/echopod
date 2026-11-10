#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library.h"
#include "library/user_playlist.h"

enum {
    PLAY_LOG_ENTRIES = 200,
    PLAY_LOG_LIST_TRACKS = 100,
    PLAY_LOG_HEADER_BYTES = 16,
    PLAY_LOG_ENTRY_BYTES = 12,
    PLAY_LOG_BYTES = PLAY_LOG_HEADER_BYTES + PLAY_LOG_ENTRIES * PLAY_LOG_ENTRY_BYTES + 4,
    PLAY_LOG_COPY_STRIDE = 4096,
    PLAY_LOG_FILE_BYTES = 2 * PLAY_LOG_COPY_STRIDE,
    PLAY_LOG_NO_TRACK = 0xFFFF,
    PLAY_LOG_MAX_PLAYS = 0xFFFF
};

typedef enum PlayLogOrder {
    PLAY_LOG_RECENT,
    PLAY_LOG_MOST
} PlayLogOrder;

typedef struct PlayLogEntry {
    uint32_t key;
    uint16_t track;
    uint16_t plays;
    uint32_t last;
} PlayLogEntry;

void play_log_clear(uint8_t *log, uint32_t fingerprint);
bool play_log_valid(const uint8_t *log);
uint32_t play_log_generation(const uint8_t *log);
uint16_t play_log_count(const uint8_t *log);
uint32_t play_log_fingerprint(const uint8_t *log);
PlayLogEntry play_log_entry(const uint8_t *log, uint16_t index);
void play_log_seal(uint8_t *log, uint32_t generation);
void play_log_add(uint8_t *log, uint32_t key, uint16_t track);
uint16_t play_log_list(const uint8_t *log, PlayLogOrder order, uint16_t *tracks, uint16_t capacity);
void play_log_resolve(uint8_t *log, const Library *library, UserPlaylistPause pause);
