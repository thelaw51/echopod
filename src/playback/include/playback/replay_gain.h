#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"

enum {
    REPLAY_GAIN_LIMIT_CDB = 6000
};

typedef struct ReplayGain {
    int16_t track_cdb;
    int16_t album_cdb;
    bool has_track;
    bool has_album;
} ReplayGain;

bool replay_gain_read(ByteSource file, uint32_t size, const char *name, ReplayGain *gain);
bool replay_gain_parse(const char *text, uint32_t length, int16_t *cdb);
int16_t replay_gain_pick(ReplayGain gain, bool album);
