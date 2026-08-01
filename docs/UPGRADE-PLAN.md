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
| 1 | MPU-6050 motion sensor | ✅ | ✅ 28 Jul 2026 | — self-arms | ☐ |
| 2 | Battery monitoring + heartbeat SMS | ✅ | ✅ 28 Jul 2026 | ✅ 30 Jul 2026 | ☐ |
| 3 | External charging socket | ✅ | ✅ 28 Jul 2026 | — | ☐ |
| 4 | Native UK SIM (roaming sunset) | — (no code) | ✅ 30 Jul 2026 | — | ☐ |

Hardware fitted 28 Jul 2026: GY-521 soldered in (its four unused pins INT/AD0/XCL/XDA
are parked on strips shared with unused MKR digital pins — **treat those digital pins
as reserved in any future code**), SDA/SCL/power run by wire, old tilt switches
desoldered, battery divider wired to A1, external socket wired to the 5V pin, and a
fresh LiPo fitted. External 5 V confirmed to power the board + sensor. Next:
flash, DIVIDER_RATIO check, bench arming + alarm tests, ship.

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

**Self-arming detection (no manual calibration, no mode switch):** the device is
shipped to a remote operator, so nothing may require a laptop. The sketch learns
its own "upright" reference every time it is fitted: at power-on, and again after
every unplug from the charger, it enters a *settling* state (alarms off). Once its
gravity direction has held steady for 2 minutes — what naturally happens when it's
clipped on and hanging — it locks the reference and texts **"Foal alarm ARMED"**.
"Lying flat" is then any tilt more than 60° away from that reference. If it hasn't
armed within 30 minutes it texts a nudge to check it's clipped on. Every charge
cycle therefore re-fits the reference automatically.

Prefer the **dangling** arrangement: a hanging box self-plumbs, so grazing, head
position and box spin never move it off its reference. A rigid flat-to-cheekpiece
belt-clip mount also works, but a grazing head-down pose comes within ~10–15° of
the 60° flat threshold, so the margin is slimmer.

**Operator rules to ship with the device:** fit it to a *standing* horse, and
switch it on (or unplug it from the charger) around fitting time rather than hours
before — it locks onto the first steady orientation it sees, so don't let that be
a shelf. Wait for the ARMED text before walking away.

**Bench tests before shipping** (Serial Monitor @ 9600 shows
`state / |a| / tilt / stable / motion` once a second; for quick tests drop
`ARM_SECS` to ~15 and `CALM_FLAT_BACKSTOP` to ~20, then restore):
- Hang the box still by its clip → "Foal alarm ARMED" SMS after the settle period.
- Armed + hanging still → no further SMS.
- Lay it flat + shake/rock ~8 s → LABOUR SMS.
- Lay it flat + hold still → backstop SMS only after `CALM_FLAT_BACKSTOP`.
- Flat ≥3 s / upright / repeat ×3 → EARLY WARNING SMS.
- Plug the charger in → "Charger connected", alarms dormant; unplug → re-arms after
  a fresh settle.

**Main tuning dials:** `MOTION_THRESHOLD` (30 °/s) and `ACTIVE_SECS_LABOUR` (8 s) —
raise if jumpy, lower if deaf. `FLAT_ANGLE_DEG` (60°) for flat-detection margin,
`ARM_SECS` (120 s) for how long settling takes.

## 2. Battery monitoring + daily heartbeat SMS

Battery state becomes visible remotely instead of via the power-LED window.

**Hardware:** two 100 kΩ resistors (1% metal film) as a divider —
battery + (JST side of the on/off switch) → 100 kΩ → **A1** → 100 kΩ → GND.
Same diagram as update 1. Drain ~20 µA, negligible.

**Firmware (already in the sketch):**

| SMS | When |
|-----|------|
| `Foal alarm online. Batt 87% (3.95V). Arming once fitted & settled` | every power-on (doubles as install check) |
| `Foal alarm ARMED. Batt 85%` | orientation held steady 2 min — reference locked, monitoring live |
| `Foal alarm NOT armed yet - check it is clipped on and hanging still` | one nudge if still unarmed 30 min after power-on/unplug |
| `Foal alarm OK (armed). Batt 78% (3.90V)` | daily heartbeat (~24 h) with armed state; a *missing* heartbeat means the unit is dead/flat/out of signal |
| `LOW BATTERY 3.38V - charge foal alarm` | sustained reading < 3.40 V; re-arms after charging past 3.70 V |
| `Charger connected. Batt 42%` | external 5 V detected — posture alarms suppressed while on charge |
| `Battery full - foal alarm ready` | charger present and the BQ24195 reports charge complete |

