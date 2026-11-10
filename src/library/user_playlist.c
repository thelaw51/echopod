#include "library/user_playlist.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    VERSION = 1,
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    NUMBER_FIELD = 6,
    COUNT_FIELD = 8,
    FINGERPRINT_FIELD = 12,
    ENTRIES_FIELD = USER_PLAYLIST_HEADER_BYTES,
    TRACK_OFFSET = 4,
    CHECKSUM_FIELD = USER_PLAYLIST_SLOT_BYTES - 4,
    PATH_CAPACITY = 520,
    BITS_PER_BYTE = 8
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'E', 'M', 'P', 'L'};

static void put_u16(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> BITS_PER_BYTE);
}

static void put_u32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    put_u16(bytes, offset, value & 0xFFFFu);
    put_u16(bytes, offset + 2, value >> 16);
}

static uint16_t get_u16(const uint8_t *bytes, uint32_t offset)
{
    return (uint16_t)(bytes[offset] | bytes[offset + 1] << BITS_PER_BYTE);
}

static uint32_t get_u32(const uint8_t *bytes, uint32_t offset)
{
    return (uint32_t)get_u16(bytes, offset) | (uint32_t)get_u16(bytes, offset + 2) << 16;
}

static uint32_t entry_field(uint16_t position)
{
    return ENTRIES_FIELD + (uint32_t)position * USER_PLAYLIST_ENTRY_BYTES;
}

void user_playlist_clear(UserPlaylist *playlist, uint16_t number)
{
    playlist->number = number;
    playlist->count = 0;
    playlist->fingerprint = 0;
}

bool user_playlist_add(UserPlaylist *playlist, uint32_t key, uint16_t track)
{
    if (playlist->number == 0 || playlist->count == USER_PLAYLIST_TRACKS) {
        return false;
    }
    playlist->keys[playlist->count] = key;
    playlist->tracks[playlist->count] = track;
    playlist->count++;
    return true;
}

bool user_playlist_remove_at(UserPlaylist *playlist, uint16_t position)
{
    if (position >= playlist->count) {
        return false;
    }
    for (uint16_t index = position; index + 1 < playlist->count; index++) {
        playlist->keys[index] = playlist->keys[index + 1];
        playlist->tracks[index] = playlist->tracks[index + 1];
    }
    playlist->count--;
    return true;
}

void user_playlist_encode(const UserPlaylist *playlist, uint8_t *bytes)
{
    for (uint32_t index = 0; index < USER_PLAYLIST_SLOT_BYTES; index++) {
        bytes[index] = 0;
    }
    for (int index = 0; index < MAGIC_SIZE; index++) {
        bytes[index] = MAGIC[index];
    }
    put_u16(bytes, VERSION_FIELD, VERSION);
    put_u16(bytes, NUMBER_FIELD, playlist->number);
    put_u16(bytes, COUNT_FIELD, playlist->count);
    put_u32(bytes, FINGERPRINT_FIELD, playlist->fingerprint);
    for (uint16_t index = 0; index < playlist->count; index++) {
        put_u32(bytes, entry_field(index), playlist->keys[index]);
        put_u16(bytes, entry_field(index) + TRACK_OFFSET, playlist->tracks[index]);
    }
    put_u32(bytes, CHECKSUM_FIELD, hash_update(HASH_START, bytes, CHECKSUM_FIELD));
}

static bool slot_valid(const uint8_t *bytes, uint32_t length)
{
    if (length < USER_PLAYLIST_SLOT_BYTES) {
        return false;
    }
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (bytes[index] != MAGIC[index]) {
            return false;
        }
    }
    return get_u16(bytes, VERSION_FIELD) == VERSION &&
           get_u16(bytes, COUNT_FIELD) <= USER_PLAYLIST_TRACKS &&
           get_u32(bytes, CHECKSUM_FIELD) == hash_update(HASH_START, bytes, CHECKSUM_FIELD);
}

bool user_playlist_decode(const uint8_t *bytes, uint32_t length, UserPlaylist *playlist)
{
    user_playlist_clear(playlist, 0);
    if (!slot_valid(bytes, length)) {
        return false;
    }
    playlist->number = get_u16(bytes, NUMBER_FIELD);
    playlist->count = playlist->number == 0 ? 0 : get_u16(bytes, COUNT_FIELD);
    playlist->fingerprint = get_u32(bytes, FINGERPRINT_FIELD);
    for (uint16_t index = 0; index < playlist->count; index++) {
        playlist->keys[index] = get_u32(bytes, entry_field(index));
        playlist->tracks[index] = get_u16(bytes, entry_field(index) + TRACK_OFFSET);
    }
    return true;
}

static size_t text_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

bool user_playlist_track_key(const Library *library, uint16_t track, uint32_t *key)
{
    LibraryTrack record;
    char path[PATH_CAPACITY];
    if (!library_read_track(library, track, &record) ||
        !library_read_string(library, record.path, path, sizeof path)) {
        return false;
    }
    const char *start = path;
    while (*start == '/') {
        start++;
    }
    *key = hash_update(HASH_START, start, text_length(start));
    if (record.cue) {
        uint8_t part[2] = {'#', record.number};
        *key = hash_update(*key, part, sizeof part);
    }
    return true;
}

bool user_playlist_is_current(const UserPlaylist *playlist, const Library *library)
{
    if (playlist->fingerprint != library_fingerprint(library)) {
        return false;
    }
    uint32_t tracks = library_count(library, LIBRARY_TRACKS);
    for (uint16_t index = 0; index < playlist->count; index++) {
        uint16_t track = playlist->tracks[index];
        uint32_t key = 0;
        if (track == USER_PLAYLIST_NO_TRACK) {
            continue;
        }
        if (track >= tracks || !user_playlist_track_key(library, track, &key) ||
            key != playlist->keys[index]) {
            return false;
        }
    }
    return true;
}

static void match_track(UserPlaylist *playlist, uint32_t key, uint16_t track)
{
    for (uint16_t index = 0; index < playlist->count; index++) {
        if (playlist->keys[index] == key && playlist->tracks[index] == USER_PLAYLIST_NO_TRACK) {
            playlist->tracks[index] = track;
        }
    }
}

void user_playlist_resolve(UserPlaylist *playlist, const Library *library, UserPlaylistPause pause)
{
    for (uint16_t index = 0; index < playlist->count; index++) {
        playlist->tracks[index] = USER_PLAYLIST_NO_TRACK;
    }
    uint32_t tracks = library_count(library, LIBRARY_TRACKS);
    for (uint32_t track = 0; track < tracks && playlist->count > 0; track++) {
        uint32_t key = 0;
        if (user_playlist_track_key(library, (uint16_t)track, &key)) {
            match_track(playlist, key, (uint16_t)track);
        }
        if (pause.between_tracks != NULL && track % USER_PLAYLIST_TRACKS_PER_PAUSE == 0) {
            pause.between_tracks(pause.context);
        }
    }
    playlist->fingerprint = library_fingerprint(library);
}

uint16_t user_playlist_playable(const UserPlaylist *playlist, uint16_t *tracks, uint16_t capacity)
{
    uint16_t found = 0;
    for (uint16_t index = 0; index < playlist->count && found < capacity; index++) {
        if (playlist->tracks[index] != USER_PLAYLIST_NO_TRACK) {
            tracks[found++] = playlist->tracks[index];
        }
    }
    return found;
}
