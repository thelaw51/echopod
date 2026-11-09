#include <string.h>

#include "check.h"
#include "playback/embedded_lyrics.h"

enum {
    FILE_CAPACITY = 300000,
    PART_CAPACITY = 200000
};

typedef struct Bytes {
    uint8_t data[FILE_CAPACITY];
    uint32_t size;
} Bytes;

typedef struct CountingSource {
    const Bytes *bytes;
    uint32_t requested;
} CountingSource;

static Bytes file;
static Bytes frame;
static Bytes packet;
static Lyrics lyrics;
static CountingSource counter;

static void add(Bytes *bytes, const void *data, uint32_t length)
{
    memcpy(bytes->data + bytes->size, data, length);
    bytes->size += length;
}

static void add_text(Bytes *bytes, const char *text)
{
    add(bytes, text, (uint32_t)strlen(text));
}

static void add_byte(Bytes *bytes, uint8_t value)
{
    add(bytes, &value, 1);
}

static void add_be32(Bytes *bytes, uint32_t value)
{
    uint8_t word[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8),
                       (uint8_t)value};
    add(bytes, word, 4);
}

static void add_le32(Bytes *bytes, uint32_t value)
{
    uint8_t word[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                       (uint8_t)(value >> 24)};
    add(bytes, word, 4);
}

static void put_synchsafe(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 21 & 0x7F);
    at[1] = (uint8_t)(value >> 14 & 0x7F);
    at[2] = (uint8_t)(value >> 7 & 0x7F);
    at[3] = (uint8_t)(value & 0x7F);
}

static void put_be32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
}

static uint32_t next_code_point(const char **text)
{
    const uint8_t *bytes = (const uint8_t *)*text;
    uint32_t extra = bytes[0] >= 0xF0 ? 3 : bytes[0] >= 0xE0 ? 2 : bytes[0] >= 0xC0 ? 1 : 0;
    uint32_t code_point = bytes[0] & (0x7Fu >> extra);
    for (uint32_t index = 1; index <= extra; index++) {
        code_point = code_point << 6 | (bytes[index] & 0x3Fu);
    }
    *text += 1 + extra;
    return code_point;
}

static void add_unit(Bytes *bytes, uint32_t unit, bool big)
{
    add_byte(bytes, (uint8_t)(big ? unit >> 8 : unit));
    add_byte(bytes, (uint8_t)(big ? unit : unit >> 8));
}

static void add_utf16(Bytes *bytes, const char *text, bool big)
{
    if (!big) {
        add_unit(bytes, 0xFEFF, false);
    }
    while (*text != '\0') {
        uint32_t code_point = next_code_point(&text);
        if (code_point >= 0x10000) {
            add_unit(bytes, 0xD800 + ((code_point - 0x10000) >> 10), big);
            add_unit(bytes, 0xDC00 + ((code_point - 0x10000) & 0x3FF), big);
        } else {
            add_unit(bytes, code_point, big);
        }
    }
}

static void add_encoded(Bytes *bytes, uint8_t encoding, const char *text, bool terminated)
{
    bool wide = encoding == 1 || encoding == 2;
    if (wide) {
        add_utf16(bytes, text, encoding == 2);
    } else {
        add_text(bytes, text);
    }
    if (terminated) {
        add_byte(bytes, 0);
        if (wide) {
            add_byte(bytes, 0);
        }
    }
}

static void make_uslt(uint8_t encoding, const char *description, const char *text)
{
    frame.size = 0;
    add_byte(&frame, encoding);
    add_text(&frame, "eng");
    add_encoded(&frame, encoding, description, true);
    add_encoded(&frame, encoding, text, false);
}

typedef struct SyltEntry {
    const char *text;
    uint32_t time_ms;
} SyltEntry;

static void make_sylt(uint8_t encoding, uint8_t format, const SyltEntry *entries, size_t count)
{
    frame.size = 0;
    add_byte(&frame, encoding);
    add_text(&frame, "eng");
    add_byte(&frame, format);
    add_byte(&frame, 1);
    add_encoded(&frame, encoding, "Description", true);
    for (size_t index = 0; index < count; index++) {
        add_encoded(&frame, encoding, entries[index].text, true);
        add_be32(&frame, entries[index].time_ms);
    }
}

static void id3_start(uint8_t version, uint8_t flags)
{
    file.size = 0;
    add_text(&file, "ID3");
    add_byte(&file, version);
    add_byte(&file, 0);
    add_byte(&file, flags);
    add_be32(&file, 0);
}

