Receiver logging usage

This file explains how to control logging at runtime for `receiver_Final.ino`.

Overview

- Logging is gated by categories. By default all logs are disabled to keep the serial output clean.
- You can enable/disable categories at runtime via the serial monitor.

Categories

- ldr : LDR sensor readings
- packet : Received packet / joystick values (coarse)
- motor : Motor state messages (stop/start)
- servo : Servo actions and recovery logs
- motorout : Numeric motor outputs (left/right PWM values)
- all : Enable all categories
- none : Disable all categories

Commands (send via serial, end with newline)

- `help` : show help text
- `show` : show current mask and which categories are ON
- `set <hex|dec>` : set log mask directly (e.g. `set 0x1F` or `set 31`)
- `enable <name>` : enable a category (e.g. `enable ldr`)
- `disable <name>` : disable a category
- `toggle <name>` : toggle a category on/off

Examples

- Enable LDR and motor outputs together:
  - `enable ldr` then `enable motorout`
  - or `set 0x11` (where bits correspond to categories)
- Enable everything:
  - `enable all` or `set 0xFF`
- Turn logs off:
  - `set 0` or `disable all`

Notes

- Changes take effect immediately.
- `show` prints the current mask and which categories are ON/OFF.
- Use the `help` command from the serial monitor for the quick reference.
