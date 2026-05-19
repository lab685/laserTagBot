# laserTagBot

## Overview

laserTagBot is a compact ESP32-based controller project. It includes transmitter and receiver sketches with both development (beta) and release (prod) variants. The transmitter reads a joystick and a laser button and sends pre-mixed motor commands over ESP-NOW; the receiver accepts those packets, drives motors, controls a laser, monitors LDR sensors for hits, and operates a servo.

## Quick notes

- Development (beta) sketches include verbose logging, test helpers, and runtime commands useful while tuning.
- Release (prod) sketches are leaner, removing debug helpers while retaining core behavior (calibration, deadzone, joystick curve).

## Documentation

- Receiver logging and runtime commands: [docs/RECEIVER_LOGGING.md](docs/RECEIVER_LOGGING.md)
- Transmitter logging and throttle curve: [docs/TRANSMITTER_LOGGING.md](docs/TRANSMITTER_LOGGING.md)
- Receiver features and behavior: [docs/RECEIVER_FEATURES.md](docs/RECEIVER_FEATURES.md)
- Transmitter features and behavior: [docs/TRANSMITTER_FEATURES.md](docs/TRANSMITTER_FEATURES.md)
- When to use Beta vs Prod and branch guidance: [docs/USAGE_AND_BRANCHES.md](docs/USAGE_AND_BRANCHES.md)

## Repository layout

- `beta/` — development sketches with logging and helpers (e.g., `transmitter_final.ino`, `receiver_Final.ino`).
- `prod/` — release-ready sketches (e.g., `transmitter_release.ino`, `receiver_release.ino`).
- `docs/` — documentation files linked above.

## Tuning

- The joystick/throttle curve can be tuned in the transmitter `applyJoystickCurve()` function by adjusting the `expo` constant. Higher `expo` (closer to 1.0) softens low-end response; lower values make controls more linear/aggressive.

