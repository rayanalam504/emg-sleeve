#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/util/queue.h"
#include "hardware/adc.h"
#include "config.h"
#include "ble_midi.h"

#define ENV_PIN       26  // MyoWare ENV
#define ENV_ADC_INPUT 0

constexpr int OUT_EVERY = SAMPLE_HZ / OUT_HZ;  // samples per output tick

static float alpha;    // release coefficient, 0 = bypass
static float ceiling;  // log_map(WINDOWING), computed once at startup

// trailing mean: ring buffer with an integer running sum
static uint16_t win_buf[WIN];
static uint32_t win_sum = 0;
static int win_idx = 0;
static int win_filled = 0;

static float held = 0.0f;  // release envelope state
static int sent = -1;      // last CC sent, -1 forces the first one out
static int out_count = 0;

// Debug lines are queued by the sample interrupt and printed by the main loop,
// so USB printing never delays sampling.
struct DebugLine {
    int raw, held, sent;
};
static queue_t debug_q;

static float log_map(float value) {
    float above = value - FLOOR;
    if (above < 0.0f) above = 0.0f;
    float scaled = above / REF;
    return logf(1.0f + K * scaled);
}

static float trailing_mean(uint16_t v) {
    win_sum -= win_buf[win_idx];
    win_buf[win_idx] = v;
    win_sum += v;
    win_idx = (win_idx + 1) % WIN;
    if (win_filled < WIN) win_filled++;
    return (float)win_sum / win_filled;
}

static float release(float v) {
    if (v >= held) held = v;
    else held = v + (held - v) * alpha;
    return held;
}

static int to_cc(float d) {
    if (d > ceiling) d = ceiling;
    return (int)(d / ceiling * 127.0f);
}

// Runs at SAMPLE_HZ from a hardware timer interrupt.
static bool sample_tick(repeating_timer_t *) {
    uint16_t raw = adc_read() << 4;  // 16-bit counts, as CircuitPython's AnalogIn.value
    float h = release(trailing_mean(raw));
    float d = log_map(h);

    if (++out_count >= OUT_EVERY) {
        out_count = 0;
        int cc = to_cc(d);
        // Send only on a real change (the Python resent 0 and 127 on every tick).
        if (cc != sent && (abs(cc - sent) >= DEADBAND || cc == 0 || cc == 127)) {
            sent = cc;
            ble_midi_send_cc(sent);
        }
        if (DEBUG) {
            DebugLine line = {raw, (int)h, sent};
            queue_try_add(&debug_q, &line);  // dropped if the printer falls behind
        }
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

    adc_init();
    adc_gpio_init(ENV_PIN);
    adc_select_input(ENV_ADC_INPUT);

    alpha = RELEASE_MS > 0 ? expf(-1.0f / (RELEASE_MS / 1000.0f * SAMPLE_HZ)) : 0.0f;
    ceiling = log_map(WINDOWING);
    queue_init(&debug_q, sizeof(DebugLine), 64);

    static repeating_timer_t sample_timer;
    add_repeating_timer_us(-1000000 / SAMPLE_HZ, sample_tick, nullptr, &sample_timer);

    DebugLine line;
    while (true) {
        queue_remove_blocking(&debug_q, &line);
        printf("%d %d %d\n", line.raw, line.held, line.sent);
    }
}
