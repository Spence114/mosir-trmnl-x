#include <mosir_hot.h>
#if defined(BOARD_TRMNL_X) && defined(HOT_MODE)
#include <HTTPClient.h>
#include <WiFi.h>
#include <api-client/header.h>
#include <api-client/request_headers.h>
#include <api_response_parsing.h>
#include <display.h>
#include <displayed_image.h>
#include <filesystem.h>
#include <globals.h>
#include <http_utils.h>
#include <iqs323_task.h>
#include <mosir_hot_policy.h>
#include <power.h>
#include <touchbar_actions.h>
#include <trmnl_log.h>
#include <stdlib.h>

namespace {
constexpr uint32_t POLL_MS = 1000;
constexpr uint32_t BODY_TIMEOUT_MS = 2500;
constexpr uint32_t MAX_JSON = 16384;
struct Body {
  uint8_t *data = nullptr;
  size_t size = 0;
  ~Body() { free(data); }
};
bool usbConnected() {
  iqs323_task_i2c_lock();
  bool connected = power().usbStatus() == UsbStatus::CONNECTED;
  iqs323_task_i2c_unlock();
  return connected;
}
bool allowed(const ApiDisplayResponse &response) {
  return Mosir::eligible(usbConnected(), g_modem == nullptr && WiFi.status() == WL_CONNECTED,
      response.mosir_hot_mode, response.refresh_rate,
      response.outcome == ApiDisplayOutcome::Ok && response.status == 0 &&
      !response.reset_firmware && !response.update_firmware && response.filename.length() > 0,
      special_function != SF_NONE || response.special_function != SF_NONE);
}
// Hot mode intentionally uses bounded, single-attempt HTTP on the local network.
// No redirect, reconnection loop, cloud credentials sent to another origin, or flash writes.
bool get(const String &url, const ApiDisplayInputs &inputs, size_t limit, Body &body) {
  if (!url.startsWith("http://") || !isSameOrigin(url, inputs.baseUrl)) return false;
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(1000);
  http.setTimeout(2500);
  http.setReuse(false);
  if (!http.begin(client, url)) return false;
  http.addHeader("Accept-Encoding", "identity");
  auto headers = buildDisplayHeaders(inputs);
  for (const auto &header : headers) http.addHeader(header.first, header.second);
  http.addHeader("MOSIR-Hot-Poll", "true");
  int code = http.GET();
  int length = http.getSize();
  if (code != HTTP_CODE_OK || length <= 0 || size_t(length) > limit) { http.end(); return false; }
  body.data = static_cast<uint8_t *>(malloc(size_t(length) + 1));
  if (!body.data) { http.end(); return false; }
  uint32_t started = millis();
  auto stream = http.getStreamPtr();
  while (body.size < size_t(length) && uint32_t(millis() - started) < BODY_TIMEOUT_MS) {
    int available = stream->available();
    if (available > 0) {
      size_t want = size_t(length) - body.size;
      if (want > size_t(available)) want = available;
      int got = stream->read(body.data + body.size, want);
      if (got > 0) body.size += got;
    } else if (!stream->connected()) break;
    else delay(1);
  }
  http.end();
  body.data[body.size] = 0;
  return body.size == size_t(length);
}
// Save the final frame once at a control handoff or sleep, never once per streamed frame.
void cacheFrame(const String &name, Body &image) {
  if (!image.data) return;
  char path[36];
  filesystem_fix_filename(name.c_str(), path);
  filesystem_purge_old_file(path);
  if (filesystem_write_to_file(path, image.data, image.size) != image.size) {
    Log_error_serial("MOSIR: final frame cache failed; retaining previous cache"); return;
  }
  String previous = preferences.getString(PREFERENCES_CURRENT_PATH_KEY, "");
  if (!previous.isEmpty() && previous != path) preferences.putString(PREFERENCES_LAST_PATH_KEY, previous);
  preferences.putString(PREFERENCES_CURRENT_PATH_KEY, path);
  preferences.putString(PREFERENCES_BROWSE_PATH_KEY, path);
  update_playlist_order(preferences, path, previous.c_str());
  DisplayedImage::remember(path);
  free(image.data); image.data = nullptr; image.size = 0;
}
}

