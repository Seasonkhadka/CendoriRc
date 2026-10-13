/*
 * Physical capacitive button input (touch IC U14, ANMG08KPL1, 8ch, I2C on
 * the shared sensor bus SDA=40/SCL=41 - no INT line, it's NC on this board).
 *
 * Button <-> touch pad <-> IC channel mapping, per the board's physical
 * silkscreen:
 *   Back=TC1/CS1  Ok=TC2/CS2  Right=TC3/CS3  Down=TC4/CS4
 *   Up=TC5/CS5    Left=TC6/CS6  Setting=TC7/CS7  Power=TC8/CS8
 *
 * Protocol (per datasheet + on-board verification): I2C address 0x24,
 * output-data register 0x2A, bit N = CS(N+1), 1 = touch detected.
 */
#pragma once

#include <Arduino.h>

namespace touch {

enum class Button : uint8_t {
  Back = 0,
  Ok,
  Right,
  Down,
  Up,
  Left,
  Setting,
  Power,
  Count
};

using Callback = void (*)(Button);

void init();
void poll(); // call every loop() iteration

bool isPressed(Button b);
bool justPressed(Button b);

// Registers a callback fired once per button on the press edge.
void onPress(Callback cb);

} // namespace touch
