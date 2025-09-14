import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_VOLTAGE,
    UNIT_VOLT,
    DEVICE_CLASS_VOLTAGE,
)


Lilygot547battery_ns = cg.esphome_ns.namespace("lilygo_t5_47_battery")
Lilygot547battery = Lilygot547battery_ns.class_(
    "Lilygot547Battery", cg.PollingComponent
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Lilygot547battery),
        cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_VOLTAGE,
        ),
    }
).extend(cv.polling_component_schema("5s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    conf = config[CONF_VOLTAGE]
    sens = await sensor.new_sensor(conf)
    cg.add(var.set_voltage_sensor(sens))

    # Bit of a guess on what might be needed for new build
    cg.add_library("https://github.com/Fabian-Schmidt/epdiy.git#lilygos3", None)
    # cg.add_library("file:///home/fabian/Repos/Fabian-Schmidt/epdiy/", None)
    cg.add_build_flag("-DCONFIG_EPD_DISPLAY_TYPE_ED047TC2")
    cg.add_build_flag("-DCONFIG_EPD_BOARD_REVISION_LILYGO_S3_47")
    cg.add_build_flag("-DBOARD_HAS_PSRAM")
    
    cg.add_library("Wire", version="3.2.1")  # required by LilyGoEPD47
    cg.add_library("LilyGoEPD47", repository="https://github.com/Xinyuan-LilyGO/LilyGo-EPD47", version="v0.3.0")

    # Old config caused build errors with screen
    # cg.add_build_flag("-DCONFIG_EPD_DISPLAY_TYPE_ED047TC1")
    # cg.add_build_flag("-DCONFIG_EPD_BOARD_REVISION_LILYGO_T5_47")
    # cg.add_library("https://github.com/vroland/epdiy.git", None)