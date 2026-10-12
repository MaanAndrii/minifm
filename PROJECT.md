# ESP32-S3-Touch-AMOLED-2.16 Radio Clock

Status: first firmware version implemented and built with ESP-IDF 5.5.5. Native C tests, JavaScript tests and simulated browser UI tests passed. Physical board validation pending. See README.md for supported formats and first-release limitations.

## Platform

- ESP-IDF + LVGL; board revision still unconfirmed.
- Internal speaker through the board audio codec.
- Browser control over the local network, hosted by the device itself.
- Standard Bluetooth speaker output requires external A2DP transmitter hardware: ESP32-S3 does not support Bluetooth Classic.

## First release

- Wi-Fi provisioning through a device access point; subsequent operation on the local Wi-Fi network.
- Radio station list with editable stream URLs, play/stop, volume, stream reconnection.
- Clock synchronized using SNTP, Europe/Kyiv timezone including daylight-saving changes.
- Alarms with weekday selection, station or local tone, snooze, and offline fallback tone.
- Countdown timer and separate sleep timer for radio playback.
- Persistent station, network, volume, timezone, and alarm settings in NVS.
- Local LVGL display: clock, current station, volume, network status, next alarm, timer; touch playback and alarm dismissal.
- Responsive web interface: playback, station management, alarms, timers, Wi-Fi and display settings.
- Web mutations authenticated; no credentials in status responses; credentials excluded from logs.

## Architecture

Separate modules: board adapter, network provisioning, audio stream/decoder/output, clock/RTC, alarm scheduler, monotonic timers, NVS settings, HTTP API and embedded web assets, LVGL UI.

Audio decoding and network buffering run outside HTTP and LVGL tasks. Both interfaces submit commands to a shared controller; playback and scheduler state are serialized there. Timers use monotonic time, alarms use synchronized local calendar time, with duplicate-trigger protection after time corrections. Radio failure must not block local alarm playback.

## Verification required

Confirm hardware revision against the schematic and BSP before power/display initialization. Check ESP-IDF, LVGL and BSP version compatibility before pinning dependencies. Build firmware, then verify display, touch, audio, provisioning, stream interruptions, offline alarm, snooze, restart persistence and daylight-saving transitions on the physical board.

## References

- https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16
- https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/bluetooth/index.html
