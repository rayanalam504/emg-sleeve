# cc_monitor.py -- plots only the CC value the board is sending.
#
# Computes nothing. Reads "raw smooth cc" from the serial console and draws
# field 3. That number is the byte that went to midi_out.write(), so this is
# literally what REAPER receives.
#
# Requires code.py with DEBUG = True.

import collections, threading, time, glob, sys
import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

WINDOW = 1000       # output ticks. At OUT_HZ=100 that's ~10 s of history.

def find_port():
    ports = sorted(glob.glob("/dev/tty.usbmodem*"))
    if not ports:
        sys.exit("No /dev/tty.usbmodem* found - is the board plugged in?")
    return ports[0]

PORT = find_port()
cc_buf = collections.deque([0]*WINDOW, maxlen=WINDOW)

def reader():
    n = 0
    while True:
        try:
            parts = ser.readline().decode("ascii", "ignore").split()
            if len(parts) != 3:
                if parts and parts[0] == "!!":
                    print(f"\nboard: {' '.join(parts)}")
                continue
            try:
                cc = int(parts[2])
            except ValueError:
                continue

            cc_buf.append(cc)
            n += 1
            if n % 20 == 0:
                print(f"\r CC={cc:3d}", end="")

        except Exception as ex:
            print(f"\n!! reader error: {type(ex).__name__}: {ex}")
            time.sleep(0.1)

print(f"Opening {PORT}")
ser = serial.Serial(PORT, 115200, timeout=1)
threading.Thread(target=reader, daemon=True).start()

fig, ax = plt.subplots(figsize=(11, 4))
(line,) = ax.plot(range(WINDOW), [0]*WINDOW, lw=1.2, color="darkred")
ax.set_ylim(-2, 129)
ax.set_yticks([0, 32, 64, 96, 127])
ax.axhline(0,   color="0.7", lw=0.6, ls="--")   # heel down
ax.axhline(127, color="0.7", lw=0.6, ls="--")   # toe down
ax.set_title("MIDI CC 11 sent to REAPER")
ax.set_xlabel("output ticks (~100/s)")

def update(_):
    line.set_ydata(list(cc_buf))
    return (line,)

try:
    ani = FuncAnimation(fig, update, interval=33, blit=False,
                        cache_frame_data=False)
    plt.show()
finally:
    ser.close()