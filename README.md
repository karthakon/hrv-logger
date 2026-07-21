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

## Role in HRV validation

hrv-logger's live gated stats readout (per-minute delivery, rejection
rate, R/J split, RMSSD) is the diagnostic tool used to isolate HRV
delivery and rejection problems in short daytime tests, without the
30-minute save minimum of overnight recordings. The overnight
validation writeup and the resulting firmware fixes (staleness guard,
100 Hz sampling-rate fix) are documented in the hrv-monitor README.
