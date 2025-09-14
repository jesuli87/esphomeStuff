#pragma once
#include <Arduino.h>
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/hal.h"

//#include <driver/adc.h>
//Removed above to resolve Wire Compile errors
//Added below for same reason
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc_cal.h"
//End of additions

#ifndef EPD_DRIVER
#define EPD_DRIVER
#include <epd_driver.h>
#include "epd_highlevel.h"
#endif

namespace esphome {
namespace lilygo_t5_47_battery {

class Lilygot547Battery : public PollingComponent {
 public:
  sensor::Sensor *voltage{nullptr};

  int vref = 1100;
  void setup() override;
  void update() override;
  void update_battery_info();
  void correct_adc_reference();

  void set_voltage_sensor(sensor::Sensor *voltage_sensor) { voltage = voltage_sensor; }
};
}  // namespace lilygo_t5_47_battery
}  // namespace esphome