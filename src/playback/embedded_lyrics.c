#include "playback/embedded_lyrics.h"

#include <stddef.h>

enum {
    CHUNK = 128,
    OUT_CAPACITY = 128,
    LINE_CAPACITY = LYRICS_PENDING_CAPACITY - 16,
    TIME_CAPACITY = 16,
    KEY_PREFIX = 16,
    ID3_HEADER = 10,
    ID3_V22_FRAME_HEADER = 6,
    ID3_UNSYNC = 0x80,
    ID3_EXTENDED = 0x40,
    V23_COMPRESS = 0x80,
    V23_ENCRYPT = 0x40,
    V23_GROUP = 0x20,
    V24_GROUP = 0x40,
    V24_COMPRESS = 0x08,
    V24_ENCRYPT = 0x04,
    V24_UNSYNC = 0x02,
    V24_DATA_LENGTH = 0x01,
    SYLT_MILLISECONDS = 2,
    FLAC_VORBIS_COMMENT = 4,
    OGG_HEADER = 27,
    OPUS_TAGS_PREFIX = 8,
    VORBIS_COMMENT_PREFIX = 7,
    ATOM_HEADER = 8,
    MP4_DATA_HEADER = 8
};

typedef enum Encoding {
    ENCODING_LATIN1 = 0,
    ENCODING_UTF16 = 1,
    ENCODING_UTF16BE = 2,
    ENCODING_UTF8 = 3
} Encoding;

typedef enum Container {
    CONTAINER_NONE,
    CONTAINER_ID3,
    CONTAINER_FLAC,
    CONTAINER_OGG,
    CONTAINER_MP4
} Container;

typedef struct Span {
    uint32_t offset;
    uint32_t end;
    bool unsync;
    bool found;
} Span;

typedef struct Cursor {
    ByteSource source;
    uint32_t offset;
    uint32_t end;
    uint32_t budget;
    uint32_t buffered;
    uint32_t at;
    bool unsync;
    bool after_ff;
    uint8_t buffer[CHUNK];
} Cursor;

typedef struct Text {
    char *bytes;
    uint32_t used;
    uint32_t capacity;
    Lyrics *lyrics;
    bool after_cr;
} Text;

typedef struct OggStream {
    ByteSource file;
    uint32_t size;
    uint32_t first_offset;
    uint32_t page_offset;
    uint32_t page_header;
    uint32_t page_body;
    uint32_t page_start;
} OggStream;

static bool source_read(ByteSource source, uint32_t offset, void *buffer, uint32_t length)
{
    return length == 0 || source.read(source.context, offset, buffer, length);
}

static uint32_t le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static uint32_t be32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 |
           (uint32_t)bytes[3];
}

static uint32_t synchsafe(const uint8_t *bytes)
{
    return (uint32_t)(bytes[0] & 0x7F) << 21 | (uint32_t)(bytes[1] & 0x7F) << 14 |
           (uint32_t)(bytes[2] & 0x7F) << 7 | (uint32_t)(bytes[3] & 0x7F);
}

