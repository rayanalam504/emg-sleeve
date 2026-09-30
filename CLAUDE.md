# EMG Sleeve firmware

Wearable EMG MIDI controller. A MyoWare 2.0 on the forearm feeds a Raspberry Pi Pico 2 W, which runs a small DSP chain and sends MIDI CC 11 over BLE MIDI to REAPER on macOS, where it drives a wah/filter plugin.

This repo is a C++ (pico-sdk) port of a working CircuitPython firmware. The original is in `reference/code.py`. It is the ground truth for behaviour.

## Who you're working with

Rayan is a physics student. He's fluent in Python and NumPy and not in C++, so he cannot review C++ line by line. That means:

- Don't narrate individual edits. At the end of each stage, give a short plain-language summary: what changed, anything that deviates from this brief, and exactly what he should check on the hardware.
- Keep the code simple and flat. Globals and free functions are fine for firmware. No class hierarchies, templates or abstraction layers the task doesn't need.
- When something is ambiguous (hardware, wiring, feel), ask. Don't guess.
- Rayan is direct and prefers being told plainly when something is wrong, including his own requests.

## Hardware

- Board: Raspberry Pi Pico 2 W (RP2350, CYW43 radio). `PICO_BOARD=pico2_w`.
- GP26 (ADC0): MyoWare ENV output. The MyoWare is powered from 3V3, the same rail as the ADC reference.
- GP27 (ADC1): battery voltage divider. Divider ratio unknown, so ask before stage 4.
- GP15: button. Unused for now. Wiring (active-low or active-high) unknown, so ask if needed.
- GP16: LED, PWM.
- Power: single-cell LiPo through a LiPo Amigo charge board (no power switch).
- Onboard LED: driven through the CYW43 (`cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, ...)`), not a normal GPIO.

## Safety rule (hard)

Electrodes go on skin only when nothing in the chain is mains-connected: the laptop is on battery, not charging, and the pedalboard or amp is unplugged or isolated. Never ask Rayan to wear the sleeve for a test without reminding him of this.

## Toolchain

- Installed by the VS Code Raspberry Pi Pico extension under `~/.pico-sdk/` (SDK, ARM GCC, CMake, Ninja, picotool). Nothing comes from Homebrew.
- Build: find the ninja binary under `~/.pico-sdk/ninja/` and run `<ninja> -C build`. If `build/` is missing or stale, configure with the CMake under `~/.pico-sdk/cmake/` using `-G Ninja`.
- Flash: use picotool from `~/.pico-sdk/picotool/`: `picotool load -x build/emg_sleeve.uf2 -f`. The `-f` flag reboots a running board into BOOTSEL over USB, so no button press is needed while the current firmware has USB stdio. If it fails, ask Rayan to hold BOOTSEL and replug.
- Serial: `/dev/tty.usbmodem*` at 115200. Read it non-interactively (e.g. a short pyserial script), never `screen`. Close the port afterwards so Rayan's `plot.py` can use it.
- Build after every change. Never leave the tree in a non-building state at the end of a turn.
- Reference code: clone `https://github.com/raspberrypi/pico-examples` to `../pico-examples` if it isn't there. The standalone BLE server example under `pico_w/bt/` is the template for advertising, GATT and notifications. `btstack_config.h` comes from `pico_w/bt/config/`.

## Status

- Toolchain verified: blink on the CYW43 LED builds, flashes and prints over USB serial.
- Next: stage 1.

## Keeping this file current

This file is the project's memory between sessions. You maintain it, not Rayan.

