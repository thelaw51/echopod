#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    LOW_BATTERY_EMPTY_MV = 3450,
    LOW_BATTERY_HOLD_MS = 2000,
    LOW_BATTERY_INSTALL_MV = 3710
};

typedef struct LowBatteryReading {
    bool known;
    uint32_t millivolts;
    bool usb_power;
} LowBatteryReading;

typedef enum InstallPower {
    INSTALL_POWER_OK,
    INSTALL_POWER_LOW,
    INSTALL_POWER_UNKNOWN
} InstallPower;

typedef struct LowBattery {
    bool below;
    uint32_t below_since_ms;
} LowBattery;

void low_battery_init(LowBattery *low);
bool low_battery_update(LowBattery *low, LowBatteryReading reading, uint32_t now_ms);
InstallPower low_battery_install_power(LowBatteryReading reading);
