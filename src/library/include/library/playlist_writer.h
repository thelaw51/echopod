#pragma once

#include <stdint.h>

#include "library/library.h"

#define PLAYLIST_WRITER_HEADER "#EXTM3U\n"

enum {
    PLAYLIST_WRITER_LINE_CAPACITY = 1024
};

uint32_t playlist_writer_entry(const Library *library, uint16_t track, const char *prefix,
                               char *out, uint32_t capacity);
