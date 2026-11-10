#include <string.h>

#include "check.h"
#include "library/cue_sheet.h"
#include "memory_source.h"

typedef struct Parsed {
    CueAlbum album;
    CueTrack tracks[8];
    uint32_t count;
} Parsed;

static MemorySource memory;
static CueSheet sheet;
static Parsed parsed;
static char wanted_file[64];

static bool named(void *context, const char *file)
{
    return strcmp((const char *)context, file) == 0;
}

static void parse_for(const char *text, uint32_t size, const char *file)
{
    memory = (MemorySource){.bytes = (const uint8_t *)text, .size = size};
    strcpy(wanted_file, file);
    cue_sheet_open(&sheet, memory_source(&memory), size, named, wanted_file);
    parsed.count = 0;
    while (parsed.count < 8 && cue_sheet_next(&sheet, &parsed.tracks[parsed.count])) {
        parsed.count++;
    }
    parsed.album = sheet.album;
}

#define PARSE(text, file) parse_for((text), sizeof(text) - 1, (file))

static void check_text(const char *wanted, const char *actual)
{
    CHECK_EQUAL(0, strcmp(wanted, actual));
}

static void an_album_reads_its_tracks_in_order(void)
{
    static const char TEXT[] = "REM GENRE Rock\n"
                               "REM DATE 1999\n"
                               "PERFORMER \"The Band\"\n"
                               "TITLE \"First Album\"\n"
                               "FILE \"Album.flac\" WAVE\n"
                               "  TRACK 01 AUDIO\n"
                               "    TITLE \"Opening\"\n"
                               "    INDEX 01 00:00:00\n"
                               "  TRACK 02 AUDIO\n"
                               "    TITLE \"Second\"\n"
                               "    PERFORMER \"Guest\"\n"
                               "    INDEX 00 03:58:30\n"
                               "    INDEX 01 04:00:00\n";
    PARSE(TEXT, "Album.flac");
    check_text("First Album", parsed.album.title);
    check_text("The Band", parsed.album.performer);
    check_text("Rock", parsed.album.genre);
    check_text("1999", parsed.album.date);
    CHECK_EQUAL(2, parsed.count);
    CHECK_EQUAL(1, parsed.tracks[0].number);
    check_text("Opening", parsed.tracks[0].title);
    check_text("", parsed.tracks[0].performer);
    CHECK_EQUAL(0, parsed.tracks[0].start_ms);
    CHECK_EQUAL(2, parsed.tracks[1].number);
    check_text("Second", parsed.tracks[1].title);
    check_text("Guest", parsed.tracks[1].performer);
    CHECK_EQUAL(240000, parsed.tracks[1].start_ms);
}

static void frames_round_to_the_nearest_millisecond(void)
{
    uint64_t frames = 0;
    CHECK(cue_sheet_parse_time("00:00:01", &frames));
    CHECK_EQUAL(1, frames);
    CHECK_EQUAL(13, cue_sheet_frames_to_ms(1));
    CHECK_EQUAL(27, cue_sheet_frames_to_ms(2));
    CHECK_EQUAL(1000, cue_sheet_frames_to_ms(75));
    CHECK(cue_sheet_parse_time("120:30:74", &frames));
    CHECK_EQUAL((120u * 60 + 30) * 75 + 74, frames);
    CHECK(!cue_sheet_parse_time("12:30", &frames));
    CHECK(!cue_sheet_parse_time("aa:00:00", &frames));
    CHECK(!cue_sheet_parse_time("01:02:03x", &frames));
}

static void a_track_without_index_01_is_left_out(void)
{
    static const char TEXT[] = "FILE \"a.wav\" WAVE\n"
                               "TRACK 01 AUDIO\n"
                               "INDEX 01 00:00:00\n"
                               "TRACK 02 AUDIO\n"
                               "TITLE \"No start\"\n"
                               "INDEX 00 01:00:00\n"
                               "TRACK 03 AUDIO\n"
                               "INDEX 01 02:00:00\n";
    PARSE(TEXT, "a.wav");
    CHECK_EQUAL(2, parsed.count);
    CHECK_EQUAL(1, parsed.tracks[0].number);
    CHECK_EQUAL(3, parsed.tracks[1].number);
    CHECK_EQUAL(120000, parsed.tracks[1].start_ms);
}

