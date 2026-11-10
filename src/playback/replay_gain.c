#include "playback/replay_gain.h"

#include <stddef.h>

enum {
    FIELD_CAPACITY = 48,
    FRAME_CAPACITY = 160,
    ID3_HEADER = 10,
    ID3_V22_FRAME_HEADER = 6,
    ID3_UNSYNC = 0x80,
    ID3_EXTENDED = 0x40,
    V23_COMPRESS_OR_ENCRYPT = 0xC0,
    V23_GROUP = 0x20,
    V24_GROUP = 0x40,
    V24_COMPRESS_OR_ENCRYPT = 0x0C,
    V24_UNSYNC = 0x02,
    V24_DATA_LENGTH = 0x01,
    FLAC_VORBIS_COMMENT = 4,
    OGG_HEADER = 27,
    OPUS_HEAD_SIZE = 19,
    OPUS_HEAD_GAIN = 16,
    OPUS_TAGS_PREFIX = 8,
    VORBIS_COMMENT_PREFIX = 7,
    ATOM_HEADER = 8,
    ATOM_LARGE_HEADER = 16,
    ATOM_FULL_BOX = 4,
    ATOM_DATA_PREFIX = 8,
    Q78_ONE = 256,
    R128_TO_REPLAY_GAIN_CDB = 500
};

typedef enum Container {
    CONTAINER_NONE,
    CONTAINER_ID3,
    CONTAINER_FLAC,
    CONTAINER_OGG,
    CONTAINER_MP4
} Container;

typedef enum GainKey {
    KEY_NONE,
    KEY_TRACK,
    KEY_ALBUM,
    KEY_R128_TRACK,
    KEY_R128_ALBUM
} GainKey;

typedef struct Gains {
    ReplayGain gain;
    bool from_r128[2];
} Gains;

typedef struct Range {
    uint32_t start;
    uint32_t end;
} Range;

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

static bool same_bytes(const uint8_t *bytes, const char *wanted, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (bytes[index] != (uint8_t)wanted[index]) {
            return false;
        }
    }
    return true;
}

static int16_t clamp_cdb(int32_t cdb)
{
    return (int16_t)(cdb > REPLAY_GAIN_LIMIT_CDB    ? REPLAY_GAIN_LIMIT_CDB
                     : cdb < -REPLAY_GAIN_LIMIT_CDB ? -REPLAY_GAIN_LIMIT_CDB
                                                    : cdb);
}

static uint32_t skip_spaces(const char *text, uint32_t length, uint32_t at)
{
    while (at < length && (text[at] == ' ' || text[at] == '\t')) {
        at++;
    }
    return at;
}

static bool read_sign(const char *text, uint32_t length, uint32_t *at)
{
    bool negative = *at < length && text[*at] == '-';
    *at += *at < length && (text[*at] == '-' || text[*at] == '+') ? 1u : 0u;
    return negative;
}

static bool is_digit(char character)
{
    return character >= '0' && character <= '9';
}

bool replay_gain_parse(const char *text, uint32_t length, int16_t *cdb)
{
    uint32_t at = skip_spaces(text, length, 0);
    bool negative = read_sign(text, length, &at);
    int32_t milli = 0;
    bool digits = false;
    for (; at < length && is_digit(text[at]); at++) {
        milli = milli < 1000000 ? milli * 10 + (text[at] - '0') * 1000 : milli;
        digits = true;
    }
    int32_t place = 100;
    if (at < length && text[at] == '.') {
        for (at++; at < length && is_digit(text[at]); at++) {
            milli += (text[at] - '0') * place;
            place /= 10;
            digits = true;
        }
    }
    if (!digits) {
        return false;
    }
    int32_t rounded = (milli + 5) / 10;
    *cdb = clamp_cdb(negative ? -rounded : rounded);
    return true;
}

static bool parse_q78(const char *text, uint32_t length, int16_t *cdb)
{
    uint32_t at = skip_spaces(text, length, 0);
    bool negative = read_sign(text, length, &at);
    int32_t value = 0;
    uint32_t first = at;
    for (; at < length && is_digit(text[at]) && value < 100000; at++) {
        value = value * 10 + (text[at] - '0');
    }
    if (at == first) {
        return false;
    }
    int32_t rounded = (value * 100 + Q78_ONE / 2) / Q78_ONE;
    *cdb = clamp_cdb((negative ? -rounded : rounded) + R128_TO_REPLAY_GAIN_CDB);
    return true;
}

