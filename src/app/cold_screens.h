#pragma once

#include <stdbool.h>

#include "bluetooth_link.h"
#include "clock_store.h"
#include "equalizer_store.h"
#include "screens/bluetooth_screen.h"
#include "screens/date_time_screen.h"
#include "screens/equalizer_screen.h"
#include "settings_store.h"
#include "ui/screen.h"
#include "ui/screen_stack.h"
#include "ui/theme.h"
#include "ui/toast.h"

typedef enum ColdScreenKind {
    COLD_SCREEN_NONE,
    COLD_SCREEN_BLUETOOTH,
    COLD_SCREEN_EQUALIZER,
    COLD_SCREEN_DATE_TIME
} ColdScreenKind;

typedef enum ColdScreenEntryId {
    COLD_ENTRY_BLUETOOTH,
    COLD_ENTRY_BLUETOOTH_DETAILS,
    COLD_ENTRY_EQUALIZER,
    COLD_ENTRY_DATE_TIME,
    COLD_ENTRY_COUNT
} ColdScreenEntryId;

typedef union ColdScreenSlot {
    BluetoothScreen bluetooth;
    EqualizerScreen equalizer;
    DateTimeScreen date_time;
} ColdScreenSlot;

typedef struct ColdScreens ColdScreens;

typedef struct ColdScreenEntry {
    ColdScreens *owner;
    ColdScreenKind kind;
    bool details;
} ColdScreenEntry;

typedef struct ColdScreensConfig {
    BluetoothLink *bluetooth;
    EqualizerStore *equalizer;
    ClockStore *clock;
    SettingsStore *settings;
    const Theme *theme;
    Toast *toast;
    const ScreenStack *stack;
} ColdScreensConfig;

struct ColdScreens {
    ColdScreensConfig config;
    ColdScreenKind held;
    ColdScreenEntry entries[COLD_ENTRY_COUNT];
    ColdScreenSlot slot;
};

void cold_screens_init(ColdScreens *cold, ColdScreensConfig config);
ColdScreenEntry *cold_screens_entry(ColdScreens *cold, ColdScreenEntryId id);
ScreenAction cold_screens_open(void *entry);
const char *cold_screens_summary(const void *entry);
ColdScreenKind cold_screens_held(const ColdScreens *cold);
