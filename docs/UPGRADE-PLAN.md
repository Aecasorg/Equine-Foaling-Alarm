# Upgrade programme — July 2026

The 2020 build triggers on a tilt switch alone, which can only answer *"is the mare
flat right now?"*. Mares also lie flat in normal REM sleep, so a tilt-only alarm is
structurally stuck between too sensitive (fires on naps) and not sensitive enough
(raise the threshold and miss the real thing). The sleeping position and the labour
position are the same position — the fix is to detect **behaviour** (motion patterns)
rather than **position**.

Three updates are planned. Code for 1 + 2 is already merged into
[`GiiFoalAlarm.ino`](../GiiFoalAlarm.ino); hardware is fitted step by step, sensor first.

## Status at a glance

| # | Update | Code | Hardware | Calibrated | Field-tested |
|---|--------|:----:|:--------:|:----------:|:------------:|
| 1 | MPU-6050 motion sensor | ✅ | ☐ | ☐ | ☐ |
| 2 | Battery monitoring + heartbeat SMS | ✅ | ☐ | ☐ | ☐ |
| 3 | External charging socket | — (no code) | ☐ | — | ☐ |

Legacy tilt-switch firmware (deployed 2022–2026) is archived in
[`legacy/GiiFoalAlarm-tilt-2022.ino`](../legacy/GiiFoalAlarm-tilt-2022.ino).

---

## 1. MPU-6050 motion sensor (major update)

Replace the two parallel ball tilt switches with a GY-521 (MPU-6050) accelerometer +
gyroscope, and replace the single "flat for 30 s" trigger with pattern detection.

**Wiring** (see [wiring-mpu6050-battery.svg](wiring-mpu6050-battery.svg)):

| GY-521 pin | MKR GSM 1400 pin |
|------------|------------------|
| VCC | VCC (**3.3 V pin — not 5V**) |
| GND | GND |
| SCL | D12 (SCL) |
| SDA | D11 (SDA) |
| XDA / XCL / AD0 / INT | not connected (AD0 floating = I²C address 0x68, which the library expects) |

**Space-saving tip:** only the first four pins in the GY-521's row (VCC, GND, SCL,
SDA) are needed — snap the header down to a 4-pin piece, solder just those, and
leave XDA/XCL/AD0/INT pads empty (AD0 unconnected = address 0x68, which the library
expects). Dab of hot glue under the unsupported end. On perma-proto board, holes in
a column strip are connected — so a soldered-in pin "touching" an MKR pin's strip is
wired to it. Unused GY pins sharing strips with unused MKR pins (~2–~5) is
tolerable, but keep AD0 off anything that can sit high, and keep everything off the
D1 strip while the old tilt switches + pull-up are fitted — that strip still swings
3.3 V/GND as the balls roll. Best: desolder the tilt switches and their pull-up
(code-dead since the rewrite) — it frees board space too.

**Alarm tiers** (each sends a distinct SMS):

| Tier | Condition | Meaning |
|------|-----------|---------|
| LABOUR | flat AND gyro-active for 8 s | fast, confident alarm — a sleeping mare is flat but *still* |
| BACKSTOP | flat 25 min even if calm | safety net: missed event or cast mare |
| EARLY WARNING | ≥3 lie-down episodes in 20 min (3 s debounce) | stage-1 restlessness starting |