- When Rayan confirms a stage, update **Status** (what works, what's next) in the same commit as the stage.
- When Rayan makes a decision, or answers an open item, write it into the relevant section and remove it from **Open items**.
- When you learn something non-obvious about this hardware or toolchain (a gotcha, a measured value such as the negotiated BLE interval), add it under **Learnings**.
- Keep edits short. Replace outdated lines instead of appending history; git holds the history.

## Learnings

(none yet)

## Git

- Commit only after Rayan confirms a stage works on the hardware. One commit per stage, with a plain message.
- Push to `origin` after committing (private GitHub repo `emg-sleeve`).

## DSP spec (port exactly, except the two listed changes)

All tunables live in one header, `config.h`, with the same names and values as `reference/code.py`:

```
FLOOR = 1200, REF = 6000, K = 30
WIN = 50
SAMPLE_HZ = 500, RELEASE_MS = 0 (0 = bypass)
WINDOWING = 3500
OUT_HZ = 100, DEADBAND = 3
MIDI_CH = 0, MIDI_CC = 11
DEBUG = true
```

Pipeline, per sample at exactly 500 Hz (hardware timer, not a busy-wait loop):

1. `raw = adc_read() << 4`. **Change 1 (scale).** CircuitPython's `AnalogIn.value` reports 16-bit counts (the 12-bit reading shifted left by 4), and every constant above is on that scale. The shift keeps all constants, the notebook and `plot.py` valid.
2. Trailing mean over `WIN` samples: ring buffer with a running sum. Keep the sum as an integer (`uint32_t`) so it never drifts. During the first `WIN` samples divide by the number filled so far, as the Python does.
3. Release envelope: rises pass through immediately; falls decay as `held = v + (held - v) * alpha`, with `alpha = exp(-1 / tau)` and `tau = RELEASE_MS / 1000 * SAMPLE_HZ`. `alpha = 0` when `RELEASE_MS = 0`.
4. Log map: `above = max(h - FLOOR, 0)`, `scaled = above / REF`, `d = log(1 + K * scaled)`. Use `float` and `logf`: the RP2350 FPU is single precision.
5. Every `1 / OUT_HZ` seconds: `cc = (int)(min(d, CEILING) / CEILING * 127)`, where `CEILING = log_map(WINDOWING)` is computed once at startup. Truncate as Python's `int()` does.

Send rule. **Change 2 (flooding fix).** The Python sends CC 0 or CC 127 on every output tick while pinned at either end. The port sends only on a real change:

```
if (cc != sent && (abs(cc - sent) >= DEADBAND || cc == 0 || cc == 127)) { sent = cc; send(sent); }
```

`sent` starts at -1, so the first value always goes out.

Note: only the ratio `K / REF` affects the output, because the result is normalised by `CEILING`. Keep both constants for parity with the analysis notebook, and put a comment in `config.h` saying that tuning `K` alone is enough.

Debug output: when `DEBUG`, print exactly `"%d %d %d\n"` of `raw` (shifted), `(int)held`, `sent` on every output tick. `plot.py` and `cc_monitor.py` parse this format, so don't change it.

Don't change the DSP maths or constants without asking Rayan.

## BLE MIDI spec

- BTstack via pico-sdk (`pico_btstack_ble`, `pico_btstack_cyw43`, a cyw43_arch variant). The GATT database is a `.gatt` file compiled with `pico_btstack_make_gatt_header`.
- Service UUID `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`. One characteristic, `7772E5DB-3868-4112-A1A9-F2669D106BF3`, with read (returns an empty value), write-without-response and notify.
- Advertising: connectable, with the 128-bit MIDI service UUID in the advert. The name "EMG Sleeve" goes in the scan response if it doesn't fit.
- Security: no bonding required. If macOS requests pairing, use Just Works (IO capability NoInputNoOutput). No button involvement.
- On connect, request the minimum connection interval (7.5 ms). Print the negotiated interval on serial.
- On disconnect, restart advertising.
- Packet for one CC: `[0x80 | ((ts >> 7) & 0x3F), 0x80 | (ts & 0x7F), 0xB0 | MIDI_CH, MIDI_CC, value]`, where `ts` is milliseconds mod 8192.
- Latest value wins. When the stack can't send yet, overwrite the pending value; never queue old values. Use BTstack's can-send-now mechanism. Effective send rate is capped by the connection interval.
- BLE handling must never disturb the 500 Hz sample timing.

## Stages

Do one stage, build it, flash it, summarise it (see above), then stop. Move on only after he confirms.

1. **BLE skeleton.** Advertise as "EMG Sleeve"; while connected, send CC 11 sweeping 0 to 127 and back, about once every 2 s. No ADC yet. Rayan checks: it appears in Audio MIDI Setup (MIDI Studio, Bluetooth), connects, and moves a MIDI-learned control in REAPER.
2. **DSP port.** Full pipeline above, output over BLE, debug prints over USB serial. Rayan checks: `plot.py` shows sensible values and it plays like the CircuitPython build.
3. **LED on GP16.** Slow breathe while advertising; brightness follows the CC value while connected.
4. **Battery on GP27.** Read the divider (ask for the ratio first); double-blink the LED on low battery (ask for the threshold).
5. **USB MIDI alongside BLE.** Send the same value as CC 12 over USB MIDI at the same moment as CC 11 over BLE, for a latency comparison in REAPER. USB MIDI and the USB serial console must coexist (composite TinyUSB device), or debug moves to UART. Discuss the options with Rayan before building.

## Open items (ask, don't assume)

- Battery divider ratio and low-battery threshold (stage 4).
- Button wiring on GP15 and whether it gets a job.