**Calibration:** compare the boot SMS voltage against a multimeter on the battery and
nudge `DIVIDER_RATIO` (2.0 nominal) until they agree. **Done 30 Jul 2026:** off-charger,
same-minute readings were SMS 4.02 V vs meter 4.03 V — 0.25% off, within resistor
tolerance, ratio kept at 2.0. (Note: "Battery full" tops out around ~89% on the SMS
scale by design — the charger terminates at ~4.11 V, not 4.20 V, for cell longevity.)

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

**Firmware charger-awareness:** because the box is powered while charging (switch
must be ON) and lies flat on a shelf for hours, the sketch reads the BQ24195's
status register (read-only, shared I²C bus) each second. While external 5 V is
present the posture alarms are suppressed and their state zeroed — otherwise the
25-min backstop would text "CHECK MARE" during every charge. Plugging in sends
`Charger connected. Batt XX%` (proof the socket made contact), and charge
completion sends `Battery full - foal alarm ready`.

---

## 4. SIM replacement (diagnosed 29 Jul 2026)

The original Things Mobile (Italian roaming) SIM can no longer connect anywhere in
the UK on this hardware, through no fault of the device:

- The SARA-U201 modem is **2G/3G only**.
- All four UK operators completed their **3G switch-offs** by 2025.
- **O2 — the last UK network accepting inbound 2G roamers — withdrew that access
  from 1 Oct 2025** (Ofcom, "advice for IoT and third-party device suppliers").
  Foreign roaming SIMs on 2G-only devices now get rejected by every UK network.
- Confirmed by the AT diagnostic: modem healthy (IMEI OK), SIM healthy
  (`+CPIN: READY`), network heard and attach attempted, ends `+CREG: 0,3`
  = registration **denied** (policy, not signal).

**Fix:** a native UK PAYG SIM on the EE, O2 or Vodafone families (native/MVNO SIMs
are unaffected; their 2G runs until ~2029–2030).

**Decision: Lebara (Vodafone MVNO).** Already used for the farm's LTE surveillance
camera with good reception, so Vodafone has proven presence on site; Lebara SIMs
include Vodafone 2G access. Note the camera proves the 4G layer — verify the 2G
"calls & texts" layer for the farm postcode on Lebara's coverage checker before
relying on it. Lebara's ~90-day inactivity policy is covered by the daily
heartbeat SMS. Steps:

- [ ] Check **2G coverage at the farm** per network (signalchecker.co.uk) and pick
      accordingly — rural 900 MHz (O2/Vodafone) often carries further than
      1800 MHz (EE).
- [x] Fit SIM (slot on the underside of the MKR); Lebara ships PIN-free, so
      `SECRET_PINNUMBER` is now `""`.
- [x] Re-run the GSM diagnostic — **passed 30 Jul 2026**: registered on
      "Lebara" (2G, `+CREG: 0,1`). Bench signal CSQ 5/31 (weak indoors, as
      expected at that spot); status SMSes now report signal so the farm
      fitting gives the number that matters.
- [ ] Note: the daily heartbeat SMS doubles as the PAYG keep-alive — most PAYG
      SIMs deactivate after ~6 months without chargeable activity.

Long-term note: UK 2G sunsets ~2029–2030. If the alarm must outlive that, the
successor path is the pin-compatible Arduino MKR NB 1500 (LTE-M) — the MKRNB
library mirrors MKRGSM almost 1:1.

**Tool — reading SMS sent TO the alarm's SIM:** flash
[`tools/SmsInbox/SmsInbox.ino`](../tools/SmsInbox/SmsInbox.ino) (copy your
`arduino_secrets.h` into its folder) and open the Serial Monitor — every
incoming SMS is printed. Needed for one-time verification codes (Lebara app
registration), balance texts, etc. Re-flash the main sketch afterwards.

## Battery-only GSM attach failure (30 Jul 2026) — ROOT CAUSE FOUND, fix pending

Symptom: on battery only, no 4-blink (GSM `begin()` never succeeds), no SMS; plug
the charger in at the same spot and it registers and texts fine. Same location →
not signal: GSM attach draws ~2 A transmit bursts (max power at Sig 10–12), so
this was power delivery.