int16_t replay_gain_pick(ReplayGain gain, bool album)
{
    if (album && gain.has_album) {
        return gain.album_cdb;
    }
    return gain.has_track ? gain.track_cdb : 0;
}

static GainKey key_of(const char *key, uint32_t length)
{
    return same_folded(key, length, "replaygain_track_gain")   ? KEY_TRACK
           : same_folded(key, length, "replaygain_album_gain") ? KEY_ALBUM
           : same_folded(key, length, "r128_track_gain")       ? KEY_R128_TRACK
           : same_folded(key, length, "r128_album_gain")       ? KEY_R128_ALBUM
                                                               : KEY_NONE;
}

static void store(Gains *gains, GainKey key, const char *value, uint32_t length)
{
    bool album = key == KEY_ALBUM || key == KEY_R128_ALBUM;
    bool r128 = key == KEY_R128_TRACK || key == KEY_R128_ALBUM;
    int16_t cdb = 0;
    bool parsed = r128 ? parse_q78(value, length, &cdb) : replay_gain_parse(value, length, &cdb);
    if (key == KEY_NONE || !parsed || (!r128 && gains->from_r128[album])) {
        return;
    }
    gains->from_r128[album] = gains->from_r128[album] || r128;
    if (album) {
        gains->gain.album_cdb = cdb;
        gains->gain.has_album = true;
    } else {
        gains->gain.track_cdb = cdb;
        gains->gain.has_track = true;
    }
}

static void store_field(Gains *gains, const char *field, uint32_t length)
{
    uint32_t key = 0;
    while (key < length && field[key] != '=') {
        key++;
    }
    if (key < length) {
        store(gains, key_of(field, key), field + key + 1, length - key - 1);
    }
}

static bool vorbis_walk(ByteSource source, Range range, Gains *gains)
{
    uint8_t word[4];
    char field[FIELD_CAPACITY];
    if (range.end - range.start < 8 || !source_read(source, range.start, word, 4)) {
        return false;
    }
    uint32_t cursor = range.start + 4 + le32(word);
    if (cursor < range.start || cursor > range.end - 4 || !source_read(source, cursor, word, 4)) {
        return false;
    }
    uint32_t count = le32(word);
    cursor += 4;
    for (uint32_t entry = 0; entry < count && cursor <= range.end - 4; entry++) {
        if (!source_read(source, cursor, word, 4) || le32(word) > range.end - cursor - 4) {
            break;
        }
        uint32_t length = le32(word);
        uint32_t prefix = length < FIELD_CAPACITY ? length : FIELD_CAPACITY;
        if (!source_read(source, cursor + 4, field, prefix)) {
            break;
        }
        store_field(gains, field, prefix);
        cursor += 4 + length;
    }
    return true;
}

static uint32_t id3_frame_size(uint8_t version, const uint8_t *header)
{
    if (version == 2) {
        return (uint32_t)header[3] << 16 | (uint32_t)header[4] << 8 | header[5];
    }
    bool safe = ((header[4] | header[5] | header[6] | header[7]) & 0x80) == 0;
    return version == 4 && safe ? synchsafe(header + 4) : be32(header + 4);
}

static uint32_t id3_skip_flags(uint8_t version, uint8_t flags, bool *unsync)
{
    if (version == 3) {
        return (flags & V23_COMPRESS_OR_ENCRYPT) != 0 ? UINT32_MAX
                                                      : ((flags & V23_GROUP) != 0 ? 1u : 0u);
    }
    if (version == 4) {
        *unsync = *unsync || (flags & V24_UNSYNC) != 0;
        return (flags & V24_COMPRESS_OR_ENCRYPT) != 0
                   ? UINT32_MAX
                   : ((flags & V24_GROUP) != 0 ? 1u : 0u) +
                         ((flags & V24_DATA_LENGTH) != 0 ? 4u : 0u);
    }
    return 0;
}

static uint32_t remove_unsync(uint8_t *bytes, uint32_t length)
{
    uint32_t kept = 0;
    for (uint32_t index = 0; index < length; index++) {
        bool dropped = index > 0 && bytes[index] == 0 && bytes[index - 1] == 0xFF;
        if (!dropped) {
            bytes[kept++] = bytes[index];
        }
    }
    return kept;
}

