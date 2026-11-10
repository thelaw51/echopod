#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "overlay.h"

#define COLD_IMAGE_OFFSET 0x278000u
#define COLD_MAGIC 0x444C4345u
#define COLD_RAM_END 0x0308F000u

extern uint8_t device_cold_start[];

bool board_cold_load(void)
{
    uint8_t *base = device_cold_start;
    uint32_t capacity = (uint32_t)(COLD_RAM_END - (uintptr_t)base);
    BoardOverlay code = {COLD_IMAGE_OFFSET, COLD_MAGIC, base, capacity};
    if (board_overlay_load(&code)) {
        return true;
    }
    for (uint32_t index = 0; index < capacity; index++) {
        base[index] = 0;
    }
    return false;
}

void hal_system_release_rescan_code(void)
{
    board_cold_load();
}