static void only_tracks_under_the_matching_file_are_read(void)
{
    static const char TEXT[] = "FILE \"disc1.flac\" WAVE\n"
                               "TRACK 01 AUDIO\n"
                               "INDEX 01 00:00:00\n"
                               "TRACK 02 AUDIO\n"
                               "INDEX 01 01:00:00\n"
                               "FILE \"disc2.flac\" WAVE\n"
                               "TRACK 03 AUDIO\n"
                               "INDEX 01 00:00:00\n"
                               "TRACK 04 AUDIO\n"
                               "INDEX 01 02:00:00\n";
    PARSE(TEXT, "disc2.flac");
    CHECK_EQUAL(2, parsed.count);
    CHECK_EQUAL(3, parsed.tracks[0].number);
    CHECK_EQUAL(0, parsed.tracks[0].start_ms);
    CHECK_EQUAL(4, parsed.tracks[1].number);
    PARSE(TEXT, "disc1.flac");
    CHECK_EQUAL(2, parsed.count);
    CHECK_EQUAL(1, parsed.tracks[0].number);
    CHECK_EQUAL(2, parsed.tracks[1].number);
}

static void a_track_belongs_to_the_file_of_its_index_01(void)
{
    static const char TEXT[] = "FILE \"one.wav\" WAVE\n"
                               "TRACK 01 AUDIO\n"
                               "INDEX 01 00:00:00\n"
                               "TRACK 02 AUDIO\n"
                               "INDEX 00 03:00:00\n"
                               "FILE \"two.wav\" WAVE\n"
                               "INDEX 01 00:00:00\n";
    PARSE(TEXT, "one.wav");
    CHECK_EQUAL(1, parsed.count);
    PARSE(TEXT, "two.wav");
    CHECK_EQUAL(1, parsed.count);
    CHECK_EQUAL(2, parsed.tracks[0].number);
}

static void a_bom_and_crlf_line_ends_are_read(void)
{
    static const char TEXT[] = "\xef\xbb\xbfTITLE \"Caf\xc3\xa9\"\r\n"
                               "FILE \"x.ape\" WAVE\r\n"
                               "TRACK 01 AUDIO\r\n"
                               "TITLE \"Un\"\r\n"
                               "INDEX 01 00:00:00\r\n"
                               "TRACK 02 AUDIO\r\n"
                               "TITLE \"Deux\"\r\n"
                               "INDEX 01 00:10:00\r\n";
    PARSE(TEXT, "x.ape");
    check_text("Caf\xc3\xa9", parsed.album.title);
    CHECK_EQUAL(2, parsed.count);
    check_text("Un", parsed.tracks[0].title);
    check_text("Deux", parsed.tracks[1].title);
    CHECK_EQUAL(10000, parsed.tracks[1].start_ms);
}

static void windows_1252_text_is_decoded(void)
{
    static const char TEXT[] = "PERFORMER \"Bj\xf6rk\"\n"
                               "FILE \"x.wav\" WAVE\n"
                               "TRACK 01 AUDIO\n"
                               "TITLE \"J\xf3ga \x96 live\"\n"
                               "INDEX 01 00:00:00\n";
    PARSE(TEXT, "x.wav");
    check_text("Bj\xc3\xb6rk", parsed.album.performer);
    check_text("J\xc3\xb3ga \xe2\x80\x93 live", parsed.tracks[0].title);
}

static void keywords_ignore_case_and_values_may_be_bare(void)
{
    static const char TEXT[] = "title Bare Album Name\n"
                               "rem discnumber 2\n"
                               "file My Image.wav wave\n"
                               "track 7 audio\n"
                               "\ttitle\t\"Tabbed\n"
                               "index 01 1:02:03\n";
    PARSE(TEXT, "My Image.wav");
    check_text("Bare Album Name", parsed.album.title);
    check_text("2", parsed.album.disc);
    CHECK_EQUAL(1, parsed.count);
    CHECK_EQUAL(7, parsed.tracks[0].number);
    check_text("Tabbed", parsed.tracks[0].title);
    CHECK_EQUAL(cue_sheet_frames_to_ms((1 * 60 + 2) * 75 + 3), parsed.tracks[0].start_ms);
}

static void a_start_before_the_previous_one_is_left_out(void)
{
    static const char TEXT[] = "FILE \"a.wav\" WAVE\n"
                               "TRACK 01 AUDIO\n"
                               "INDEX 01 00:00:00\n"
                               "TRACK 02 AUDIO\n"
                               "INDEX 01 02:00:00\n"
                               "TRACK 03 AUDIO\n"
                               "INDEX 01 01:00:00\n"
                               "TRACK 04 AUDIO\n"
                               "INDEX 01 02:00:00\n"
                               "TRACK 05 AUDIO\n"
                               "INDEX 01 03:00:00\n";
    PARSE(TEXT, "a.wav");
    CHECK_EQUAL(3, parsed.count);
    CHECK_EQUAL(1, parsed.tracks[0].number);
    CHECK_EQUAL(2, parsed.tracks[1].number);
    CHECK_EQUAL(5, parsed.tracks[2].number);
}

