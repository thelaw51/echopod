#include "cold_screens.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/clock.h"

static const ColdScreenEntry ENTRY_SHAPES[COLD_ENTRY_COUNT] = {
    [COLD_ENTRY_BLUETOOTH] = {.kind = COLD_SCREEN_BLUETOOTH},
    [COLD_ENTRY_BLUETOOTH_DETAILS] = {.kind = COLD_SCREEN_BLUETOOTH, .details = true},
    [COLD_ENTRY_EQUALIZER] = {.kind = COLD_SCREEN_EQUALIZER},
    [COLD_ENTRY_DATE_TIME] = {.kind = COLD_SCREEN_DATE_TIME},
};

void cold_screens_init(ColdScreens *cold, ColdScreensConfig config)
{
    cold->config = config;
    cold->held = COLD_SCREEN_NONE;
    for (int index = 0; index < COLD_ENTRY_COUNT; index++) {
        cold->entries[index] = ENTRY_SHAPES[index];
        cold->entries[index].owner = cold;
    }
}

ColdScreenEntry *cold_screens_entry(ColdScreens *cold, ColdScreenEntryId id)
{
    return &cold->entries[id];
}

ColdScreenKind cold_screens_held(const ColdScreens *cold)
{
    return cold->held;
}

static const Screen *held_screen(const ColdScreens *cold)
{
    if (cold->held == COLD_SCREEN_BLUETOOTH) {
        return &cold->slot.bluetooth.screen;
    }
    if (cold->held == COLD_SCREEN_EQUALIZER) {
        return &cold->slot.equalizer.screen;
    }
    return &cold->slot.date_time.screen;
}

static bool slot_in_use(const ColdScreens *cold)
{
    return cold->held != COLD_SCREEN_NONE &&
           screen_stack_holds(cold->config.stack, held_screen(cold));
}

static void refuse(const ColdScreens *cold)
{
    const char *arguments[] = {held_screen(cold)->title};
    char text[TOAST_TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format(&builder, STR_SETTINGS_CLOSE_FIRST, arguments, 1);
    toast_show(cold->config.toast, text, hal_clock_milliseconds());
}

static EqualizerConfig equalizer_config(const ColdScreensConfig *config)
{
    return (EqualizerConfig){
        .store = config->equalizer, .toast = config->toast, .theme = config->theme};
}

static DateTimeConfig date_time_config(const ColdScreensConfig *config)
{
    return (DateTimeConfig){.clock = config->clock,
                            .store = config->settings,
                            .toast = config->toast,
                            .theme = config->theme};
}

static void fill(ColdScreens *cold, ColdScreenKind kind)
{
    const ColdScreensConfig *config = &cold->config;
    if (kind == COLD_SCREEN_BLUETOOTH) {
        bluetooth_screen_init(&cold->slot.bluetooth, config->bluetooth, config->theme);
    } else if (kind == COLD_SCREEN_EQUALIZER) {
        equalizer_screen_init(&cold->slot.equalizer, equalizer_config(config));
    } else {
        date_time_screen_init(&cold->slot.date_time, date_time_config(config));
    }
    cold->held = kind;
}

static ScreenAction open_held(ColdScreens *cold, bool details)
{
    if (cold->held == COLD_SCREEN_BLUETOOTH) {
        return details ? bluetooth_screen_open_details(&cold->slot.bluetooth)
                       : bluetooth_screen_open(&cold->slot.bluetooth);
    }
    if (cold->held == COLD_SCREEN_EQUALIZER) {
        return equalizer_screen_open(&cold->slot.equalizer);
    }
    return date_time_screen_open(&cold->slot.date_time);
}

ScreenAction cold_screens_open(void *entry)
{
    const ColdScreenEntry *opening = entry;
    ColdScreens *cold = opening->owner;
    if (cold->held != opening->kind) {
        if (slot_in_use(cold)) {
            refuse(cold);
            return screen_action_none();
        }
        fill(cold, opening->kind);
    }
    return open_held(cold, opening->details);
}

const char *cold_screens_summary(const void *entry)
{
    const ColdScreenEntry *shown = entry;
    const ColdScreensConfig *config = &shown->owner->config;
    if (shown->kind == COLD_SCREEN_BLUETOOTH) {
        return bluetooth_link_summary(config->bluetooth);
    }
    if (shown->kind == COLD_SCREEN_EQUALIZER) {
        EqualizerConfig equalizer = equalizer_config(config);
        return equalizer_screen_config_summary(&equalizer);
    }
    DateTimeConfig date_time = date_time_config(config);
    return date_time_screen_config_summary(&date_time);
}
