#include "library/cue_sheet.h"

#include <stddef.h>

#include "tags_internal.h"

enum {
    MILLISECONDS_PER_FRAME_NUMERATOR = 40,
    MILLISECONDS_PER_FRAME_DENOMINATOR = 3,
    SECONDS_PER_MINUTE = 60,
    TRACK_NUMBER_MAX = 255,
    UNTITLED_DIGITS = 2
};

static const char UNTITLED[] = "Track ";

static bool is_blank(char character)
{
    return character == ' ' || character == '\t';
}

static char lower(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static uint32_t text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static const char *skip_blanks(const char *text)
{
    while (is_blank(*text)) {
        text++;
    }
    return text;
}

static const char *word_end(const char *text)
{
    while (*text != '\0' && !is_blank(*text)) {
        text++;
    }
    return text;
}

static bool is_word(const char *start, const char *end, const char *wanted)
{
    for (; start < end && *wanted != '\0'; start++, wanted++) {
        if (lower(*start) != *wanted) {
            return false;
        }
    }
    return start == end && *wanted == '\0';
}

static void clean_into(char *out, const char *text, uint32_t length)
{
    tag_decode(out, (const uint8_t *)text, length, TAG_UTF8);
}

static void quoted_value(const char *text, const char **start, uint32_t *length)
{
    if (*text != '"') {
        *start = text;
        *length = text_length(text);
        return;
    }
    const char *end = text + 1;
    while (*end != '\0' && *end != '"') {
        end++;
    }
    *start = text + 1;
    *length = (uint32_t)(end - *start);
}

static void read_value(const char *text, char *out)
{
    const char *start = NULL;
    uint32_t length = 0;
    quoted_value(text, &start, &length);
    clean_into(out, start, length);
}

static void read_file_value(const char *text, char *out)
{
    const char *start = NULL;
    uint32_t length = 0;
    quoted_value(text, &start, &length);
    if (*text != '"') {
        uint32_t cut = length;
        while (cut > 0 && !is_blank(start[cut - 1])) {
            cut--;
        }
        while (cut > 0 && is_blank(start[cut - 1])) {
            cut--;
        }
        length = cut > 0 ? cut : length;
    }
    clean_into(out, start, length);
}

static bool read_digits(const char **text, uint64_t *value)
{
    const char *cursor = *text;
    uint64_t number = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        number = number * 10 + (uint64_t)(*cursor - '0');
        number = number > UINT32_MAX ? UINT32_MAX : number;
        cursor++;
    }
    if (cursor == *text) {
        return false;
    }
    *text = cursor;
    *value = number;
    return true;
}

bool cue_sheet_parse_time(const char *text, uint64_t *frames)
{
    uint64_t minutes = 0;
    uint64_t seconds = 0;
    uint64_t rest = 0;
    if (!read_digits(&text, &minutes) || *text++ != ':' || !read_digits(&text, &seconds) ||
        *text++ != ':' || !read_digits(&text, &rest) || (*text != '\0' && !is_blank(*text))) {
        return false;
    }
    *frames = (minutes * SECONDS_PER_MINUTE + seconds) * CUE_FRAMES_PER_SECOND + rest;
    return true;
}

uint32_t cue_sheet_frames_to_ms(uint64_t frames)
{
    uint64_t milliseconds =
        (frames * MILLISECONDS_PER_FRAME_NUMERATOR + 1) / MILLISECONDS_PER_FRAME_DENOMINATOR;
    return milliseconds > UINT32_MAX ? UINT32_MAX : (uint32_t)milliseconds;
}

bool cue_sheet_is_name(const char *name)
{
    uint32_t length = text_length(name);
    return length > 4 && name[length - 4] == '.' && lower(name[length - 3]) == 'c' &&
           lower(name[length - 2]) == 'u' && lower(name[length - 1]) == 'e';
}

void cue_sheet_untitled(uint8_t number, char *title)
{
    uint32_t length = 0;
    for (; UNTITLED[length] != '\0'; length++) {
        title[length] = UNTITLED[length];
    }
    char digits[3];
    uint32_t count = 0;
    uint32_t value = number;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0);
    while (count < UNTITLED_DIGITS) {
        digits[count++] = '0';
    }
    while (count > 0) {
        title[length++] = digits[--count];
    }
    title[length] = '\0';
}

void cue_sheet_open(CueSheet *sheet, ByteSource source, uint32_t size, CueFileMatch match,
                    void *match_context)
{
    sheet->match = match;
    sheet->match_context = match_context;
    sheet->album.title[0] = '\0';
    sheet->album.performer[0] = '\0';
    sheet->album.genre[0] = '\0';
    sheet->album.date[0] = '\0';
    sheet->album.disc[0] = '\0';
    sheet->file_matches = false;
    sheet->in_track = false;
    sheet->has_start = false;
    sheet->start_matches = false;
    sheet->has_previous = false;
    sheet->previous_start_ms = 0;
    sheet->building = (CueTrack){.number = 0};
    playlist_reader_open(&sheet->lines, source, size);
}

