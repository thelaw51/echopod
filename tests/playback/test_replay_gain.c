#include <string.h>

#include "check.h"
#include "playback/replay_gain.h"

enum {
    FILE_CAPACITY = 20000
};

typedef struct Bytes {
    uint8_t data[FILE_CAPACITY];
    uint32_t size;
} Bytes;

static Bytes file;
static Bytes frame;
static Bytes packet;
static ReplayGain gain;

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

static bool memory_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const Bytes *bytes = context;
    if (offset > bytes->size || length > bytes->size - offset) {
        return false;
    }
    memcpy(buffer, bytes->data + offset, length);
    return true;
}

static bool read_file(const char *name)
{
    ByteSource source = {.read = memory_read, .context = &file};
    return replay_gain_read(source, file.size, name, &gain);
}

static int16_t parsed(const char *text)
{
    int16_t cdb = 12345;
    CHECK(replay_gain_parse(text, (uint32_t)strlen(text), &cdb));
    return cdb;
}

static void gains_parse_in_hundredths_of_a_decibel(void)
{
    CHECK_EQUAL(-648, parsed("-6.48 dB"));
    CHECK_EQUAL(301, parsed("+3.005 dB"));
    CHECK_EQUAL(-50, parsed("  -0.5dB"));
    CHECK_EQUAL(0, parsed("0"));
    CHECK_EQUAL(-1235, parsed("-12.345678 dB"));
    CHECK_EQUAL(-25, parsed("-.25 dB"));
}

static void gains_beyond_sixty_decibels_are_clamped_and_text_without_digits_is_refused(void)
{
    CHECK_EQUAL(REPLAY_GAIN_LIMIT_CDB, parsed("70 dB"));
    CHECK_EQUAL(-REPLAY_GAIN_LIMIT_CDB, parsed("-99999999999 dB"));
    int16_t cdb = 7;
    CHECK(!replay_gain_parse("dB", 2, &cdb));
    CHECK(!replay_gain_parse("-", 1, &cdb));
    CHECK(!replay_gain_parse("", 0, &cdb));
    CHECK_EQUAL(7, cdb);
}

