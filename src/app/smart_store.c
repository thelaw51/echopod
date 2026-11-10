#include "smart_store.h"

#include "hal/storage.h"

const char PLAY_STATS_PATH[] = "/.echo/plays.bin";
const char ADDED_FILES_PATH[] = "/.echo/added.bin";

typedef struct SmartStore {
    uint32_t revision;
    uint32_t next_tick;
    bool ticked;
} SmartStore;

static SmartStore store;

static bool read_bucket(HalFile file, uint32_t bucket, uint8_t *bytes)
{
    uint32_t offset = bucket * PLAY_STATS_BUCKET_BYTES;
    return file.size >= offset + PLAY_STATS_BUCKET_BYTES &&
           hal_file_read(file, offset, bytes, PLAY_STATS_BUCKET_BYTES);
}

static void each_stat(void (*visit)(void *context, const PlayStat *stat, uint16_t place),
                      void *context)
{
    HalFile file;
    uint8_t bucket[PLAY_STATS_BUCKET_BYTES];
    if (!hal_file_open(&file, PLAY_STATS_PATH)) {
        return;
    }
    for (uint32_t index = 0; index < PLAY_STATS_BUCKETS; index++) {
        if (!read_bucket(file, index, bucket)) {
            continue;
        }
        for (uint16_t entry = 0; entry < PLAY_STATS_BUCKET_ENTRIES; entry++) {
            PlayStat stat;
            if (play_stat_decode(bucket + entry * PLAY_STAT_BYTES, &stat)) {
                visit(context, &stat, (uint16_t)(index * PLAY_STATS_BUCKET_ENTRIES + entry));
            }
        }
    }
    hal_file_close(&file);
}

static void find_tick(void *context, const PlayStat *stat, uint16_t place)
{
    (void)context;
    (void)place;
    if (stat->last >= store.next_tick) {
        store.next_tick = stat->last + 1;
    }
}

static void ensure_tick(void)
{
    if (!store.ticked) {
        store.next_tick = 1;
        each_stat(find_tick, NULL);
        store.ticked = true;
    }
}

void smart_store_note_play(uint32_t key, uint16_t track)
{
    HalFile file;
    uint8_t bucket[PLAY_STATS_BUCKET_BYTES];
    ensure_tick();
    uint32_t index = play_stats_bucket(key);
    if (!hal_file_open(&file, PLAY_STATS_PATH)) {
        return;
    }
    bool read = read_bucket(file, index, bucket);
    hal_file_close(&file);
    if (!read) {
        return;
    }
    uint16_t place = play_stats_note(bucket, key, track, store.next_tick);
    uint32_t offset = index * PLAY_STATS_BUCKET_BYTES + place * PLAY_STAT_BYTES;
    if (hal_file_write_at(PLAY_STATS_PATH, offset, bucket + place * PLAY_STAT_BYTES,
                          PLAY_STAT_BYTES)) {
        store.next_tick++;
        store.revision++;
    }
}

typedef struct PlayRanking {
    SmartRank *rank;
    bool most;
} PlayRanking;

static void offer_stat(void *context, const PlayStat *stat, uint16_t place)
{
    PlayRanking *ranking = context;
    smart_rank_offer(ranking->rank,
                     (SmartEntry){.key = stat->key,
                                  .primary = ranking->most ? stat->plays : stat->last,
                                  .secondary = ranking->most ? stat->last : 0,
                                  .place = place,
                                  .track = stat->track});
    find_tick(NULL, stat, place);
}

void smart_store_rank_plays(SmartRank *rank, bool most)
{
    PlayRanking ranking = {.rank = rank, .most = most};
    rank->count = 0;
    if (!store.ticked) {
        store.next_tick = 1;
    }
    each_stat(offer_stat, &ranking);
    store.ticked = true;
}

void smart_store_set_play_track(uint16_t place, uint32_t key, uint16_t track)
{
    HalFile file;
    uint8_t bytes[PLAY_STAT_BYTES];
    PlayStat stat;
    uint32_t offset = (uint32_t)place * PLAY_STAT_BYTES;
    if (place >= PLAY_STATS_ENTRIES || !hal_file_open(&file, PLAY_STATS_PATH)) {
        return;
    }
    bool read = file.size >= offset + PLAY_STAT_BYTES &&
                hal_file_read(file, offset, bytes, PLAY_STAT_BYTES);
    hal_file_close(&file);
    if (!read || !play_stat_decode(bytes, &stat) || stat.key != key || stat.track == track) {
        return;
    }
    stat.track = track;
    play_stat_encode(&stat, bytes);
    (void)hal_file_write_at(PLAY_STATS_PATH, offset, bytes, PLAY_STAT_BYTES);
}

static bool write_added(const SmartEntry *entries, uint16_t count, uint32_t fingerprint)
{
    uint8_t bytes[ADDED_FILES_BYTES];
    uint32_t length = added_files_encode(entries, count, fingerprint, bytes);
    return hal_file_write_at(ADDED_FILES_PATH, 0, bytes, length);
}

static bool same_added(const SmartEntry *entries, uint16_t count)
{
    SmartEntry stored[SMART_LIST_TRACKS];
    SmartRank rank;
    uint32_t fingerprint = 0;
    smart_rank_init(&rank, stored, SMART_LIST_TRACKS);
    smart_store_load_added(&rank, &fingerprint);
    if (rank.count != count) {
        return false;
    }
    for (uint16_t index = 0; index < count; index++) {
        if (stored[index].key != entries[index].key ||
            stored[index].primary != entries[index].primary) {
            return false;
        }
    }
    return true;
}

bool smart_store_save_added(const SmartEntry *entries, uint16_t count)
{
    if (same_added(entries, count)) {
        return true;
    }
    store.revision++;
    return write_added(entries, count, 0);
}

void smart_store_keep_added_tracks(const SmartEntry *entries, uint16_t count, uint32_t fingerprint)
{
    (void)write_added(entries, count, fingerprint);
}

void smart_store_load_added(SmartRank *rank, uint32_t *fingerprint)
{
    HalFile file;
    uint8_t bytes[ADDED_FILES_BYTES];
    rank->count = 0;
    *fingerprint = 0;
    if (!hal_file_open(&file, ADDED_FILES_PATH)) {
        return;
    }
    uint32_t length = file.size < sizeof bytes ? file.size : sizeof bytes;
    bool read = hal_file_read(file, 0, bytes, length);
    hal_file_close(&file);
    if (read) {
        (void)added_files_decode(bytes, length, rank, fingerprint);
    }
}

uint32_t smart_store_revision(void)
{
    return store.revision;
}