static void id3_frame(uint8_t version, const char *id, uint8_t flags, const Bytes *data)
{
    if (version == 2) {
        add(&file, id, 3);
        add_byte(&file, (uint8_t)(data->size >> 16));
        add_byte(&file, (uint8_t)(data->size >> 8));
        add_byte(&file, (uint8_t)data->size);
    } else {
        add(&file, id, 4);
        add_be32(&file, 0);
        if (version == 4) {
            put_synchsafe(file.data + file.size - 4, data->size);
        } else {
            put_be32(file.data + file.size - 4, data->size);
        }
        add_byte(&file, 0);
        add_byte(&file, flags);
    }
    add(&file, data->data, data->size);
}

static void id3_finish(void)
{
    static const uint8_t PADDING[16] = {0};
    add(&file, PADDING, sizeof PADDING);
    put_synchsafe(file.data + 6, file.size - 10);
    static const uint8_t AUDIO[8] = {0xFF, 0xFB, 0x90, 0x64, 0, 0, 0, 0};
    add(&file, AUDIO, sizeof AUDIO);
}

static void add_text_frame(uint8_t version, const char *id, const char *text)
{
    frame.size = 0;
    add_byte(&frame, 3);
    add_text(&frame, text);
    id3_frame(version, id, 0, &frame);
}

static bool counted_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    CountingSource *source = context;
    source->requested += length;
    if (offset > source->bytes->size || length > source->bytes->size - offset) {
        return false;
    }
    memcpy(buffer, source->bytes->data + offset, length);
    return true;
}

static bool read_file(const char *name)
{
    counter = (CountingSource){.bytes = &file, .requested = 0};
    lyrics_start(&lyrics);
    ByteSource source = {.read = counted_read, .context = &counter};
    return embedded_lyrics_read(source, file.size, name, &lyrics);
}

static bool line_is(int index, const char *expected)
{
    return strcmp(expected, lyrics_line(&lyrics, index)) == 0;
}

static void latin1_uslt_in_id3v23_reads_as_unsynced_lines(void)
{
    id3_start(3, 0);
    add_text_frame(3, "TIT2", "Title");
    make_uslt(0, "", "Caf\xe9 au lait\nSecond line\n");
    id3_frame(3, "USLT", 0, &frame);
    id3_finish();
    CHECK(read_file("/Music/song.mp3"));
    CHECK(!lyrics.synced);
    CHECK_EQUAL(2, lyrics.count);
    CHECK(line_is(0, "Caf\xc3\xa9 au lait"));
    CHECK(line_is(1, "Second line"));
}

static void utf16_uslt_decodes_surrogates_and_crlf(void)
{
    id3_start(3, 0);
    make_uslt(1, "Lyrics by me", "Notes \xf0\x9f\x8e\xb5 here\r\n\xc3\x85ngstr\xc3\xb6m\r\nEnd");
    id3_frame(3, "USLT", 0, &frame);
    id3_finish();
    CHECK(read_file("song.MP3"));
    CHECK_EQUAL(3, lyrics.count);
    CHECK(line_is(0, "Notes \xf0\x9f\x8e\xb5 here"));
    CHECK(line_is(1, "\xc3\x85ngstr\xc3\xb6m"));
    CHECK(line_is(2, "End"));
}

static void big_endian_utf16_without_a_mark_reads(void)
{
    id3_start(4, 0);
    make_uslt(2, "", "One\nTwo");
    id3_frame(4, "USLT", 0, &frame);
    id3_finish();
    CHECK(read_file("song.mp3"));
    CHECK_EQUAL(2, lyrics.count);
    CHECK(line_is(1, "Two"));
}

static void utf8_uslt_with_lrc_times_is_synced(void)
{
    id3_start(4, 0);
    make_uslt(3, "", "[ar:Someone]\n[00:01.50]First\n[00:04.00]Second\n");
    id3_frame(4, "USLT", 0, &frame);
    id3_finish();
    CHECK(read_file("song.mp3"));
    CHECK(lyrics.synced);
    CHECK_EQUAL(2, lyrics.count);
    CHECK_EQUAL(1500u, lyrics.lines[0].time_ms);
    CHECK(line_is(1, "Second"));
}

