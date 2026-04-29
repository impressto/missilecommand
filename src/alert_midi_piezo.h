#pragma once

#include <stdint.h>

/* Auto-generated from alert.mid by scripts/midi_to_piezo.py */
static const uint16_t alert_midi_piezo[][2] = {
    {2637, 415},
    {1976, 1184},
};

static const int alert_midi_piezo_count = (int)(sizeof(alert_midi_piezo) / sizeof(alert_midi_piezo[0]));
