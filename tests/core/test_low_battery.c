#include "check.h"
#include "core/low_battery.h"

static LowBatteryReading on_battery(uint32_t millivolts)
{
    return (LowBatteryReading){.known = true, .millivolts = millivolts, .usb_power = false};
}

static void a_reading_below_empty_for_two_seconds_asks_for_shutdown(void)
{
    LowBattery low;
    low_battery_init(&low);
    CHECK(!low_battery_update(&low, on_battery(3440), 1000));
    CHECK(!low_battery_update(&low, on_battery(3440), 2999));
    CHECK(low_battery_update(&low, on_battery(3440), 3000));
}

static void a_reading_back_above_empty_restarts_the_wait(void)
{
    LowBattery low;
    low_battery_init(&low);
    low_battery_update(&low, on_battery(3440), 1000);
    CHECK(!low_battery_update(&low, on_battery(3450), 2500));
    CHECK(!low_battery_update(&low, on_battery(3440), 3000));
    CHECK(!low_battery_update(&low, on_battery(3440), 4999));
    CHECK(low_battery_update(&low, on_battery(3440), 5000));
}

static void usb_power_never_shuts_down(void)
{
    LowBattery low;
    low_battery_init(&low);
    LowBatteryReading plugged = {.known = true, .millivolts = 3300, .usb_power = true};
    low_battery_update(&low, plugged, 0);
    CHECK(!low_battery_update(&low, plugged, 10000));
}

static void an_unknown_reading_never_shuts_down(void)
{
    LowBattery low;
    low_battery_init(&low);
    LowBatteryReading unknown = {.known = false, .millivolts = 0, .usb_power = false};
    low_battery_update(&low, unknown, 0);
    CHECK(!low_battery_update(&low, unknown, 10000));
}

static void unplugging_restarts_the_wait(void)
{
    LowBattery low;
    low_battery_init(&low);
    low_battery_update(&low, on_battery(3400), 0);
    low_battery_update(
        &low, (LowBatteryReading){.known = true, .millivolts = 3400, .usb_power = true}, 1500);
    CHECK(!low_battery_update(&low, on_battery(3400), 2500));
    CHECK(low_battery_update(&low, on_battery(3400), 4500));
}

static void an_install_on_usb_power_goes_ahead_at_any_reading(void)
{
    LowBatteryReading plugged = {.known = true, .millivolts = 3300, .usb_power = true};
    LowBatteryReading plugged_unknown = {.known = false, .millivolts = 0, .usb_power = true};
    CHECK(low_battery_install_power(plugged) == INSTALL_POWER_OK);
    CHECK(low_battery_install_power(plugged_unknown) == INSTALL_POWER_OK);
}

static void an_install_on_battery_needs_the_install_level(void)
{
    CHECK(low_battery_install_power(on_battery(LOW_BATTERY_INSTALL_MV - 1)) == INSTALL_POWER_LOW);
    CHECK(low_battery_install_power(on_battery(LOW_BATTERY_EMPTY_MV)) == INSTALL_POWER_LOW);
    CHECK(low_battery_install_power(on_battery(LOW_BATTERY_INSTALL_MV)) == INSTALL_POWER_OK);
    CHECK(low_battery_install_power(on_battery(4200)) == INSTALL_POWER_OK);
}

static void an_install_on_battery_without_a_reading_is_unknown(void)
{
    LowBatteryReading unknown = {.known = false, .millivolts = 4200, .usb_power = false};
    CHECK(low_battery_install_power(unknown) == INSTALL_POWER_UNKNOWN);
}

static void a_start_on_battery_needs_the_start_level(void)
{
    CHECK(low_battery_start_power(on_battery(LOW_BATTERY_START_MV - 1)) == START_POWER_REFUSE);
    CHECK(low_battery_start_power(on_battery(LOW_BATTERY_EMPTY_MV)) == START_POWER_REFUSE);
    CHECK(low_battery_start_power(on_battery(LOW_BATTERY_START_MV)) == START_POWER_OK);
    CHECK(low_battery_start_power(on_battery(4200)) == START_POWER_OK);
}

static void the_start_level_sits_above_the_running_cut_off(void)
{
    CHECK(LOW_BATTERY_START_MV > LOW_BATTERY_EMPTY_MV);
    LowBattery low;
    low_battery_init(&low);
    low_battery_update(&low, on_battery(LOW_BATTERY_START_MV), 0);
    CHECK(!low_battery_update(&low, on_battery(LOW_BATTERY_START_MV), 10000));
}

static void a_low_start_on_usb_power_goes_to_the_charge_screen(void)
{
    LowBatteryReading plugged = {.known = true, .millivolts = 3300, .usb_power = true};
    CHECK(low_battery_start_power(plugged) == START_POWER_CHARGE);
    plugged.millivolts = LOW_BATTERY_START_MV;
    CHECK(low_battery_start_power(plugged) == START_POWER_OK);
}

static void a_start_without_a_reading_goes_ahead(void)
{
    LowBatteryReading unknown = {.known = false, .millivolts = 0, .usb_power = false};
    CHECK(low_battery_start_power(unknown) == START_POWER_OK);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_reading_below_empty_for_two_seconds_asks_for_shutdown),
        TEST_CASE(a_reading_back_above_empty_restarts_the_wait),
        TEST_CASE(usb_power_never_shuts_down),
        TEST_CASE(an_unknown_reading_never_shuts_down),
        TEST_CASE(unplugging_restarts_the_wait),
        TEST_CASE(an_install_on_usb_power_goes_ahead_at_any_reading),
        TEST_CASE(an_install_on_battery_needs_the_install_level),
        TEST_CASE(an_install_on_battery_without_a_reading_is_unknown),
        TEST_CASE(a_start_on_battery_needs_the_start_level),
        TEST_CASE(the_start_level_sits_above_the_running_cut_off),
        TEST_CASE(a_low_start_on_usb_power_goes_to_the_charge_screen),
        TEST_CASE(a_start_without_a_reading_goes_ahead),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
