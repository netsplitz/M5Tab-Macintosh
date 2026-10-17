/*
 * board_power_olimex640.cpp - Waveshare P4 + Olimex 640x480 power hooks
 *
 * The board has no battery, so the Mac sees no Power Manager. The Olimex
 * module has no controllable backlight pin, so the backlight is fixed at
 * full: BoardPowerHW_InitialBacklight() returns -1 which makes the shared
 * power layer report "backlight fixed", and SetBacklight() is a no-op that
 * accepts the level.
 */
#include "../board_power.h"

extern "C" bool BoardPowerHW_Describe(BoardBatteryDescription *out)
{
    (void)out;
    return false;
}

extern "C" bool BoardPowerHW_Read(BoardBatteryRaw *out)
{
    (void)out;
    return false;
}

extern "C" bool BoardPowerHW_SetBacklight(int percent)
{
    (void)percent;
    return true;
}

extern "C" int BoardPowerHW_InitialBacklight(void)
{
    /* Unknown / fixed backlight - the shared layer reports it as such. */
    return -1;
}

extern "C" bool BoardPowerHW_SetCharging(bool enable)
{
    (void)enable;
    return false;
}
