Beta vs Prod usage

Overview

This repository contains both development (beta) sketches and lean release (prod) sketches for the laser-tag controller set. Use the files under `beta/` when experimenting, tuning, or debugging; use `prod/` when preparing a minimal build for deployment.

When to use beta

- You're actively developing or tuning behavior (joystick curve, LDR thresholds, servo timings).
- You need verbose logging, runtime commands, and test helpers (servo sweep, extended serial commands).
- You want to try new features before committing to a release.

When to use prod

- You're preparing firmware for stable deployment to devices where logging and test helpers are unnecessary.
- You need a smaller binary footprint and reduced serial noise.
- You want behavior identical to the release policy (minimal runtime control surface).

How we sync changes

- Important algorithmic fixes (e.g., joystick response curve applied before mixing) should be implemented in `beta/`, tested, then backported to `prod/` by copying the relevant logic and removing development-only helpers (logging, sweep commands).
- Use the `docs/` folder to describe feature differences and keep both teams aligned.

Quick checklist before creating a release

- Verify calibration and deadzone values for the target joystick hardware.
- Confirm `MAX_SPEED`, PWM settings, and motor wiring match the hardware.
- Remove any test/debug prints from the `prod/` sketch or gate them behind a build flag.
- Test edge cases: transmitter disconnects, LDR hit behavior, servo/manual hold.