static void an_unsynchronised_v24_frame_drops_the_inserted_zeros(void)
{
    static const uint8_t TEXT[] = {'y', 0xFF, 0x00, 'a', '\n', 'b'};
    packet.size = 0;
    add_be32(&packet, 0);
    add_byte(&packet, 0);
    add_text(&packet, "eng");
    add_byte(&packet, 0);
    add(&packet, TEXT, sizeof TEXT);
    id3_start(4, 0);
    id3_frame(4, "USLT", 0x02 | 0x01, &packet);
    id3_finish();
    CHECK(read_file("song.mp3"));
    CHECK_EQUAL(2, lyrics.count);
    CHECK(line_is(0, "y\xc3\xbf"
                     "a"));
    CHECK(line_is(1, "b"));
}

static void id3v22_ult_reads(void)
{
    id3_start(2, 0);
    add_text_frame(2, "TT2", "Title");
    make_uslt(0, "", "Old tag\nStill works");
    id3_frame(2, "ULT", 0, &frame);
    id3_finish();
    CHECK(read_file("song.mp3"));
    CHECK_EQUAL(2, lyrics.count);
    CHECK(line_is(0, "Old tag"));
}

static void utf16_sylt_keeps_each_entry_time(void)
{
    static const SyltEntry ENTRIES[] = {
        {"Intro line", 500},
        {"Second \xc3\xa9", 2250},
        {"Third \xf0\x9f\x8e\xb5", 61005},
    };
    id3_start(3, 0);
    make_sylt(1, 2, ENTRIES, 3);
    id3_frame(3, "SYLT", 0, &frame);
    id3_finish();
    CHECK(read_file("song.mp3"));
    CHECK(lyrics.synced);
    CHECK_EQUAL(3, lyrics.count);
    CHECK_EQUAL(500u, lyrics.lines[0].time_ms);
    CHECK_EQUAL(2250u, lyrics.lines[1].time_ms);
    CHECK_EQUAL(61005u, lyrics.lines[2].time_ms);
    CHECK(line_is(0, "Intro line"));
    CHECK(line_is(1, "Second \xc3\xa9"));
    CHECK(line_is(2, "Third \xf0\x9f\x8e\xb5"));
    CHECK_EQUAL(1, lyrics_current(&lyrics, 3000, 90000));
}

static void karaoke_sylt_joins_syllables_into_lines(void)
{
    static const SyltEntry ENTRIES[] = {
        {"\nHel", 1000}, {"lo ", 1400}, {"there", 1800}, {"\nNext", 3000}, {" line", 3500},
    };
    id3_start(4, 0);
    make_sylt(3, 2, ENTRIES, 5);
    id3_frame(4, "SYLT", 0, &frame);
    id3_finish();
    CHECK(read_file("song.mp3"));
    CHECK_EQUAL(2, lyrics.count);
    CHECK(line_is(0, "Hello there"));
    CHECK(line_is(1, "Next line"));
    CHECK_EQUAL(1000u, lyrics.lines[0].time_ms);
    CHECK_EQUAL(3000u, lyrics.lines[1].time_ms);
}

static void sylt_wins_over_uslt_and_frame_times_fall_back_to_uslt(void)
{
    static const SyltEntry ENTRIES[] = {{"Synced", 1000}};
    id3_start(3, 0);
    make_uslt(0, "", "Plain");
    id3_frame(3, "USLT", 0, &frame);
    make_sylt(0, 2, ENTRIES, 1);
    id3_frame(3, "SYLT", 0, &frame);
    id3_finish();
    CHECK(read_file("song.mp3"));
    CHECK(lyrics.synced);
    CHECK(line_is(0, "Synced"));

    id3_start(3, 0);
    make_sylt(0, 1, ENTRIES, 1);
    id3_frame(3, "SYLT", 0, &frame);
    make_uslt(0, "", "Plain");
    id3_frame(3, "USLT", 0, &frame);
    id3_finish();
    CHECK(read_file("song.mp3"));
    CHECK(!lyrics.synced);
    CHECK_EQUAL(1, lyrics.count);
    CHECK(line_is(0, "Plain"));
}

static void compressed_frames_and_missing_tags_find_nothing(void)
{
    id3_start(3, 0);
    make_uslt(0, "", "Hidden");
    id3_frame(3, "USLT", 0x80, &frame);
    id3_finish();
    CHECK(!read_file("song.mp3"));
    CHECK_EQUAL(0, lyrics.count);

    file.size = 0;
    add_text(&file, "not a tagged file at all");
    CHECK(!read_file("song.mp3"));
    CHECK(!read_file("song.flac"));
    CHECK(!read_file("song.ogg"));
    CHECK(!read_file("song.m4a"));
    CHECK(!read_file("song.wav"));
    CHECK(!read_file("no_extension"));
}