static void album_mode_falls_back_to_the_track_gain(void)
{
    ReplayGain both = {.track_cdb = -500, .album_cdb = -700, .has_track = true, .has_album = true};
    ReplayGain track = {.track_cdb = -500, .album_cdb = 0, .has_track = true, .has_album = false};
    ReplayGain none = {.track_cdb = 0, .album_cdb = 0, .has_track = false, .has_album = false};
    CHECK_EQUAL(-500, replay_gain_pick(both, false));
    CHECK_EQUAL(-700, replay_gain_pick(both, true));
    CHECK_EQUAL(-500, replay_gain_pick(track, true));
    CHECK_EQUAL(0, replay_gain_pick(none, true));
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

static void add_utf16(Bytes *bytes, const char *text)
{
    add_byte(bytes, 0xFF);
    add_byte(bytes, 0xFE);
    for (; *text != '\0'; text++) {
        add_byte(bytes, (uint8_t)*text);
        add_byte(bytes, 0);
    }
    add_byte(bytes, 0);
    add_byte(bytes, 0);
}

static void make_txxx(uint8_t encoding, const char *key, const char *value)
{
    frame.size = 0;
    add_byte(&frame, encoding);
    if (encoding == 1) {
        add_utf16(&frame, key);
        add_utf16(&frame, value);
        return;
    }
    add_text(&frame, key);
    add_byte(&frame, 0);
    add_text(&frame, value);
}

static void add_txxx(uint8_t version, uint8_t encoding, const char *key, const char *value)
{
    make_txxx(encoding, key, value);
    id3_frame(version, version == 2 ? "TXX" : "TXXX", 0, &frame);
}

static void id3v24_txxx_frames_give_the_track_and_album_gains(void)
{
    id3_start(4, 0);
    frame.size = 0;
    add_byte(&frame, 3);
    add_text(&frame, "A title");
    id3_frame(4, "TIT2", 0, &frame);
    add_txxx(4, 3, "REPLAYGAIN_TRACK_GAIN", "-7.89 dB");
    add_txxx(4, 3, "REPLAYGAIN_TRACK_PEAK", "0.988");
    add_txxx(4, 3, "REPLAYGAIN_ALBUM_GAIN", "-8.12 dB");
    id3_finish();
    CHECK(read_file("Artist/Album/01 Song.mp3"));
    CHECK(gain.has_track);
    CHECK(gain.has_album);
    CHECK_EQUAL(-789, gain.track_cdb);
    CHECK_EQUAL(-812, gain.album_cdb);
}

static void id3v23_lowercase_and_utf16_keys_read(void)
{
    id3_start(3, 0);
    add_txxx(3, 0, "replaygain_track_gain", "+1.50 dB");
    add_txxx(3, 1, "ReplayGain_Album_Gain", "-2.25 dB");
    id3_finish();
    CHECK(read_file("song.MP3"));
    CHECK_EQUAL(150, gain.track_cdb);
    CHECK_EQUAL(-225, gain.album_cdb);
}

static void id3v22_txx_frames_read(void)
{
    id3_start(2, 0);
    add_txxx(2, 0, "REPLAYGAIN_TRACK_GAIN", "-3.00 dB");
    id3_finish();
    CHECK(read_file("song.mp3"));
    CHECK(gain.has_track);
    CHECK(!gain.has_album);
    CHECK_EQUAL(-300, gain.track_cdb);
}

static void compressed_frames_other_descriptions_and_missing_tags_give_nothing(void)
{
    id3_start(3, 0);
    make_txxx(3, "REPLAYGAIN_TRACK_GAIN", "-3.00 dB");
    id3_frame(3, "TXXX", 0x80, &frame);
    add_txxx(3, 3, "MusicBrainz Album Id", "-4.00 dB");
    id3_finish();
    CHECK(!read_file("song.mp3"));
    CHECK(!gain.has_track);
    file.size = 0;
    add_text(&file, "not a tag at all");
    CHECK(!read_file("song.mp3"));
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

static void flac_vorbis_comments_give_the_gains(void)
{
    static const char *const ENTRIES[] = {
        "TITLE=Song",
        "COMMENT=A comment much longer than the part of each field the reader looks at",
        "replaygain_album_gain=-9.40 dB",
        "REPLAYGAIN_TRACK_GAIN=-10.02 dB",
    };
    file.size = 0;
    add_text(&file, "fLaC");
    frame.size = 0;
    for (int index = 0; index < 34; index++) {
        add_byte(&frame, 0);
    }
    add_flac_block(0, false, &frame);
    frame.size = 0;
    add_vorbis_comment(&frame, ENTRIES, 4);
    add_flac_block(4, true, &frame);
    CHECK(read_file("song.flac"));
    CHECK_EQUAL(-1002, gain.track_cdb);
    CHECK_EQUAL(-940, gain.album_cdb);
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

static void opus_file(int16_t header_q78, const char *const *entries, size_t count)
{
    file.size = 0;
    packet.size = 0;
    add_text(&packet, "OpusHead");
    add_byte(&packet, 1);
    add_byte(&packet, 2);
    for (int index = 0; index < 6; index++) {
        add_byte(&packet, 0);
    }
    add_byte(&packet, (uint8_t)header_q78);
    add_byte(&packet, (uint8_t)((uint16_t)header_q78 >> 8));
    add_byte(&packet, 0);
    add_ogg_page(packet.data, packet.size);
    packet.size = 0;
    add_text(&packet, "OpusTags");
    add_vorbis_comment(&packet, entries, count);
    add_ogg_packet(&packet, 30);
}

static void opus_r128_gains_are_raised_to_the_replay_gain_level_and_beat_replay_gain_tags(void)
{
    static const char *const ENTRIES[] = {
        "REPLAYGAIN_TRACK_GAIN=-20.00 dB",
        "R128_TRACK_GAIN=-1536",
        "R128_ALBUM_GAIN=-1408",
    };
    opus_file(0, ENTRIES, 3);
    CHECK(read_file("song.opus"));
    CHECK_EQUAL(-100, gain.track_cdb);
    CHECK_EQUAL(-50, gain.album_cdb);
}

static void the_opus_output_gain_is_added_and_counts_on_its_own(void)
{
    static const char *const ENTRIES[] = {"R128_TRACK_GAIN=-1536"};
    opus_file(256, ENTRIES, 1);
    CHECK(read_file("song.opus"));
    CHECK_EQUAL(0, gain.track_cdb);
    opus_file(-384, NULL, 0);
    CHECK(read_file("song.opus"));
    CHECK(gain.has_track);
    CHECK_EQUAL(-150, gain.track_cdb);
    opus_file(0, NULL, 0);
    CHECK(!read_file("song.opus"));
}

static void ogg_vorbis_replay_gain_reads(void)
{
    static const char *const ENTRIES[] = {"REPLAYGAIN_TRACK_GAIN=-4.75 dB"};
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
    CHECK_EQUAL(-475, gain.track_cdb);
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

static void add_freeform(const char *name, const char *value)
{
    uint32_t item = atom_start("----");
    uint32_t mean = atom_start("mean");
    add_be32(&file, 0);
    add_text(&file, "com.apple.iTunes");
    atom_end(mean);
    uint32_t key = atom_start("name");
    add_be32(&file, 0);
    add_text(&file, name);
    atom_end(key);
    uint32_t data = atom_start("data");
    add_be32(&file, 1);
    add_be32(&file, 0);
    add_text(&file, value);
    atom_end(data);
    atom_end(item);
}

static void mp4_freeform_atoms_after_the_audio_give_the_gains(void)
{
    file.size = 0;
    uint32_t ftyp = atom_start("ftyp");
    add_text(&file, "M4A ");
    atom_end(ftyp);
    uint32_t mdat = atom_start("mdat");
    for (int index = 0; index < 4000; index++) {
        add_byte(&file, 0x55);
    }
    atom_end(mdat);
    uint32_t moov = atom_start("moov");
    uint32_t udta = atom_start("udta");
    uint32_t meta = atom_start("meta");
    add_be32(&file, 0);
    uint32_t hdlr = atom_start("hdlr");
    add_be32(&file, 0);
    atom_end(hdlr);
    uint32_t ilst = atom_start("ilst");
    uint32_t title = atom_start("\xA9nam");
    add_text(&file, "skipped");
    atom_end(title);
    add_freeform("replaygain_track_gain", "-6.20 dB");
    add_freeform("replaygain_album_gain", "-6.90 dB");
    atom_end(ilst);
    atom_end(meta);
    atom_end(udta);
    atom_end(moov);
    CHECK(read_file("Album/01.m4a"));
    CHECK_EQUAL(-620, gain.track_cdb);
    CHECK_EQUAL(-690, gain.album_cdb);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(gains_parse_in_hundredths_of_a_decibel),
        TEST_CASE(gains_beyond_sixty_decibels_are_clamped_and_text_without_digits_is_refused),
        TEST_CASE(album_mode_falls_back_to_the_track_gain),
        TEST_CASE(id3v24_txxx_frames_give_the_track_and_album_gains),
        TEST_CASE(id3v23_lowercase_and_utf16_keys_read),
        TEST_CASE(id3v22_txx_frames_read),
        TEST_CASE(compressed_frames_other_descriptions_and_missing_tags_give_nothing),
        TEST_CASE(flac_vorbis_comments_give_the_gains),
        TEST_CASE(opus_r128_gains_are_raised_to_the_replay_gain_level_and_beat_replay_gain_tags),
        TEST_CASE(the_opus_output_gain_is_added_and_counts_on_its_own),
        TEST_CASE(ogg_vorbis_replay_gain_reads),
        TEST_CASE(mp4_freeform_atoms_after_the_audio_give_the_gains),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
