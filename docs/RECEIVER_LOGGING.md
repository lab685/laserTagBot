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
- `servo <0-180>` : move receiver servo directly for testing
- `servo sweep` : run servo sweep test (0 to 180 to 0)
- `servo <0-180>` : move receiver servo directly for testing
- `servohold on|off` : keep manual servo position or return to auto LDR control
- `laser on|off|toggle` : force receiver laser for testing
- `laser auto` : return laser control to transmitter

Receiver logging usage

This file explains how to control logging at runtime for `receiver_Final.ino`.

Overview

- Logging is gated by categories. By default all logs are disabled to keep the serial output clean.
- You can enable/disable categories at runtime via the serial monitor.

Categories

- `ldr` : LDR sensor readings
- `packet` : Received packet / joystick values (coarse)
- `motor` : Motor state messages (stop/start)
- `servo` : Servo actions and recovery logs
- `motorout` : Numeric motor outputs (left/right PWM values)
- `all` : Enable all categories
- `none` : Disable all categories

Commands (send via serial, end with newline)

- `help` : show help text
- `show` : show current mask and which categories are ON/OFF (also reports runtime servo/laser state)
- `set <hex|dec>` : set log mask directly (e.g. `set 0x1F` or `set 31`)
- `enable <name>` : enable a category (e.g. `enable ldr`)
- `disable <name>` : disable a category
- `servo <0-180>` : move receiver servo directly for testing
- `servohold on|off` : keep manual servo position or return to auto LDR control
- `laser on|off|toggle` : force receiver laser for testing
- `laser auto` : return laser control to transmitter

Examples

- Enable LDR and motor outputs together:
  - `enable ldr` then `enable motorout`
  - or `set 0x11` (where bits correspond to categories)
- Enable everything:
  - `enable all` or `set 0xFF`
- Turn logs off:
  - `set 0` or `disable all`
- Move servo to 90 degrees for testing:
  - `servo 90`
- Resume automatic LDR-based servo control:
  - `servohold off`
- Force laser on for testing:
  - `laser on`
- Return laser control to transmitter:
  - `laser auto`

Notes

- Changes take effect immediately.
- `show` prints the current mask and which categories are ON/OFF.
- `show` also reports runtime state: `SERVO_HOLD` (ON/OFF) and `LASER_CONTROL` (MANUAL/TRANSMITTER).
- Use the `help` command from the serial monitor for the quick reference.
