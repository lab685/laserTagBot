Transmitter logging usage

This file explains how to control logging at runtime for `transmitter_final.ino`.

Overview

- Logging is gated by categories.
- By default all transmitter logs are disabled to keep the serial output clean.
- You can enable/disable categories at runtime via the serial monitor.

Categories

- joystick : Raw joystick readings and joystick mapping details
- laser : Laser button state changes
- send : Transmit output values and ESP-NOW send status
- system : Startup and system messages
- all : Enable all categories
- none : Disable all categories

Commands (send via serial, end with newline)

- `help` : show help text
- `show` : show current mask and which categories are ON/OFF
- `set <hex|dec>` : set log mask directly (e.g. `set 0x0F` or `set 15`)
- `enable <name>` : enable a category (e.g. `enable joystick`)
- `disable <name>` : disable a category
- `toggle <name>` : toggle a category on/off

Examples

- Enable joystick and send logs together:
  - `enable joystick`
  - `enable send`
  - or `set 0x05`
- Enable everything:
  - `enable all` or `set 0xFF`
- Turn logs off:
  - `set 0` or `disable all`

Notes

- Changes take effect immediately.
- `show` prints the current mask and which categories are ON/OFF.
- Use the `help` command from the serial monitor for the quick reference.