static void add_vorbis_comment(Bytes *bytes, const char *const *entries, size_t count)
{
    add_le32(bytes, 6);
    add_text(bytes, "vendor");
    add_le32(bytes, (uint32_t)count);
    for (size_t index = 0; index < count; index++) {
        add_le32(bytes, (uint32_t)strlen(entries[index]));
        add_text(bytes, entries[index]);
    }
}

static void add_flac_block(uint8_t type, bool last, const Bytes *body)
{
    add_byte(&file, (uint8_t)(type | (last ? 0x80 : 0)));
    add_byte(&file, (uint8_t)(body->size >> 16));
    add_byte(&file, (uint8_t)(body->size >> 8));
    add_byte(&file, (uint8_t)body->size);
    add(&file, body->data, body->size);
}

static void flac_lyrics_win_over_unsynced_lyrics(void)
{
    static const char *const ENTRIES[] = {
        "TITLE=Song",
        "UNSYNCEDLYRICS=Plain words",
        "Lyrics=[00:02.00]Timed one\n[00:05.25]Timed two",
    };
    file.size = 0;
    add_text(&file, "fLaC");
    frame.size = 0;
    for (int index = 0; index < 34; index++) {
        add_byte(&frame, 0);
    }
    add_flac_block(0, false, &frame);
    frame.size = 0;
    for (int index = 0; index < 5000; index++) {
        add_byte(&frame, 0xAB);
    }
    add_flac_block(6, false, &frame);
    frame.size = 0;
    add_vorbis_comment(&frame, ENTRIES, 3);
    add_flac_block(4, true, &frame);
    CHECK(read_file("song.flac"));
    CHECK(lyrics.synced);
    CHECK_EQUAL(2, lyrics.count);
    CHECK_EQUAL(5250u, lyrics.lines[1].time_ms);
    CHECK(line_is(0, "Timed one"));
    CHECK(counter.requested < 1000);
}

static void add_ogg_page(const uint8_t *body, uint32_t length)
{
    add_text(&file, "OggS");
    for (int index = 0; index < 22; index++) {
        add_byte(&file, 0);
    }
    uint32_t segments = length / 255 + 1;
    add_byte(&file, (uint8_t)segments);
    for (uint32_t index = 0; index + 1 < segments; index++) {
        add_byte(&file, 255);
    }
    add_byte(&file, (uint8_t)(length % 255));
    add(&file, body, length);
}

static void add_ogg_packet(const Bytes *bytes, uint32_t page_size)
{
    for (uint32_t offset = 0; offset < bytes->size; offset += page_size) {
        uint32_t length = bytes->size - offset < page_size ? bytes->size - offset : page_size;
        add_ogg_page(bytes->data + offset, length);
    }
}

static void opus_unsynced_lyrics_cross_page_boundaries(void)
{
    static const char *const ENTRIES[] = {
        "ARTIST=Someone",
        "UNSYNCEDLYRICS=A line that is long enough to cross a page\r\n"
        "and another line after the break\r\nand a third",
    };
    file.size = 0;
    packet.size = 0;
    add_text(&packet, "OpusHead");
    for (int index = 0; index < 11; index++) {
        add_byte(&packet, 0);
    }
    add_ogg_page(packet.data, packet.size);
    packet.size = 0;
    add_text(&packet, "OpusTags");
    add_vorbis_comment(&packet, ENTRIES, 2);
    add_ogg_packet(&packet, 40);
    CHECK(read_file("song.opus"));
    CHECK(!lyrics.synced);
    CHECK_EQUAL(3, lyrics.count);
    CHECK(line_is(0, "A line that is long enough to cross a page"));
    CHECK(line_is(1, "and another line after the break"));
    CHECK(line_is(2, "and a third"));
}