bool mosir_hot_eligible() {
  return allowed(apiDisplayResult.response) &&
      preferences.getString(PREFERENCES_API_URL, API_BASE_URL).startsWith("http://");
}

void mosir_hot_run(https_request_err_e initialResult) {
  if ((initialResult != HTTPS_SUCCESS && initialResult != HTTPS_NO_ERR) || !mosir_hot_eligible()) return;
  if (!iqs323_task_set_streaming_mode(true)) return;
  if (!iqs323_task_hot_gestures(true)) { iqs323_task_set_streaming_mode(false); return; }
  Log_info_serial("MOSIR: hot mode started (USB, native Wi-Fi, server opt-in)");
  String lastName = apiDisplayResult.response.filename;
  Body latest;
  auto inputs = createApiHeaders();
  inputs.baseUrl = preferences.getString(PREFERENCES_API_URL, API_BASE_URL);
  inputs.batteryVoltage = battery_voltage;
  uint32_t lastPoll = millis();
  display_hot_invalidate(); // the stock initial frame may have been grayscale
  while (mosir_hot_eligible()) {
    uint8_t channel; bool held;
    if (iqs323_task_take_hot_gesture(&channel, &held)) {
      // Do not poll the server over an open menu. Dispatch the exact stock v1.8.18 action.
      if (!iqs323_task_hot_gestures(false)) break;
      cacheFrame(lastName, latest);
      display_hot_invalidate();
      touchbar_dispatch_live(channel, held); // stock actions may shut down, reset, or deep sleep
      if (!iqs323_task_set_streaming_mode(true) || !iqs323_task_hot_gestures(true)) break;
      // A returned action may have drawn an indicator: force the next image to repaint.
      lastName = "";
    }
    if (!display_hot_clean()) break;
    if (uint32_t(millis() - lastPoll) < POLL_MS) { delay(20); continue; }
    lastPoll = millis();
    inputs.rssi = WiFi.RSSI();
    Body json;
    if (!get(inputs.baseUrl + "/api/display", inputs, MAX_JSON, json)) break;
    String payload(reinterpret_cast<const char *>(json.data));
    auto response = parseResponse_apiDisplay(payload);
    if (!allowed(response)) {
      if (response.outcome == ApiDisplayOutcome::Ok && response.status == 0 && response.refresh_rate > 0 &&
          response.refresh_rate <= UINT32_MAX) refreshInterval.applyServerRate(response.refresh_rate);
      break;
    }
    if (response.filename == lastName) continue;
    Body image;
    if (!get(response.image_url, inputs, MAX_IMAGE_SIZE, image)) break;
    if (!usbConnected()) break;
    // Honor touches captured during network I/O before drawing another frame.
    if (iqs323_task_take_hot_gesture(&channel, &held)) {
      if (!iqs323_task_hot_gestures(false)) break;
      cacheFrame(lastName, latest);
      display_hot_invalidate();
      touchbar_dispatch_live(channel, held);
      if (!iqs323_task_set_streaming_mode(true) || !iqs323_task_hot_gestures(true)) break;
    }
    if (!display_show_hot_png(image.data, image.size, response.maximum_compatibility || response.temp_profile != 0)) break;
    free(latest.data); latest.data = image.data; latest.size = image.size; image.data = nullptr;
    lastName = response.filename;
    new_filename = lastName;
    // Deliberately leave global response/persistent state at the stock wake's values.
  }
  iqs323_task_hot_gestures(false);
  iqs323_task_set_streaming_mode(false);
  cacheFrame(lastName, latest);
  display_hot_invalidate();
  Log_info_serial("MOSIR: hot mode ended; returning to stock sleep");
}
#endif