static char lower(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static bool same_text(const char *text, uint32_t length, const char *wanted)
{
    uint32_t index = 0;
    for (; index < length && wanted[index] != '\0'; index++) {
        if (text[index] != wanted[index]) {
            return false;
        }
    }
    return index == length && wanted[index] == '\0';
}

static bool same_folded(const char *text, uint32_t length, const char *wanted)
{
    uint32_t index = 0;
    for (; index < length && wanted[index] != '\0'; index++) {
        if (lower(text[index]) != wanted[index]) {
            return false;
        }
    }
    return index == length && wanted[index] == '\0';
}

static Container container_for(const char *name)
{
    const char *dot = NULL;
    for (const char *cursor = name; *cursor != '\0'; cursor++) {
        dot = *cursor == '.' ? cursor : *cursor == '/' ? NULL : dot;
    }
    if (dot == NULL) {
        return CONTAINER_NONE;
    }
    uint32_t length = 0;
    while (dot[1 + length] != '\0') {
        length++;
    }
    const char *extension = dot + 1;
    if (same_folded(extension, length, "mp3")) {
        return CONTAINER_ID3;
    }
    if (same_folded(extension, length, "flac")) {
        return CONTAINER_FLAC;
    }
    if (same_folded(extension, length, "ogg") || same_folded(extension, length, "opus")) {
        return CONTAINER_OGG;
    }
    return same_folded(extension, length, "m4a") ? CONTAINER_MP4 : CONTAINER_NONE;
}

static void cursor_open(Cursor *cursor, ByteSource source, const Span *span)
{
    cursor->source = source;
    cursor->offset = span->offset;
    cursor->end = span->end;
    cursor->budget = EMBEDDED_LYRICS_READ_LIMIT;
    cursor->buffered = 0;
    cursor->at = 0;
    cursor->unsync = span->unsync;
    cursor->after_ff = false;
}

static bool next_byte(Cursor *cursor, uint8_t *byte)
{
    for (;;) {
        if (cursor->at == cursor->buffered) {
            uint32_t left = cursor->end - cursor->offset;
            left = left < cursor->budget ? left : cursor->budget;
            uint32_t length = left < CHUNK ? left : CHUNK;
            if (length == 0 ||
                !source_read(cursor->source, cursor->offset, cursor->buffer, length)) {
                return false;
            }
            cursor->offset += length;
            cursor->budget -= length;
            cursor->buffered = length;
            cursor->at = 0;
        }
        uint8_t value = cursor->buffer[cursor->at++];
        bool dropped = cursor->unsync && cursor->after_ff && value == 0;
        cursor->after_ff = value == 0xFF;
        if (!dropped) {
            *byte = value;
            return true;
        }
    }
}

static bool skip_bytes(Cursor *cursor, uint32_t count)
{
    uint8_t ignored = 0;
    for (uint32_t index = 0; index < count; index++) {
        if (!next_byte(cursor, &ignored)) {
            return false;
        }
    }
    return true;
}

static void text_flush(Text *text)
{
    if (text->lyrics != NULL && text->used > 0) {
        lyrics_feed(text->lyrics, text->bytes, text->used);
        text->used = 0;
    }
}

static bool text_room(Text *text, uint32_t size)
{
    if (text->used + size > text->capacity) {
        text_flush(text);
    }
    return text->used + size <= text->capacity;
}

static void put_raw(Text *text, uint8_t byte)
{
    text->after_cr = false;
    if (text_room(text, 1)) {
        text->bytes[text->used++] = (char)byte;
    }
}

static void put_code_point(Text *text, uint32_t code_point)
{
    if (code_point == '\n' && text->after_cr) {
        text->after_cr = false;
        return;
    }
    text->after_cr = code_point == '\r';
    code_point = code_point == '\r' ? '\n' : code_point;
    bool hidden = (code_point < 0x20 && code_point != '\n' && code_point != '\t') ||
                  code_point == 0x7F || code_point == 0xFEFF || code_point > 0x10FFFF;
    uint32_t size = code_point < 0x80 ? 1 : code_point < 0x800 ? 2 : code_point < 0x10000 ? 3 : 4;
    if (hidden || !text_room(text, size)) {
        return;
    }
    static const uint8_t LEADS[5] = {0, 0x00, 0xC0, 0xE0, 0xF0};
    for (uint32_t index = size - 1; index > 0; index--) {
        text->bytes[text->used + index] = (char)(0x80 | (code_point & 0x3F));
        code_point >>= 6;
    }
    text->bytes[text->used] = (char)(LEADS[size] | code_point);
    text->used += size;
}

static uint32_t latin1(uint8_t byte)
{
    static const uint16_t WINDOWS_1252[32] = {
        0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};
    return byte >= 0x80 && byte < 0xA0 ? WINDOWS_1252[byte - 0x80] : byte;
}

static bool wide(uint8_t encoding)
{
    return encoding == ENCODING_UTF16 || encoding == ENCODING_UTF16BE;
}

static bool decode(Cursor *cursor, uint8_t encoding, bool stop_at_nul, Text *text)
{
    bool big = encoding == ENCODING_UTF16BE;
    bool first = true;
    uint32_t high = 0;
    uint8_t lead = 0;
    uint8_t trail = 0;
    while ((text->lyrics == NULL || !lyrics_full(text->lyrics)) && next_byte(cursor, &lead)) {
        if (!wide(encoding)) {
            if (lead == 0 && stop_at_nul) {
                return true;
            }
            if (encoding == ENCODING_UTF8 && lead >= 0x80) {
                put_raw(text, lead);
            } else {
                put_code_point(text, latin1(lead));
            }
            continue;
        }
        if (!next_byte(cursor, &trail)) {
            return false;
        }
        uint32_t unit = big ? (uint32_t)lead << 8 | trail : (uint32_t)trail << 8 | lead;
        if (first && encoding == ENCODING_UTF16 && (unit == 0xFFFE || unit == 0xFEFF)) {
            big = unit == 0xFFFE;
            first = false;
            continue;
        }
        first = false;
        if (unit == 0 && stop_at_nul) {
            return true;
        }
        if (unit >= 0xD800 && unit < 0xDC00) {
            high = unit;
            continue;
        }
        if (unit >= 0xDC00 && unit < 0xE000) {
            if (high != 0) {
                put_code_point(text, 0x10000 + ((high - 0xD800) << 10) + (unit - 0xDC00));
            }
            high = 0;
            continue;
        }
        high = 0;
        put_code_point(text, unit);
    }
    return false;
}

static bool skip_string(Cursor *cursor, uint8_t encoding)
{
    Text nowhere = {.bytes = NULL, .used = 0, .capacity = 0, .lyrics = NULL, .after_cr = false};
    return decode(cursor, encoding, true, &nowhere);
}

static bool read_value(ByteSource source, const Span *span, uint8_t encoding, Lyrics *lyrics)
{
    Cursor cursor;
    char out[OUT_CAPACITY];
    Text text = {.bytes = out, .used = 0, .capacity = sizeof out, .lyrics = lyrics};
    cursor_open(&cursor, source, span);
    decode(&cursor, encoding, false, &text);
    text_flush(&text);
    return true;
}

static bool read_uslt(ByteSource source, const Span *span, Lyrics *lyrics)
{
    Cursor cursor;
    uint8_t encoding = 0;
    cursor_open(&cursor, source, span);
    if (!next_byte(&cursor, &encoding) || !skip_bytes(&cursor, 3) ||
        !skip_string(&cursor, encoding)) {
        return false;
    }
    char out[OUT_CAPACITY];
    Text text = {.bytes = out, .used = 0, .capacity = sizeof out, .lyrics = lyrics};
    decode(&cursor, encoding, false, &text);
    text_flush(&text);
    return true;
}

typedef struct SyltLine {
    Lyrics *lyrics;
    char text[LINE_CAPACITY];
    uint32_t used;
    uint32_t time_ms;
    bool open;
} SyltLine;

static void put_digits(Text *text, uint32_t value, uint32_t width)
{
    char digits[10];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0 && count < sizeof digits);
    while (count < width) {
        digits[count++] = '0';
    }
    while (count > 0) {
        put_raw(text, (uint8_t)digits[--count]);
    }
}

