/*
 * board_touch_olimex640.cpp - no-op touch HAL for the Olimex 640x480 combo
 *
 * The Olimex MIPI-LCD2.8-640x480 module has no touch controller and the
 * Waveshare ESP32-P4-WIFI6 board does not wire a separate one, so input
 * comes from the USB HID host port and the on-screen keyboard through the
 * shared input layer. Board_Init tolerates a touch-less board.
 */

#include "board_touch.h"

extern "C" bool BoardTouch_Init(void)
{
    return true;
}

extern "C" void BoardTouch_Update(void)
{
}

extern "C" BoardTouchDetail BoardTouch_GetDetail(void)
{
    BoardTouchDetail empty{};
    empty.pressed = false;
    empty.x = 0;
    empty.y = 0;
    empty.id = -1;
    return empty;
}

extern "C" void BoardTouch_GetMulti(BoardTouchMulti *out)
{
    if (!out) return;
    out->count = 0;
}
