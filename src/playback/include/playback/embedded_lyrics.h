#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"
#include "playback/lyrics.h"

enum {
    EMBEDDED_LYRICS_READ_LIMIT = 64 * 1024
};

bool embedded_lyrics_read(ByteSource file, uint32_t size, const char *name, Lyrics *lyrics);