**Mounting hardware:** spring belt clips
([eBay 334736377513](https://www.ebay.co.uk/itm/334736377513), ✅ bought) — the box
clips onto the headcollar and pops off one-handed for charging, replacing the old
duct-tape/trigger-clip arrangement. Fit notes: the box must go back in the **same
position and orientation every time** (the axis calibration assumes it — mark the
spot), and a small keeper strap/lanyard is a cheap backstop since spring clips can
work loose on a horse that rubs or rolls.

**Mounting modes** — set `HANGING_MOUNT` in the sketch to match how the box
actually sits once the belt clip is fitted:

- `true` (box dangles freely, as with the old pendant clip): the box is
  self-plumbing, so the axis along the strap reads ~1 g in any upright head position
  and collapses when she lies flat. Detection = hang axis < 0.45 g.
- `false` (box held flat against the cheekpiece — the likely belt-clip geometry):
  side axis picks up gravity when she lies on her side. Detection = side axis
  > 0.70 g. This is the more deterministic mode — prefer it if the clip holds the
  box firmly flat against the strap.

Re-run the axis calibration whenever the mounting arrangement changes.

**Calibration (required before field use):** Serial Monitor @ 9600 prints
`accX/accY/accZ/axisG/motion/flat` once a second. Hang the box still by its clip —
the axis reading ~±1.0 is the one; set it in `monitoredAxisAccel()` (default `accX`).
Lay the box on its side — reading should drop near 0 and `flat=1` appear.
Power-on orientation doesn't matter (gyro-only offset calibration); just hold the
box still for a second after switching on.

**Bench tests before trusting it:**
- Hold hanging + still → no SMS.
- Lay flat + shake/rock ~8 s → LABOUR SMS.
- Lay flat + hold still → nothing (temporarily drop `CALM_FLAT_BACKSTOP` to ~20 to
  prove the backstop path).
- Flat ≥3 s / upright / repeat ×3 → EARLY WARNING SMS.

**Main tuning dials:** `MOTION_THRESHOLD` (30 °/s) and `ACTIVE_SECS_LABOUR` (8 s) —
raise if jumpy, lower if deaf. `SLACK_THRESHOLD_G` if flat detection needs margin.

## 2. Battery monitoring + daily heartbeat SMS

Battery state becomes visible remotely instead of via the power-LED window.

**Hardware:** two 100 kΩ resistors (1% metal film) as a divider —
battery + (JST side of the on/off switch) → 100 kΩ → **A1** → 100 kΩ → GND.
Same diagram as update 1. Drain ~20 µA, negligible.

**Firmware (already in the sketch):**

| SMS | When |
|-----|------|
| `Foal alarm online. Batt 87% (3.95V)` | every power-on (doubles as install check) |
| `Foal alarm OK. Batt 78% (3.90V)` | daily heartbeat (~24 h); a *missing* heartbeat means the unit is dead/flat/out of signal |
| `LOW BATTERY 3.38V - charge foal alarm` | sustained reading < 3.40 V; re-arms after charging past 3.70 V |

**Calibration:** compare the boot SMS voltage against a multimeter on the battery and
nudge `DIVIDER_RATIO` (2.0 nominal) until they agree.

**Expected battery life:** ~2–4 days per charge (modem dominates; weak signal and
cold nights push it toward the low end). The heartbeat's day-over-day % drop gives
the true figure.

## 3. External charging socket (no more opening the box)

**Part:** 2-pin waterproof panel-mount USB-C female socket (e.g. RUNCCI-YUN,
Amazon B0CRD36F38 / B0CPLS1X29). Power-only — firmware flashing still uses the
internal USB port.

**Why it works without the USB connector** (see
[wiring-charge-port.svg](wiring-charge-port.svg)): on the MKR GSM 1400 schematic the
`5V` header pin is the same net as USB VBUS, behind polyfuse F1; that net feeds the
BQ24195L charger input through Schottky D3. Solder the socket's **+** to the `5V`
pin row and **−** to GND and charging is electrically identical to plugging in USB.
(`VIN` is *not* equivalent — it enters via a FET switch and does not run the charger.)

The board's LEDs (visible through the housing window) become the charge indicator:
green DL3 = charger present, orange DL2 = charging, orange off = full.

**Usage rules:**
1. Meter the socket's two wires for polarity before first connection — reversed
   polarity is unrecoverable.
2. Charge from a **USB-A charger with an A-to-C cable**. The 2-pin socket has no CC
   pull-down resistors, so C-to-C chargers deliver 0 V.
3. On/off switch **ON** while charging, otherwise the board runs but the battery
   can't fill.
4. Never feed the internal USB port and the external socket at the same time.

**Charge rate:** the charger's USB D+/D− detection lines are unpopulated on this
board, so input is capped at ~500 mA regardless of charger — a full 2500 mAh charge
from empty is ~5–6 h.

**Mounting:** ~12–14 mm hole in a *downward-facing* wall, gasket on a flat surface,
cap on in the field. The socket is a dead input while on the horse — no corrosion
or short risk.

**Charging routine** (with updates 1–3 all fitted): unclip the box from the
headcollar → plug into the dedicated USB-A charger in the tack room → clip back on.
The belt clip removes the tape faff, the socket removes the unscrew-the-lid step;
charging itself always happens off the horse.

---

## Parts list (all three updates)

| Part | Spec | Qty | ~Cost |
|------|------|-----|-------|
| MPU-6050 module | GY-521 breakout | 2 (one spare) | £2–3 ea |
| Resistors | 100 kΩ 1% metal film | 2 | pennies |
| USB-C panel socket | 2-pin waterproof, screw-in (RUNCCI-YUN) | pack | ~£8–10 |
| Hook-up wire | thin solid-core | — | ~£2 |
| Belt clips | spring clip, box-to-headcollar quick release ([eBay 334736377513](https://www.ebay.co.uk/itm/334736377513)) | — | ✅ bought |

## Parked ideas (not scheduled)

- Charging-status SMS ("plugged in" / "battery full") via the `Arduino_PMIC` library.
- Rigid GoPro-style or Fidlock cradle — fallback if the belt clips (bought, see
  update 1 mounting) prove insecure or don't hold a repeatable orientation.
- Heartbeat at a fixed clock time (RTCZero + network time) instead of ~24 h rolling.
- MCU sleep + MPU motion-interrupt wake for multi-week battery life.
- Second complete unit as a hot spare / swap-to-charge.
- Vulva magnet + reed switch (vet-fitted, Foalert-style) as a near-certain stage-2
  trigger complementing the behavioural early warning.