**Root cause (confirmed by test): the on/off switch's own contacts.** Jumpering
across the switch terminals — leaving all wires/crimps/connectors in circuit —
restored battery-only attach immediately. Aged, oxidised contacts (never
self-cleaning at ~50 mA idle) drop too much voltage during the 2 A bursts. The
switch ran the old battery for years on shrinking margin; the rebuild moved the
operating point just enough to expose it (hence the intermittent successes
earlier the same day).

**Update 31 Jul 2026 — battery shorted during the rebuild.** Red and black battery
leads got soldered to the same strip: dead short across the cell. Cell now rests at
2.87 V (below the 3.0 V over-discharge floor), "charges" suspiciously fast but won't
run the board — classic short-damaged cell (internal resistance up, capacity gone).
Board itself unharmed (runs on charger). **The cell is compromised and will be
replaced, not rehabilitated** — shorted + over-discharged LiPos are a charge-time
fire risk, unacceptable for an unattended stable device. Do not charge it
unattended; check for swelling/heat/smell and recycle it properly.

**Battery-path rebuild (one work package):**
- [ ] Remove the shorting joint; meter-verify + and − lines are not ~0 Ω.
- [ ] Inspect harness for heat-damaged insulation; replace suspect wire.
- [ ] Replacement 2500 mAh LiPo with protection PCB rated **≥3 A discharge**.
- [ ] Inline fuse (~3 A) in the battery + lead — a soldering slip then costs a
      fuse, not a cell.
- [ ] Fit the replacement switch rated **≥3 A** (sealed/booted preferred), leads
      **soldered** to the lugs, heat-shrunk, short 22–24 AWG run.
- [ ] Re-verify: several battery-only boots (4 blinks + boot SMS each time) and
      at least one alarm SMS sent on battery.

## Before shipping — final checklist

- [ ] **Set the ship recipients in `arduino_secrets.h`**: `SECRET_PHONE_NUMBER`
      = mum (testing currently uses Henrik's number), `SECRET_PHONE_NUMBER_2`
      = Henrik, for remote monitoring ("" disables a slot). Re-flash and
      confirm the boot SMS arrives on both phones. Two recipients = double
      the SMS count on the Lebara tariff.
- [ ] Restore any thresholds relaxed for bench testing (`ARM_SECS` 120,
      `CALM_FLAT_BACKSTOP` 1500).
- [ ] Full bench pass: ARMED SMS, labour, backstop, restless, charger
      connect/full/re-arm.
- [ ] New ≥3 A power switch fitted; battery-only GSM attach re-verified
      (several boots + an alarm SMS on battery).
- [ ] Charge to 100 % (orange LED off, `Battery full` SMS).
- [ ] Pack the dedicated USB-A charger + A-to-C cable with the device, plus the
      one-page operator note (fit to a standing horse, wait for the ARMED text,
      charge daily, heartbeat = it's alive).

## Parts list (all three updates)

| Part | Spec | Qty | ~Cost |
|------|------|-----|-------|
| MPU-6050 module | GY-521 breakout | 2 (one spare) | £2–3 ea |
| Resistors | 100 kΩ 1% metal film | 2 | pennies |
| USB-C panel socket | 2-pin waterproof, screw-in (RUNCCI-YUN) | pack | ~£8–10 |
| Hook-up wire | thin solid-core | — | ~£2 |
| On/off switch | ≥3 A rated, sealed or rubber-booted | 1 | ~£2–3 |
| LiPo battery | Existing pack is a 785060 (7.8×50×60). Two candidates, decided by **measured clearance above the battery bay** (a LiPo must never be compressed, and packs swell with age): **≥12 mm free height → 105151** (10×51×53, 3000 mAh, 1.5 A std / 3 A max — electrically the best); **tighter → 724957** (7.2×49×57–59, 2500 mAh; buy the listing that specs 1.25 A std / 2.5 A max — seller tables vary). 375678 rejected: 80 mm too long. **Meter the JST plug polarity before connecting — aftermarket packs often ship reversed and a swapped plug kills the board** | 1 | ~£10 |
| Inline fuse + holder | ~3 A, battery + lead | 1 | ~£1 |
| On/off switch | ≥3 A rated, sealed/rubber boot preferred | 1 | ~£2–3 |
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
