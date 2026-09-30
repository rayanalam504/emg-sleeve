# code.py -- CircuitPython on RP2350
# MyoWare ENV (GP26) -> trailing mean -> release -> log map -> CC -> USB MIDI

import board
import analogio
import usb_midi
import time
import math

DEBUG = True

# --- params -------------------------------------------------------------------

# log map
FLOOR = 1200        # resting forearm reading
REF   = 6000        # counts above FLOOR that normalise to 1.0
K     = 30           # sensitivity (higher K favours weaker flexion)

# smoothing
WIN = 50            # smoothing window, samples

# release
SAMPLE_HZ  = 500    # ADC samples per second
RELEASE_MS = 00    # 0 = true bypass

# cc
WINDOWING = 3500   # flexion level that saturates the output

# output (firmware only — no equivalent in the notebook)
OUT_HZ   = 100      # CC messages per second
DEADBAND = 3        # don't send changes smaller than this
MIDI_CH  = 0        # channel 1
MIDI_CC  = 11       # Expression

# --- hardware -----------------------------------------------------------------
env      = analogio.AnalogIn(board.GP26)
midi_out = usb_midi.ports[1]

# --- log map ------------------------------------------------------------------
def log_map(value):
    above = value - FLOOR
    if above < 0.0:
        above = 0.0
    scaled = above / REF
    return math.log(1.0 + K * scaled)

# --- trailing mean ------------------------------------------------------------
class TrailingMean:
    def __init__(self, win):
        self.win    = win
        self.buf    = [0.0] * win
        self.idx    = 0
        self.total  = 0.0
        self.filled = 0

    def step(self, v):
        self.total -= self.buf[self.idx]
        self.buf[self.idx] = v
        self.total += v
        self.idx = (self.idx + 1) % self.win
        if self.filled < self.win:
            self.filled += 1
        return self.total / self.filled

# --- release envelope ---------------------------------------------------------
class Release:
    def __init__(self, release_ms, sample_hz):
        if release_ms > 0:
            tau = (release_ms / 1000.0) * sample_hz
            self.alpha = math.exp(-1.0 / tau)
        else:
            self.alpha = 0.0
        self.held = 0.0

    def step(self, v):
        if v >= self.held:
            self.held = v
        else:
            self.held = v + (self.held - v) * self.alpha
        return self.held

# --- CC conversion ------------------------------------------------------------
CEILING = log_map(WINDOWING)    # hoisted: computed once, not per sample

def to_cc(value):
    out = value
    if out > CEILING:
        out = CEILING
    return int(out / CEILING * 127.0)

# --- MIDI ---------------------------------------------------------------------
_status = 0xB0 | MIDI_CH

def send_cc(value):
    midi_out.write(bytes((_status, MIDI_CC, value)))

# --- main loop ----------------------------------------------------------------
tm = TrailingMean(WIN)
rl = Release(RELEASE_MS, SAMPLE_HZ)

PERIOD_NS    = int(1_000_000_000 / SAMPLE_HZ)
OUT_EVERY_NS = int(1_000_000_000 / OUT_HZ)

sent     = -1                       # forces the first message out
next_t   = time.monotonic_ns()
last_out = next_t

while True:
    try:
        raw = env.value
        s   = tm.step(raw)
        h   = rl.step(s)
        d   = log_map(h)

        now = time.monotonic_ns()
        if now - last_out >= OUT_EVERY_NS:
            last_out = now
            cc = to_cc(d)
            if abs(cc - sent) >= DEADBAND or cc in (0, 127):
                sent = cc
                send_cc(sent)
            if DEBUG:
                print(raw, int(h), sent)

    except Exception as ex:
        if DEBUG:
            print("!!", type(ex).__name__, ex)
        time.sleep(0.1)

    next_t += PERIOD_NS
    while time.monotonic_ns() < next_t:
        pass

