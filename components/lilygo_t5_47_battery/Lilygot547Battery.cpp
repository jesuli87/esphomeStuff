#include "Lilygot547Battery.h"
#include "esphome/core/log.h"

static const char *TAG = "lilygo_battery";

// Battery ADC pin on LilyGo T5 4.7" S3 = GPIO14 = ADC2 channel 3.
//
// IMPORTANT: On ESP32-S3, GPIO1-GPIO10 are ADC1, GPIO11-GPIO20 are ADC2.
// GPIO14 therefore maps to ADC2_CHANNEL_3, NOT ADC1.
// ADC2 conflicts with WiFi: adc2_get_raw() returns ESP_ERR_INVALID_STATE
// while WiFi is active. The component handles this gracefully by retaining
// the last known voltage rather than publishing NAN on conflict.
#define BATT_ADC_CHANNEL ADC2_CHANNEL_3   // GPIO14 on ESP32-S3

namespace esphome {
namespace lilygo_t5_47_battery {

void Lilygot547Battery::setup() {
  // ADC_ATTEN_DB_11 = maximum attenuation, 0–3.1 V input range (legacy enum).
  esp_err_t ret = adc2_config_channel_atten(BATT_ADC_CHANNEL, ADC_ATTEN_DB_11);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "adc2_config_channel_atten failed: %s", esp_err_to_name(ret));
    return;
  }

  init_ok_ = true;
  ESP_LOGI(TAG, "Battery monitor initialised (GPIO14 / ADC2-CH3)");
  ESP_LOGW(TAG, "Note: ADC2 conflicts with WiFi — readings may be skipped while WiFi is active");
}

void Lilygot547Battery::update() {
  if (!init_ok_) {
    if (voltage != nullptr) voltage->publish_state(NAN);
    return;
  }

  // Power on EPD rail — battery voltage divider is on this rail.
  epd_poweron();
  delay(10);

  int raw = 0;
  esp_err_t ret = adc2_get_raw(BATT_ADC_CHANNEL, ADC_WIDTH_BIT_12, &raw);

  epd_poweroff();

  if (ret == ESP_ERR_INVALID_STATE) {
    // WiFi is active — ADC2 arbitration lost. Skip this reading; retain last value.
    ESP_LOGD(TAG, "ADC2 read skipped — WiFi active");
    return;
  }
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "adc2_get_raw failed: %s", esp_err_to_name(ret));
    if (voltage != nullptr) voltage->publish_state(NAN);
    return;
  }

  // Uncalibrated: 12-bit, 3.3 V reference, voltage divider * 2.
  float batt_v = ((float)raw / 4095.0f) * 3.3f * 2.0f;
  ESP_LOGD(TAG, "ADC raw: %d  battery: %.3f V", raw, batt_v);

  if (voltage != nullptr) voltage->publish_state(batt_v);
}

}  // namespace lilygo_t5_47_battery
}  // namespace esphome
