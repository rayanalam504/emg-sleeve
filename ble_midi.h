#pragma once
#include <cstdint>

// Start BTstack, advertise as "EMG Sleeve" with the BLE MIDI service.
// Call once, after cyw43_arch_init().
void ble_midi_init();

// Queue a CC value (0..127) to go out as MIDI_CC on MIDI_CH.
// Latest value wins: if the previous one hasn't been sent yet it is overwritten.
// Safe to call from any context, including a timer interrupt.
void ble_midi_send_cc(uint8_t value);
