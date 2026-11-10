#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/smart_list.h"

extern const char PLAY_STATS_PATH[];
extern const char ADDED_FILES_PATH[];

void smart_store_note_play(uint32_t key, uint16_t track);
void smart_store_rank_plays(SmartRank *rank, bool most);
void smart_store_set_play_track(uint16_t place, uint32_t key, uint16_t track);
bool smart_store_save_added(const SmartEntry *entries, uint16_t count);
void smart_store_keep_added_tracks(const SmartEntry *entries, uint16_t count, uint32_t fingerprint);
void smart_store_load_added(SmartRank *rank, uint32_t *fingerprint);
uint32_t smart_store_revision(void);
