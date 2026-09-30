#pragma once

// All tunables, same names and values as reference/code.py.

constexpr bool DEBUG = true;  // print "raw held sent" on every output tick

// log map
constexpr int FLOOR = 1200;  // resting forearm reading
constexpr int REF   = 6000;  // counts above FLOOR that normalise to 1.0
constexpr int K     = 30;    // sensitivity (higher K favours weaker flexion)
// Only the ratio K / REF affects the output, because the result is normalised
// by CEILING. Tuning K alone is enough; REF is kept for parity with the notebook.

// smoothing
constexpr int WIN = 50;  // trailing mean window, samples

// release
constexpr int SAMPLE_HZ  = 500;  // ADC samples per second
constexpr int RELEASE_MS = 0;    // 0 = true bypass

// cc
constexpr int WINDOWING = 3500;  // flexion level that saturates the output

// output
constexpr int OUT_HZ   = 100;  // CC ticks per second
constexpr int DEADBAND = 3;    // don't send changes smaller than this
constexpr int MIDI_CH  = 0;    // channel 1
constexpr int MIDI_CC  = 11;   // Expression
