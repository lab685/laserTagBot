Receiver (features and notes)

This document describes the features in the receiver sketches (`receiver_Final.ino` and related variants).

Core features

- ESP-NOW Receiver: listens for joystick + laser packets from the transmitter.
- Motor control: drives two TB6612FNG motor drivers (left/right) with direction and PWM.
- Laser control: receiver can accept laser ON/OFF from transmitter, or be forced to manual via serial commands.
- LDR (light-dependent resistor) detection: detects hits via two LDR sensors and enters a latched "hit" state (stops motors, turns laser off, moves servo to hit position).
- Servo trigger: moves a servo on hit detection; servo supports manual hold/test via serial commands.
- Connection safety: detects transmitter timeout and stops systems if connection lost.
- Runtime logging control: enable/disable categories over serial for targeted debugging.

Runtime controls

- Serial `help`/`show`/`enable`/`disable`/`set` commands control runtime logging. `show` now reports runtime state including `SERVO_HOLD` (ON/OFF) and `LASER_CONTROL` (MANUAL/TRANSMITTER).
- `servo <angle>` for direct servo positioning.
- `servohold on|off` to keep a manual hold (prevents automatic LDR servo updates).
- `laser on|off|toggle|auto` for manual laser testing and returning control to the transmitter.

Behavior notes

- When an LDR hit is detected the receiver latches into "hit" mode; the device must be restarted to resume normal operation.
- Use the serial `show` command to quickly verify current log categories and runtime servo/laser control state.

Which sketch to use

- Use `receiver_Final.ino` (beta) during development: it contains full logging, testing helpers, and additional safety/debug features useful while tuning.
- Use `receiver_release.ino` (prod) for lean deployments where runtime logging and heavy test helpers are not needed.

Contact

If you need help adjusting servo timings, LDR thresholds, or motor `MAX_SPEED`, open an issue or ask for a tuning session.

Schematics

Below is a placeholder schematic image for the receiver.

![Receiver schematic](images/receiver_schematic.pmg)

Circuit diagram link (placeholder): [Receiver circuit diagram](https://app.cirkitdesigner.com/project/0004e2bf-ef1f-424e-ad21-442a3b71bbf9)
