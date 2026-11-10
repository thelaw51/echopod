#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"
#include "library/playlist_reader.h"
#include "library/track_tags.h"

enum {
    CUE_TEXT_CAPACITY = TRACK_TAG_TEXT_CAPACITY,
    CUE_LINE_CAPACITY = PLAYLIST_ENTRY_CAPACITY,
    CUE_FRAMES_PER_SECOND = 75
};

typedef bool (*CueFileMatch)(void *context, const char *file);

typedef struct CueAlbum {
    char title[CUE_TEXT_CAPACITY];
    char performer[CUE_TEXT_CAPACITY];
    char genre[CUE_TEXT_CAPACITY];
    char date[CUE_TEXT_CAPACITY];
    char disc[CUE_TEXT_CAPACITY];
} CueAlbum;

typedef struct CueTrack {
    uint8_t number;
    uint32_t start_ms;
    char title[CUE_TEXT_CAPACITY];
    char performer[CUE_TEXT_CAPACITY];
} CueTrack;

typedef struct CueSheet {
    PlaylistReader lines;
    char line[CUE_LINE_CAPACITY];
    CueFileMatch match;
    void *match_context;
    CueAlbum album;
    bool file_matches;
    bool in_track;
    bool has_start;
    bool start_matches;
    bool has_previous;
    uint32_t previous_start_ms;
    CueTrack building;
} CueSheet;

typedef struct CueSplit {
    CueTrack held;
    bool holding;
    uint32_t file_ms;
} CueSplit;

uint32_t cue_sheet_frames_to_ms(uint64_t frames);
bool cue_sheet_parse_time(const char *text, uint64_t *frames);
bool cue_sheet_is_name(const char *name);
void cue_sheet_open(CueSheet *sheet, ByteSource source, uint32_t size, CueFileMatch match,
                    void *match_context);
bool cue_sheet_next(CueSheet *sheet, CueTrack *track);
uint32_t cue_sheet_count(CueSheet *sheet, CueTrack *scratch);
void cue_sheet_untitled(uint8_t number, char *title);

void cue_split_start(CueSplit *split, uint32_t file_ms);
bool cue_split_push(CueSplit *split, const CueTrack *next, CueTrack *out, uint32_t *duration_ms);
bool cue_split_finish(CueSplit *split, CueTrack *out, uint32_t *duration_ms);
