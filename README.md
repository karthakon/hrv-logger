# hrv-logger

A minimal Pebble watchapp that validates the HRV data path added by
PebbleOS PR #1670 (HRV support for the GH3X2X sensor on emery / Pebble
Time 2). It is a transport harness only: it subscribes to the health
service, requests HRV sampling, and displays the raw peak-to-peak
interval (PPI) stream and event counts. No filtering, no HRV math, no
sleep staging.

## What it proves

While sampling is ON, the watch receives `HealthEventHRVUpdate` events
carrying PPI values in milliseconds. The screen shows:

- **PPI** — the most recent interval (ms)
- **Range** — min-max PPI seen this run (a spread confirms the
  intervals vary beat to beat, i.e. real data, not a stuck value)
- **HRV ev** — count of HRV events received
- **HR / HR ev** — heart rate and HR event count, to confirm HR-only
  operation is unaffected

Long-press SELECT toggles sampling ON/OFF. Toggling OFF confirms the
sensor returns to HR-only when no app requests HRV.

## Branches

- **`main`** — builds against the PR #1670 firmware/SDK
  (https://github.com/karthakon/PebbleOS, branch `hrv-gh3x2x`). Use this
  branch for testing PR #1670.
- **`unified-fw`** — testing branch for the experimental unified
  HRV + SpO2 firmware
  (https://github.com/karthakon/PebbleOS/tree/hrv-spo2-unified). Adds an
  SpO2 readout and calls SDK symbols that only exist in that firmware; it
  will crash on stock or #1670 firmware. Firmware, SDK, and app must all
  come from the same tree.

## Requires custom firmware

This app calls `health_service_set_hrv_sample_period()` and reads
`health_service_peek_hrv_ppi_ms()`, which only exist in firmware and an
SDK built from the PR #1670 tree. It will NOT build or run against
stock PebbleOS or the stock SDK. You must flash the fork firmware and
build the SDK from the same tree first (steps 1-2 below).

## Building and installing

### 1. Flash the fork firmware

Clone https://github.com/karthakon/PebbleOS, branch `hrv-gh3x2x`,
with submodules, then build in the official Docker image:

    git clone -b hrv-gh3x2x --recurse-submodules https://github.com/karthakon/PebbleOS.git
    cd PebbleOS
    docker run --rm -it -v "$PWD":/pebble -w /pebble ghcr.io/coredevices/pebbleos-docker:v5 bash
    pip install -r requirements.txt
    git config --global --add safe.directory '*'
    ./waf configure --board obelix@pvt -DCONFIG_FIRMWARE_SLOT=1
    ./waf build && ./waf bundle && exit

You must manually keep track of which slot you are flashing. If this
is your first custom firmware, build for slot 1 (assuming original
firmware is on slot 0). For all future flashes, build for the
alternate slot. Install with the pebble tool: enable Developer
Connection in the Pebble app, then

    pebble fw --phone <PHONE_IP> install build/normal_obelix_pvt_*.pbz

Do NOT accept any "Update PebbleOS" prompt afterward — that reverts
to stock.

### 2. Build the SDK from the same tree

    pebble sdk install --tintin /path/to/PebbleOS

### 3. Build and install this app

    git clone https://github.com/karthakon/hrv-logger.git
    cd hrv-logger
    pebble build
    pebble install --phone <PHONE_IP>

Target platform is **emery** only — the GH3X2X HRV path exists on
Pebble Time 2 hardware; other platforms lack the sensor support.

## Reproducing the transport check

1. Install on a PT2 running firmware built from the #1670 tree (above).
2. Open the app, long-press SELECT to start sampling.
3. Sit still ~3 minutes. Confirm PPI settles into a physiological
   range (roughly 600-1100 ms at rest), Range shows a spread, and the
   HRV event counter climbs steadily.
4. Long-press SELECT again; confirm sampling flips OFF and HR keeps
   updating.

The on-screen numbers after step 3 are the transport evidence; no
`pebble logs` connection is required.

## Note on the wscript

`wscript` appends `-D_TIME_T_DECLARED` and `-D__time_t_defined` in
`configure()`. This is required: without it the SDK headers re-typedef
`time_t` and collide with the build's `-Dtime_t=long`, failing the
build. Keep those lines.

## Overnight HRV validation (July 2026)

Firmware: PebbleOS `hrv-gh3x2x` (PR #1670), Pebble Time 2 (obelix@pvt), reference device Garmin Instinct Crossover Solar on the other wrist.

**Problem:** overnight HRV delivery was ~2 intervals/min with a ~99% jump-rejection rate — too sparse for meaningful RMSSD. The root cause was isolated using hrv-logger's live gated stats readout, which made per-minute delivery and rejection rates visible in short daytime tests.

**Two firmware fixes:**

1. **Staleness guard** (`9627bca`): reset the jump gate after a 10 s gap (`HRV_STALE_SEC 10`) so the first interval after a dropout isn't compared against a stale predecessor. Result: acceptance ratio 37%→50%, jump rejections 1.69→1.01/min. Helped, but delivery stayed sparse.
2. **Sampling-rate fix** (`7eb5f49c`): the Goodix HRV algorithm's vendor config declares `fs = 100`, but the driver registered the HRV function at 25 Hz — the algo ran at a quarter of its design rate. New `GH3X2X_HRV_SAMPLING_RATE 100` for HRV registration (HR stays at 25 Hz).

**Results (fw `7eb5f49c`):**

| Metric | Before (25 Hz) | After (100 Hz) |
|---|---|---|
| Awake delivery (5 min sitting) | — | ~92 intervals/min |
| Overnight delivery | ~2/min | 64/min (38,244 beats / 9h57m) |
| Overnight rejection ratio | ~50% | 1.3% |
| Overnight RMSSD vs reference | — | 63 ms vs Instinct 44 ms (peak 5-min 65 ms) |
| Overnight battery (static screen, release) | 0.4–0.8 %/hr | ~0.9 %/hr (9% / 9h57m) |

100 Hz costs roughly 4–5% extra battery per night; kept as the default.