static void line_flush(SyltLine *line)
{
    if (!line->open || line->lyrics == NULL) {
        return;
    }
    char stamp[TIME_CAPACITY];
    Text text = {.bytes = stamp, .used = 0, .capacity = sizeof stamp, .lyrics = NULL};
    uint32_t seconds = line->time_ms / 1000;
    put_raw(&text, '[');
    put_digits(&text, seconds / 60, 2);
    put_raw(&text, ':');
    put_digits(&text, seconds % 60, 2);
    put_raw(&text, '.');
    put_digits(&text, line->time_ms % 1000, 3);
    put_raw(&text, ']');
    lyrics_feed(line->lyrics, stamp, text.used);
    lyrics_feed(line->lyrics, line->text, line->used);
    lyrics_feed(line->lyrics, "\n", 1);
    line->open = false;
}

static void line_add(SyltLine *line, const char *bytes, uint32_t length, uint32_t time_ms,
                     bool starts)
{
    if (starts || !line->open) {
        line_flush(line);
        line->used = 0;
        line->time_ms = time_ms;
        line->open = true;
    }
    for (uint32_t index = 0; index < length && line->used < sizeof line->text; index++) {
        line->text[line->used++] = bytes[index];
    }
}

static bool sylt_entries(Cursor *cursor, uint8_t encoding, SyltLine *line, bool grouped)
{
    bool any_break = false;
    bool after_break = true;
    for (;;) {
        char bytes[LINE_CAPACITY];
        Text text = {.bytes = bytes, .used = 0, .capacity = sizeof bytes, .lyrics = NULL};
        uint8_t stamp[4] = {0};
        bool ended = decode(cursor, encoding, true, &text);
        if (!ended || !next_byte(cursor, &stamp[0]) || !next_byte(cursor, &stamp[1]) ||
            !next_byte(cursor, &stamp[2]) || !next_byte(cursor, &stamp[3])) {
            break;
        }
        uint32_t start = 0;
        uint32_t end = text.used;
        while (start < end && bytes[start] == '\n') {
            start++;
        }
        bool leading = start > 0;
        while (end > start && bytes[end - 1] == '\n') {
            end--;
        }
        bool trailing = end < text.used;
        any_break = any_break || leading || trailing;
        line_add(line, bytes + start, end - start, be32(stamp), !grouped || leading || after_break);
        after_break = trailing;
    }
    return any_break;
}

