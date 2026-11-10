#pragma once

#include <stdint.h>

#include "cold_screens.h"
#include "core/i18n.h"
#include "core/power_policy.h"
#include "player.h"
#include "screens/folders_screen.h"
#include "settings_store.h"
#include "ui/choice_screen.h"
#include "ui/menu_screen.h"
#include "ui/theme.h"
#include "ui/toast.h"

typedef enum SettingsGroup {
    SETTINGS_SOUND,
    SETTINGS_DISPLAY,
    SETTINGS_LIBRARY,
    SETTINGS_BUTTONS,
    SETTINGS_SYSTEM,
    SETTINGS_GROUP_COUNT
} SettingsGroup;

enum {
    SETTINGS_CHOICE_COUNT = 35,
    SETTINGS_TOP_COUNT = 5 + SETTINGS_GROUP_COUNT,
    SETTINGS_SOUND_ROWS = 6,
    SETTINGS_DISPLAY_ROWS = 11,
    SETTINGS_LIBRARY_ROWS = 9,
    SETTINGS_BUTTONS_ROWS = 4,
    SETTINGS_SYSTEM_ROWS = 4,
    SETTINGS_GROUP_ROWS_MAX = 11
};

_Static_assert(SETTINGS_SOUND_ROWS <= SETTINGS_GROUP_ROWS_MAX &&
                   SETTINGS_DISPLAY_ROWS <= SETTINGS_GROUP_ROWS_MAX &&
                   SETTINGS_LIBRARY_ROWS <= SETTINGS_GROUP_ROWS_MAX &&
                   SETTINGS_BUTTONS_ROWS <= SETTINGS_GROUP_ROWS_MAX &&
                   SETTINGS_SYSTEM_ROWS <= SETTINGS_GROUP_ROWS_MAX,
               "every Settings group must fit the rows array");

typedef struct SettingsContext {
    SettingsStore *store;
    Player *player;
    PowerPolicy *power;
    Toast *toast;
    bool *rescan_requested;
    ColdScreenEntry *bluetooth;
    ColdScreenEntry *equalizer;
    ColdScreenEntry *date_time;
    FoldersScreen *folders;
    int sleep_option;
} SettingsContext;

typedef struct SettingChoice {
    StringId title;
    const StringId *options;
    const char *const *plain_options;
    int option_count;
    int (*current)(const void *context);
    void (*choose)(void *context, int index);
    const char *(*describe)(int current);
    uint8_t slot;
} SettingChoice;

typedef struct SettingsScreens SettingsScreens;

typedef struct SettingsEntry {
    const SettingChoice *choice;
    SettingsScreens *owner;
} SettingsEntry;

typedef struct SettingsGroupLink {
    SettingsScreens *owner;
    uint8_t group;
} SettingsGroupLink;

struct SettingsScreens {
    SettingsContext context;
    const Theme *theme;
    MenuScreen menu;
    MenuScreen group;
    ChoiceScreen choice;
    SettingsEntry entries[SETTINGS_CHOICE_COUNT];
    SettingsGroupLink links[SETTINGS_GROUP_COUNT];
    uint8_t selected[SETTINGS_GROUP_COUNT];
    uint8_t shown;
    MenuItem items[SETTINGS_TOP_COUNT];
    MenuItem rows[SETTINGS_GROUP_ROWS_MAX];
};

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context,
                              const Theme *theme);
