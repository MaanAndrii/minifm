#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"
#define STATION_MAX 16
#define ALARM_MAX 8
typedef struct { char name[64]; char url[384]; } station_t;
typedef struct {
    bool enabled; uint8_t hour, minute, weekdays; int station;
    uint8_t volume, snooze_minutes, ramp_seconds;
    bool one_shot;
    int64_t once_at;
} alarm_t;
typedef struct {
    uint32_t version;
    char ssid[33], password[65];
    char token[33];
    char timezone[96];
    int volume, brightness, station_count;
    station_t stations[STATION_MAX];
    alarm_t alarms[ALARM_MAX];
    bool auto_rotate, lift_wake, night_quiet;
    uint16_t night_start, night_end;
    uint8_t rotation_offset;
} settings_t;
typedef enum { AUDIO_IDLE, AUDIO_RADIO, AUDIO_RECORD, AUDIO_FILE, AUDIO_TONE } audio_mode_t;
typedef struct { audio_mode_t mode; uint32_t generation; int station; char source[384]; bool paused; } audio_request_t;
typedef struct {
    audio_mode_t mode; bool paused, sd_ready, time_valid, ringing;
    int volume, station, mic_peak;
    uint32_t recording_bytes;
    int64_t timer_deadline, sleep_deadline;
    int64_t timer_remaining;
    int timer_seconds;
    bool timer_paused, timer_signal;
    char message[128], recording[96];
} state_t;
extern settings_t g_settings;
void settings_init(void);
esp_err_t settings_save(void);
void app_lock(void);
void app_unlock(void);
void app_init(void);
void app_snapshot(settings_t *cfg, state_t *state);
esp_err_t app_command(const cJSON *json);
cJSON *app_status_json(void);
cJSON *app_settings_json(void);
void app_audio_request(audio_request_t *req, int *volume);
void app_audio_status(uint32_t generation, const char *message, int peak, uint32_t bytes);
void app_audio_finished(uint32_t generation, const char *message);
void app_sd_ready(bool ready);
void audio_start(void);
bool audio_record_busy(void);
bool audio_file_busy(const char *name);
void network_start(void);
void network_status(char *out, size_t size);
int network_indicator(int *rssi); // 0 disconnected, 1 connected station, 2 access point.
void web_start(void);
void ui_start(void);
bool display_start(void);
void rtc_start(void);
void rtc_sync_from_system(void);