static bool read_sylt(ByteSource source, const Span *span, Lyrics *lyrics)
{
    Cursor cursor;
    uint8_t header[6] = {0};
    cursor_open(&cursor, source, span);
    for (uint32_t index = 0; index < sizeof header; index++) {
        if (!next_byte(&cursor, &header[index])) {
            return false;
        }
    }
    if (header[4] != SYLT_MILLISECONDS || !skip_string(&cursor, header[0])) {
        return false;
    }
    Cursor entries = cursor;
    SyltLine line = {.lyrics = NULL, .used = 0, .time_ms = 0, .open = false};
    bool grouped = sylt_entries(&cursor, header[0], &line, false);
    line = (SyltLine){.lyrics = lyrics, .used = 0, .time_ms = 0, .open = false};
    sylt_entries(&entries, header[0], &line, grouped);
    line_flush(&line);
    return true;
}

typedef struct Id3Frames {
    Span uslt;
    Span sylt;
} Id3Frames;

static uint32_t id3_frame_size(uint8_t version, const uint8_t *header)
{
    if (version == 2) {
        return (uint32_t)header[3] << 16 | (uint32_t)header[4] << 8 | header[5];
    }
    bool safe = ((header[4] | header[5] | header[6] | header[7]) & 0x80) == 0;
    return version == 4 && safe ? synchsafe(header + 4) : be32(header + 4);
}

static bool id3_usable(uint8_t version, uint8_t flags, bool tag_unsync, Span *span)
{
    if (version == 3) {
        if ((flags & (V23_COMPRESS | V23_ENCRYPT)) != 0) {
            return false;
        }
        span->offset += (flags & V23_GROUP) != 0 ? 1u : 0u;
    } else if (version == 4) {
        if ((flags & (V24_COMPRESS | V24_ENCRYPT)) != 0) {
            return false;
        }
        span->offset +=
            ((flags & V24_GROUP) != 0 ? 1u : 0u) + ((flags & V24_DATA_LENGTH) != 0 ? 4u : 0u);
        span->unsync = (flags & V24_UNSYNC) != 0 || tag_unsync;
    }
    return span->offset <= span->end;
}

static void id3_keep(Id3Frames *frames, const char *id, uint8_t version, Span span)
{
    uint32_t length = version == 2 ? 3 : 4;
    bool uslt = same_text(id, length, version == 2 ? "ULT" : "USLT");
    bool sylt = same_text(id, length, version == 2 ? "SLT" : "SYLT");
    Span *slot = uslt ? &frames->uslt : sylt ? &frames->sylt : NULL;
    if (slot != NULL && !slot->found) {
        span.found = true;
        *slot = span;
    }
}

static void id3_find(ByteSource source, uint32_t size, Id3Frames *frames)
{
    uint8_t header[ID3_HEADER];
    if (size < ID3_HEADER || !source_read(source, 0, header, sizeof header) ||
        !same_text((const char *)header, 3, "ID3") || header[3] < 2 || header[3] > 4) {
        return;
    }
    uint8_t version = header[3];
    bool tag_unsync = (header[5] & ID3_UNSYNC) != 0;
    if (tag_unsync && version < 4) {
        return;
    }
    uint32_t end = ID3_HEADER + synchsafe(header + 6);
    end = end < size ? end : size;
    uint32_t cursor = ID3_HEADER;
    if (version >= 3 && (header[5] & ID3_EXTENDED) != 0 && source_read(source, cursor, header, 4)) {
        cursor += version == 4 ? synchsafe(header) : be32(header) + 4;
    }
    uint32_t header_size = version == 2 ? ID3_V22_FRAME_HEADER : ID3_HEADER;
    while (cursor <= end && end - cursor >= header_size &&
           source_read(source, cursor, header, header_size) && header[0] != 0) {
        uint32_t frame_size = id3_frame_size(version, header);
        uint32_t data = cursor + header_size;
        if (frame_size > end - data) {
            return;
        }
        Span span = {.offset = data, .end = data + frame_size, .unsync = false, .found = false};
        if (id3_usable(version, version == 2 ? 0 : header[9], tag_unsync, &span)) {
            id3_keep(frames, (const char *)header, version, span);
        }
        cursor = data + frame_size;
    }
}

