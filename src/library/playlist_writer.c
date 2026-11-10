#include "library/playlist_writer.h"

#include "core/string_builder.h"
#include "core/volume_path.h"

enum {
    PATH_CAPACITY = 520,
    NAME_CAPACITY = 256,
    MS_PER_SECOND = 1000
};

static bool on_internal_storage(const char *path)
{
    size_t length = 0;
    while (path[length] != '\0' && path[length] != '/') {
        length++;
    }
    return volume_is_internal_name(path, length);
}

static void append_name(StringBuilder *builder, const char *text)
{
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        bool breaks = *cursor == '\r' || *cursor == '\n';
        string_builder_append_char(builder, breaks ? ' ' : *cursor);
    }
}

static void append_info(StringBuilder *builder, const Library *library, const LibraryTrack *record)
{
    char text[NAME_CAPACITY];
    LibraryGroup artist;
    string_builder_append(builder, "#EXTINF:");
    string_builder_append_unsigned(builder, record->duration_ms / MS_PER_SECOND);
    string_builder_append_char(builder, ',');
    if (library_read_group(library, LIBRARY_ARTISTS, record->artist, &artist) &&
        library_read_string(library, artist.name, text, sizeof text) && text[0] != '\0') {
        append_name(builder, text);
        string_builder_append(builder, " - ");
    }
    if (library_read_string(library, record->title, text, sizeof text)) {
        append_name(builder, text);
    }
    string_builder_append_char(builder, '\n');
}

uint32_t playlist_writer_entry(const Library *library, uint16_t track, const char *prefix,
                               char *out, uint32_t capacity)
{
    LibraryTrack record;
    char path[PATH_CAPACITY];
    if (!library_read_track(library, track, &record) ||
        !library_read_string(library, record.path, path, sizeof path)) {
        return 0;
    }
    const char *start = path;
    while (*start == '/') {
        start++;
    }
    if (on_internal_storage(start)) {
        return 0;
    }
    StringBuilder builder = string_builder_make(out, capacity);
    append_info(&builder, library, &record);
    string_builder_append(&builder, prefix);
    string_builder_append(&builder, start);
    string_builder_append_char(&builder, '\n');
    return builder.overflowed || start[0] == '\0' ? 0 : (uint32_t)builder.length;
}
