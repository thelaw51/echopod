#include <stdio.h>
#include <string.h>

#include "check.h"
#include "fixture_library.h"
#include "library/playlist_writer.h"

static FixtureLibrary fixture;

static uint16_t track_titled(const Library *library, const char *title)
{
    uint32_t tracks = library_count(library, LIBRARY_TRACKS);
    for (uint32_t index = 0; index < tracks; index++) {
        LibraryTrack track;
        char text[128];
        CHECK(library_read_track(library, index, &track));
        CHECK(library_read_string(library, track.title, text, sizeof text));
        if (strcmp(text, title) == 0) {
            return (uint16_t)index;
        }
    }
    CHECK(false);
    return 0;
}

static void a_track_becomes_an_info_line_and_a_relative_path(void)
{
    const Library *library = fixture_library_open(&fixture);
    uint16_t index = track_titled(library, "Shoot to Thrill");
    LibraryTrack track;
    CHECK(library_read_track(library, index, &track));
    char expected[PLAYLIST_WRITER_LINE_CAPACITY];
    snprintf(expected, sizeof expected,
             "#EXTINF:%u,AC/DC - Shoot to Thrill\n../AC DC/Back In Black/1-02 - Shoot to "
             "Thrill.flac\n",
             (unsigned)(track.duration_ms / 1000));
    char line[PLAYLIST_WRITER_LINE_CAPACITY];
    uint32_t length = playlist_writer_entry(library, index, "../", line, sizeof line);
    CHECK_EQUAL(strlen(expected), length);
    CHECK(strcmp(expected, line) == 0);
}

static void names_keep_their_utf8(void)
{
    const Library *library = fixture_library_open(&fixture);
    char line[PLAYLIST_WRITER_LINE_CAPACITY];
    uint16_t index = track_titled(library, "J\xc3\xb3ga");
    CHECK(playlist_writer_entry(library, index, "", line, sizeof line) > 0);
    CHECK(strstr(line, ",Bj\xc3\xb6rk - J\xc3\xb3ga\n") != NULL);
    CHECK(strstr(line, "\nBj\xc3\xb6rk/Homogenic/02 - J\xc3\xb3ga.flac\n") != NULL);
}

static void an_entry_that_does_not_fit_is_left_out(void)
{
    const Library *library = fixture_library_open(&fixture);
    char line[40];
    CHECK_EQUAL(0, playlist_writer_entry(library, 0, "../", line, sizeof line));
}

static void a_missing_track_is_left_out(void)
{
    const Library *library = fixture_library_open(&fixture);
    char line[PLAYLIST_WRITER_LINE_CAPACITY];
    CHECK_EQUAL(0, playlist_writer_entry(library, FIXTURE_TRACKS, "../", line, sizeof line));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_track_becomes_an_info_line_and_a_relative_path),
        TEST_CASE(names_keep_their_utf8),
        TEST_CASE(an_entry_that_does_not_fit_is_left_out),
        TEST_CASE(a_missing_track_is_left_out),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
