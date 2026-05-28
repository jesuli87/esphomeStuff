#pragma once
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/hal.h"

// Use the legacy driver ADC API — available via the 'driver' IDF component which
// is always a dependency in ESPHome builds. Avoids needing an explicit esp_adc
// component declaration that the external-component CMake setup doesn't propagate.
#include "driver/adc.h"

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
  bool init_ok_{false};
  // Last successfully read raw value — retained across WiFi-conflict skips.
  int last_raw_{-1};
};

}  // namespace lilygo_t5_47_battery
}  // namespace esphome
