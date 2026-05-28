#include "Lilygot547Battery.h"
#include "esphome/core/log.h"

static const char *TAG = "lilygo_battery";

// Battery ADC pin on LilyGo T5 4.7" S3 = GPIO14 = ADC1 channel 3
// Voltage divider ratio is 2:1 (two equal resistors), so actual battery
// voltage = ADC reading * 2.
#define BATT_ADC_UNIT    ADC_UNIT_1
#define BATT_ADC_CHANNEL ADC_CHANNEL_3   // GPIO14 on ESP32-S3

namespace esphome {
namespace lilygo_t5_47_battery {

void Lilygot547Battery::setup() {
  // --- ADC oneshot unit ---
  adc_oneshot_unit_init_cfg_t init_cfg = {};
  init_cfg.unit_id  = BATT_ADC_UNIT;
  init_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

  esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &adc_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(ret));
    return;
  }

  adc_oneshot_chan_cfg_t chan_cfg = {};
  chan_cfg.atten    = ADC_ATTEN_DB_12;   // 0–3.1 V input range (renamed from DB_11 in IDF 5.x)
  chan_cfg.bitwidth = ADC_BITWIDTH_12;

  ret = adc_oneshot_config_channel(adc_handle_, BATT_ADC_CHANNEL, &chan_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(ret));
    return;
  }

  // --- Calibration: try curve-fitting (ESP32-S3 preferred), fall back to line fitting ---
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  {
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id  = BATT_ADC_UNIT;
    cali_cfg.chan     = BATT_ADC_CHANNEL;
    cali_cfg.atten    = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_12;
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle_);
    if (ret == ESP_OK) {
      calibrated_ = true;
      ESP_LOGI(TAG, "ADC calibration: curve fitting");
    } else {
      ESP_LOGW(TAG, "Curve-fitting calibration unavailable: %s", esp_err_to_name(ret));
    }
  }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  if (!calibrated_) {
    adc_cali_line_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id  = BATT_ADC_UNIT;
    cali_cfg.atten    = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_12;
    ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_handle_);
    if (ret == ESP_OK) {
      calibrated_ = true;
      ESP_LOGI(TAG, "ADC calibration: line fitting");
    } else {
      ESP_LOGW(TAG, "Line-fitting calibration unavailable: %s", esp_err_to_name(ret));
    }
  }
#endif

  if (!calibrated_) {
    ESP_LOGW(TAG, "No ADC calibration available — voltage will be approximate");
  }

  init_ok_ = true;
  ESP_LOGI(TAG, "Battery monitor initialised (GPIO14 / ADC1-CH3)");
}

void Lilygot547Battery::update() {
  if (!init_ok_) {
    if (voltage != nullptr) voltage->publish_state(NAN);
    return;
  }

  // The battery voltage divider on this board is powered via the EPD power rail.
  // Power it on briefly to get a stable reading, then power off immediately.
  epd_poweron();
  delay(10);  // wait for rail to stabilise (original used 100 ms; 10 ms is sufficient)

  int raw = 0;
  esp_err_t ret = adc_oneshot_read(adc_handle_, BATT_ADC_CHANNEL, &raw);

  epd_poweroff();

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "adc_oneshot_read failed: %s", esp_err_to_name(ret));
    if (voltage != nullptr) voltage->publish_state(NAN);
    return;
  }

  float batt_v = 0.0f;
  if (calibrated_) {
    int mv = 0;
    adc_cali_raw_to_voltage(cali_handle_, raw, &mv);
    batt_v = (float)mv * 2.0f / 1000.0f;  // voltage divider * 2, mV -> V
  } else {
    // Uncalibrated fallback: 12-bit, 3.3 V reference, divider * 2
    batt_v = ((float)raw / 4095.0f) * 3.3f * 2.0f;
  }

  ESP_LOGD(TAG, "ADC raw: %d  battery: %.3f V", raw, batt_v);

  if (voltage != nullptr) voltage->publish_state(batt_v);
}

}  // namespace lilygo_t5_47_battery
}  // namespace esphome