static bool id3_read(ByteSource source, uint32_t size, Lyrics *lyrics)
{
    Id3Frames frames = {.uslt = {.found = false}, .sylt = {.found = false}};
    id3_find(source, size, &frames);
    return (frames.sylt.found && read_sylt(source, &frames.sylt, lyrics)) ||
           (frames.uslt.found && read_uslt(source, &frames.uslt, lyrics));
}

static bool vorbis_find(ByteSource source, uint32_t offset, uint32_t limit, Span *found)
{
    uint8_t word[KEY_PREFIX];
    if (offset > limit || limit - offset < 4 || !source_read(source, offset, word, 4)) {
        return false;
    }
    uint32_t cursor = offset + 4 + le32(word);
    if (cursor < offset || cursor > limit || limit - cursor < 4 ||
        !source_read(source, cursor, word, 4)) {
        return false;
    }
    uint32_t count = le32(word);
    cursor += 4;
    found->found = false;
    for (uint32_t entry = 0; entry < count; entry++) {
        if (cursor > limit || limit - cursor < 4 || !source_read(source, cursor, word, 4)) {
            break;
        }
        uint32_t length = le32(word);
        cursor += 4;
        if (length > limit - cursor) {
            break;
        }
        uint32_t prefix = length < KEY_PREFIX ? length : KEY_PREFIX;
        if (!source_read(source, cursor, word, prefix)) {
            break;
        }
        uint32_t key = 0;
        while (key < prefix && word[key] != '=') {
            key++;
        }
        bool lyrics = key < prefix && same_folded((const char *)word, key, "lyrics");
        bool unsynced = key < prefix && same_folded((const char *)word, key, "unsyncedlyrics");
        if (lyrics || (unsynced && !found->found)) {
            *found = (Span){
                .offset = cursor + key + 1, .end = cursor + length, .unsync = false, .found = true};
        }
        if (lyrics) {
            return true;
        }
        cursor += length;
    }
    return found->found;
}

static uint32_t skip_id3(ByteSource source, uint32_t size)
{
    uint32_t offset = 0;
    uint8_t header[ID3_HEADER];
    while (size - offset >= ID3_HEADER && source_read(source, offset, header, sizeof header) &&
           same_text((const char *)header, 3, "ID3")) {
        uint32_t length = ID3_HEADER + synchsafe(header + 6);
        if (length > size - offset) {
            return size;
        }
        offset += length;
    }
    return offset;
}

static bool flac_read(ByteSource source, uint32_t size, Lyrics *lyrics)
{
    uint32_t offset = skip_id3(source, size);
    uint8_t header[4];
    if (size - offset < 4 || !source_read(source, offset, header, 4) ||
        !same_text((const char *)header, 4, "fLaC")) {
        return false;
    }
    offset += 4;
    bool last = false;
    while (!last && size - offset >= 4 && source_read(source, offset, header, 4)) {
        last = (header[0] & 0x80) != 0;
        uint32_t length = (uint32_t)header[1] << 16 | (uint32_t)header[2] << 8 | header[3];
        uint32_t body = offset + 4;
        if (length > size - body) {
            return false;
        }
        Span span = {.found = false};
        if ((header[0] & 0x7F) == FLAC_VORBIS_COMMENT) {
            return vorbis_find(source, body, body + length, &span) &&
                   read_value(source, &span, ENCODING_UTF8, lyrics);
        }
        offset = body + length;
    }
    return false;
}

static bool ogg_page(const OggStream *stream, uint32_t offset, uint32_t *header_length,
                     uint32_t *body_length)
{
    uint8_t header[OGG_HEADER];
    uint8_t segments[255];
    if (offset > stream->size || stream->size - offset < OGG_HEADER ||
        !source_read(stream->file, offset, header, sizeof header) ||
        !same_text((const char *)header, 4, "OggS") ||
        !source_read(stream->file, offset + OGG_HEADER, segments, header[26])) {
        return false;
    }
    uint32_t body = 0;
    for (uint32_t index = 0; index < header[26]; index++) {
        body += segments[index];
    }
    *header_length = OGG_HEADER + header[26];
    *body_length = body;
    return true;
}

static bool ogg_rewind(OggStream *stream)
{
    stream->page_offset = stream->first_offset;
    stream->page_start = 0;
    return ogg_page(stream, stream->page_offset, &stream->page_header, &stream->page_body);
}

