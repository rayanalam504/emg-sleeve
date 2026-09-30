// Latency test firmware: a 0..127 sawtooth sent at the same instant as
// CC 11 over BLE (via ble_midi) and CC 12 over USB MIDI.
// Control from the host over USB MIDI:
//   CC 20 value n   -> step every n ms
//   Note-on 127     -> reboot into BOOTSEL (for reflashing without the button)
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"
#include "tusb.h"
#include "config.h"
#include "ble_midi.h"

int main() {
    tud_init(0);
    if (cyw43_arch_init()) return 1;
    ble_midi_init();

    uint32_t step_ms = 10;
    uint8_t v = 0;
    absolute_time_t next = make_timeout_time_ms(step_ms);
    while (true) {
        tud_task();
        if (time_reached(next)) {
            next = make_timeout_time_ms(step_ms);
            v = (v + 1) & 0x7F;
            ble_midi_send_cc(v);
            if (tud_midi_mounted()) {
                uint8_t msg[3] = {(uint8_t)(0xB0 | MIDI_CH), 12, v};
                tud_midi_stream_write(0, msg, 3);
            }
        }
        uint8_t pkt[4];
        while (tud_midi_available() && tud_midi_packet_read(pkt)) {
            if ((pkt[1] & 0xF0) == 0xB0 && pkt[2] == 20 && pkt[3] > 0) step_ms = pkt[3];
            if ((pkt[1] & 0xF0) == 0x90 && pkt[2] == 127 && pkt[3] > 0) reset_usb_boot(0, 0);
        }
    }
}
