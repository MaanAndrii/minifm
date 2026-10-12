#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
bool core_alarm_due(const struct tm *local, unsigned hour, unsigned minute,
                    unsigned weekdays, bool enabled, int64_t *last_day);
bool core_recording_name(const char *name);
void core_wav_header(uint8_t header[44], uint32_t bytes, uint32_t sample_rate);