static bool ogg_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    OggStream *stream = context;
    uint8_t *out = buffer;
    if (offset < stream->page_start && !ogg_rewind(stream)) {
        return false;
    }
    while (length > 0) {
        while (offset - stream->page_start >= stream->page_body) {
            stream->page_start += stream->page_body;
            stream->page_offset += stream->page_header + stream->page_body;
            if (!ogg_page(stream, stream->page_offset, &stream->page_header, &stream->page_body)) {
                return false;
            }
        }
        uint32_t within = offset - stream->page_start;
        uint32_t chunk = stream->page_body - within;
        chunk = chunk < length ? chunk : length;
        if (!source_read(stream->file, stream->page_offset + stream->page_header + within, out,
                         chunk)) {
            return false;
        }
        out += chunk;
        offset += chunk;
        length -= chunk;
    }
    return true;
}

static bool ogg_read(ByteSource file, uint32_t size, Lyrics *lyrics)
{
    OggStream stream = {.file = file, .size = size};
    uint32_t header = 0;
    uint32_t body = 0;
    if (!ogg_page(&stream, 0, &header, &body)) {
        return false;
    }
    stream.first_offset = header + body;
    if (!ogg_rewind(&stream)) {
        return false;
    }
    ByteSource source = {.read = ogg_stream_read, .context = &stream};
    uint8_t prefix[OPUS_TAGS_PREFIX];
    if (!source_read(source, 0, prefix, sizeof prefix)) {
        return false;
    }
    uint32_t start =
        same_text((const char *)prefix, OPUS_TAGS_PREFIX, "OpusTags")        ? OPUS_TAGS_PREFIX
        : prefix[0] == 3 && same_text((const char *)prefix + 1, 6, "vorbis") ? VORBIS_COMMENT_PREFIX
                                                                             : 0;
    Span span = {.found = false};
    return start > 0 && vorbis_find(source, start, UINT32_MAX, &span) &&
           read_value(source, &span, ENCODING_UTF8, lyrics);
}

typedef struct Atom {
    uint32_t body;
    uint32_t end;
} Atom;

static bool find_atom(ByteSource source, uint32_t start, uint32_t end, const char *type,
                      Atom *found)
{
    uint8_t header[16];
    uint32_t cursor = start;
    while (cursor <= end && end - cursor >= ATOM_HEADER &&
           source_read(source, cursor, header, ATOM_HEADER)) {
        uint64_t size = be32(header);
        uint32_t body = cursor + ATOM_HEADER;
        if (size == 1) {
            if (end - body < 8 || !source_read(source, body, header + 8, 8)) {
                return false;
            }
            size = (uint64_t)be32(header + 8) << 32 | be32(header + 12);
            body += 8;
        } else if (size == 0) {
            size = end - cursor;
        }
        if (size < body - cursor || size > end - cursor) {
            return false;
        }
        if (same_text((const char *)header + 4, 4, type)) {
            *found = (Atom){.body = body, .end = cursor + (uint32_t)size};
            return true;
        }
        cursor += (uint32_t)size;
    }
    return false;
}

static bool mp4_read(ByteSource source, uint32_t size, Lyrics *lyrics)
{
    static const char *const PATH[] = {"moov", "udta", "meta", "ilst", "\xa9lyr", "data"};
    Atom atom = {.body = 0, .end = size};
    for (size_t index = 0; index < sizeof PATH / sizeof PATH[0]; index++) {
        uint32_t start = atom.body;
        if (index == 3) {
            start += 4;
        }
        if (start > atom.end || !find_atom(source, start, atom.end, PATH[index], &atom)) {
            return false;
        }
    }
    if (atom.end - atom.body < MP4_DATA_HEADER) {
        return false;
    }
    Span span = {.offset = atom.body + MP4_DATA_HEADER, .end = atom.end, .found = true};
    return read_value(source, &span, ENCODING_UTF8, lyrics);
}

bool embedded_lyrics_read(ByteSource file, uint32_t size, const char *name, Lyrics *lyrics)
{
    bool found = false;
    switch (container_for(name)) {
    case CONTAINER_ID3:
        found = id3_read(file, size, lyrics);
        break;
    case CONTAINER_FLAC:
        found = flac_read(file, size, lyrics);
        break;
    case CONTAINER_OGG:
        found = ogg_read(file, size, lyrics);
        break;
    case CONTAINER_MP4:
        found = mp4_read(file, size, lyrics);
        break;
    case CONTAINER_NONE:
        break;
    }
    if (found) {
        lyrics_finish(lyrics);
    }
    return found;
}
