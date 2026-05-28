#pragma once
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/hal.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#ifndef EPD_DRIVER
#define EPD_DRIVER
#include "epd_driver.h"
#endif

namespace esphome {
namespace lilygo_t5_47_battery {

class Lilygot547Battery : public PollingComponent {
 public:
  sensor::Sensor *voltage{nullptr};

  void setup() override;
  void update() override;

  void set_voltage_sensor(sensor::Sensor *voltage_sensor) { voltage = voltage_sensor; }

 protected:
  adc_oneshot_unit_handle_t adc_handle_{nullptr};
  adc_cali_handle_t cali_handle_{nullptr};
  bool calibrated_{false};
  bool init_ok_{false};
};

}  // namespace lilygo_t5_47_battery
}  // namespace esphome
