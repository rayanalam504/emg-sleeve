# latency.py -- BLE vs USB MIDI latency for the EMG sleeve.
#
# Needs tools/latency_test flashed, with BOTH "EMG Sleeve Bluetooth" (connect it
# in Audio MIDI Setup's Bluetooth window) and "EMG Sleeve USB" visible.
# Setup:  python3 -m venv venv && venv/bin/pip install mido python-rtmidi
# Usage:  venv/bin/python latency.py [step_ms ...]     (default: 10 50 100)
#         venv/bin/python latency.py --reboot           (board -> BOOTSEL, to reflash)
#
# For each step period it sets the board's sawtooth rate (CC 20 = step in ms),
# records arrival times for DUR seconds, pairs each BLE CC 11 with the USB CC 12
# of the same value, and prints the BLE-minus-USB delay in ms.
import time, threading, statistics, sys, mido

if sys.argv[1:] == ["--reboot"]:
    out = mido.open_output(next(n for n in mido.get_output_names() if "USB" in n))
    out.send(mido.Message("note_on", note=127, velocity=127))
    sys.exit("sent reboot-to-BOOTSEL")

conds = [int(a) for a in sys.argv[1:]] or [10, 50, 100]   # step periods in ms
DUR = 30
ins = mido.get_input_names()
ble_name = next(n for n in ins if 'Bluetooth' in n)
usb_name = next(n for n in ins if 'USB' in n)
usb_out_name = next(n for n in mido.get_output_names() if 'USB' in n)
print("BLE:", ble_name, "| USB:", usb_name)

events = {'ble': [], 'usb': []}
lock = threading.Lock()
def cb(tag):
    def f(msg):
        t = time.perf_counter()
        if msg.type == 'control_change':
            with lock: events[tag].append((t, msg.control, msg.value))
    return f

ble_in = mido.open_input(ble_name, callback=cb('ble'))
usb_in = mido.open_input(usb_name, callback=cb('usb'))
out = mido.open_output(usb_out_name)

def pctl(x, p):
    x = sorted(x); return x[min(len(x) - 1, int(p / 100 * len(x)))]

results = []
for step in conds:
    out.send(mido.Message('control_change', control=20, value=step))
    time.sleep(1.5)
    with lock: events['ble'].clear(); events['usb'].clear()
    time.sleep(DUR)
    with lock:
        ble = [e for e in events['ble'] if e[1] == 11]
        usb = [e for e in events['usb'] if e[1] == 12]
    window = 128 * step / 1000 / 2          # half a sawtooth period: values unique within it
    delays = []
    j = 0
    for tb, _, v in ble:
        # latest USB event with the same value, earlier than the BLE arrival and within the window
        best = None
        for tu, _, vu in reversed(usb):
            if tu > tb: continue
            if tb - tu > window: break
            if vu == v: best = tu; break
        if best is not None: delays.append((tb - best) * 1000)
    r = dict(step=step, usb=len(usb), ble=len(ble), matched=len(delays))
    if delays:
        r.update(min=min(delays), median=statistics.median(delays), mean=statistics.mean(delays),
                 p95=pctl(delays, 95), max=max(delays), sd=statistics.pstdev(delays))
    results.append(r)
    print(r, flush=True)

import json
json.dump(results, open(sys.argv[0].replace('.py', '_results.json'), 'w'), indent=1)
ble_in.close(); usb_in.close(); out.close()