static void a_cue_for_another_file_reads_nothing(void)
{
    static const char TEXT[] = "FILE \"a.wav\" WAVE\n"
                               "TRACK 01 AUDIO\n"
                               "INDEX 01 00:00:00\n";
    PARSE(TEXT, "b.wav");
    CHECK_EQUAL(0, parsed.count);
    memory = (MemorySource){.bytes = (const uint8_t *)TEXT, .size = sizeof TEXT - 1};
    strcpy(wanted_file, "a.wav");
    cue_sheet_open(&sheet, memory_source(&memory), sizeof TEXT - 1, named, wanted_file);
    CueTrack scratch;
    CHECK_EQUAL(1, cue_sheet_count(&sheet, &scratch));
}

static void an_empty_file_reads_nothing(void)
{
    PARSE("", "a.wav");
    CHECK_EQUAL(0, parsed.count);
    check_text("", parsed.album.title);
}

static void untitled_tracks_are_numbered(void)
{
    char title[16];
    cue_sheet_untitled(3, title);
    check_text("Track 03", title);
    cue_sheet_untitled(120, title);
    check_text("Track 120", title);
    cue_sheet_untitled(0, title);
    check_text("Track 00", title);
}

static void cue_names_end_in_cue(void)
{
    CHECK(cue_sheet_is_name("Album.cue"));
    CHECK(cue_sheet_is_name("a.CUE"));
    CHECK(!cue_sheet_is_name(".cue"));
    CHECK(!cue_sheet_is_name("cue"));
    CHECK(!cue_sheet_is_name("Album.cuesheet"));
}

static CueTrack starting_at(uint32_t start_ms)
{
    return (CueTrack){.number = 1, .start_ms = start_ms};
}

static void a_split_ends_each_track_where_the_next_starts(void)
{
    CueSplit split;
    CueTrack out;
    uint32_t duration = 0;
    cue_split_start(&split, 300000);
    CueTrack first = starting_at(0);
    CueTrack second = starting_at(120000);
    CHECK(!cue_split_push(&split, &first, &out, &duration));
    CHECK(cue_split_push(&split, &second, &out, &duration));
    CHECK_EQUAL(0, out.start_ms);
    CHECK_EQUAL(120000, duration);
    CHECK(cue_split_finish(&split, &out, &duration));
    CHECK_EQUAL(120000, out.start_ms);
    CHECK_EQUAL(180000, duration);
    CHECK(!cue_split_finish(&split, &out, &duration));
}

static void tracks_past_the_end_of_the_file_are_left_out(void)
{
    CueSplit split;
    CueTrack out;
    uint32_t duration = 0;
    cue_split_start(&split, 100000);
    CueTrack first = starting_at(0);
    CueTrack late = starting_at(150000);
    CueTrack later = starting_at(200000);
    CHECK(!cue_split_push(&split, &first, &out, &duration));
    CHECK(cue_split_push(&split, &late, &out, &duration));
    CHECK_EQUAL(100000, duration);
    CHECK(!cue_split_push(&split, &later, &out, &duration));
    CHECK(!cue_split_finish(&split, &out, &duration));
}

static void an_unknown_length_keeps_every_track(void)
{
    CueSplit split;
    CueTrack out;
    uint32_t duration = 7;
    cue_split_start(&split, 0);
    CueTrack first = starting_at(0);
    CueTrack second = starting_at(5000);
    CHECK(!cue_split_push(&split, &first, &out, &duration));
    CHECK(cue_split_push(&split, &second, &out, &duration));
    CHECK_EQUAL(5000, duration);
    CHECK(cue_split_finish(&split, &out, &duration));
    CHECK_EQUAL(0, duration);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(an_album_reads_its_tracks_in_order),
        TEST_CASE(frames_round_to_the_nearest_millisecond),
        TEST_CASE(a_track_without_index_01_is_left_out),
        TEST_CASE(only_tracks_under_the_matching_file_are_read),
        TEST_CASE(a_track_belongs_to_the_file_of_its_index_01),
        TEST_CASE(a_bom_and_crlf_line_ends_are_read),
        TEST_CASE(windows_1252_text_is_decoded),
        TEST_CASE(keywords_ignore_case_and_values_may_be_bare),
        TEST_CASE(a_start_before_the_previous_one_is_left_out),
        TEST_CASE(a_cue_for_another_file_reads_nothing),
        TEST_CASE(an_empty_file_reads_nothing),
        TEST_CASE(untitled_tracks_are_numbered),
        TEST_CASE(cue_names_end_in_cue),
        TEST_CASE(a_split_ends_each_track_where_the_next_starts),
        TEST_CASE(tracks_past_the_end_of_the_file_are_left_out),
        TEST_CASE(an_unknown_length_keeps_every_track),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
