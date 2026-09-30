#include <cstdio>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "ble_midi.h"

// Stage 1: no ADC yet. Sweep CC 0 -> 127 -> 0 once every 2 s.
#define SWEEP_TICK_MS   10
#define SWEEP_PERIOD_MS 2000

static bool sweep_tick(repeating_timer_t *) {
    static uint32_t tick = 0;
    static int last = -1;
    uint32_t phase = (tick++ * SWEEP_TICK_MS) % SWEEP_PERIOD_MS;
    uint32_t half = SWEEP_PERIOD_MS / 2;
    int v = phase < half ? phase * 127 / half : (SWEEP_PERIOD_MS - phase) * 127 / half;
    if (v != last) {
        last = v;
        ble_midi_send_cc(v);
    }
    return true;
}

int main() {
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("cyw43 init failed\n");
        return 1;
    }
    ble_midi_init();

    // Runs in a hardware timer interrupt; BLE runs in the background cyw43 interrupt.
    static repeating_timer_t sweep_timer;
    add_repeating_timer_ms(-SWEEP_TICK_MS, sweep_tick, nullptr, &sweep_timer);

    while (true) {
        sleep_ms(1000);
    }
}
