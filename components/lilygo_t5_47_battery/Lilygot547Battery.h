#pragma once
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/hal.h"

// Use the legacy driver ADC API — available via the 'driver' IDF component which
// is always a dependency in ESPHome builds. Avoids needing an explicit esp_adc
// component declaration that the external-component CMake setup doesn't propagate.
#include "driver/adc.h"

// Forward-declare the EPD power functions from the t547 component.
// epd_driver.h is not on the include path for this component, but the symbols
// are available at link time because t547 is compiled into the same firmware.
extern "C" {
  void epd_poweron();
  void epd_poweroff();
}

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