static uint32_t id3_string(const uint8_t *bytes, uint32_t length, uint8_t encoding, uint32_t at,
                           char *out)
{
    bool wide = encoding == 1 || encoding == 2;
    bool big = encoding == 2;
    if (wide && length - at >= 2 && (bytes[at] == 0xFE || bytes[at] == 0xFF)) {
        big = bytes[at] == 0xFE;
        at += 2;
    }
    uint32_t used = 0;
    uint32_t step = wide ? 2 : 1;
    for (; length - at >= step; at += step) {
        uint32_t unit = !wide ? bytes[at]
                        : big ? bytes[at + 1] | (uint32_t)bytes[at] << 8
                              : bytes[at] | (uint32_t)bytes[at + 1] << 8;
        if (unit == 0) {
            at += step;
            break;
        }
        if (used < FIELD_CAPACITY) {
            out[used++] = unit < 0x80 ? (char)unit : '?';
        }
    }
    out[used < FIELD_CAPACITY ? used : FIELD_CAPACITY - 1] = '\0';
    return at;
}

static uint32_t text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static void id3_txxx(ByteSource source, Range frame, bool unsync, Gains *gains)
{
    uint8_t bytes[FRAME_CAPACITY];
    uint32_t length = frame.end - frame.start;
    length = length < FRAME_CAPACITY ? length : FRAME_CAPACITY;
    if (length < 2 || !source_read(source, frame.start, bytes, length)) {
        return;
    }
    length = unsync ? remove_unsync(bytes, length) : length;
    char key[FIELD_CAPACITY];
    char value[FIELD_CAPACITY];
    uint32_t at = id3_string(bytes, length, bytes[0], 1, key);
    id3_string(bytes, length, bytes[0], at, value);
    store(gains, key_of(key, text_length(key)), value, text_length(value));
}

static bool id3_txxx_id(uint8_t version, const uint8_t *header)
{
    return version == 2 ? same_bytes(header, "TXX", 3) : same_bytes(header, "TXXX", 4);
}

static uint32_t id3_frames_start(ByteSource source, uint8_t version, uint8_t flags)
{
    uint8_t word[4];
    if (version < 3 || (flags & ID3_EXTENDED) == 0 || !source_read(source, ID3_HEADER, word, 4)) {
        return ID3_HEADER;
    }
    return ID3_HEADER + (version == 4 ? synchsafe(word) : be32(word) + 4);
}

static void id3_walk(ByteSource source, uint32_t size, Gains *gains)
{
    uint8_t header[ID3_HEADER];
    if (size < ID3_HEADER || !source_read(source, 0, header, sizeof header) ||
        !same_bytes(header, "ID3", 3) || header[3] < 2 || header[3] > 4) {
        return;
    }
    uint8_t version = header[3];
    bool tag_unsync = (header[5] & ID3_UNSYNC) != 0;
    if (tag_unsync && version < 4) {
        return;
    }
    uint32_t end = ID3_HEADER + synchsafe(header + 6);
    end = end < size ? end : size;
    uint32_t cursor = id3_frames_start(source, version, header[5]);
    uint32_t header_size = version == 2 ? ID3_V22_FRAME_HEADER : ID3_HEADER;
    while (cursor <= end && end - cursor >= header_size &&
           source_read(source, cursor, header, header_size) && header[0] != 0) {
        uint32_t data = cursor + header_size;
        uint32_t frame_size = id3_frame_size(version, header);
        if (frame_size > end - data) {
            return;
        }
        bool unsync = tag_unsync;
        uint32_t skip = id3_skip_flags(version, version == 2 ? 0 : header[9], &unsync);
        if (id3_txxx_id(version, header) && skip < frame_size) {
            id3_txxx(source, (Range){data + skip, data + frame_size}, unsync, gains);
        }
        cursor = data + frame_size;
    }
}

static uint32_t skip_id3(ByteSource source, uint32_t size)
{
    uint32_t offset = 0;
    uint8_t header[ID3_HEADER];
    while (size - offset >= ID3_HEADER && source_read(source, offset, header, sizeof header) &&
           same_bytes(header, "ID3", 3)) {
        uint32_t length = ID3_HEADER + synchsafe(header + 6);
        if (length > size - offset) {
            return size;
        }
        offset += length;
    }
    return offset;
}