static void ogg_vorbis_lyrics_read(void)
{
    static const char *const ENTRIES[] = {"LYRICS=Vorbis words\nMore words"};
    file.size = 0;
    packet.size = 0;
    add_text(&packet, "\x01vorbis");
    for (int index = 0; index < 23; index++) {
        add_byte(&packet, 0);
    }
    add_ogg_page(packet.data, packet.size);
    packet.size = 0;
    add_text(&packet, "\x03vorbis");
    add_vorbis_comment(&packet, ENTRIES, 1);
    add_byte(&packet, 1);
    add_ogg_packet(&packet, 1000);
    CHECK(read_file("song.ogg"));
    CHECK_EQUAL(2, lyrics.count);
    CHECK(line_is(0, "Vorbis words"));
}

static uint32_t atom_start(const char *type)
{
    uint32_t start = file.size;
    add_be32(&file, 0);
    add(&file, type, 4);
    return start;
}

static void atom_end(uint32_t start)
{
    put_be32(file.data + start, file.size - start);
}

static void add_data_atom(const char *text)
{
    uint32_t data = atom_start("data");
    add_be32(&file, 1);
    add_be32(&file, 0);
    add_text(&file, text);
    atom_end(data);
}

static void mp4_lyrics_with_carriage_returns_read(void)
{
    file.size = 0;
    atom_end(atom_start("ftyp"));
    uint32_t moov = atom_start("moov");
    atom_end(atom_start("mvhd"));
    uint32_t udta = atom_start("udta");
    uint32_t meta = atom_start("meta");
    add_be32(&file, 0);
    atom_end(atom_start("hdlr"));
    uint32_t ilst = atom_start("ilst");
    uint32_t title = atom_start("\xa9nam");
    add_data_atom("Title");
    atom_end(title);
    uint32_t lyric = atom_start("\xa9lyr");
    add_data_atom("iTunes line one\rline two\r\rline four");
    atom_end(lyric);
    atom_end(ilst);
    atom_end(meta);
    atom_end(udta);
    atom_end(moov);
    atom_end(atom_start("mdat"));
    CHECK(read_file("song.m4a"));
    CHECK_EQUAL(4, lyrics.count);
    CHECK(line_is(0, "iTunes line one"));
    CHECK(line_is(1, "line two"));
    CHECK(line_is(2, ""));
    CHECK(line_is(3, "line four"));
}

static void a_huge_value_is_read_only_up_to_the_cap(void)
{
    id3_start(3, 0);
    frame.size = 0;
    add_byte(&frame, 0);
    add_text(&frame, "eng");
    add_byte(&frame, 0);
    while (frame.size < PART_CAPACITY) {
        add_text(&frame, "x");
    }
    id3_frame(3, "USLT", 0, &frame);
    id3_finish();
    CHECK(read_file("song.mp3"));
    CHECK(counter.requested < EMBEDDED_LYRICS_READ_LIMIT + 1024);
    CHECK_EQUAL(1, lyrics.count);

    id3_start(3, 0);
    frame.size = 0;
    add_byte(&frame, 0);
    add_text(&frame, "eng");
    add_byte(&frame, 0);
    while (frame.size < PART_CAPACITY) {
        add_text(&frame, "Line\n");
    }
    id3_frame(3, "USLT", 0, &frame);
    id3_finish();
    CHECK(read_file("song.mp3"));
    CHECK(lyrics_full(&lyrics));
    CHECK_EQUAL(LYRICS_LINE_CAPACITY, lyrics.count);
    CHECK(counter.requested < 4096);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(latin1_uslt_in_id3v23_reads_as_unsynced_lines),
        TEST_CASE(utf16_uslt_decodes_surrogates_and_crlf),
        TEST_CASE(big_endian_utf16_without_a_mark_reads),
        TEST_CASE(utf8_uslt_with_lrc_times_is_synced),
        TEST_CASE(an_unsynchronised_v24_frame_drops_the_inserted_zeros),
        TEST_CASE(id3v22_ult_reads),
        TEST_CASE(utf16_sylt_keeps_each_entry_time),
        TEST_CASE(karaoke_sylt_joins_syllables_into_lines),
        TEST_CASE(sylt_wins_over_uslt_and_frame_times_fall_back_to_uslt),
        TEST_CASE(compressed_frames_and_missing_tags_find_nothing),
        TEST_CASE(flac_lyrics_win_over_unsynced_lyrics),
        TEST_CASE(opus_unsynced_lyrics_cross_page_boundaries),
        TEST_CASE(ogg_vorbis_lyrics_read),
        TEST_CASE(mp4_lyrics_with_carriage_returns_read),
        TEST_CASE(a_huge_value_is_read_only_up_to_the_cap),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
