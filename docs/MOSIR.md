# MOSIR TRMNL X prototype

Based on the released TRMNL v1.8.18, commit `4ffa33acbe317e559aa50ee505ffa5fe4ed0272d`. This is an experimental application build. No hardware latency or gesture tests have been completed on it yet.

## Behavior

The normal wake/fetch/sleep behavior remains in place. USB-powered hot mode starts only after a successful stock screen fetch when native 2.4 GHz Wi-Fi is connected, the server URL is plain HTTP, and `/api/display` explicitly returns `mosir_hot_mode: true` with a refresh interval from 1 to 5 seconds. Cloud responses without this flag never activate hot mode. The initial frame follows the normal cached path.

Hot mode polls once per second (measured from the previous request start). Its `MOSIR-Hot-Poll: true` header tells the local server to answer immediately, without long polling. API and image URLs must share an origin. Responses must have Content-Length. API bodies are limited to 16 KiB; images are bounded by the firmware's MAX_IMAGE_SIZE. Only native-size, non-interlaced, 1-bit grayscale PNG frames are accepted. Filenames must change whenever pixels change and must not be reused for different images.

A new frame is decoded in RAM and uses partial refresh after a full seed refresh. A full refresh runs after at most 40 partials or 20 seconds, including during idle. Menus/indicators invalidate the partial-refresh state. These conservative limits are build-time configurable with HOT_MAX_PARTIALS and HOT_IDLE_CLEAN_MS. No panel waveform tables were changed.

The final streaming frame is saved once before handing control to an original gesture or returning to sleep. Streaming frames are not individually written to flash. Original cached playlist navigation remains navigation of saved screens, not every transient animation frame. USB removal, loss of native Wi-Fi, HTTP/PNG errors, or withdrawal of server opt-in exits hot mode to the stock sleep path. Network operations are bounded but may defer touch dispatch for several seconds; the five-second finished-answer target remains unmeasured.

## Original controls

The stock gesture dispatcher, wake gesture resolver, menus, shipping/power routines, sleep routines, partition table, and production SDK settings are retained. A bounded FIFO in the existing touch-controller task captures completed taps and two-second holds during network requests. The foreground passes those events to the original dispatcher. Original actions may enter deep sleep, reset Wi-Fi, restart, or shut down; hot mode does not intercept those actions. The dock/magnet reset and boot-mode hardware paths are not changed.

`tests/mosir/check_stock_controls.py` compares the protected code against the upstream release. `tests/mosir/policy_test.cpp` tests activation gates, all three zones' tap/hold timing, delayed event consumption, queue overflow, clock wrap, and refresh limits. These checks do not substitute for physical gesture testing.

Automatic main-controller OTA is disabled in this build. Factory companion-radio flashing is disabled, preserving its existing firmware. Normal radio connectivity and setup are retained.

## Build and installation boundary

Build with `pio run -e TRMNL_X_hot` or the MOSIR TRMNL X GitHub workflow. The custom environment inherits production settings and explicitly uses sdkconfig.TRMNL_X. It omits the development build's serial-wait flags and whole-flash merge scripts.

The artifact `mosir-trmnl-x-app.bin` is an APPLICATION IMAGE ONLY. Do not write it at address zero and do not use it as a full-flash backup. Installation must first inspect the device's partition table and selected OTA slot; use the matching application partition without changing bootloader, partition table, saved settings, or filesystem. No flash command is included here because the correct target must be established on the connected device.

## Physical acceptance checklist (pending)

- Record stock version, verify private full-flash backup and matching application partition layout.
- Compare cloud dashboard, battery sleep, Wi-Fi setup, left/right cached navigation, middle tap menu, and original hold behavior with stock v1.8.18.
- Verify menu close, restart, Wi-Fi reset, shutdown/wake, and the factory ship-check gesture where applicable.
- Verify official dock soft reset and boot-mode entry, with USB detection after each; retain original recovery instructions.
- With local opt-in enabled: verify rapid updates, repeated filename suppression, periodic full refresh, controls during API/image delays, and menu return without corrupted pixels.
- Withdraw opt-in, unplug USB, disconnect Wi-Fi, stop the server, serve malformed/truncated/oversized PNGs, and verify clean fallback and recovery.
- Run 30 minutes on USB and observe ghosting, power/temperature behavior, then measure ten end-to-end finished-answer trials.

Official recovery instructions:
https://help.trmnl.com/en/articles/11936721-put-your-trmnl-in-flashing-mode
https://help.trmnl.com/en/articles/12407673-troubleshooting-an-unresponsive-device