static void flac_walk(ByteSource source, uint32_t size, Gains *gains)
{
    uint32_t offset = skip_id3(source, size);
    uint8_t header[4];
    if (size - offset < 4 || !source_read(source, offset, header, 4) ||
        !same_bytes(header, "fLaC", 4)) {
        return;
    }
    offset += 4;
    bool last = false;
    while (!last && size - offset >= 4 && source_read(source, offset, header, 4)) {
        last = (header[0] & 0x80) != 0;
        uint32_t length = (uint32_t)header[1] << 16 | (uint32_t)header[2] << 8 | header[3];
        uint32_t body = offset + 4;
        if (length > size - body) {
            return;
        }
        if ((header[0] & 0x7F) == FLAC_VORBIS_COMMENT) {
            vorbis_walk(source, (Range){body, body + length}, gains);
            return;
        }
        offset = body + length;
    }
}

static bool ogg_page(const OggStream *stream, uint32_t offset, uint32_t *header_length,
                     uint32_t *body_length)
{
    uint8_t header[OGG_HEADER];
    uint8_t segments[255];
    if (offset > stream->size || stream->size - offset < OGG_HEADER ||
        !source_read(stream->file, offset, header, sizeof header) ||
        !same_bytes(header, "OggS", 4) ||
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

static bool ogg_next_page(OggStream *stream)
{
    stream->page_start += stream->page_body;
    stream->page_offset += stream->page_header + stream->page_body;
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
            if (!ogg_next_page(stream)) {
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

static void add_header_gain(ReplayGain *gain, int16_t header_cdb)
{
    if (header_cdb == 0) {
        return;
    }
    gain->track_cdb = clamp_cdb(gain->track_cdb + header_cdb);
    gain->album_cdb = clamp_cdb(gain->album_cdb + header_cdb);
    gain->has_track = true;
}

static int16_t opus_header_gain(const OggStream *stream, uint32_t body_offset)
{
    uint8_t head[OPUS_HEAD_SIZE];
    if (!source_read(stream->file, body_offset, head, sizeof head) ||
        !same_bytes(head, "OpusHead", 8)) {
        return 0;
    }
    int32_t q78 = (int16_t)(head[OPUS_HEAD_GAIN] | head[OPUS_HEAD_GAIN + 1] << 8);
    int32_t scaled = q78 * 100;
    return clamp_cdb((scaled + (scaled >= 0 ? Q78_ONE / 2 : -Q78_ONE / 2)) / Q78_ONE);
}

static void ogg_walk(ByteSource file, uint32_t size, Gains *gains)
{
    OggStream stream = {.file = file, .size = size};
    uint32_t header = 0;
    uint32_t body = 0;
    if (!ogg_page(&stream, 0, &header, &body)) {
        return;
    }
    int16_t header_cdb = opus_header_gain(&stream, header);
    stream.first_offset = header + body;
    if (!ogg_rewind(&stream)) {
        return;
    }
    ByteSource source = {.read = ogg_stream_read, .context = &stream};
    uint8_t prefix[OPUS_TAGS_PREFIX];
    if (!source_read(source, 0, prefix, sizeof prefix)) {
        return;
    }
    uint32_t start = same_bytes(prefix, "OpusTags", OPUS_TAGS_PREFIX)        ? OPUS_TAGS_PREFIX
                     : prefix[0] == 3 && same_bytes(prefix + 1, "vorbis", 6) ? VORBIS_COMMENT_PREFIX
                                                                             : 0;
    if (start > 0) {
        vorbis_walk(source, (Range){start, UINT32_MAX}, gains);
    }
    add_header_gain(&gains->gain, header_cdb);
}

static bool atom_at(ByteSource source, uint32_t offset, Range within, Range *body, uint8_t type[4])
{
    uint8_t header[ATOM_LARGE_HEADER];
    if (offset > within.end || within.end - offset < ATOM_HEADER ||
        !source_read(source, offset, header, ATOM_HEADER)) {
        return false;
    }
    uint32_t size = be32(header);
    uint32_t header_size = ATOM_HEADER;
    if (size == 1) {
        if (within.end - offset < ATOM_LARGE_HEADER ||
            !source_read(source, offset + ATOM_HEADER, header + ATOM_HEADER, 8)) {
            return false;
        }
        size = be32(header + 8) != 0 ? within.end - offset : be32(header + 12);
        header_size = ATOM_LARGE_HEADER;
    }
    size = size == 0 ? within.end - offset : size;
    if (size < header_size || size > within.end - offset) {
        return false;
    }
    for (int index = 0; index < 4; index++) {
        type[index] = header[4 + index];
    }
    *body = (Range){offset + header_size, offset + size};
    return true;
}

static bool atom_find(ByteSource source, Range within, const char *wanted, Range *found)
{
    uint8_t type[4];
    Range body;
    for (uint32_t offset = within.start; atom_at(source, offset, within, &body, type);
         offset = body.end) {
        if (same_bytes(type, wanted, 4)) {
            *found = body;
            return true;
        }
    }
    return false;
}

static uint32_t atom_text(ByteSource source, Range body, uint32_t skip, char *text)
{
    if (body.end - body.start < skip) {
        return 0;
    }
    uint32_t length = body.end - body.start - skip;
    length = length < FIELD_CAPACITY ? length : FIELD_CAPACITY;
    return source_read(source, body.start + skip, text, length) ? length : 0;
}

static void mp4_freeform(ByteSource source, Range item, Gains *gains)
{
    Range name;
    Range data;
    char key[FIELD_CAPACITY];
    char value[FIELD_CAPACITY];
    if (!atom_find(source, item, "name", &name) || !atom_find(source, item, "data", &data)) {
        return;
    }
    uint32_t key_length = atom_text(source, name, ATOM_FULL_BOX, key);
    uint32_t value_length = atom_text(source, data, ATOM_DATA_PREFIX, value);
    store(gains, key_of(key, key_length), value, value_length);
}

static bool mp4_items(ByteSource source, uint32_t size, Range *items)
{
    Range moov;
    Range udta;
    Range meta;
    if (!atom_find(source, (Range){0, size}, "moov", &moov) ||
        !atom_find(source, moov, "udta", &udta) || !atom_find(source, udta, "meta", &meta)) {
        return false;
    }
    Range full = {meta.start + ATOM_FULL_BOX, meta.end};
    return (full.start <= full.end && atom_find(source, full, "ilst", items)) ||
           atom_find(source, meta, "ilst", items);
}

static void mp4_walk(ByteSource source, uint32_t size, Gains *gains)
{
    Range items;
    if (!mp4_items(source, size, &items)) {
        return;
    }
    uint8_t type[4];
    Range item;
    for (uint32_t offset = items.start; atom_at(source, offset, items, &item, type);
         offset = item.end) {
        if (same_bytes(type, "----", 4)) {
            mp4_freeform(source, item, gains);
        }
    }
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
    const char *extension = dot + 1;
    uint32_t length = text_length(extension);
    if (same_folded(extension, length, "mp3")) {
        return CONTAINER_ID3;
    }
    if (same_folded(extension, length, "flac")) {
        return CONTAINER_FLAC;
    }
    if (same_folded(extension, length, "ogg") || same_folded(extension, length, "opus")) {
        return CONTAINER_OGG;
    }
    return same_folded(extension, length, "m4a") || same_folded(extension, length, "mp4") ||
                   same_folded(extension, length, "m4b")
               ? CONTAINER_MP4
               : CONTAINER_NONE;
}

bool replay_gain_read(ByteSource file, uint32_t size, const char *name, ReplayGain *gain)
{
    Gains gains = {.gain = {.track_cdb = 0, .album_cdb = 0, .has_track = false, .has_album = false},
                   .from_r128 = {false, false}};
    switch (container_for(name)) {
    case CONTAINER_ID3:
        id3_walk(file, size, &gains);
        break;
    case CONTAINER_FLAC:
        flac_walk(file, size, &gains);
        break;
    case CONTAINER_OGG:
        ogg_walk(file, size, &gains);
        break;
    case CONTAINER_MP4:
        mp4_walk(file, size, &gains);
        break;
    case CONTAINER_NONE:
        break;
    }
    *gain = gains.gain;
    return gain->has_track || gain->has_album;
}