static void album_line(CueSheet *sheet, const char *key, const char *key_end, const char *rest)
{
    if (is_word(key, key_end, "title")) {
        read_value(rest, sheet->album.title);
    } else if (is_word(key, key_end, "performer")) {
        read_value(rest, sheet->album.performer);
    } else if (is_word(key, key_end, "rem")) {
        const char *name_end = word_end(rest);
        const char *value = skip_blanks(name_end);
        if (is_word(rest, name_end, "genre")) {
            read_value(value, sheet->album.genre);
        } else if (is_word(rest, name_end, "date")) {
            read_value(value, sheet->album.date);
        } else if (is_word(rest, name_end, "discnumber")) {
            read_value(value, sheet->album.disc);
        }
    }
}

static void track_line(CueSheet *sheet, const char *key, const char *key_end, const char *rest)
{
    if (is_word(key, key_end, "title")) {
        read_value(rest, sheet->building.title);
    } else if (is_word(key, key_end, "performer")) {
        read_value(rest, sheet->building.performer);
    } else if (is_word(key, key_end, "index")) {
        uint64_t index = 0;
        uint64_t frames = 0;
        const char *cursor = rest;
        if (!sheet->has_start && read_digits(&cursor, &index) && index == 1 && is_blank(*cursor) &&
            cue_sheet_parse_time(skip_blanks(cursor), &frames)) {
            sheet->has_start = true;
            sheet->start_matches = sheet->file_matches;
            sheet->building.start_ms = cue_sheet_frames_to_ms(frames);
        }
    }
}

static bool finish_track(CueSheet *sheet, CueTrack *track)
{
    if (!sheet->in_track || !sheet->has_start || !sheet->start_matches ||
        (sheet->has_previous && sheet->building.start_ms <= sheet->previous_start_ms)) {
        return false;
    }
    sheet->has_previous = true;
    sheet->previous_start_ms = sheet->building.start_ms;
    *track = sheet->building;
    return true;
}

static void begin_track(CueSheet *sheet, const char *rest)
{
    uint64_t number = 0;
    const char *cursor = rest;
    if (!read_digits(&cursor, &number)) {
        number = 0;
    }
    sheet->in_track = true;
    sheet->has_start = false;
    sheet->start_matches = false;
    sheet->building =
        (CueTrack){.number = (uint8_t)(number > TRACK_NUMBER_MAX ? TRACK_NUMBER_MAX : number)};
}

bool cue_sheet_next(CueSheet *sheet, CueTrack *track)
{
    while (playlist_reader_next(&sheet->lines, sheet->line, sizeof sheet->line)) {
        const char *key = sheet->line;
        const char *key_end = word_end(key);
        const char *rest = skip_blanks(key_end);
        if (is_word(key, key_end, "file")) {
            char file[CUE_TEXT_CAPACITY];
            read_file_value(rest, file);
            sheet->file_matches = file[0] != '\0' && sheet->match(sheet->match_context, file);
        } else if (is_word(key, key_end, "track")) {
            bool finished = finish_track(sheet, track);
            begin_track(sheet, rest);
            if (finished) {
                return true;
            }
        } else if (sheet->in_track) {
            track_line(sheet, key, key_end, rest);
        } else {
            album_line(sheet, key, key_end, rest);
        }
    }
    bool finished = finish_track(sheet, track);
    sheet->in_track = false;
    return finished;
}

uint32_t cue_sheet_count(CueSheet *sheet, CueTrack *scratch)
{
    uint32_t count = 0;
    while (cue_sheet_next(sheet, scratch)) {
        count++;
    }
    return count;
}

void cue_split_start(CueSplit *split, uint32_t file_ms)
{
    split->holding = false;
    split->file_ms = file_ms;
}

static bool within_file(const CueSplit *split, uint32_t start_ms)
{
    return split->file_ms == 0 || start_ms < split->file_ms;
}

static uint32_t end_of(const CueSplit *split, uint32_t end_ms)
{
    return split->file_ms != 0 && end_ms > split->file_ms ? split->file_ms : end_ms;
}

bool cue_split_push(CueSplit *split, const CueTrack *next, CueTrack *out, uint32_t *duration_ms)
{
    bool ready = split->holding;
    if (ready) {
        *out = split->held;
        *duration_ms = end_of(split, next->start_ms) - out->start_ms;
    }
    split->holding = within_file(split, next->start_ms);
    if (split->holding) {
        split->held = *next;
    }
    return ready;
}

bool cue_split_finish(CueSplit *split, CueTrack *out, uint32_t *duration_ms)
{
    if (!split->holding) {
        return false;
    }
    split->holding = false;
    *out = split->held;
    *duration_ms = split->file_ms > out->start_ms ? split->file_ms - out->start_ms : 0;
    return true;
}
